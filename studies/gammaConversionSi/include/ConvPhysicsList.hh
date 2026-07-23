/// \file ConvPhysicsList.hh
/// \brief Definition of the ConvPhysicsList class

#ifndef CONV_PHYSICS_LIST_HH
#define CONV_PHYSICS_LIST_HH

#include "G4String.hh"
#include "G4VUserPhysicsList.hh"

/**
 * @brief Physics list with gamma conversion as the only physical process.
 *
 * Only gamma, e- and e+ are constructed. The gamma gets transportation plus
 * G4GammaConversion; the pair gets transportation only, so the e- and e+ fly
 * straight from the conversion vertex and cannot feed anything back into the
 * event. Nothing competes with conversion, so every photon interaction in the
 * run is a conversion.
 *
 * The final-state model matters for this study and is therefore explicit:
 *
 * - "BetheHeitler5D" (default) samples the full five-dimensional final state,
 *   so the polar angles, the energy sharing and the azimuthal correlation of
 *   the pair are all physical. This is what the accurate reference lists
 *   (EM opt3/opt4, Livermore) use.
 * - "PairProductionRel" is what a bare G4GammaConversion falls back to. It is
 *   a relativistic approximation that emits an exactly coplanar pair
 *   (phiElectron - phiPositron is identically pi), so it is only useful as a
 *   comparison point, not for an angular study.
 */
class ConvPhysicsList : public G4VUserPhysicsList
{
  public:
    /// @param aModel           "BetheHeitler5D" or "PairProductionRel"
    /// @param aConversionType  0 mixed, 1 nuclear only, 2 triplet only
    /// @param aSimMode         "full" adds G4GammaConversion to the photon;
    ///        "fast" adds the G4FastSimulationManagerProcess instead, so the
    ///        ConvFastSimModel owns the conversion and its flow supplies the pair.
    explicit ConvPhysicsList(const G4String& aModel = "BetheHeitler5D", G4int aConversionType = 0,
                             const G4String& aSimMode = "full");
    ~ConvPhysicsList() override = default;

    void ConstructParticle() override;
    void ConstructProcess() override;
    void SetCuts() override;

  private:
    G4String fModel;
    G4int fConversionType;
    G4String fSimMode;
};

#endif /* CONV_PHYSICS_LIST_HH */
