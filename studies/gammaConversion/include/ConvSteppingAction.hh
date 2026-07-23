/// \file ConvSteppingAction.hh
/// \brief Definition of the ConvSteppingAction class

#ifndef CONV_STEPPING_ACTION_HH
#define CONV_STEPPING_ACTION_HH

#include "G4UserSteppingAction.hh"

class G4Step;
class ConvEventAction;

/**
 * @brief Reads out the pair produced by the primary photon's conversion.
 *
 * Only steps of the primary photon are considered. Nothing competes with
 * conversion and it can only happen inside the block, so a "conv" post-step
 * process is the conversion vertex; the three secondaries are read straight out
 * of that step before they are stacked.
 */
class ConvSteppingAction : public G4UserSteppingAction
{
  public:
    explicit ConvSteppingAction(ConvEventAction* aEventAction);
    ~ConvSteppingAction() override = default;

    void UserSteppingAction(const G4Step* aStep) override;

  private:
    ConvEventAction* fEventAction = nullptr;
};

#endif /* CONV_STEPPING_ACTION_HH */
