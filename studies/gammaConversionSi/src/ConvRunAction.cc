/// \file ConvRunAction.cc
/// \brief Implementation of the ConvRunAction class

#include "ConvRunAction.hh"

#include "G4AccumulableManager.hh"
#include "G4AnalysisManager.hh"
#include "G4Run.hh"

#include <filesystem>

ConvRunAction::ConvRunAction(const G4String& aSimMode) : fSimMode(aSimMode)
{
  auto accumulableManager = G4AccumulableManager::Instance();
  accumulableManager->Register(fNbEvents);
  accumulableManager->Register(fNbConverted);

  auto analysisManager = G4AnalysisManager::Instance();
  analysisManager->SetDefaultFileType("root");
  // Fallback name for runs not driven by a config file (interactive, macro);
  // a config run overrides it through /analysis/setFileName. Kept inside
  // ntuples/ so that output never lands loose in the working directory.
  std::filesystem::create_directories("ntuples");
  analysisManager->SetFileName("ntuples/gammaConversionSi");
  analysisManager->SetNtupleMerging(true);
  analysisManager->SetVerboseLevel(0);

  // One row per converted event. Energies in MeV, lengths in mm, angles in rad
  // -- see README.md for the full description of each column.
  analysisManager->CreateNtuple("conversions", "Photon conversions in silicon");
  analysisManager->CreateNtupleDColumn("eGamma");
  analysisManager->CreateNtupleDColumn("pathInBlock");
  analysisManager->CreateNtupleDColumn("eElectron");
  analysisManager->CreateNtupleDColumn("ePositron");
  analysisManager->CreateNtupleDColumn("thetaElectron");
  analysisManager->CreateNtupleDColumn("thetaPositron");
  analysisManager->CreateNtupleDColumn("phiElectron");
  analysisManager->CreateNtupleDColumn("phiPositron");
  analysisManager->CreateNtupleDColumn("openingAngle");
  analysisManager->CreateNtupleDColumn("zConv");
  analysisManager->CreateNtupleDColumn("eRecoil");
  analysisManager->CreateNtupleIColumn("isTriplet");
  analysisManager->FinishNtuple();
}

void ConvRunAction::BeginOfRunAction(const G4Run*)
{
  G4AccumulableManager::Instance()->Reset();
  G4AnalysisManager::Instance()->OpenFile();
  // Time the event loop only: start after the file is open, stop before it is
  // written, so I/O and one-time setup (flow load) sit outside the number.
  fTimer.Start();
}

void ConvRunAction::EndOfRunAction(const G4Run* aRun)
{
  fTimer.Stop();

  auto analysisManager = G4AnalysisManager::Instance();
  analysisManager->Write();
  analysisManager->CloseFile();

  G4AccumulableManager::Instance()->Merge();

  if (!IsMaster()) return;

  const G4int nbEvents = fNbEvents.GetValue();
  const G4int nbConverted = fNbConverted.GetValue();
  const G4double fraction = (nbEvents > 0) ? 100. * nbConverted / nbEvents : 0.;

  // One machine-parseable line per experiment for benchmark/run_benchmark.sh.
  // The event count comes from the run rather than the accumulable so it is
  // right even in a mode that fills no row. Times are in seconds.
  G4cout << "BENCHMARK mode=" << fSimMode << " events=" << aRun->GetNumberOfEvent()
         << " realElapsed=" << fTimer.GetRealElapsed() << " userCPU=" << fTimer.GetUserElapsed()
         << " sysCPU=" << fTimer.GetSystemElapsed() << G4endl;

  G4cout << "\n--------------------End of Global Run-----------------------\n"
         << "  simulation mode    : " << fSimMode << "\n"
         << "  photons fired      : " << nbEvents << "\n"
         << "  photons converted  : " << nbConverted << " (" << fraction << " %)\n"
         << "  written to         : " << analysisManager->GetFileName() << "\n"
         << "  A low fraction means the block is too thin or minEnergy sits too\n"
         << "  close to the 1.022 MeV threshold.\n"
         << "------------------------------------------------------------\n"
         << G4endl;
}

void ConvRunAction::CountEvent(G4bool aConverted)
{
  fNbEvents += 1;
  if (aConverted) fNbConverted += 1;
}
