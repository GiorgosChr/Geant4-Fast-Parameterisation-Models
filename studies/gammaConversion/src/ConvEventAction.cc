/// \file ConvEventAction.cc
/// \brief Implementation of the ConvEventAction class

#include "ConvEventAction.hh"

#include "ConvDetectorConstruction.hh"
#include "ConvRunAction.hh"

#include "G4AnalysisManager.hh"
#include "G4Event.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4SystemOfUnits.hh"

ConvEventAction::ConvEventAction(ConvRunAction* aRunAction,
                                 const ConvDetectorConstruction* aDetector)
  : fRunAction(aRunAction), fDetector(aDetector)
{}

void ConvEventAction::BeginOfEventAction(const G4Event*)
{
  fConverted = false;
  fNbElectrons = 0;
  fNbPositrons = 0;
  fElectronDirection.set(0., 0., 0.);
  fPositronDirection.set(0., 0., 0.);
  fElectronEnergy = 0.;
  fPositronEnergy = 0.;
  fRecoilEnergy = 0.;
  fIsTriplet = false;
}

void ConvEventAction::AddElectron(G4double aEnergy, const G4ThreeVector& aDirection)
{
  ++fNbElectrons;
  fElectronEnergy = aEnergy;
  fElectronDirection = aDirection;
}

void ConvEventAction::AddPositron(G4double aEnergy, const G4ThreeVector& aDirection)
{
  ++fNbPositrons;
  fPositronEnergy = aEnergy;
  fPositronDirection = aDirection;
}

void ConvEventAction::SetRecoil(G4double aEnergy, G4bool aIsTriplet)
{
  fRecoilEnergy = aEnergy;
  fIsTriplet = aIsTriplet;
}

void ConvEventAction::EndOfEventAction(const G4Event* aEvent)
{
  fRunAction->CountEvent(fConverted);

  if (!fConverted) return;
  if (fNbElectrons != 1 || fNbPositrons != 1) {
    G4Exception("ConvEventAction::EndOfEventAction", "UnexpectedFinalState", JustWarning,
                "Conversion did not yield one pair electron and one positron; event skipped.");
    return;
  }

  // The incident photon as it was generated, not as it was at conversion
  auto primary = aEvent->GetPrimaryVertex()->GetPrimary();
  const G4double eGamma = primary->GetKineticEnergy();
  const G4ThreeVector dirGamma = primary->GetMomentumDirection().unit();

  // theta is the polar angle of the leading (higher-energy) lepton with respect
  // to the incident photon -- the quantity the fast-simulation model is trained
  // to predict. eElectron and ePositron are stored separately, so which lepton
  // leads stays recoverable downstream.
  const G4bool electronLeads = fElectronEnergy >= fPositronEnergy;
  const G4ThreeVector& leadDirection = electronLeads ? fElectronDirection : fPositronDirection;
  const G4double theta = dirGamma.angle(leadDirection);

  // The ntuple is re-booked per run, so fills target this run's id explicitly.
  const G4int id = fRunAction->GetNtupleId();
  auto analysisManager = G4AnalysisManager::Instance();
  analysisManager->FillNtupleDColumn(id, ConvRunAction::kEGamma, eGamma / MeV);
  analysisManager->FillNtupleDColumn(id, ConvRunAction::kZ, fDetector->GetBlockZ());
  analysisManager->FillNtupleIColumn(id, ConvRunAction::kIsTriplet, fIsTriplet ? 1 : 0);
  analysisManager->FillNtupleDColumn(id, ConvRunAction::kERecoil, fRecoilEnergy / MeV);
  analysisManager->FillNtupleDColumn(id, ConvRunAction::kEElectron, fElectronEnergy / MeV);
  analysisManager->FillNtupleDColumn(id, ConvRunAction::kEPositron, fPositronEnergy / MeV);
  analysisManager->FillNtupleDColumn(id, ConvRunAction::kTheta, theta / rad);
  analysisManager->AddNtupleRow(id);
}
