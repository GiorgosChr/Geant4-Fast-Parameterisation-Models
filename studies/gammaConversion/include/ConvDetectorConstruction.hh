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
 * path, so that essentially every photon converts before leaving it. The world
 * is filled with G4_Galactic, so the photon travels from the gun to the block
 * without interacting.
 *
 * Material and both dimensions can be changed at run time with
 * /study/det/material, /study/det/blockThickness and /study/det/blockWidth.
 * The material loop in main() steps the material between runs this way, so the
 * study covers a whole list of materials in one job; GetBlockZ() then exposes
 * the current material's atomic number for the ntuple.
 */
class ConvDetectorConstruction : public G4VUserDetectorConstruction
{
  public:
    ConvDetectorConstruction();
    ~ConvDetectorConstruction() override;

    G4VPhysicalVolume* Construct() override;

    /// Set the block material by NIST name, e.g. G4_Si or G4_Pb.
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
    /// Atomic number of the (single-element) block material, or 0 before the
    /// geometry is built. Read per event to tag each ntuple row with its Z.
    G4double GetBlockZ() const;

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
};

#endif /* CONV_DETECTOR_CONSTRUCTION_HH */
