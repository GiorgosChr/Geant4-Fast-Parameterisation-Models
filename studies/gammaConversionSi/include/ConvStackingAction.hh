/// \file ConvStackingAction.hh
/// \brief Definition of the ConvStackingAction class

#ifndef CONV_STACKING_ACTION_HH
#define CONV_STACKING_ACTION_HH

#include "G4UserStackingAction.hh"

class G4Track;

/**
 * @brief Kills every secondary once it has been recorded.
 *
 * ConvSteppingAction reads the pair out of the conversion step itself, which
 * happens before the secondaries are pushed onto the stack. Tracking them
 * afterwards would only make them coast to the world boundary (they have no
 * process but transportation), so killing them here saves that time and
 * guarantees no secondary step can pollute the photon path measurement.
 */
class ConvStackingAction : public G4UserStackingAction
{
  public:
    ConvStackingAction() = default;
    ~ConvStackingAction() override = default;

    G4ClassificationOfNewTrack ClassifyNewTrack(const G4Track* aTrack) override;
};

#endif /* CONV_STACKING_ACTION_HH */
