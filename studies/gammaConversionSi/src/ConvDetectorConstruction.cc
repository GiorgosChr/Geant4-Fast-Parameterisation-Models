/// \file ConvDetectorConstruction.cc
/// \brief Implementation of the ConvDetectorConstruction class

#include "ConvDetectorConstruction.hh"

#include "G4Box.hh"
#include "G4GenericMessenger.hh"
#include "G4GeometryManager.hh"
#include "G4LogicalVolume.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4Material.hh"
#include "G4NistManager.hh"
#include "G4PVPlacement.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4RunManager.hh"
#include "G4SolidStore.hh"
#include "G4ThreeVector.hh"
#include "G4UnitsTable.hh"
#include "G4VisAttributes.hh"

ConvDetectorConstruction::ConvDetectorConstruction()
{
  DefineCommands();
}

ConvDetectorConstruction::~ConvDetectorConstruction()
{
  delete fMessenger;
}

G4VPhysicalVolume* ConvDetectorConstruction::Construct()
{
  // Allow the geometry to be rebuilt after /study/det/... commands
  G4GeometryManager::GetInstance()->OpenGeometry();
  G4PhysicalVolumeStore::GetInstance()->Clean();
  G4LogicalVolumeStore::GetInstance()->Clean();
  G4SolidStore::GetInstance()->Clean();

  auto nist = G4NistManager::Instance();
  auto vacuum = nist->FindOrBuildMaterial("G4_Galactic");
  auto blockMaterial = nist->FindOrBuildMaterial(fMaterialName);
  if (blockMaterial == nullptr) {
    G4Exception("ConvDetectorConstruction::Construct", "UnknownMaterial", FatalException,
                ("No NIST material named '" + fMaterialName + "'").c_str());
  }

  // World: vacuum, 20% larger than the block, so the photon reaches the block
  // without interacting anywhere else
  const G4double worldHalfXY = 0.6 * fBlockWidth;
  const G4double worldHalfZ = 0.6 * fBlockThickness;
  auto worldSolid = new G4Box("World", worldHalfXY, worldHalfXY, worldHalfZ);
  auto worldLogical = new G4LogicalVolume(worldSolid, vacuum, "World");
  auto worldPhysical = new G4PVPlacement(nullptr, G4ThreeVector(), worldLogical, "World", nullptr,
                                         false, 0, true);
  worldLogical->SetVisAttributes(G4VisAttributes::GetInvisible());

  // The block itself, centred on the origin
  auto blockSolid = new G4Box("Block", 0.5 * fBlockWidth, 0.5 * fBlockWidth, 0.5 * fBlockThickness);
  fBlockVolume = new G4LogicalVolume(blockSolid, blockMaterial, "Block");
  new G4PVPlacement(nullptr, G4ThreeVector(), fBlockVolume, "Block", worldLogical, false, 0, true);

  G4cout << "ConvDetectorConstruction: " << fMaterialName << " block "
         << G4BestUnit(fBlockWidth, "Length") << " x " << G4BestUnit(fBlockWidth, "Length") << " x "
         << G4BestUnit(fBlockThickness, "Length") << G4endl;

  return worldPhysical;
}

void ConvDetectorConstruction::SetMaterial(const G4String& aName)
{
  fMaterialName = aName;
  G4RunManager::GetRunManager()->GeometryHasBeenModified();
}

void ConvDetectorConstruction::SetBlockThickness(G4double aThickness)
{
  fBlockThickness = aThickness;
  G4RunManager::GetRunManager()->GeometryHasBeenModified();
}

void ConvDetectorConstruction::SetBlockWidth(G4double aWidth)
{
  fBlockWidth = aWidth;
  G4RunManager::GetRunManager()->GeometryHasBeenModified();
}

void ConvDetectorConstruction::DefineCommands()
{
  fMessenger = new G4GenericMessenger(this, "/study/det/", "Block material and geometry");

  fMessenger->DeclareMethod("material", &ConvDetectorConstruction::SetMaterial,
                            "NIST name of the block material, e.g. G4_Si.");

  auto& thicknessCmd = fMessenger->DeclareMethodWithUnit(
    "blockThickness", "m", &ConvDetectorConstruction::SetBlockThickness,
    "Full extent of the silicon block along the beam axis.");
  thicknessCmd.SetParameterName("thickness", true);
  thicknessCmd.SetRange("thickness>0.");
  thicknessCmd.SetDefaultValue("1.");

  auto& widthCmd = fMessenger->DeclareMethodWithUnit(
    "blockWidth", "m", &ConvDetectorConstruction::SetBlockWidth,
    "Full transverse extent of the silicon block.");
  widthCmd.SetParameterName("width", true);
  widthCmd.SetRange("width>0.");
  widthCmd.SetDefaultValue("1.");
}
