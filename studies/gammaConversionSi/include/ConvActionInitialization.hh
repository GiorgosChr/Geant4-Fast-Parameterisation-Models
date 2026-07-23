/// \file ConvActionInitialization.hh
/// \brief Definition of the ConvActionInitialization class

#ifndef CONV_ACTION_INITIALIZATION_HH
#define CONV_ACTION_INITIALIZATION_HH

#include "G4String.hh"
#include "G4VUserActionInitialization.hh"

class ConvDetectorConstruction;
class ConvLogger;

/// @brief Creates the user actions on the master and on each worker thread.
class ConvActionInitialization : public G4VUserActionInitialization
{
  public:
    /// @param aLogger  optional; when given, Build() also routes this worker
    ///        thread's output into the shared log file.
    /// @param aSimMode "full" or "fast"; in "fast" the ConvFastSimModel does the
    ///        readout, so the ConvSteppingAction is not installed.
    ConvActionInitialization(const ConvDetectorConstruction* aDetector,
                             ConvLogger* aLogger = nullptr, const G4String& aSimMode = "full");
    ~ConvActionInitialization() override = default;

    void BuildForMaster() const override;
    void Build() const override;

  private:
    const ConvDetectorConstruction* fDetector = nullptr;
    ConvLogger* fLogger = nullptr;
    G4String fSimMode = "full";
};

#endif /* CONV_ACTION_INITIALIZATION_HH */
