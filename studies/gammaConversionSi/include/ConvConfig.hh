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
 * One file fully describes a run: block material and size, photon energy
 * range, number of events, threads, and the conversion model. The name of the
 * output ROOT file is derived from the material, the energy range and the
 * number of events, so that repeated runs with different settings never
 * overwrite each other and the file name states what it contains.
 *
 * Values may carry a Geant4 unit ("10 GeV", "1 m"); they are parsed with
 * G4UIcommand::ConvertToDimensionedDouble.
 */
class ConvConfig
{
  public:
    ConvConfig() = default;

    /// Read a configuration file. Returns false on an unreadable file, an
    /// unknown key or an unparsable value.
    G4bool Read(const G4String& aPath);

    /// UI commands that have to be applied before /run/initialize.
    std::vector<G4String> PreInitCommands() const;
    /// UI commands that have to be applied after /run/initialize.
    std::vector<G4String> PostInitCommands() const;

    G4String GetModel() const { return fModel; }
    /// G4EmParameters conversion type: 0 mixed, 1 nuclear only, 2 triplet only.
    G4int GetConversionType() const;
    G4int GetNbEvents() const { return fNbEvents; }

    /// "full" (Geant4 conversion) or "fast" (the normalising-flow model).
    G4String GetSimMode() const { return fSimMode; }
    /// Directory of the exported ONNX flow, used only in the "fast" mode.
    G4String GetFlowModelDir() const { return fFlowModelDir; }

    /// Full path of the ROOT file, without the .root extension that
    /// G4AnalysisManager appends. Creates the output directory if needed.
    G4String OutputFilePath() const;
    /// Full path of the run log, sharing the stem of the ROOT file.
    G4String LogFilePath() const;

    void Print() const;

  private:
    /// Bare file name shared by the ntuple and the log, derived from material,
    /// energy range and event count unless outputName overrides it.
    G4String FileStem() const;
    /// Compact, file-name safe rendering of an energy, e.g. "10GeV", "1p5MeV".
    static G4String FormatEnergy(G4double aEnergy);

    G4String fMaterial = "G4_Si";
    G4double fBlockThickness = 1. * m;
    G4double fBlockWidth = 1. * m;
    G4double fMinEnergy = 2. * MeV;
    G4double fMaxEnergy = 10. * GeV;
    G4int fNbEvents = 100000;
    G4int fNbThreads = 10;
    /// "BetheHeitler5D" (default, full 5-dimensional final state) or
    /// "PairProductionRel" (the bare G4GammaConversion default).
    G4String fModel = "BetheHeitler5D";
    /// "mixed" (nuclear and triplet in their natural 1/Z ratio), "nuclear" or
    /// "triplet". Only acts on the BetheHeitler5D model.
    G4String fConversionType = "mixed";
    /// "full" runs G4GammaConversion; "fast" runs the ConvFastSimModel flow.
    G4String fSimMode = "full";
    /// Where the exported flow (trunk.onnx, heads, flow_constants.txt) lives.
    G4String fFlowModelDir = "models/onnx";
    G4String fOutputDir = "ntuples";
    G4String fLogDir = "logs";
    /// Optional explicit file name; auto-generated when left empty.
    G4String fOutputName = "";
};

#endif /* CONV_CONFIG_HH */
