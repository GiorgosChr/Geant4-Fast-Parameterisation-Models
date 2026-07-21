// --------------------------------------------------------------
//   Preliminary study: gamma -> e+ e- conversion in a dense block
// --------------------------------------------------------------
// Photons are fired into a thick block (silicon by default) with pair
// conversion as the only physics process. Each conversion is written to a ROOT
// ntuple with the photon energy, the path travelled in the block before
// converting, and the energies and angles of the produced pair.
//
//   gammaConversionSi config/default.cfg   batch run driven by a config file
//   gammaConversionSi run.mac              batch run driven by a Geant4 macro
//   gammaConversionSi                      interactive session (vis.mac)
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

#include <algorithm>
#include <string>

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
}  // namespace

int main(int argc, char** argv)
{
  G4String inputName;
  G4String helpMsg(
    "Usage: " + G4String(argv[0])
    + " [CONFIG|MACRO]\n"
      "  CONFIG  a 'key = value' run configuration (see config/default.cfg)\n"
      "  MACRO   a Geant4 macro, recognised by its .mac extension\n"
      "  no argument starts an interactive session executing vis.mac\n"
      " Options:\n\t-h\tdisplay this help message\n");
  for (G4int i = 1; i < argc; ++i) {
    G4String argument(argv[i]);
    if (argument == "-h" || argument == "--help") {
      G4cout << helpMsg << G4endl;
      return 0;
    }
    inputName = argument;
  }

  const G4bool useConfig = !inputName.empty() && !IsMacro(inputName);

  // The conversion model has to be known before the physics list is built
  ConvConfig config;
  if (useConfig && !config.Read(inputName)) return 1;

  // Installed before the run manager exists, so the log holds the Geant4
  // banner and the whole initialisation, not just the run itself
  const G4String logPath = useConfig  ? config.LogFilePath()
                           : inputName.empty() ? "logs/interactive.log"
                                               : "logs/" + Stem(inputName) + ".log";
  ConvLogger logger(logPath);
  G4UImanager::GetUIpointer()->SetCoutDestination(&logger);

  auto runManager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::Default);

  auto detector = new ConvDetectorConstruction();
  runManager->SetUserInitialization(detector);
  runManager->SetUserInitialization(
    new ConvPhysicsList(config.GetModel(), config.GetConversionType()));
  runManager->SetUserInitialization(new ConvActionInitialization(detector, &logger));

  G4VisManager* visManager = new G4VisExecutive;
  visManager->Initialize();
  G4UImanager* uiManager = G4UImanager::GetUIpointer();

  if (useConfig) {
    config.Print();
    for (const auto& command : config.PreInitCommands()) {
      uiManager->ApplyCommand(command);
    }
    uiManager->ApplyCommand("/run/initialize");
    for (const auto& command : config.PostInitCommands()) {
      uiManager->ApplyCommand(command);
    }
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
