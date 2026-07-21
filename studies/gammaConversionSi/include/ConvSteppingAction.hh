/// \file ConvSteppingAction.hh
/// \brief Definition of the ConvSteppingAction class

#ifndef CONV_STEPPING_ACTION_HH
#define CONV_STEPPING_ACTION_HH

#include "G4UserSteppingAction.hh"

class G4Step;
class ConvDetectorConstruction;
class ConvEventAction;

/**
 * @brief Measures the photon path in silicon and reads out the produced pair.
 *
 * Only steps of the primary photon are considered. The path inside silicon is
 * accumulated step by step rather than taken from G4Track::GetTrackLength(),
 * so it stays correct however far upstream of the block the gun sits.
 */
class ConvSteppingAction : public G4UserSteppingAction
{
  public:
    ConvSteppingAction(ConvEventAction* aEventAction, const ConvDetectorConstruction* aDetector);
    ~ConvSteppingAction() override = default;

    void UserSteppingAction(const G4Step* aStep) override;

  private:
    ConvEventAction* fEventAction = nullptr;
    const ConvDetectorConstruction* fDetector = nullptr;
};

#endif /* CONV_STEPPING_ACTION_HH */
