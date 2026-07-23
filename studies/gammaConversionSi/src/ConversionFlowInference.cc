/// \file ConversionFlowInference.cc
/// \brief Implementation of the ConversionFlowInference class

#include "ConversionFlowInference.hh"

#include "G4Exception.hh"
#include "Randomize.hh"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

// The rational-quadratic spline reimplemented below mirrors
// nflows/transforms/splines/rational_quadratic.py exactly. The hyper-parameters
// (num_bins, tail_bound, the three floors) are read from flow_constants.txt and
// checked against what the graphs were trained with; the maths -- softmax over
// widths/heights, the linear-tail derivative padding, the quadratic root in the
// inverse direction -- is a line-by-line port whose agreement with the library
// (max abs err 1e-5) is asserted by export_flow_onnx.py before this ever runs.

namespace
{
constexpr G4double kTiny = 1e-30;  // matches conversion_flow._TINY

G4double Sigmoid(G4double aX)
{
  return 1. / (1. + std::exp(-aX));
}

G4double Softplus(G4double aX)
{
  // log1p(exp(-|x|)) + max(x, 0) -- overflow-safe, as in the numpy reference
  return std::log1p(std::exp(-std::abs(aX))) + std::max(aX, 0.);
}
}  // namespace

class ConversionFlowInference::Impl
{
  public:
    explicit Impl(const G4String& aModelDir)
      : fEnv(ORT_LOGGING_LEVEL_WARNING, "ConversionFlow"),
        fMemory(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
    {
      // One thread per session: the fast-sim loop already calls Sample once per
      // event on the worker thread, so intra-op parallelism would only add
      // contention. Keep ONNX single-threaded and let Geant4 own the threads.
      fOptions.SetIntraOpNumThreads(1);
      fOptions.SetInterOpNumThreads(1);
      fOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

      ReadConstants(aModelDir + "/flow_constants.txt");
      fTrunk = MakeSession(aModelDir + "/trunk.onnx");
      fRecoil = MakeSession(aModelDir + "/recoil_head.onnx");
      fLead = MakeSession(aModelDir + "/lead_head.onnx");
      fTheta = MakeSession(aModelDir + "/theta_head.onnx");

      fTrunkBuf.resize(fTrunkFeatures);
      fContext.resize(fTrunkFeatures + 1);
    }

    ConvFlowSample Sample(G4double aEGammaMeV);

  private:
    std::unique_ptr<Ort::Session> MakeSession(const G4String& aPath)
    {
      try {
        return std::make_unique<Ort::Session>(fEnv, aPath.c_str(), fOptions);
      }
      catch (const Ort::Exception& e) {
        G4Exception("ConversionFlowInference", "ModelLoad", FatalException,
                    ("could not load '" + aPath + "': " + e.what()).c_str());
        return nullptr;  // unreachable
      }
    }

    void ReadConstants(const G4String& aPath);
    G4double SplineInverse(const float* aParams, G4double aZ) const;

    // Run a single-row graph: `aInput` of length `aInLen` -> `aOut` of length
    // `aOutLen`, reading the graph's first output.
    void RunGraph(Ort::Session& aSession, const char* aInName, const float* aInput,
                  int64_t aInLen, const char* aOutName, float* aOut, int64_t aOutLen);

    Ort::Env fEnv;
    Ort::SessionOptions fOptions;
    Ort::MemoryInfo fMemory;
    std::unique_ptr<Ort::Session> fTrunk, fRecoil, fLead, fTheta;

    std::vector<float> fTrunkBuf;  // reused (trunk_features)
    std::vector<float> fContext;   // reused (trunk_features + 1)

    // -- constants from flow_constants.txt --
    int fNumBins = 16;
    int fTrunkFeatures = 64;
    G4double fTailBound = 6.;
    G4double fMinBinWidth = 1e-3;
    G4double fMinBinHeight = 1e-3;
    G4double fMinDerivative = 1e-3;
    G4double fParamScale = 1.;
    G4double fElectronMass = 0.51099895;
    G4double fRecoilMu[2] = {0., 0.};
    G4double fRecoilSigma[2] = {1., 1.};
    G4double fLeadMu = 0., fLeadSigma = 1.;
    G4double fThetaMu = 0., fThetaSigma = 1.;
};

void ConversionFlowInference::Impl::ReadConstants(const G4String& aPath)
{
  std::ifstream in(aPath);
  if (!in) {
    G4Exception("ConversionFlowInference", "ConstantsMissing", FatalException,
                ("cannot open " + aPath).c_str());
  }
  std::string key;
  while (in >> key) {
    if (key == "num_bins")
      in >> fNumBins;
    else if (key == "trunk_features")
      in >> fTrunkFeatures;
    else if (key == "context_features") {
      int ignored;
      in >> ignored;  // == trunk_features + 1, kept for the file's completeness
    }
    else if (key == "tail_bound")
      in >> fTailBound;
    else if (key == "min_bin_width")
      in >> fMinBinWidth;
    else if (key == "min_bin_height")
      in >> fMinBinHeight;
    else if (key == "min_derivative")
      in >> fMinDerivative;
    else if (key == "param_scale")
      in >> fParamScale;
    else if (key == "electron_mass")
      in >> fElectronMass;
    else if (key == "recoil_mu")
      in >> fRecoilMu[0] >> fRecoilMu[1];
    else if (key == "recoil_sigma")
      in >> fRecoilSigma[0] >> fRecoilSigma[1];
    else if (key == "lead_mu")
      in >> fLeadMu;
    else if (key == "lead_sigma")
      in >> fLeadSigma;
    else if (key == "theta_mu")
      in >> fThetaMu;
    else if (key == "theta_sigma")
      in >> fThetaSigma;
    else {
      G4double skip;
      in >> skip;  // unknown key, one value -- forward-compatible
    }
  }
}

void ConversionFlowInference::Impl::RunGraph(Ort::Session& aSession, const char* aInName,
                                             const float* aInput, int64_t aInLen,
                                             const char* aOutName, float* aOut, int64_t aOutLen)
{
  const int64_t inShape[2] = {1, aInLen};
  Ort::Value input = Ort::Value::CreateTensor<float>(fMemory, const_cast<float*>(aInput), aInLen,
                                                     inShape, 2);
  const char* inNames[1] = {aInName};
  const char* outNames[1] = {aOutName};
  auto outputs = aSession.Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
  const float* data = outputs.front().GetTensorData<float>();
  for (int64_t i = 0; i < aOutLen; ++i) aOut[i] = data[i];
}

G4double ConversionFlowInference::Impl::SplineInverse(const float* aParams, G4double aZ) const
{
  const int nb = fNumBins;
  const G4double tb = fTailBound;
  if (aZ < -tb || aZ > tb) return aZ;  // linear tail: identity

  // widths and heights on [-tb, tb]
  std::vector<G4double> cumw(nb + 1), cumh(nb + 1), width(nb), height(nb);
  auto buildKnots = [&](int offset, std::vector<G4double>& cum, std::vector<G4double>& bin,
                        G4double lo, G4double hi, G4double minBin) {
    G4double maxu = -1e300;
    for (int i = 0; i < nb; ++i) maxu = std::max(maxu, aParams[offset + i] / fParamScale);
    G4double sum = 0.;
    std::vector<G4double> e(nb);
    for (int i = 0; i < nb; ++i) {
      e[i] = std::exp(aParams[offset + i] / fParamScale - maxu);
      sum += e[i];
    }
    cum[0] = 0.;
    for (int i = 0; i < nb; ++i) {
      const G4double w = minBin + (1. - minBin * nb) * (e[i] / sum);
      cum[i + 1] = cum[i] + w;
    }
    for (int i = 0; i <= nb; ++i) cum[i] = (hi - lo) * cum[i] + lo;
    cum[0] = lo;
    cum[nb] = hi;
    for (int i = 0; i < nb; ++i) bin[i] = cum[i + 1] - cum[i];
  };
  buildKnots(0, cumw, width, -tb, tb, fMinBinWidth);
  buildKnots(nb, cumh, height, -tb, tb, fMinBinHeight);

  // derivatives: nb-1 raw values, padded to nb+1 with the linear-tail constant
  std::vector<G4double> deriv(nb + 1);
  const G4double edgeConst = std::log(std::exp(1. - fMinDerivative) - 1.);
  deriv[0] = fMinDerivative + Softplus(edgeConst);
  deriv[nb] = fMinDerivative + Softplus(edgeConst);
  for (int i = 0; i < nb - 1; ++i) {
    deriv[i + 1] = fMinDerivative + Softplus(aParams[2 * nb + i]);
  }

  // bin index: last knot with cumh[k] <= z (searchsorted right, minus one)
  int bin = 0;
  for (int i = 0; i <= nb; ++i) {
    if (cumh[i] <= aZ) bin = i;
  }
  if (bin > nb - 1) bin = nb - 1;
  if (bin < 0) bin = 0;

  const G4double inCumw = cumw[bin];
  const G4double inBw = width[bin];
  const G4double inCumh = cumh[bin];
  const G4double inH = height[bin];
  const G4double delta = inH / inBw;
  const G4double d0 = deriv[bin];
  const G4double d1 = deriv[bin + 1];

  const G4double dz = aZ - inCumh;
  const G4double a = dz * (d0 + d1 - 2. * delta) + inH * (delta - d0);
  const G4double b = inH * d0 - dz * (d0 + d1 - 2. * delta);
  const G4double c = -delta * dz;
  G4double disc = b * b - 4. * a * c;
  if (disc < 0.) disc = 0.;
  const G4double root = (2. * c) / (-b - std::sqrt(disc));
  return root * inBw + inCumw;
}

ConvFlowSample ConversionFlowInference::Impl::Sample(G4double aEGammaMeV)
{
  // (1) trunk + triplet logit -- the trunk graph returns both in one run
  const float eGamma = static_cast<float>(aEGammaMeV);
  float logit = 0.f;
  {
    const int64_t inShape[2] = {1, 1};
    Ort::Value input = Ort::Value::CreateTensor<float>(fMemory, const_cast<float*>(&eGamma), 1,
                                                       inShape, 2);
    const char* inNames[1] = {"eGamma"};
    const char* outNames[2] = {"trunk", "triplet_logit"};
    auto out = fTrunk->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 2);
    const float* trunk = out[0].GetTensorData<float>();
    for (int i = 0; i < fTrunkFeatures; ++i) fTrunkBuf[i] = trunk[i];
    logit = out[1].GetTensorData<float>()[0];
  }

  // (2) triplet mode
  const G4double pTriplet = Sigmoid(logit);
  const G4bool isTriplet = G4UniformRand() < pTriplet;
  const int mode = isTriplet ? 1 : 0;

  // helper: build context [trunk, conditioner], run head, invert spline
  auto drawHead = [&](Ort::Session& head, G4double conditioner) {
    for (int i = 0; i < fTrunkFeatures; ++i) fContext[i] = fTrunkBuf[i];
    fContext[fTrunkFeatures] = static_cast<float>(conditioner);
    const int nParams = 3 * fNumBins - 1;
    std::vector<float> params(nParams);
    RunGraph(head, "context", fContext.data(), fTrunkFeatures + 1, "params", params.data(),
             nParams);
    return SplineInverse(params.data(), G4RandGauss::shoot(0., 1.));
  };

  // (3-5) chained heads, each conditioned on the previous standardised sample
  const G4double uRecoil = drawHead(*fRecoil, isTriplet ? 1. : 0.);
  const G4double uLead = drawHead(*fLead, uRecoil);
  const G4double uTheta = drawHead(*fTheta, uLead);

  // (6) de-standardise
  const G4double zRecoil = uRecoil * fRecoilSigma[mode] + fRecoilMu[mode];
  const G4double zLead = uLead * fLeadSigma + fLeadMu;
  const G4double zTheta = uTheta * fThetaSigma + fThetaMu;

  // (7) from_learned -> physical, energy-conserving by construction
  const G4double total = std::max(aEGammaMeV - 2. * fElectronMass, kTiny);
  const G4double eRecoil = Sigmoid(zRecoil) * total;
  const G4double shared = total - eRecoil;
  const G4double eLead = 0.5 * (Sigmoid(zLead) + 1.) * shared;
  const G4double eSub = shared - eLead;
  const G4double thetaLead = std::exp(zTheta) * fElectronMass / std::max(eLead, kTiny);

  return {eLead, eSub, thetaLead, eRecoil, isTriplet};
}

// -- public wrapper ---------------------------------------------------------

ConversionFlowInference::ConversionFlowInference(const G4String& aModelDir)
  : fImpl(std::make_unique<Impl>(aModelDir))
{}

ConversionFlowInference::~ConversionFlowInference() = default;

ConvFlowSample ConversionFlowInference::Sample(G4double aEGammaMeV)
{
  return fImpl->Sample(aEGammaMeV);
}
