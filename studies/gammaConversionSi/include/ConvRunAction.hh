/// \file ConvRunAction.hh
/// \brief Definition of the ConvRunAction class

#ifndef CONV_RUN_ACTION_HH
#define CONV_RUN_ACTION_HH

#include "G4Accumulable.hh"
#include "G4String.hh"
#include "G4Timer.hh"
#include "G4Types.hh"
#include "G4UserRunAction.hh"

class G4Run;

/**
 * @brief Owns the ROOT output and the end-of-run conversion statistics.
 *
 * The ntuple is created once per thread in the constructor (so that repeated
 * /run/beamOn calls reuse it) and the file is opened and written in
 * BeginOfRunAction / EndOfRunAction. Column meanings are documented in
 * README.md; energies are stored in MeV, lengths in mm, angles in rad.
 */
class ConvRunAction : public G4UserRunAction
{
  public:
    /// Column indices of the "conversions" ntuple.
    enum Column
    {
      kEGamma = 0,
      kPathInBlock,
      kEElectron,
      kEPositron,
      kThetaElectron,
      kThetaPositron,
      kPhiElectron,
      kPhiPositron,
      kOpeningAngle,
      kZConv,
      kERecoil,
      kIsTriplet
    };

    /// @param aSimMode "full" or "fast", reported on the BENCHMARK line so the
    ///        harness can attribute each timing to the right mode.
    explicit ConvRunAction(const G4String& aSimMode = "full");
    ~ConvRunAction() override = default;

    void BeginOfRunAction(const G4Run* aRun) override;
    void EndOfRunAction(const G4Run* aRun) override;

    /// Called once per event to keep track of the conversion efficiency.
    void CountEvent(G4bool aConverted);

  private:
    G4String fSimMode;
    /// Times only the event loop -- started after the file is opened, stopped
    /// before it is written -- so the per-experiment number excludes I/O.
    G4Timer fTimer;
    G4Accumulable<G4int> fNbEvents{"nbEvents", 0};
    G4Accumulable<G4int> fNbConverted{"nbConverted", 0};
};

#endif /* CONV_RUN_ACTION_HH */
