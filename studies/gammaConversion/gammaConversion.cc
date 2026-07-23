// --------------------------------------------------------------
//   Preliminary study: gamma -> e+ e- conversion across materials
// --------------------------------------------------------------
// Photons are fired into a thick block with pair conversion as the only physics
// process. The same experiment is run once per material in a list; each
// conversion is written to a ROOT ntuple with the photon energy, the material's
// atomic number Z, the triplet flag, the recoil and pair energies and the polar
// angle of the leading lepton.
//
//   gammaConversion config/default.cfg               loop over the config list
//   gammaConversion config/default.cfg materials=G4_Pb  override one setting
//   gammaConversion run.mac                          batch run driven by a macro
//   gammaConversion                                  interactive session (vis.mac)
// --------------------------------------------------------------

#include "ConvActionInitialization.hh"
#include "ConvConfig.hh"
#include "ConvDetectorConstruction.hh"
#include "ConvLogger.hh"
#include "ConvPhysicsList.hh"

#include "G4RunManagerFactory.hh"
#include "G4Types.hh"
#include "G4UIExecutive.hh"
#include "G4UImanager.hh"
#include "G4VisExecutive.hh"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
/// A .mac argument is a plain Geant4 macro, anything else a ConvConfig file.
G4bool IsMacro(const G4String& aPath)
{
  return aPath.size() > 4 && aPath.compare(aPath.size() - 4, 4, ".mac") == 0;
}

/// File name of a path, without directories or extension.
G4String Stem(const G4String& aPath)
{
  const auto slash = aPath.find_last_of('/');
  G4String name = (slash == G4String::npos) ? aPath : G4String(aPath.substr(slash + 1));
  const auto dot = name.find_last_of('.');
  return (dot == G4String::npos) ? name : name.substr(0, dot);
}

/// Run the whole material list by re-executing this program once per material,
/// each child with a single-material "materials=<name>" override appended to the
/// original arguments. A fresh process gives each material a clean
/// G4AnalysisManager, which is what makes the per-material files reliable --
/// switching the output file between runs inside one process leaves all but the
/// first ntuple empty. Returns 0 only if every child succeeded.
int RunMaterialScan(int argc, char** argv, const std::vector<G4String>& aMaterials)
{
  int status = 0;
  for (const auto& material : aMaterials) {
    const std::string override = "materials=" + material;

    std::vector<char*> childArgv;
    childArgv.reserve(argc + 2);
    for (int i = 0; i < argc; ++i) childArgv.push_back(argv[i]);
    childArgv.push_back(const_cast<char*>(override.c_str()));  // appended: wins
    childArgv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
      std::perror("gammaConversion: fork");
      return 1;
    }
    if (pid == 0) {
      execv(argv[0], childArgv.data());
      std::perror("gammaConversion: execv");  // only reached if execv fails
      _exit(127);
    }

    int childStatus = 0;
    waitpid(pid, &childStatus, 0);
    if (!WIFEXITED(childStatus) || WEXITSTATUS(childStatus) != 0) status = 1;
  }
  return status;
}
}  // namespace

int main(int argc, char** argv)
{
  G4String inputName;
  std::vector<std::pair<G4String, G4String>> overrides;
  G4String helpMsg(
    "Usage: " + G4String(argv[0])
    + " [CONFIG|MACRO] [key=value ...]\n"
      "  CONFIG   a 'key = value' run configuration (see config/default.cfg)\n"
      "  MACRO    a Geant4 macro, recognised by its .mac extension\n"
      "  key=value  override a config setting, e.g. materials=G4_Pb nEvents=20000\n"
      "  no argument starts an interactive session executing vis.mac\n"
      " Options:\n\t-h\tdisplay this help message\n");
  for (G4int i = 1; i < argc; ++i) {
    G4String argument(argv[i]);
    if (argument == "-h" || argument == "--help") {
      G4cout << helpMsg << G4endl;
      return 0;
    }
    const auto equals = argument.find('=');
    if (equals != G4String::npos) {
      overrides.emplace_back(argument.substr(0, equals), argument.substr(equals + 1));
    }
    else {
      inputName = argument;
    }
  }

  const G4bool isMacro = !inputName.empty() && IsMacro(inputName);
  // Batch config mode when given a config file, or when only overrides are
  // passed (they run against the built-in defaults).
  const G4bool useConfig = !isMacro && (!inputName.empty() || !overrides.empty());

  ConvConfig config;
  if (useConfig) {
    if (!inputName.empty() && !config.Read(inputName)) return 1;
    for (const auto& [key, value] : overrides) {
      if (!config.ApplyOverride(key, value)) return 1;
    }
    if (!config.Validate()) return 1;

    // Several materials: run each in its own process, so every material gets a
    // clean analysis lifecycle and its own file. Each child appends a
    // single-material override, so it takes this branch's else and runs one.
    if (config.GetMaterials().size() > 1) {
      return RunMaterialScan(argc, argv, config.GetMaterials());
    }
  }
  else if (!overrides.empty()) {
    G4cerr << argv[0] << ": key=value overrides are ignored for a macro run" << G4endl;
  }

  // Installed before the run manager exists, so the log holds the Geant4 banner
  // and the whole initialisation. Exactly one material reaches this point (a
  // list was dispatched to child processes above), so its log is self-contained.
  const G4String logPath = useConfig  ? config.LogFilePath(config.GetMaterials().front())
                           : inputName.empty() ? "logs/interactive.log"
                                               : "logs/" + Stem(inputName) + ".log";
  ConvLogger logger(logPath);
  G4UImanager::GetUIpointer()->SetCoutDestination(&logger);

  auto runManager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::Default);

  auto detector = new ConvDetectorConstruction();
  runManager->SetUserInitialization(detector);
  runManager->SetUserInitialization(new ConvPhysicsList(config.GetModel(), config.GetConversionType()));
  runManager->SetUserInitialization(new ConvActionInitialization(detector, &logger));

  G4VisManager* visManager = new G4VisExecutive;
  visManager->Initialize();
  G4UImanager* uiManager = G4UImanager::GetUIpointer();

  if (useConfig) {
    config.Print();

    // Exactly one material here: a multi-material list was dispatched to child
    // processes above. Set it before /run/initialize so the geometry is built
    // with the right material from the start -- no post-init geometry rebuild,
    // which does not play well with MT analysis output.
    const G4String& material = config.GetMaterials().front();
    for (const auto& command : config.PreInitCommands()) {
      uiManager->ApplyCommand(command);
    }
    uiManager->ApplyCommand("/study/det/material " + material);
    uiManager->ApplyCommand("/run/initialize");
    for (const auto& command : config.PostInitCommands()) {
      uiManager->ApplyCommand(command);
    }
    uiManager->ApplyCommand("/analysis/setFileName " + config.OutputFilePath(material));
    uiManager->ApplyCommand("/run/printProgress "
                            + std::to_string(std::max(1, config.GetNbEvents() / 10)));
    uiManager->ApplyCommand("/run/beamOn " + std::to_string(config.GetNbEvents()));
  }
  else if (!inputName.empty()) {
    uiManager->ApplyCommand("/control/execute " + inputName);
  }
  else {
    auto ui = new G4UIExecutive(argc, argv);
    uiManager->ApplyCommand("/control/execute vis.mac");
    ui->SessionStart();
    delete ui;
  }

  delete visManager;
  delete runManager;

  // The logger is about to go out of scope; do not leave it as a destination
  if (G4UImanager::GetUIpointer() != nullptr) {
    G4UImanager::GetUIpointer()->SetCoutDestination(nullptr);
  }

  return 0;
}
