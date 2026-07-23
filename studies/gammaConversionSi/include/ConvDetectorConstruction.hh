/// \file ConvDetectorConstruction.hh
/// \brief Definition of the ConvDetectorConstruction class

#ifndef CONV_DETECTOR_CONSTRUCTION_HH
#define CONV_DETECTOR_CONSTRUCTION_HH

#include "G4String.hh"
#include "G4SystemOfUnits.hh"
#include "G4Types.hh"
#include "G4VUserDetectorConstruction.hh"

class G4GenericMessenger;
class G4LogicalVolume;
class G4VPhysicalVolume;

/**
 * @brief A single block of one NIST material inside a vacuum world.
 *
 * The block is deliberately much thicker than the pair-production mean free
 * path (~13 cm in silicon at 1 GeV), so that essentially every photon converts
 * before leaving it. The world is filled with G4_Galactic, so the photon
 * travels from the gun to the block without interacting and the path
 * accumulated by ConvSteppingAction is purely the path inside the block.
 *
 * Material and both dimensions can be changed at run time with
 * /study/det/material, /study/det/blockThickness and /study/det/blockWidth.
 */
class ConvDetectorConstruction : public G4VUserDetectorConstruction
{
  public:
    /// @param aSimMode      "full" or "fast"; in "fast" the block becomes a
    ///        G4Region envelope carrying the ConvFastSimModel.
    /// @param aFlowModelDir directory of the exported flow, forwarded to the model.
    explicit ConvDetectorConstruction(const G4String& aSimMode = "full",
                                      const G4String& aFlowModelDir = "models/onnx");
    ~ConvDetectorConstruction() override;

    G4VPhysicalVolume* Construct() override;
    /// Attaches the fast-simulation model in "fast" mode; runs per worker thread.
    void ConstructSDandField() override;

    /// Set the block material by NIST name, e.g. G4_Si or G4_PbWO4.
    void SetMaterial(const G4String& aName);
    /// Set the full extent of the block along the beam axis (z).
    void SetBlockThickness(G4double aThickness);
    /// Set the full transverse extent of the block (x and y).
    void SetBlockWidth(G4double aWidth);

    G4String GetMaterialName() const { return fMaterialName; }
    G4double GetBlockThickness() const { return fBlockThickness; }
    G4double GetBlockWidth() const { return fBlockWidth; }
    /// Logical volume of the block, used to identify steps inside it.
    const G4LogicalVolume* GetBlockVolume() const { return fBlockVolume; }

  private:
    void DefineCommands();

    G4GenericMessenger* fMessenger = nullptr;
    G4LogicalVolume* fBlockVolume = nullptr;
    /// NIST name of the block material.
    G4String fMaterialName = "G4_Si";
    /// Full extent of the block along the beam axis.
    G4double fBlockThickness = 1. * m;
    /// Full transverse extent of the block.
    G4double fBlockWidth = 1. * m;
    /// "full" or "fast"; only "fast" builds a region and a fast-sim model.
    G4String fSimMode = "full";
    /// Directory of the exported flow, passed to ConvFastSimModel.
    G4String fFlowModelDir = "models/onnx";
};

#endif /* CONV_DETECTOR_CONSTRUCTION_HH */
