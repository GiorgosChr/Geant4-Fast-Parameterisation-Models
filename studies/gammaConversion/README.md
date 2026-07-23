# Preliminary study: γ → e⁺e⁻ conversion across materials

Fires photons into a thick block with **pair conversion as the only physics process** and writes
one ROOT ntuple row per conversion. The same experiment is repeated over a **list of materials**,
so the recorded final state can be studied as a function of the target's atomic number `Z`.

Nothing competes with conversion — no Compton, no photoelectric effect, and no ionisation or
bremsstrahlung on the e± — so the recorded final state is exactly what the conversion model
produced, undisturbed. That makes it clean ground truth to fit or validate a fast-simulation
parameterisation against. This study is the sibling of `gammaConversionSi`, generalised from one
silicon block to a material scan and trimmed to the columns a `(eGamma, Z)`-conditioned model
trains and validates on.

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
./gammaConversion config/default.cfg              # loop over the config material list
./gammaConversion config/default.cfg materials=G4_Pb   # one material, overriding the list
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
config/default.cfg` directly covers the whole list by re-executing itself once per material (a
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
| `materials` | `G4_Si` | Whitespace/comma-separated NIST names. **Single-element materials only**, so the recorded `Z` is unambiguous. The experiment runs once per material |
| `blockThickness`, `blockWidth` | `1 m` (`default.cfg` ships `10 m` thickness) | Full block dimensions. Only the thickness matters: the photon is on axis and the pair is killed once recorded |
| `minEnergy`, `maxEnergy` | `2 MeV`, `10 GeV` | Photon energy, sampled **log-uniformly**; set equal for a mono-energetic run |
| `nEvents`, `nThreads` | `100000`, `10` | Per material |
| `model` | `BetheHeitler5D` | Conversion model, see below |
| `conversionType` | `mixed` | `mixed`, `nuclear` or `triplet` |
| `outputDir` | `ntuples` | Created if missing |
| `logDir` | `logs` | Created if missing |
| `outputName` | *(derived)* | Overrides the derived file name (intended for a single material) |

### Command-line overrides

Any `key=value` argument after the config path overrides that key, using the same parser. This is
how `run_materials.sh` drives one material per invocation:

```bash
./gammaConversion config/default.cfg materials=G4_W nEvents=20000 nThreads=1
```

Quote values that contain spaces, e.g. `"minEnergy=1 GeV"`.

### Output naming and logging

Each material's output is named from the material, the energy range and the number of events, so
per-material runs never overwrite each other; the log shares the stem:

```
ntuples/Si_2MeV-100GeV_10000000.root   logs/Si_2MeV-100GeV_10000000.log
ntuples/Pb_2MeV-100GeV_10000000.root   logs/Pb_2MeV-100GeV_10000000.log
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
| `Z` | Atomic number of the block material for this run |
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
d = uproot.open("ntuples/Pb_2MeV-100GeV_10000000.root")["conversions"].arrays(library="np")
assert np.ptp(d["Z"]) == 0                        # Z is constant within a file
print(d["isTriplet"].mean(), "vs", 1 / (d["Z"][0] + 1))   # triplet fraction ≈ 1/(Z+1)
```
