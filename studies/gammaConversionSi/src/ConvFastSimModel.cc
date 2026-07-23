/// \file ConvFastSimModel.cc
/// \brief Implementation of the ConvFastSimModel class

#include "ConvFastSimModel.hh"

#include "ConvEventAction.hh"
#include "ConversionFlowInference.hh"

#include "G4BetheHeitler5DModel.hh"
#include "G4DataVector.hh"
#include "G4DynamicParticle.hh"
#include "G4Electron.hh"
#include "G4EventManager.hh"
#include "G4FastStep.hh"
#include "G4FastTrack.hh"
#include "G4Gamma.hh"
#include "G4Material.hh"
#include "G4PhysicalConstants.hh"
#include "G4Positron.hh"
#include "G4Region.hh"
#include "G4SystemOfUnits.hh"
#include "G4Track.hh"
#include "G4VSolid.hh"
#include "Randomize.hh"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
/// Momentum magnitude of a lepton of kinetic energy `aEKin`.
G4double Momentum(G4double aEKin)
{
  return std::sqrt(aEKin * (aEKin + 2. * electron_mass_c2));
}
}  // namespace

ConvFastSimModel::ConvFastSimModel(const G4String& aName, G4Region* aEnvelope,
                                   const G4String& aModelDir)
  : G4VFastSimulationModel(aName, aEnvelope)
{
  fFlow = std::make_unique<ConversionFlowInference>(aModelDir);

  // A standalone copy of the full-run cross section: Initialise fills the static
  // element/LPM data so CrossSectionPerVolume below is a pure analytic call, no
  // run-time tables needed. Runs per worker thread (this ctor is reached from
  // ConstructSDandField), which is where G4Gamma is already defined.
  fXsModel = new G4BetheHeitler5DModel();
  fXsModel->Initialise(G4Gamma::Gamma(), G4DataVector());
}

ConvFastSimModel::~ConvFastSimModel()
{
  delete fXsModel;
}

G4bool ConvFastSimModel::IsApplicable(const G4ParticleDefinition& aParticle)
{
  return &aParticle == G4Gamma::GammaDefinition();
}

G4bool ConvFastSimModel::ModelTrigger(const G4FastTrack& aFastTrack)
{
  const G4Track* track = aFastTrack.GetPrimaryTrack();
  const G4double energy = track->GetKineticEnergy();
  const G4Material* material = track->GetMaterial();

  const G4double sigma =
    fXsModel->CrossSectionPerVolume(material, G4Gamma::Gamma(), energy, 0., DBL_MAX);
  if (sigma <= 0.) return false;

  // Sample the interaction length once over the whole traversal -- exact for a
  // uniform block, since the mean free path is constant along the path.
  const G4double depth = -std::log(G4UniformRand()) / sigma;
  const G4double distanceToOut = aFastTrack.GetEnvelopeSolid()->DistanceToOut(
    aFastTrack.GetPrimaryTrackLocalPosition(), aFastTrack.GetPrimaryTrackLocalDirection());
  if (depth > distanceToOut) return false;  // photon leaves before it converts

  fConversionDepth = depth;
  return true;
}

void ConvFastSimModel::DoIt(const G4FastTrack& aFastTrack, G4FastStep& aFastStep)
{
  const G4Track* track = aFastTrack.GetPrimaryTrack();
  const G4double eGamma = track->GetKineticEnergy();

  // The one call the whole exercise is about: photon energy -> pair kinematics.
  const ConvFlowSample sample = fFlow->Sample(eGamma / MeV);

  aFastStep.KillPrimaryTrack();
  aFastStep.ProposePrimaryTrackPathLength(fConversionDepth);
  aFastStep.ProposeTotalEnergyDeposited(0.);

  // Conversion vertex in the global frame. The photon travels undisturbed from
  // the region entry (its current position), so the vertex is simply that many
  // mm downstream along its direction.
  const G4ThreeVector gDir = track->GetMomentumDirection().unit();
  const G4ThreeVector vertex = track->GetPosition() + fConversionDepth * gDir;

  // Pair directions about the photon direction. The flow gives the leading
  // lepton's polar angle only; the sub-lepton is placed coplanar and opposite,
  // balancing transverse momentum, and the azimuth is uniform.
  const G4ThreeVector xAxis = gDir.orthogonal().unit();
  const G4ThreeVector yAxis = gDir.cross(xAxis);
  auto direction = [&](G4double aTheta, G4double aPhi) {
    return std::sin(aTheta) * std::cos(aPhi) * xAxis + std::sin(aTheta) * std::sin(aPhi) * yAxis
           + std::cos(aTheta) * gDir;
  };

  const G4double eLead = sample.eLead * MeV;
  const G4double eSub = sample.eSub * MeV;
  const G4double eRecoil = sample.eRecoil * MeV;

  const G4double phiLead = twopi * G4UniformRand();
  const G4double pLead = Momentum(eLead);
  const G4double pSub = Momentum(eSub);
  G4double sinSub = (pSub > 0.) ? pLead * std::sin(sample.thetaLead) / pSub : 0.;
  sinSub = std::clamp(sinSub, -1., 1.);
  const G4ThreeVector dirLead = direction(sample.thetaLead, phiLead);
  const G4ThreeVector dirSub = direction(std::asin(sinSub), phiLead + pi);

  // The pair is sorted by energy, not charge; assign the label at random.
  const G4bool leadIsElectron = G4UniformRand() < 0.5;
  const G4double eElectron = leadIsElectron ? eLead : eSub;
  const G4double ePositron = leadIsElectron ? eSub : eLead;
  const G4ThreeVector& dirElectron = leadIsElectron ? dirLead : dirSub;
  const G4ThreeVector& dirPositron = leadIsElectron ? dirSub : dirLead;

  // Emit the pair as real secondaries at the vertex (killed straight away by the
  // stacking action, exactly as in the full run, so tracking cost matches).
  aFastStep.SetNumberOfSecondaryTracks(2);
  const G4double time = track->GetGlobalTime();
  G4DynamicParticle electron(G4Electron::Electron(), dirElectron, eElectron);
  G4DynamicParticle positron(G4Positron::Positron(), dirPositron, ePositron);
  aFastStep.CreateSecondaryTrack(electron, vertex, time, false);
  aFastStep.CreateSecondaryTrack(positron, vertex, time, false);

  // Feed the ntuple through the same interface the full-run stepping action uses.
  auto* eventAction = static_cast<ConvEventAction*>(
    G4EventManager::GetEventManager()->GetUserEventAction());
  if (eventAction != nullptr) {
    eventAction->SetConversion(vertex);
    eventAction->AddPathInBlock(fConversionDepth);
    eventAction->AddElectron(eElectron, dirElectron);
    eventAction->AddPositron(ePositron, dirPositron);
    eventAction->SetRecoil(eRecoil, sample.isTriplet);
  }
}
