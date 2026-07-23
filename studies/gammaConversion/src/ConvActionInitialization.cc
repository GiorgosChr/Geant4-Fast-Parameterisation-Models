/// \file ConvActionInitialization.cc
/// \brief Implementation of the ConvActionInitialization class

#include "ConvActionInitialization.hh"

#include "ConvEventAction.hh"
#include "ConvLogger.hh"
#include "ConvPrimaryGeneratorAction.hh"
#include "ConvRunAction.hh"
#include "ConvStackingAction.hh"
#include "ConvSteppingAction.hh"

#include "G4UImanager.hh"

ConvActionInitialization::ConvActionInitialization(const ConvDetectorConstruction* aDetector,
                                                   ConvLogger* aLogger)
  : fDetector(aDetector), fLogger(aLogger)
{}

void ConvActionInitialization::BuildForMaster() const
{
  SetUserAction(new ConvRunAction());
}

void ConvActionInitialization::Build() const
{
  // Runs on the worker thread, so this replaces that thread's own cout
  // destination and sends its output to the shared log as well as the terminal
  if (fLogger != nullptr) {
    G4UImanager::GetUIpointer()->SetCoutDestination(fLogger);
  }

  SetUserAction(new ConvPrimaryGeneratorAction());

  auto runAction = new ConvRunAction();
  SetUserAction(runAction);

  auto eventAction = new ConvEventAction(runAction, fDetector);
  SetUserAction(eventAction);

  SetUserAction(new ConvSteppingAction(eventAction));
  SetUserAction(new ConvStackingAction());
}
