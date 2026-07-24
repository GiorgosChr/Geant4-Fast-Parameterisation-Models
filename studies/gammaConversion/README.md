# Preliminary study: γ → e⁺e⁻ conversion across materials

Fires photons into a thick block with **pair conversion as the only physics process** and writes
one ROOT ntuple row per conversion, tagged with the atomic number `Z` of the element the photon
converted on. It produces the ground truth for a **`(eGamma, Z)`-conditioned** fast-simulation
model of the final state.

Nothing competes with conversion — no Compton, no photoelectric effect, and no ionisation or
bremsstrahlung on the e± — so the recorded final state is exactly what the conversion model
produced, undisturbed. This study is the sibling of `gammaConversionSi`, generalised from one
silicon block to a scan over materials and trimmed to the columns the model uses.

The data comes in two sets, both filling the same ntuple:

- **Training — pure single elements** (`config/elements.cfg`). One block per element, so every
  conversion in a run is on the same `Z`; the model sees a clean, even sample of
  `p(final state | eGamma, Z)` across the periodic table (Z = 1…82). This is the right data to learn
  the `Z`-dependence — the triplet fraction `1/(Z+1)`, screening, the Coulomb correction.
- **Validation — the ATLAS tracker composites** (`config/composites.cfg`). Each block is a real
  detector material (a blend of elements); within one run conversions happen on many different `Z`,
  weighted by the per-atom cross section, and each event is tagged with the element it actually
  converted on. Use it to check that the model, trained on pure elements, reproduces the real
  detector's blended response.

**`Z` is a property of the nucleus, not the block.** Pair production happens on one atom at a time,
and the final state (angles, energy sharing, triplet probability) is characteristic of that atom's
`Z`. So in a composite the recorded `Z` varies event to event — it is
`process->GetCurrentElement()->GetZ()` for that conversion — never a single effective `Z` of the
material, which would blur distinct elements' physics.

## Build

```bash
./studies/gammaConversion/build.sh
```

`build.sh` configures and builds into `build/gammaConversion/`. It resolves every path relative to
itself, so it works from any working directory. Environment overrides:

| Variable | Default | Effect |
| --- | --- | --- |
| `JOBS` | `10` | Parallel build jobs |
| `Geant4_DIR` | `install/geant4/lib/cmake/Geant4` | Build against a different Geant4 (system, CVMFS, …) |
| `CLEAN` | *(unset)* | Wipe the build tree before configuring |

The equivalent by hand:

```bash
cmake -S studies/gammaConversion -B build/gammaConversion \
      -DGeant4_DIR=$PWD/install/geant4/lib/cmake/Geant4
cmake --build build/gammaConversion -j10
```

## Run

```bash
source install/geant4/bin/geant4.sh        # from the repo root
cd build/gammaConversion
./gammaConversion config/elements.cfg             # training set: 29 pure elements
./gammaConversion config/composites.cfg           # validation set: 39 ATLAS composites
./gammaConversion config/elements.cfg materials=Pb   # one material, overriding the list
./gammaConversion                                 # interactive UI + vis.mac
./gammaConversion -h
```

The argument is a config file unless it ends in `.mac`, in which case it is executed as a plain
Geant4 macro. Any trailing `key=value` tokens override the config (see below).

`config/` in the build directory is a **copy** made at CMake time. Edit the originals under
`studies/gammaConversion/config/` and re-run the build for changes to survive a clean rebuild.

### Looping over materials with the job script

```bash
./studies/gammaConversion/run_materials.sh
```

`run_materials.sh` defines a bash array of materials **in the script** and runs the executable once
per material, passing `materials=<name>` as an override. Each material is a separate process — so
the runs are independent and parallelisable — and produces its own ntuple and log. Edit the
`MATERIALS=(…)` line to change the scan.

The executable *also* handles the `materials` list on its own: running `./gammaConversion
config/elements.cfg` directly covers the whole list by re-executing itself once per material (a
fresh process each, run sequentially), so you get the same per-material files without the script.
One process per material is deliberate — switching the analysis output file between runs inside a
single process leaves all but the first ntuple empty, so each material needs its own clean process.
Use whichever entry point fits: the script when you want to drive the list yourself, the internal
dispatch for a one-liner over the config's list.

## Configuration

A run is described by a `key = value` file in `config/`; numeric values may carry any Geant4 unit.
`config/default.cfg` documents every key and lists the defaults.

| Key | Default | Meaning |
| --- | --- | --- |
| `materials` | `G4_Si` | Whitespace/comma-separated material names; the experiment runs once per material. A name is a NIST material (`G4_Pb`), a pure element from `config/elements.dat` (`Pb`), or an ATLAS composite from `config/materials.dat` (`pix::Chip`) |
| `blockThickness`, `blockWidth` | `1 m` (the shipped configs use `10 m` thickness) | Full block dimensions. Only the thickness matters: the photon is on axis and the pair is killed once recorded |
| `minEnergy`, `maxEnergy` | `2 MeV`, `10 GeV` | Photon energy, sampled **log-uniformly**; set equal for a mono-energetic run |
| `nEvents`, `nThreads` | `100000`, `10` | Per material |
| `model` | `BetheHeitler5D` | Conversion model, see below |
| `conversionType` | `mixed` | `mixed`, `nuclear` or `triplet` |
| `outputDir` | `ntuples` | Created if missing |
| `logDir` | `logs` | Created if missing |
| `outputName` | *(derived)* | Overrides the derived file name (intended for a single material) |

### Material tables

Anything other than a NIST `G4_...` name is built from an element table by mass fraction, at a
nominal 1 g/cm³ — density is irrelevant to what this study records (only kinematics and the target
`Z`); it sets only the mean free path, and the 10 m block converts essentially everything either
way. Two tables ship, both copied next to the executable at build time:

- **`config/elements.dat`** — the 29 pure elements used for training (`H`, `C`, …, `Pb`). Built as
  solids so even hydrogen converts, unlike the NIST gases `G4_H`, `G4_N`, ….
- **`config/materials.dat`** — the 39 ATLAS tracker composites used for validation, one line each:
  `pix::Chip : 14=0.9971 50=0.0015 82=0.0014` (`Z=massFraction`). Compositions are deduplicated;
  a `namespace::` prefix is kept as the Geant4 material name and dropped from the output file name.

### Command-line overrides

Any `key=value` argument after the config path overrides that key, using the same parser. This is
how `run_materials.sh` drives one material per invocation:

```bash
./gammaConversion config/elements.cfg materials=W nEvents=20000 nThreads=1
```

Quote values that contain spaces, e.g. `"minEnergy=1 GeV"`.

### Output naming and logging

Each material's output is named from the material, the energy range and the number of events, so
per-material runs never overwrite each other; the log shares the stem. A composite drops its
`namespace::` prefix for the file name:

```
ntuples/Si_1GeV-100GeV_1000000.root      logs/Si_1GeV-100GeV_1000000.log
ntuples/Chip_1GeV-100GeV_1000000.root    logs/Chip_1GeV-100GeV_1000000.log   # pix::Chip
```

Both directories are git-ignored. The `<N>` in the name is the number of photons **fired**, not the
number of ntuple rows — non-converting photons write no row. Because every material runs in its own
process, each log is complete and self-contained: it holds that material's Geant4 banner,
initialisation, run and end-of-run summary.

### Choice of conversion model

A bare `G4GammaConversion` falls back to `G4PairProductionRelModel`, which emits an **exactly
coplanar** pair, so the azimuthal correlation is unusable. This study therefore sets
`G4BetheHeitler5DModel` explicitly, the same model the accurate reference lists (EM opt3/opt4,
Livermore, LowEP) use. It samples the full five-dimensional final state.

The 5D model always emits three secondaries — e⁻, e⁺ and a recoil. Usually the recoil is the
nucleus; in a fraction ≈ 1/(Z+1) of cases the photon converts on an atomic electron instead and the
recoil is a second electron. Those **triplet** events are flagged by `isTriplet` and can be removed
with `conversionType = nuclear`.

### Why the particle list is not minimal

`ConvPhysicsList::ConstructParticle()` builds the full standard particle set. Only gamma, e⁻ and e⁺
ever appear in an event — what makes this study single-process is `ConstructProcess()`. The full set
is there to keep the output clean: the 5D model needs `GenericIon`, but once it is defined
`G4RunManagerKernel::SetupPhysics()` defines the hypernuclei too, and building their decay tables
otherwise warns about every missing daughter.

## Ntuple `conversions`

One row per **converting** event; photons that leave the block without converting write no row.
Energies in **MeV**, angles in **radians**.

| Column | Meaning |
| --- | --- |
| `eGamma` | Energy of the incident photon |
| `Z` | Atomic number of the element the photon converted on (constant per file for a pure element; a spectrum for a composite) |
| `isTriplet` | 1 for conversion on an atomic electron, 0 for nuclear |
| `eRecoil` | Kinetic energy of the recoiling nucleus, or of the recoil electron for a triplet |
| `eElectron`, `ePositron` | Kinetic energy of the pair e⁻ and e⁺ |
| `theta` | Polar angle of the **leading** (higher-energy) lepton with respect to the initial photon |

`theta` is the quantity a downstream model is trained to predict; `eElectron` and `ePositron` are
stored separately so the lead/sub assignment stays recoverable. Energy is conserved row by row as
`eElectron + ePositron + eRecoil + 2 mₑc² = eGamma`.

## Reading the output

ROOT is not installed on this machine (`root/` is a submodule that would have to be built first), so
the quickest way in is `uproot`:

```python
import uproot, numpy as np

# A pure-element training file: Z is constant.
d = uproot.open("ntuples/Pb_1GeV-100GeV_1000000.root")["conversions"].arrays(library="np")
print(d["isTriplet"].mean(), "vs", 1 / (d["Z"][0] + 1))         # triplet fraction ≈ 1/(Z+1)

# A composite validation file: Z is a spectrum (the element each event converted on).
c = uproot.open("ntuples/Hybrid_1GeV-100GeV_1000000.root")["conversions"].arrays(library="np")
z, n = np.unique(c["Z"].astype(int), return_counts=True)
print(dict(zip(z, (n / n.sum()).round(3))))                    # cross-section-weighted Z mix
```

The `Z` mix in a composite is weighted by the per-atom cross section (≈ `Z²`), so high-`Z` trace
elements are over-represented relative to their mass fraction — e.g. in `pix::Chip` (99.7 % Si by
mass) about 1 % of conversions are on the Sn/Pb traces.
