/// \file ConvConfig.cc
/// \brief Implementation of the ConvConfig class

#include "ConvConfig.hh"

#include "G4UIcommand.hh"
#include "G4UnitsTable.hh"
#include "globals.hh"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
G4String Trim(const G4String& aText)
{
  const auto first = aText.find_first_not_of(" \t\r\n");
  if (first == G4String::npos) return "";
  const auto last = aText.find_last_not_of(" \t\r\n");
  return aText.substr(first, last - first + 1);
}
}  // namespace

G4bool ConvConfig::Read(const G4String& aPath)
{
  std::ifstream file(aPath);
  if (!file) {
    G4cerr << "ConvConfig: cannot open configuration file '" << aPath << "'" << G4endl;
    return false;
  }

  G4String line;
  G4int lineNumber = 0;
  while (std::getline(file, line)) {
    ++lineNumber;
    // Strip comments and surrounding whitespace; skip what is left over
    const auto hash = line.find('#');
    if (hash != G4String::npos) line = line.substr(0, hash);
    line = Trim(line);
    if (line.empty()) continue;

    const auto equals = line.find('=');
    if (equals == G4String::npos) {
      G4cerr << "ConvConfig: " << aPath << ":" << lineNumber << ": expected 'key = value'"
             << G4endl;
      return false;
    }
    const G4String key = Trim(line.substr(0, equals));
    const G4String value = Trim(line.substr(equals + 1));

    if (key == "material") {
      fMaterial = value;
    }
    else if (key == "blockThickness") {
      fBlockThickness = G4UIcommand::ConvertToDimensionedDouble(value.c_str());
    }
    else if (key == "blockWidth") {
      fBlockWidth = G4UIcommand::ConvertToDimensionedDouble(value.c_str());
    }
    else if (key == "minEnergy") {
      fMinEnergy = G4UIcommand::ConvertToDimensionedDouble(value.c_str());
    }
    else if (key == "maxEnergy") {
      fMaxEnergy = G4UIcommand::ConvertToDimensionedDouble(value.c_str());
    }
    else if (key == "nEvents") {
      fNbEvents = std::stoi(value);
    }
    else if (key == "nThreads") {
      fNbThreads = std::stoi(value);
    }
    else if (key == "model") {
      fModel = value;
    }
    else if (key == "conversionType") {
      fConversionType = value;
    }
    else if (key == "simMode") {
      fSimMode = value;
    }
    else if (key == "flowModelDir") {
      fFlowModelDir = value;
    }
    else if (key == "outputDir") {
      fOutputDir = value;
    }
    else if (key == "logDir") {
      fLogDir = value;
    }
    else if (key == "outputName") {
      fOutputName = value;
    }
    else {
      G4cerr << "ConvConfig: " << aPath << ":" << lineNumber << ": unknown key '" << key << "'"
             << G4endl;
      return false;
    }
  }

  if (fMinEnergy < 2 * 0.51099895 * MeV) {
    G4cerr << "ConvConfig: minEnergy is below the 1.022 MeV conversion threshold" << G4endl;
    return false;
  }
  if (fMaxEnergy < fMinEnergy) {
    G4cerr << "ConvConfig: maxEnergy is below minEnergy" << G4endl;
    return false;
  }
  if (fModel != "BetheHeitler5D" && fModel != "PairProductionRel") {
    G4cerr << "ConvConfig: unknown model '" << fModel
           << "', expected BetheHeitler5D or PairProductionRel" << G4endl;
    return false;
  }
  if (fConversionType != "mixed" && fConversionType != "nuclear" && fConversionType != "triplet") {
    G4cerr << "ConvConfig: unknown conversionType '" << fConversionType
           << "', expected mixed, nuclear or triplet" << G4endl;
    return false;
  }
  if (fSimMode != "full" && fSimMode != "fast") {
    G4cerr << "ConvConfig: unknown simMode '" << fSimMode << "', expected full or fast" << G4endl;
    return false;
  }
  return true;
}

G4int ConvConfig::GetConversionType() const
{
  if (fConversionType == "nuclear") return 1;
  if (fConversionType == "triplet") return 2;
  return 0;
}

std::vector<G4String> ConvConfig::PreInitCommands() const
{
  std::vector<G4String> commands;
  commands.push_back("/run/numberOfThreads " + std::to_string(fNbThreads));
  commands.push_back("/study/det/material " + fMaterial);
  commands.push_back("/study/det/blockThickness " + std::to_string(fBlockThickness / mm) + " mm");
  commands.push_back("/study/det/blockWidth " + std::to_string(fBlockWidth / mm) + " mm");
  return commands;
}

std::vector<G4String> ConvConfig::PostInitCommands() const
{
  std::vector<G4String> commands;
  commands.push_back("/study/gun/minEnergy " + std::to_string(fMinEnergy / MeV) + " MeV");
  commands.push_back("/study/gun/maxEnergy " + std::to_string(fMaxEnergy / MeV) + " MeV");
  commands.push_back("/analysis/setFileName " + OutputFilePath());
  return commands;
}

G4String ConvConfig::FileStem() const
{
  if (!fOutputName.empty()) return fOutputName;

  // Drop the NIST "G4_" prefix, so G4_Si becomes Si
  G4String material = fMaterial;
  if (material.rfind("G4_", 0) == 0) material = material.substr(3);
  std::replace(material.begin(), material.end(), '/', '_');

  const G4String energy = (fMaxEnergy > fMinEnergy)
                            ? FormatEnergy(fMinEnergy) + "-" + FormatEnergy(fMaxEnergy)
                            : FormatEnergy(fMinEnergy);

  return material + "_" + energy + "_" + std::to_string(fNbEvents);
}

G4String ConvConfig::OutputFilePath() const
{
  std::filesystem::create_directories(fOutputDir.c_str());
  return fOutputDir + "/" + FileStem();
}

G4String ConvConfig::LogFilePath() const
{
  std::filesystem::create_directories(fLogDir.c_str());
  return fLogDir + "/" + FileStem() + ".log";
}

G4String ConvConfig::FormatEnergy(G4double aEnergy)
{
  G4String unit = "MeV";
  G4double value = aEnergy / MeV;
  if (aEnergy >= TeV) {
    unit = "TeV";
    value = aEnergy / TeV;
  }
  else if (aEnergy >= GeV) {
    unit = "GeV";
    value = aEnergy / GeV;
  }
  else if (aEnergy < MeV) {
    unit = "keV";
    value = aEnergy / keV;
  }

  std::ostringstream text;
  text << std::defaultfloat << value;
  G4String digits = text.str();
  // '.' would look like a file extension; 'p' keeps the name readable
  std::replace(digits.begin(), digits.end(), '.', 'p');
  return digits + unit;
}

void ConvConfig::Print() const
{
  G4cout << "\n------------------ Run configuration -----------------------\n"
         << "  material        : " << fMaterial << "\n"
         << "  block           : " << G4BestUnit(fBlockWidth, "Length") << " x "
         << G4BestUnit(fBlockWidth, "Length") << " x " << G4BestUnit(fBlockThickness, "Length")
         << "\n"
         << "  photon energy   : " << G4BestUnit(fMinEnergy, "Energy") << " to "
         << G4BestUnit(fMaxEnergy, "Energy") << (fMaxEnergy > fMinEnergy ? " (log-uniform)" : "")
         << "\n"
         << "  events          : " << fNbEvents << " on " << fNbThreads << " thread(s)\n"
         << "  conversion model: " << fModel << " (" << fConversionType << ")\n"
         << "  simulation mode : " << fSimMode
         << (fSimMode == "fast" ? " (flow: " + fFlowModelDir + ")" : "") << "\n"
         << "  output          : " << OutputFilePath() << ".root\n"
         << "  log             : " << LogFilePath() << "\n"
         << "------------------------------------------------------------\n"
         << G4endl;
}
