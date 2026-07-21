/// \file ConvEventAction.cc
/// \brief Implementation of the ConvEventAction class

#include "ConvEventAction.hh"

#include "ConvRunAction.hh"

#include "G4AnalysisManager.hh"
#include "G4Event.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4SystemOfUnits.hh"

#include <cmath>

ConvEventAction::ConvEventAction(ConvRunAction* aRunAction) : fRunAction(aRunAction) {}

void ConvEventAction::BeginOfEventAction(const G4Event*)
{
  fPathInBlock = 0.;
  fConverted = false;
  fNbElectrons = 0;
  fNbPositrons = 0;
  fConversionPoint.set(0., 0., 0.);
  fElectronDirection.set(0., 0., 0.);
  fPositronDirection.set(0., 0., 0.);
  fElectronEnergy = 0.;
  fPositronEnergy = 0.;
  fRecoilEnergy = 0.;
  fIsTriplet = false;
}

void ConvEventAction::SetConversion(const G4ThreeVector& aVertex)
{
  fConverted = true;
  fConversionPoint = aVertex;
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

  // Right-handed frame around the photon direction, so that the difference of
  // the two azimuths measures the acoplanarity of the pair
  const G4ThreeVector xAxis = dirGamma.orthogonal().unit();
  const G4ThreeVector yAxis = dirGamma.cross(xAxis);
  auto azimuth = [&xAxis, &yAxis](const G4ThreeVector& aDirection) {
    return std::atan2(aDirection.dot(yAxis), aDirection.dot(xAxis));
  };

  auto analysisManager = G4AnalysisManager::Instance();
  analysisManager->FillNtupleDColumn(ConvRunAction::kEGamma, eGamma / MeV);
  analysisManager->FillNtupleDColumn(ConvRunAction::kPathInBlock, fPathInBlock / mm);
  analysisManager->FillNtupleDColumn(ConvRunAction::kEElectron, fElectronEnergy / MeV);
  analysisManager->FillNtupleDColumn(ConvRunAction::kEPositron, fPositronEnergy / MeV);
  analysisManager->FillNtupleDColumn(ConvRunAction::kThetaElectron,
                                     dirGamma.angle(fElectronDirection) / rad);
  analysisManager->FillNtupleDColumn(ConvRunAction::kThetaPositron,
                                     dirGamma.angle(fPositronDirection) / rad);
  analysisManager->FillNtupleDColumn(ConvRunAction::kPhiElectron, azimuth(fElectronDirection) / rad);
  analysisManager->FillNtupleDColumn(ConvRunAction::kPhiPositron, azimuth(fPositronDirection) / rad);
  analysisManager->FillNtupleDColumn(ConvRunAction::kOpeningAngle,
                                     fElectronDirection.angle(fPositronDirection) / rad);
  analysisManager->FillNtupleDColumn(ConvRunAction::kZConv, fConversionPoint.z() / mm);
  analysisManager->FillNtupleDColumn(ConvRunAction::kERecoil, fRecoilEnergy / MeV);
  analysisManager->FillNtupleIColumn(ConvRunAction::kIsTriplet, fIsTriplet ? 1 : 0);
  analysisManager->AddNtupleRow();
}
