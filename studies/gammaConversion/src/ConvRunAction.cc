/// \file ConvRunAction.cc
/// \brief Implementation of the ConvRunAction class

#include "ConvRunAction.hh"

#include "G4AccumulableManager.hh"
#include "G4AnalysisManager.hh"
#include "G4Run.hh"

#include <filesystem>

ConvRunAction::ConvRunAction()
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
  analysisManager->SetFileName("ntuples/gammaConversion");
  analysisManager->SetNtupleMerging(true);
  analysisManager->SetVerboseLevel(0);

  // Booked once here (one process per material -- a material scan runs each in
  // its own process, see gammaConversion.cc). The creation order sets the
  // column indices, so it must match the Column enum. Energies in MeV, angles
  // in rad -- see README.md for the full description of each column.
  fNtupleId = analysisManager->CreateNtuple("conversions", "Photon conversions");
  analysisManager->CreateNtupleDColumn("eGamma");     // kEGamma
  analysisManager->CreateNtupleDColumn("Z");          // kZ
  analysisManager->CreateNtupleIColumn("isTriplet");  // kIsTriplet
  analysisManager->CreateNtupleDColumn("eRecoil");    // kERecoil
  analysisManager->CreateNtupleDColumn("eElectron");  // kEElectron
  analysisManager->CreateNtupleDColumn("ePositron");  // kEPositron
  analysisManager->CreateNtupleDColumn("theta");      // kTheta
  analysisManager->FinishNtuple();
}

void ConvRunAction::BeginOfRunAction(const G4Run*)
{
  G4AccumulableManager::Instance()->Reset();
  G4AnalysisManager::Instance()->OpenFile();
}

void ConvRunAction::EndOfRunAction(const G4Run* aRun)
{
  auto analysisManager = G4AnalysisManager::Instance();
  analysisManager->Write();
  analysisManager->CloseFile();

  G4AccumulableManager::Instance()->Merge();

  if (!IsMaster()) return;

  const G4int nbEvents = fNbEvents.GetValue();
  const G4int nbConverted = fNbConverted.GetValue();
  const G4double fraction = (nbEvents > 0) ? 100. * nbConverted / nbEvents : 0.;

  G4cout << "\n--------------------End of Global Run-----------------------\n"
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
