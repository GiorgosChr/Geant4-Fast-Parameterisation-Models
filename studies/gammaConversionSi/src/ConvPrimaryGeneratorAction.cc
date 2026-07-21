/// \file ConvPrimaryGeneratorAction.cc
/// \brief Implementation of the ConvPrimaryGeneratorAction class

#include "ConvPrimaryGeneratorAction.hh"

#include "G4Box.hh"
#include "G4Event.hh"
#include "G4GenericMessenger.hh"
#include "G4LogicalVolume.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4ThreeVector.hh"
#include "Randomize.hh"

#include <cmath>

ConvPrimaryGeneratorAction::ConvPrimaryGeneratorAction()
{
  fParticleGun = new G4ParticleGun(1);
  fParticleGun->SetParticleDefinition(G4ParticleTable::GetParticleTable()->FindParticle("gamma"));
  fParticleGun->SetParticleMomentumDirection(G4ThreeVector(0., 0., 1.));
  fParticleGun->SetParticleEnergy(fMinEnergy);

  DefineCommands();
}

ConvPrimaryGeneratorAction::~ConvPrimaryGeneratorAction()
{
  delete fParticleGun;
  delete fMessenger;
}

void ConvPrimaryGeneratorAction::GeneratePrimaries(G4Event* aEvent)
{
  // Start just inside the upstream face of the (vacuum) world, so the gun
  // follows any change of the block dimensions
  G4double worldHalfZ = 1. * m;
  auto worldLogical = G4LogicalVolumeStore::GetInstance()->GetVolume("World", false);
  if (worldLogical != nullptr) {
    if (auto worldBox = dynamic_cast<G4Box*>(worldLogical->GetSolid())) {
      worldHalfZ = worldBox->GetZHalfLength();
    }
  }
  fParticleGun->SetParticlePosition(G4ThreeVector(0., 0., -0.999 * worldHalfZ));

  // Log-uniform sampling gives comparable statistics in every energy decade;
  // equal limits degenerate to a mono-energetic run
  G4double energy = fMinEnergy;
  if (fMaxEnergy > fMinEnergy) {
    energy = std::exp(std::log(fMinEnergy)
                      + G4UniformRand() * (std::log(fMaxEnergy) - std::log(fMinEnergy)));
  }
  fParticleGun->SetParticleEnergy(energy);

  fParticleGun->GeneratePrimaryVertex(aEvent);
}

void ConvPrimaryGeneratorAction::DefineCommands()
{
  fMessenger = new G4GenericMessenger(this, "/study/gun/", "Primary photon energy range");

  auto& minCmd = fMessenger->DeclareMethodWithUnit(
    "minEnergy", "MeV", &ConvPrimaryGeneratorAction::SetMinEnergy,
    "Lower edge of the log-uniform photon energy range.");
  minCmd.SetParameterName("minEnergy", true);
  minCmd.SetRange("minEnergy>=1.022");
  minCmd.SetDefaultValue("2.");

  auto& maxCmd = fMessenger->DeclareMethodWithUnit(
    "maxEnergy", "MeV", &ConvPrimaryGeneratorAction::SetMaxEnergy,
    "Upper edge of the log-uniform photon energy range. Set equal to minEnergy "
    "for a mono-energetic run.");
  maxCmd.SetParameterName("maxEnergy", true);
  maxCmd.SetRange("maxEnergy>=1.022");
  maxCmd.SetDefaultValue("10000.");
}
