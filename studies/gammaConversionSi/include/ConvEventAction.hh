/// \file ConvEventAction.hh
/// \brief Definition of the ConvEventAction class

#ifndef CONV_EVENT_ACTION_HH
#define CONV_EVENT_ACTION_HH

#include "G4ThreeVector.hh"
#include "G4Types.hh"
#include "G4UserEventAction.hh"

class G4Event;
class ConvRunAction;

/**
 * @brief Collects the conversion of one event and writes it as one ntuple row.
 *
 * ConvSteppingAction feeds the accumulated path in silicon and the properties
 * of the produced pair into this class; at the end of the event the angles
 * with respect to the incident photon are computed here and a single row is
 * filled. Events in which the photon left the block without converting fill no
 * row and are only counted.
 */
class ConvEventAction : public G4UserEventAction
{
  public:
    explicit ConvEventAction(ConvRunAction* aRunAction);
    ~ConvEventAction() override = default;

    void BeginOfEventAction(const G4Event* aEvent) override;
    void EndOfEventAction(const G4Event* aEvent) override;

    /// Add one step length travelled by the primary photon inside the block.
    void AddPathInBlock(G4double aLength) { fPathInBlock += aLength; }
    /// Flag the event as converted and record where it happened.
    void SetConversion(const G4ThreeVector& aVertex);
    void AddElectron(G4double aEnergy, const G4ThreeVector& aDirection);
    void AddPositron(G4double aEnergy, const G4ThreeVector& aDirection);
    /// Record the recoil: the nucleus, or a second electron for triplet
    /// conversion on an atomic electron.
    void SetRecoil(G4double aEnergy, G4bool aIsTriplet);

  private:
    ConvRunAction* fRunAction = nullptr;
    /// Path length of the primary photon inside the block.
    G4double fPathInBlock = 0.;
    G4bool fConverted = false;
    /// Guards against a malformed final state (should always end up 1 and 1).
    G4int fNbElectrons = 0;
    G4int fNbPositrons = 0;
    G4ThreeVector fConversionPoint;
    G4ThreeVector fElectronDirection;
    G4ThreeVector fPositronDirection;
    G4double fElectronEnergy = 0.;
    G4double fPositronEnergy = 0.;
    /// Kinetic energy taken by the recoiling nucleus or atomic electron.
    G4double fRecoilEnergy = 0.;
    G4bool fIsTriplet = false;
};

#endif /* CONV_EVENT_ACTION_HH */
