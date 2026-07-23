/// \file ConvFastSimModel.hh
/// \brief Definition of the ConvFastSimModel class

#ifndef CONV_FAST_SIM_MODEL_HH
#define CONV_FAST_SIM_MODEL_HH

#include "G4String.hh"
#include "G4VFastSimulationModel.hh"

#include <memory>

class ConversionFlowInference;
class G4BetheHeitler5DModel;
class G4ParticleDefinition;
class G4Region;

/**
 * @brief Fast-simulation model that replaces the final-state sampler of
 *        gamma -> e+e- conversion with the trained normalising flow.
 *
 * The conversion is decided exactly as Geant4 would: ModelTrigger draws the
 * interaction length from the real pair-production cross section (an owned,
 * standalone G4BetheHeitler5DModel, the same parametrisation the full run uses),
 * so the conversion rate and the vertex distribution are unchanged. Only when a
 * conversion falls inside the block does the flow run, turning the photon energy
 * into the pair kinematics -- the step the ML actually accelerates.
 *
 * Because the envelope is a single uniform block and the photon loses no energy
 * before converting, one exponential draw over the traversal is exactly
 * equivalent to Geant4's step-by-step sampling of the same constant mean free
 * path, so nothing is approximated in the "where" -- only the "what".
 *
 * DoIt reports the pair to ConvEventAction through the same calls the full-run
 * stepping action uses, so both modes fill the identical `conversions` ntuple
 * and the fast output can be validated against the full one. The flow supplies
 * the energy sharing, the leading polar angle and the recoil; the charge label,
 * the azimuth and the sub-lepton angle (by coplanar transverse-momentum balance)
 * are assigned here, since the flow does not model them.
 */
class ConvFastSimModel : public G4VFastSimulationModel
{
  public:
    /// @param aModelDir directory of the exported flow (see ConversionFlowInference).
    ConvFastSimModel(const G4String& aName, G4Region* aEnvelope, const G4String& aModelDir);
    ~ConvFastSimModel() override;

    G4bool IsApplicable(const G4ParticleDefinition& aParticle) override;
    G4bool ModelTrigger(const G4FastTrack& aFastTrack) override;
    void DoIt(const G4FastTrack& aFastTrack, G4FastStep& aFastStep) override;

  private:
    std::unique_ptr<ConversionFlowInference> fFlow;
    G4BetheHeitler5DModel* fXsModel = nullptr;
    /// Conversion depth sampled in ModelTrigger, consumed by the next DoIt.
    G4double fConversionDepth = 0.;
};

#endif /* CONV_FAST_SIM_MODEL_HH */
