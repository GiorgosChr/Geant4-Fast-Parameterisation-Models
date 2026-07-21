/// \file ConvPhysicsList.cc
/// \brief Implementation of the ConvPhysicsList class

#include "ConvPhysicsList.hh"

#include "G4BetheHeitler5DModel.hh"
#include "G4Electron.hh"
#include "G4EmParameters.hh"
#include "G4Gamma.hh"
#include "G4GammaConversion.hh"
#include "G4GenericIon.hh"
#include "G4IonConstructor.hh"
#include "G4ParticleDefinition.hh"
#include "G4Positron.hh"
#include "G4ProcessManager.hh"
#include "G4ProductionCutsTable.hh"
#include "G4SystemOfUnits.hh"

ConvPhysicsList::ConvPhysicsList(const G4String& aModel, G4int aConversionType)
  : fModel(aModel), fConversionType(aConversionType)
{
  SetVerboseLevel(1);
  defaultCutValue = 0.7 * mm;

  // Cover the full range the gun can sample, with a finely binned cross
  // section table so the extracted mean free path is not binning limited
  auto param = G4EmParameters::Instance();
  param->SetDefaults();
  param->SetMinEnergy(100 * eV);
  param->SetMaxEnergy(100 * TeV);
  param->SetNumberOfBinsPerDecade(20);
  param->SetVerbose(0);
  // Nuclear / triplet mixture of the 5D model
  param->SetConversionType(fConversionType);
}

void ConvPhysicsList::ConstructParticle()
{
  G4Gamma::GammaDefinition();
  G4Electron::ElectronDefinition();
  G4Positron::PositronDefinition();

  // G4BetheHeitler5DModel emits the recoiling nucleus as a third secondary and
  // builds it through G4IonTable, which needs GenericIon to exist first
  G4GenericIon::GenericIonDefinition();
  G4IonConstructor ionConstructor;
  ionConstructor.ConstructParticle();
}

void ConvPhysicsList::ConstructProcess()
{
  AddTransportation();

  auto particleIterator = GetParticleIterator();
  particleIterator->reset();
  while ((*particleIterator)()) {
    G4ParticleDefinition* particle = particleIterator->value();
    if (particle->GetParticleName() != "gamma") continue;

    // The one and only physical process of this study. e- and e+ deliberately
    // keep transportation alone, so the pair leaves the conversion vertex
    // undisturbed.
    auto conversion = new G4GammaConversion();
    // Left alone, G4GammaConversion falls back to G4PairProductionRelModel,
    // whose pair is exactly coplanar -- unusable for an angular study
    if (fModel == "BetheHeitler5D") {
      conversion->SetEmModel(new G4BetheHeitler5DModel());
    }
    particle->GetProcessManager()->AddDiscreteProcess(conversion);
  }
}

void ConvPhysicsList::SetCuts()
{
  // Production cuts do not affect the result -- G4GammaConversion is discrete
  // and always emits the pair -- but the table has to exist
  G4ProductionCutsTable::GetProductionCutsTable()->SetEnergyRange(100 * eV, 100 * TeV);
  SetCutsWithDefault();
}
