/// \file ConvPhysicsList.cc
/// \brief Implementation of the ConvPhysicsList class

#include "ConvPhysicsList.hh"

#include "G4BaryonConstructor.hh"
#include "G4BetheHeitler5DModel.hh"
#include "G4BosonConstructor.hh"
#include "G4EmParameters.hh"
#include "G4FastSimulationManagerProcess.hh"
#include "G4GammaConversion.hh"
#include "G4IonConstructor.hh"
#include "G4LeptonConstructor.hh"
#include "G4MesonConstructor.hh"
#include "G4ParticleDefinition.hh"
#include "G4ShortLivedConstructor.hh"
#include "G4Positron.hh"
#include "G4ProcessManager.hh"
#include "G4ProductionCutsTable.hh"
#include "G4SystemOfUnits.hh"

ConvPhysicsList::ConvPhysicsList(const G4String& aModel, G4int aConversionType,
                                 const G4String& aSimMode)
  : fModel(aModel), fConversionType(aConversionType), fSimMode(aSimMode)
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
  // The full standard set, as TestEm14 and the other single-process EM studies
  // do. Only gamma, e- and e+ ever appear in an event -- what makes this study
  // single-process is ConstructProcess(), not a short particle list.
  //
  // Defining them all is what keeps the output clean. G4BetheHeitler5DModel
  // builds its recoil nucleus through G4IonTable, so GenericIon has to exist
  // (otherwise every event warns "PART105: Can not create ions"); but once it
  // does, G4RunManagerKernel::SetupPhysics() unconditionally calls
  // G4IonConstructor::ConstructParticle(), which defines the hypernuclei, and
  // building their decay tables warns about every daughter that is missing.
  // Declaring only the daughters named in those warnings does not converge --
  // each one drags in its own decay products in turn.
  G4BosonConstructor bosonConstructor;
  bosonConstructor.ConstructParticle();

  G4LeptonConstructor leptonConstructor;
  leptonConstructor.ConstructParticle();

  G4MesonConstructor mesonConstructor;
  mesonConstructor.ConstructParticle();

  G4BaryonConstructor baryonConstructor;
  baryonConstructor.ConstructParticle();

  G4IonConstructor ionConstructor;
  ionConstructor.ConstructParticle();

  G4ShortLivedConstructor shortLivedConstructor;
  shortLivedConstructor.ConstructParticle();
}

void ConvPhysicsList::ConstructProcess()
{
  AddTransportation();

  auto particleIterator = GetParticleIterator();
  particleIterator->reset();
  while ((*particleIterator)()) {
    G4ParticleDefinition* particle = particleIterator->value();
    if (particle->GetParticleName() != "gamma") continue;
    G4ProcessManager* pmanager = particle->GetProcessManager();

    // In the "fast" mode the photon gets the fast-simulation interface instead
    // of G4GammaConversion: the ConvFastSimModel attached to the block region
    // then decides the conversion (from the real cross section) and generates
    // the pair through the flow. For a region in the mass geometry the process
    // is a plain PostStep discrete process, so ordering does not matter.
    if (fSimMode == "fast") {
      pmanager->AddDiscreteProcess(new G4FastSimulationManagerProcess("fastSimProcess_massGeom"));
      continue;
    }

    // The one and only physical process of this study. e- and e+ deliberately
    // keep transportation alone, so the pair leaves the conversion vertex
    // undisturbed.
    auto conversion = new G4GammaConversion();
    // Left alone, G4GammaConversion falls back to G4PairProductionRelModel,
    // whose pair is exactly coplanar -- unusable for an angular study
    if (fModel == "BetheHeitler5D") {
      conversion->SetEmModel(new G4BetheHeitler5DModel());
    }
    pmanager->AddDiscreteProcess(conversion);
  }
}

void ConvPhysicsList::SetCuts()
{
  // Production cuts do not affect the result -- G4GammaConversion is discrete
  // and always emits the pair -- but the table has to exist
  G4ProductionCutsTable::GetProductionCutsTable()->SetEnergyRange(100 * eV, 100 * TeV);
  SetCutsWithDefault();
}
