/// \file ConvConfig.hh
/// \brief Definition of the ConvConfig class

#ifndef CONV_CONFIG_HH
#define CONV_CONFIG_HH

#include "G4String.hh"
#include "G4SystemOfUnits.hh"
#include "G4Types.hh"

#include <vector>

/**
 * @brief A "key = value" run configuration read from a text file.
 *
 * One file describes a run: a list of block materials, the block size, photon
 * energy range, number of events, threads, and the conversion model. The same
 * experiment is run once per material; each output file is named from the
 * material, the energy range and the number of events, so the per-material runs
 * never overwrite each other and every file name states what it contains.
 *
 * Values may carry a Geant4 unit ("10 GeV", "1 m"); they are parsed with
 * G4UIcommand::ConvertToDimensionedDouble. Individual keys can also be
 * overridden from the command line (ApplyOverride), which is how
 * run_materials.sh drives one material per invocation.
 */
class ConvConfig
{
  public:
    ConvConfig() = default;

    /// Read a configuration file, then validate. Returns false on an unreadable
    /// file, an unknown key, an unparsable value or an out-of-range setting.
    G4bool Read(const G4String& aPath);

    /// Apply a single "key = value" override (e.g. from the command line).
    /// Returns false on an unknown key. Call Validate() afterwards.
    G4bool ApplyOverride(const G4String& aKey, const G4String& aValue);

    /// Re-check the cross-key constraints. Returns false on a bad setting.
    G4bool Validate() const;

    /// UI commands that have to be applied before /run/initialize. The material
    /// is set per run by the loop in main(), not here.
    std::vector<G4String> PreInitCommands() const;
    /// UI commands that have to be applied after /run/initialize.
    std::vector<G4String> PostInitCommands() const;

    G4String GetModel() const { return fModel; }
    /// G4EmParameters conversion type: 0 mixed, 1 nuclear only, 2 triplet only.
    G4int GetConversionType() const;
    G4int GetNbEvents() const { return fNbEvents; }
    /// The materials to run, in order; the study loops over them.
    const std::vector<G4String>& GetMaterials() const { return fMaterials; }

    /// Full path of the ROOT file for one material, without the .root extension
    /// that G4AnalysisManager appends. Creates the output directory if needed.
    G4String OutputFilePath(const G4String& aMaterial) const;
    /// Full path of the run log for one material, sharing the ROOT file stem.
    G4String LogFilePath(const G4String& aMaterial) const;

    void Print() const;

  private:
    /// Assign one key. Returns false if the key is unknown (used by both the
    /// file reader and the command-line override, so they parse identically).
    G4bool SetKey(const G4String& aKey, const G4String& aValue);

    /// Bare file name for one material, derived from the material, energy range
    /// and event count unless outputName overrides it.
    G4String FileStem(const G4String& aMaterial) const;
    /// Compact, file-name safe rendering of an energy, e.g. "10GeV", "1p5MeV".
    static G4String FormatEnergy(G4double aEnergy);

    std::vector<G4String> fMaterials{"G4_Si"};
    G4double fBlockThickness = 1. * m;
    G4double fBlockWidth = 1. * m;
    G4double fMinEnergy = 2. * MeV;
    G4double fMaxEnergy = 10. * GeV;
    G4int fNbEvents = 100000;
    G4int fNbThreads = 10;
    /// "BetheHeitler5D" (default, full 5-dimensional final state) or
    /// "PairProductionRel" (the bare G4GammaConversion default).
    G4String fModel = "BetheHeitler5D";
    /// "mixed" (nuclear and triplet in their natural 1/(Z+1) ratio), "nuclear"
    /// or "triplet". Only acts on the BetheHeitler5D model.
    G4String fConversionType = "mixed";
    G4String fOutputDir = "ntuples";
    G4String fLogDir = "logs";
    /// Optional explicit file name; auto-generated when left empty.
    G4String fOutputName = "";
};

#endif /* CONV_CONFIG_HH */
