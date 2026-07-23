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
                                                   ConvLogger* aLogger, const G4String& aSimMode)
  : fDetector(aDetector), fLogger(aLogger), fSimMode(aSimMode)
{}

void ConvActionInitialization::BuildForMaster() const
{
  SetUserAction(new ConvRunAction(fSimMode));
}

void ConvActionInitialization::Build() const
{
  // Runs on the worker thread, so this replaces that thread's own cout
  // destination and sends its output to the shared log as well as the terminal
  if (fLogger != nullptr) {
    G4UImanager::GetUIpointer()->SetCoutDestination(fLogger);
  }

  SetUserAction(new ConvPrimaryGeneratorAction());

  auto runAction = new ConvRunAction(fSimMode);
  SetUserAction(runAction);

  auto eventAction = new ConvEventAction(runAction);
  SetUserAction(eventAction);

  // In "fast" mode ConvFastSimModel::DoIt does the readout straight into the
  // event action, so the stepping action -- which reads the "conv" step of the
  // full run -- is only installed for "full".
  if (fSimMode != "fast") {
    SetUserAction(new ConvSteppingAction(eventAction, fDetector));
  }
  SetUserAction(new ConvStackingAction());
}
