/// \file ConvRunAction.hh
/// \brief Definition of the ConvRunAction class

#ifndef CONV_RUN_ACTION_HH
#define CONV_RUN_ACTION_HH

#include "G4Accumulable.hh"
#include "G4Types.hh"
#include "G4UserRunAction.hh"

class G4Run;

/**
 * @brief Owns the ROOT output and the end-of-run conversion statistics.
 *
 * The ntuple is created once per thread in the constructor (so that repeated
 * /run/beamOn calls, one per material, reuse it) and the file is opened and
 * written in BeginOfRunAction / EndOfRunAction. Column meanings are documented
 * in README.md; energies are stored in MeV, angles in rad.
 */
class ConvRunAction : public G4UserRunAction
{
  public:
    /// Column indices of the "conversions" ntuple.
    enum Column
    {
      kEGamma = 0,
      kZ,
      kIsTriplet,
      kERecoil,
      kEElectron,
      kEPositron,
      kTheta
    };

    ConvRunAction();
    ~ConvRunAction() override = default;

    void BeginOfRunAction(const G4Run* aRun) override;
    void EndOfRunAction(const G4Run* aRun) override;

    /// Called once per event to keep track of the conversion efficiency.
    void CountEvent(G4bool aConverted);

    /// Id of the "conversions" ntuple, for the explicit fill overloads.
    G4int GetNtupleId() const { return fNtupleId; }

  private:
    G4int fNtupleId = 0;

    G4Accumulable<G4int> fNbEvents{"nbEvents", 0};
    G4Accumulable<G4int> fNbConverted{"nbConverted", 0};
};

#endif /* CONV_RUN_ACTION_HH */
