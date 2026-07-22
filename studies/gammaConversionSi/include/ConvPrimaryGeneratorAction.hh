/// \file ConvPrimaryGeneratorAction.hh
/// \brief Definition of the ConvPrimaryGeneratorAction class

#ifndef CONV_PRIMARY_GENERATOR_ACTION_HH
#define CONV_PRIMARY_GENERATOR_ACTION_HH

#include "G4SystemOfUnits.hh"
#include "G4Types.hh"
#include "G4VUserPrimaryGeneratorAction.hh"

class G4Event;
class G4GenericMessenger;
class G4ParticleGun;

/**
 * @brief Fires one photon per event along +z from the upstream world face.
 *
 * The energy is sampled log-uniformly between /study/gun/minEnergy and
 * /study/gun/maxEnergy so that a single run covers the whole spectrum with
 * comparable statistics per order of magnitude. Setting the two limits equal
 * gives a mono-energetic run.
 *
 * The default lower limit is 2 MeV rather than the 1.022 MeV threshold: the
 * conversion cross section vanishes at threshold, so photons just above it
 * would essentially never convert even in a metre of silicon.
 */
class ConvPrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction
{
  public:
    ConvPrimaryGeneratorAction();
    ~ConvPrimaryGeneratorAction() override;

    void GeneratePrimaries(G4Event* aEvent) override;

    void SetMinEnergy(G4double aEnergy) { fMinEnergy = aEnergy; }
    void SetMaxEnergy(G4double aEnergy) { fMaxEnergy = aEnergy; }
    const G4ParticleGun* GetParticleGun() const { return fParticleGun; }

  private:
    void DefineCommands();

    G4ParticleGun* fParticleGun = nullptr;
    G4GenericMessenger* fMessenger = nullptr;
    G4double fMinEnergy = 2. * MeV;
    G4double fMaxEnergy = 10. * GeV;
};

#endif /* CONV_PRIMARY_GENERATOR_ACTION_HH */
