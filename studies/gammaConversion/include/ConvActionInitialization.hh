/// \file ConvActionInitialization.hh
/// \brief Definition of the ConvActionInitialization class

#ifndef CONV_ACTION_INITIALIZATION_HH
#define CONV_ACTION_INITIALIZATION_HH

#include "G4VUserActionInitialization.hh"

class ConvDetectorConstruction;
class ConvLogger;

/// @brief Creates the user actions on the master and on each worker thread.
class ConvActionInitialization : public G4VUserActionInitialization
{
  public:
    /// @param aLogger optional; when given, Build() also routes this worker
    ///        thread's output into the shared log file.
    ConvActionInitialization(const ConvDetectorConstruction* aDetector,
                             ConvLogger* aLogger = nullptr);
    ~ConvActionInitialization() override = default;

    void BuildForMaster() const override;
    void Build() const override;

  private:
    const ConvDetectorConstruction* fDetector = nullptr;
    ConvLogger* fLogger = nullptr;
};

#endif /* CONV_ACTION_INITIALIZATION_HH */
