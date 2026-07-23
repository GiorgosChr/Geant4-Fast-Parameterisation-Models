/// \file ConversionFlowInference.hh
/// \brief Definition of the ConversionFlowInference class

#ifndef CONVERSION_FLOW_INFERENCE_HH
#define CONVERSION_FLOW_INFERENCE_HH

#include "G4String.hh"
#include "G4Types.hh"

#include <memory>

/// One sampled gamma -> e+e- final state, in the units of the ntuple.
/// `eLead`/`eSub` are the higher/lower lepton *kinetic* energies (MeV), sorted
/// by magnitude rather than charge; `thetaLead` is the leading lepton's polar
/// angle with respect to the incident photon (rad); `eRecoil` is the recoil
/// kinetic energy (MeV) and `isTriplet` its mode (true = atomic-electron recoil).
struct ConvFlowSample
{
    G4double eLead = 0.;
    G4double eSub = 0.;
    G4double thetaLead = 0.;
    G4double eRecoil = 0.;
    G4bool isTriplet = false;
};

/**
 * @brief Runs the trained ConversionFlow from C++, via ONNX Runtime.
 *
 * The Python `nflows` model is exported by training/export_flow_onnx.py into
 * four ONNX graphs (a shared trunk plus one MADE per head) and a text file of
 * standardisation constants. Only the deterministic networks live in ONNX; the
 * stochastic remainder -- the standard-normal draws, the rational-quadratic
 * spline *inverse*, the de-standardisation and the `from_learned` coordinate
 * map -- is reimplemented here, bit-for-bit against the Python reference that
 * the export script validates.
 *
 * `Sample` draws all randomness from Geant4's engine (G4UniformRand,
 * G4RandGauss), so the fast-simulation run stays reproducible under the usual
 * /random/setSeeds. One instance owns its own ONNX sessions; construct one per
 * worker thread.
 */
class ConversionFlowInference
{
  public:
    /// @param aModelDir directory holding trunk.onnx, {recoil,lead,theta}_head.onnx
    ///        and flow_constants.txt (as written by export_flow_onnx.py).
    explicit ConversionFlowInference(const G4String& aModelDir);
    ~ConversionFlowInference();

    ConversionFlowInference(const ConversionFlowInference&) = delete;
    ConversionFlowInference& operator=(const ConversionFlowInference&) = delete;

    /// Draw one final state for a photon of kinetic energy `aEGammaMeV` (MeV).
    ConvFlowSample Sample(G4double aEGammaMeV);

  private:
    class Impl;
    std::unique_ptr<Impl> fImpl;
};

#endif /* CONVERSION_FLOW_INFERENCE_HH */
