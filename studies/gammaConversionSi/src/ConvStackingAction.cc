/// \file ConvStackingAction.cc
/// \brief Implementation of the ConvStackingAction class

#include "ConvStackingAction.hh"

#include "G4Track.hh"

G4ClassificationOfNewTrack ConvStackingAction::ClassifyNewTrack(const G4Track* aTrack)
{
  // The pair has already been recorded from the conversion step itself
  return (aTrack->GetParentID() == 0) ? fUrgent : fKill;
}
