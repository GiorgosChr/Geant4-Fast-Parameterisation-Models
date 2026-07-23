/// \file ConvSteppingAction.cc
/// \brief Implementation of the ConvSteppingAction class

#include "ConvSteppingAction.hh"

#include "ConvEventAction.hh"

#include "G4Gamma.hh"
#include "G4Step.hh"
#include "G4Track.hh"
#include "G4VProcess.hh"

ConvSteppingAction::ConvSteppingAction(ConvEventAction* aEventAction) : fEventAction(aEventAction) {}

void ConvSteppingAction::UserSteppingAction(const G4Step* aStep)
{
  const G4Track* track = aStep->GetTrack();
  // Only the primary photon is of interest; secondaries are killed anyway
  if (track->GetParentID() != 0) return;
  if (track->GetDefinition() != G4Gamma::Definition()) return;

  const G4VProcess* process = aStep->GetPostStepPoint()->GetProcessDefinedStep();
  if (process == nullptr || process->GetProcessName() != "conv") return;

  fEventAction->SetConversion();

  // Read the final state straight out of the conversion step, before it is
  // stacked. G4BetheHeitler5DModel pushes exactly three secondaries in the
  // order e-, e+, recoil -- the recoil being the nucleus for the usual nuclear
  // conversion, or a second electron for triplet conversion on an atomic
  // electron. The first e- is therefore always the one from the pair.
  G4bool haveElectron = false;
  for (const G4Track* secondary : *(aStep->GetSecondaryInCurrentStep())) {
    const G4int pdg = secondary->GetDefinition()->GetPDGEncoding();
    if (pdg == 11 && !haveElectron) {
      haveElectron = true;
      fEventAction->AddElectron(secondary->GetKineticEnergy(), secondary->GetMomentumDirection());
    }
    else if (pdg == -11) {
      fEventAction->AddPositron(secondary->GetKineticEnergy(), secondary->GetMomentumDirection());
    }
    else {
      fEventAction->SetRecoil(secondary->GetKineticEnergy(), pdg == 11);
    }
  }
}
