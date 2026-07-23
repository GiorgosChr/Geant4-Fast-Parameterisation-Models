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

/// Split a "materials" value on whitespace and commas into NIST names.
std::vector<G4String> SplitMaterials(const G4String& aValue)
{
  std::string text = aValue;
  std::replace(text.begin(), text.end(), ',', ' ');
  std::istringstream stream(text);
  std::vector<G4String> names;
  for (std::string token; stream >> token;) {
    names.emplace_back(token);
  }
  return names;
}
}  // namespace

G4bool ConvConfig::SetKey(const G4String& aKey, const G4String& aValue)
{
  if (aKey == "materials") {
    fMaterials = SplitMaterials(aValue);
  }
  else if (aKey == "blockThickness") {
    fBlockThickness = G4UIcommand::ConvertToDimensionedDouble(aValue.c_str());
  }
  else if (aKey == "blockWidth") {
    fBlockWidth = G4UIcommand::ConvertToDimensionedDouble(aValue.c_str());
  }
  else if (aKey == "minEnergy") {
    fMinEnergy = G4UIcommand::ConvertToDimensionedDouble(aValue.c_str());
  }
  else if (aKey == "maxEnergy") {
    fMaxEnergy = G4UIcommand::ConvertToDimensionedDouble(aValue.c_str());
  }
  else if (aKey == "nEvents") {
    fNbEvents = std::stoi(aValue);
  }
  else if (aKey == "nThreads") {
    fNbThreads = std::stoi(aValue);
  }
  else if (aKey == "model") {
    fModel = aValue;
  }
  else if (aKey == "conversionType") {
    fConversionType = aValue;
  }
  else if (aKey == "outputDir") {
    fOutputDir = aValue;
  }
  else if (aKey == "logDir") {
    fLogDir = aValue;
  }
  else if (aKey == "outputName") {
    fOutputName = aValue;
  }
  else {
    return false;
  }
  return true;
}

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

    if (!SetKey(key, value)) {
      G4cerr << "ConvConfig: " << aPath << ":" << lineNumber << ": unknown key '" << key << "'"
             << G4endl;
      return false;
    }
  }

  return Validate();
}

G4bool ConvConfig::ApplyOverride(const G4String& aKey, const G4String& aValue)
{
  if (!SetKey(aKey, aValue)) {
    G4cerr << "ConvConfig: unknown key '" << aKey << "' in command-line override" << G4endl;
    return false;
  }
  return true;
}

G4bool ConvConfig::Validate() const
{
  if (fMaterials.empty()) {
    G4cerr << "ConvConfig: the materials list is empty" << G4endl;
    return false;
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
  commands.push_back("/study/det/blockThickness " + std::to_string(fBlockThickness / mm) + " mm");
  commands.push_back("/study/det/blockWidth " + std::to_string(fBlockWidth / mm) + " mm");
  return commands;
}

std::vector<G4String> ConvConfig::PostInitCommands() const
{
  std::vector<G4String> commands;
  commands.push_back("/study/gun/minEnergy " + std::to_string(fMinEnergy / MeV) + " MeV");
  commands.push_back("/study/gun/maxEnergy " + std::to_string(fMaxEnergy / MeV) + " MeV");
  return commands;
}

G4String ConvConfig::FileStem(const G4String& aMaterial) const
{
  // Drop the NIST "G4_" prefix, so G4_Si becomes Si
  G4String material = aMaterial;
  if (material.rfind("G4_", 0) == 0) material = material.substr(3);
  std::replace(material.begin(), material.end(), '/', '_');

  if (!fOutputName.empty()) {
    // Keep the runs distinct even with an explicit name and several materials
    return (fMaterials.size() > 1) ? G4String(fOutputName + "_" + material) : fOutputName;
  }

  const G4String energy = (fMaxEnergy > fMinEnergy)
                            ? FormatEnergy(fMinEnergy) + "-" + FormatEnergy(fMaxEnergy)
                            : FormatEnergy(fMinEnergy);

  return material + "_" + energy + "_" + std::to_string(fNbEvents);
}

G4String ConvConfig::OutputFilePath(const G4String& aMaterial) const
{
  std::filesystem::create_directories(fOutputDir.c_str());
  return fOutputDir + "/" + FileStem(aMaterial);
}

G4String ConvConfig::LogFilePath(const G4String& aMaterial) const
{
  std::filesystem::create_directories(fLogDir.c_str());
  return fLogDir + "/" + FileStem(aMaterial) + ".log";
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
  std::ostringstream materials;
  for (std::size_t i = 0; i < fMaterials.size(); ++i) {
    materials << (i == 0 ? "" : " ") << fMaterials[i];
  }

  G4cout << "\n------------------ Run configuration -----------------------\n"
         << "  materials       : " << materials.str() << "\n"
         << "  block           : " << G4BestUnit(fBlockWidth, "Length") << " x "
         << G4BestUnit(fBlockWidth, "Length") << " x " << G4BestUnit(fBlockThickness, "Length")
         << "\n"
         << "  photon energy   : " << G4BestUnit(fMinEnergy, "Energy") << " to "
         << G4BestUnit(fMaxEnergy, "Energy") << (fMaxEnergy > fMinEnergy ? " (log-uniform)" : "")
         << "\n"
         << "  events          : " << fNbEvents << " on " << fNbThreads << " thread(s), per material\n"
         << "  conversion model: " << fModel << " (" << fConversionType << ")\n"
         << "  output dir      : " << fOutputDir << "/  (one <material>_... .root per material)\n"
         << "  log dir         : " << fLogDir << "/\n"
         << "------------------------------------------------------------\n"
         << G4endl;
}
