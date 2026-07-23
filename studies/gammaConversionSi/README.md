# Preliminary study: γ → e⁺e⁻ conversion in silicon

Fires photons into a thick block (silicon by default) with **pair conversion as the only physics
process** and writes one ROOT ntuple row per conversion: the photon energy, the path travelled
inside the block before converting, and the energies and angles of the produced pair.

Nothing competes with conversion — no Compton, no photoelectric effect, and no ionisation or
bremsstrahlung on the e± — so the recorded final state is exactly what the conversion model
produced, undisturbed. That makes it clean ground truth to fit or validate a fast-simulation
parameterisation against.

## Build

```bash
./studies/gammaConversionSi/build.sh
```

`build.sh` configures and builds into `build/gammaConversionSi/` and prints the run commands when
it finishes. It resolves every path relative to itself, so it works from any working directory.
Environment overrides:

| Variable | Default | Effect |
| --- | --- | --- |
| `JOBS` | `10` | Parallel build jobs |
| `Geant4_DIR` | `install/geant4/lib/cmake/Geant4` | Build against a different Geant4 (system, CVMFS, …) |
| `CLEAN` | *(unset)* | Wipe the build tree before configuring |

```bash
CLEAN=1 JOBS=4 ./studies/gammaConversionSi/build.sh
```

The equivalent by hand, if you prefer:

```bash
cmake -S studies/gammaConversionSi -B build/gammaConversionSi \
      -DGeant4_DIR=$PWD/install/geant4/lib/cmake/Geant4
cmake --build build/gammaConversionSi -j10
```

## Run

```bash
source install/geant4/bin/geant4.sh        # from the repo root
cd build/gammaConversionSi
./gammaConversionSi config/default.cfg     # batch run from a config file
./gammaConversionSi config/mono1GeV.cfg    # 50k photons at a fixed 1 GeV
./gammaConversionSi                        # interactive UI + vis.mac
./gammaConversionSi -h
```

100 000 events take under a second on 10 threads. The argument is a config file unless it ends in
`.mac`, in which case it is executed as a plain Geant4 macro.

`config/` in the build directory is a **copy** made at CMake time. Edit the originals under
`studies/gammaConversionSi/config/` and re-run the build if you want changes to survive a clean
rebuild.

## Configuration

A run is fully described by a `key = value` file in `config/`; numeric values may carry any Geant4
unit. `config/default.cfg` documents every key and lists the defaults; `config/mono1GeV.cfg` is a
mono-energetic run.

| Key | Default | Meaning |
| --- | --- | --- |
| `material` | `G4_Si` | Any NIST material name |
| `blockThickness`, `blockWidth` | `1 m` (`default.cfg` ships `10 m` thickness) | Full block dimensions (beam axis / transverse). Only the thickness matters: the photon is on axis and the pair is killed once recorded, so the width never contains anything |
| `minEnergy`, `maxEnergy` | `2 MeV`, `10 GeV` | Photon energy, sampled **log-uniformly**; set equal for a mono-energetic run |
| `nEvents`, `nThreads` | `100000`, `10` | |
| `model` | `BetheHeitler5D` | Conversion model, see below |
| `conversionType` | `mixed` | `mixed`, `nuclear` or `triplet` |
| `simMode` | `full` | `full` runs `G4GammaConversion`; `fast` runs the normalising-flow fast-simulation model (see below) |
| `flowModelDir` | `models/onnx` | Directory of the exported flow, used only when `simMode = fast` |
| `outputDir` | `ntuples` | Created if missing |
| `logDir` | `logs` | Created if missing |
| `outputName` | *(derived)* | Overrides the derived file name |

Output files are collected in `outputDir` and named from the material, the energy range and the
number of events, so runs with different settings never overwrite each other. The run log shares
the same stem, so a dataset and its log always sit next to each other:

```
ntuples/Si_2MeV-10GeV_100000.root   logs/Si_2MeV-10GeV_100000.log
ntuples/Si_1GeV_50000.root          logs/Si_1GeV_50000.log
```

Both directories are git-ignored. The `<N>` in the name is the number of photons **fired**, not
the number of ntuple rows — non-converting photons write no row.

### Logging

Everything Geant4 prints is teed to the log while still going to the terminal: the banner, the
physics list and geometry dumps, the run configuration, per-thread messages and the end-of-run
summary. `ConvLogger` is installed on the master *and* on every worker thread — a worker's default
`G4MTcoutDestination` writes its `G4WT0 > ` lines straight to the terminal and never reaches the
master destination, so worker-side `G4Exception` warnings would otherwise appear on screen but be
missing from the log. Runs from a macro log to `logs/<macro>.log`, interactive sessions to
`logs/interactive.log`.

The same keys are also available as UI commands (`/study/det/material`,
`/study/det/blockThickness`, `/study/gun/minEnergy`, …) for interactive work.

### Choice of conversion model

A bare `G4GammaConversion` falls back to `G4PairProductionRelModel`, which emits an **exactly
coplanar** pair — `phiElectron - phiPositron` comes out identically π, so the azimuthal correlation
is unusable. This study therefore sets `G4BetheHeitler5DModel` explicitly, the same model the
accurate reference lists (EM opt3/opt4, Livermore, LowEP) use. It samples the full five-dimensional
final state, so the polar angles, the energy sharing and the acoplanarity are all physical.
`model = PairProductionRel` is kept only as a comparison point.

The 5D model always emits three secondaries — e⁻, e⁺ and a recoil. Usually the recoil is the
nucleus; in a fraction ≈ 1/(Z+1) of cases (6.7 % in silicon) the photon converts on an atomic
electron instead and the recoil is a second electron. Those **triplet** events are flagged by
`isTriplet` and can be removed with `conversionType = nuclear`.

### Why the particle list is not minimal

`ConvPhysicsList::ConstructParticle()` builds the full standard particle set, which looks at odds
with a single-process study. Only gamma, e⁻ and e⁺ ever appear in an event — what makes this study
single-process is `ConstructProcess()`, which registers `G4GammaConversion` and nothing else. The
rest are inert.

They are there to keep the output readable. The 5D model needs `GenericIon` defined, but once it
is, `G4RunManagerKernel::SetupPhysics()` unconditionally defines the hypernuclei too, and building
their decay tables warns about every daughter that is missing — 38 lines at the head of every run.
Declaring only the named daughters does not converge, because each one drags in its own decay
products in turn.

## Ntuple `conversions`

One row per **converting** event; photons that leave the block without converting write no row and
are only counted in the end-of-run summary. Energies in **MeV**, lengths in **mm**, angles in
**radians**.

| Column | Meaning |
| --- | --- |
| `eGamma` | Energy of the incident photon |
| `pathInBlock` | Path travelled inside the block from entry to the conversion vertex |
| `eElectron`, `ePositron` | Kinetic energy of the pair |
| `thetaElectron`, `thetaPositron` | Polar angle with respect to the **initial** photon direction |
| `phiElectron`, `phiPositron` | Azimuth about the photon axis; the difference is the acoplanarity |
| `openingAngle` | Angle between e⁻ and e⁺ |
| `zConv` | z of the conversion vertex |
| `eRecoil` | Kinetic energy of the recoiling nucleus, or of the recoil electron for a triplet |
| `isTriplet` | 1 for conversion on an atomic electron, 0 for nuclear |

Energy is conserved row by row as `eElectron + ePositron + eRecoil + 2 mₑc² = eGamma`.

## Reading the output

ROOT is not installed on this machine (`root/` is a submodule that would have to be built first),
so the quickest way in is `uproot`:

```python
import uproot, numpy as np
d = uproot.open("ntuples/Si_1GeV_50000.root")["conversions"].arrays(library="np")
print(d["pathInBlock"].mean() / 10, "cm")     # conversion mean free path
nuclear = d["isTriplet"] == 0
```

## Exploring the output

`training/explore_ntuple.ipynb` opens the ntuple with `uproot` and writes **one PDF per branch**
into `training/plots/` (git-ignored) in the ATLAS style. Nothing is drawn inside the notebook —
each figure is saved and closed, so the file stays small and diffs stay clean. A single
`plot_branch(column, xlabel, logy, logx, bins)` helper does the work, driven by one `BRANCHES`
spec list and a `for` loop. The repository root is found by walking up for `.git`, so it runs
whatever directory Jupyter was launched from.

```bash
conda activate g4fastsim
jupyter lab studies/gammaConversionSi/training/explore_ntuple.ipynb
```

Histograms are drawn as a black outline with no fill, with `yerr=True` — mplhep's asymmetric
Garwood **Poisson** interval, which stays non-zero for empty bins, rather than the `√N` Gaussian
approximation. It matters only in the sparse low-count tails; at 1e5 counts per bin the bars are
invisible.

Several branches span five to ten orders of magnitude — `thetaElectron` covers 1e-7 to 3 rad,
`eRecoil` 2e-9 to 29 MeV — so those calls pass `logx=True`; on a linear axis they collapse into a
single bin.

The setup cell sets `plt.rcParams["mathtext.fallback"] = "stixsans"`. Do not remove it, and do not
"fix" it by changing `mathtext.fontset`: the ATLAS style maps every math font to TeX Gyre Heros,
which has no sized-radical glyph, so the `\sqrt` in the `com` label otherwise logs
`No TeX to Unicode mapping for '\__radicalbig__'` on every figure and falls back to `?`. Setting
only the *fallback* fixes the glyph while leaving all other math in the ATLAS font.

Two shapes are worth understanding before reading the plots:

- **`eGamma` is flat from ~3 MeV up.** The gun samples log-uniformly, but the ntuple holds only
  *converted* photons, so the histogram is the gun spectrum times the conversion probability.
  With 10 m of silicon that probability is ≥99 % above 3 MeV; only the lowest bin sits low, at
  92 % of the plateau, where the mean free path just above threshold is still metres. (With the
  earlier 1 m block this turn-on extended all the way to ~100 MeV.)
- **`pathInBlock` is concave on a log y-axis, not straight.** A single exponential would be
  straight; a run spanning 2 MeV–100 GeV superposes one exponential per energy. Use
  `config/mono1GeV.cfg` for the clean single-exponential version.

## Training a model

`training/conversion_data.py` holds `build_dataset()`: it turns the raw ntuple arrays into inputs
and targets, sorting the pair by energy rather than by charge so the model does not have to learn
the arbitrary e⁻/e⁺ labelling.

| | |
| --- | --- |
| input (1) | `eGamma` |
| predicted / sampled (3) | `eLead`, `thetaLead`, `eRecoil` |
| conversion mode | `isTriplet` |
| derived in-model | `eSub = eGamma − 2mₑc² − eLead − eRecoil` |

`eRecoil` is a target rather than assumed negligible, which is what makes the conservation step
exact. It is only tens of eV for the ~93 % of conversions that happen on a nucleus, but for the
remaining ≈1/(Z+1) the recoil is an atomic electron carrying up to two thirds of the photon
energy; dropping it would put `eSub` out by more than 100 % on those.

**`pathInBlock` is deliberately not an input.** Given that a conversion happened, the final state
depends on the photon energy and the material — not on *where* in the block it happened. The
vertex is sampled independently, from the attenuation length, so the path is a nuisance variable
the target does not depend on, and feeding it in only asks the network to learn that it should be
ignored. In fast simulation Geant4 supplies the vertex itself through the interaction length; a
model of the conversion has to supply kinematics and nothing else.

### `ConversionFlow` — sampling the final state

`training/conversion_flow.py`, built on `nflows`. Given `eGamma`, the pair kinematics are **not a
deterministic function**: Geant4 samples the energy sharing and the angles from a distribution, and
`G4BetheHeitler5DModel` does so over the full five-dimensional final state. Reproducing those
distributions needs a model that **samples** rather than regresses, and this one learns the
conditional density itself. Each quantity has its own head reading a shared trunk over `eGamma`,
and each later head also sees the
quantity sampled before it, so the four together are the exact joint density by the chain rule:

```
eGamma → log10 → [0,1] → trunk ─┬─► triplet_head   Bernoulli(isTriplet | E)
                                ├─► recoil_flow    p(z_recoil | E, isTriplet)
                                ├─► lead_flow      p(z_lead   | E, z_recoil)
                                └─► theta_flow     p(z_theta  | E, z_lead)
```

It works in physics-scaled coordinates — energy *fractions* and `θ·E/mₑc²` — which is what makes
`eSub ≥ 0`, exact energy conservation and `eLead ≥ eSub` structural rather than learned. Each
head's negative log-likelihood is averaged separately and the four are summed, so no head
dominates the total and a head that stops learning gets its own flat curve.

**[`training/README.md`](training/README.md) is the detailed reference** — dataset shapes, the
coordinate maps, the normalisation buffers, layer sizes, the loss and the L2 penalty. Keep it as
the single source for those, rather than repeating them here.

```bash
conda activate g4fastsim
jupyter lab studies/gammaConversionSi/training/train_flow.ipynb
```

The notebook ends in a **closure test**, the right way to score a sampler — there is no right
answer per event, only a right distribution. It draws one sample per validation
row at that row's true `eGamma` and compares: the four marginals with ratio panels, the same
marginals inside narrow `eGamma` slices (an inclusive marginal can look right while every
individual energy is wrong), the `eLead`–`thetaLead` correlation the chained heads exist to
reproduce, the sampled triplet fraction against 6.67 %, and `min(eSub)`, which must be ≥ 0.
Figures go to subdirectories of `plots/` named by `INPUT_PLOT_SUBDIR` and `CLOSURE_PLOT_SUBDIR`;
weights go to `training/models/` and are git-ignored along with any other `*.pt`.

`EPOCHS` is a ceiling rather than a target: the loop early-stops once the summed validation NLL has
not improved for `PATIENCE` epochs, rewrites `models/conversion_flow.pt` on every epoch that *does*
improve, and reloads those best weights before the closure test, so an interrupted run still leaves
its best model behind. The per-epoch history is pickled beside them as `models/flow_history.pkl`,
also git-ignored, and the loss curves — one per head plus a combined overview — go to `plots/`.

#### The trained model

100 epochs in 358.2 min on an M-series GPU (214.9 s/epoch); early stopping never fired. Best epoch
**91**, validation total **+3.601** — per head `isTriplet` 0.245, `eRecoil` 0.703, `eLead` 1.379,
`thetaLead` 1.276. Those are negative log-likelihoods in the standardised learned coordinates, so
compare them only with each other and with later runs of this same model.

The closure test on the 3.99 M validation rows gives a sampled triplet fraction of **6.73 %**
against Geant4's 6.68 % in the same sample, `min(eSub) = 1.5e-4 MeV`, and `eLead ≥ eSub` on every
sample — the two structural constraints hold, as `from_learned` guarantees they must. The energy
conservation residual is 3.9e-3 MeV, which is float32 rounding at GeV scale and not a broken
constraint: `eSub` is computed as the remainder, so the algebra closes exactly and only the
arithmetic is approximate. Read it the same way as the relative bound in **Validation** below.

## Validation

Reproduced on this machine with the shipped configs:

- **Mean free path.** `config/mono1GeV.cfg` gives `mean(pathInBlock) = 12.86 cm` with a standard
  deviation of 12.73 cm — the equality is the signature of a clean exponential. Scanning the
  spectrum run shows it falling with energy and flattening at ~12.3 cm, approaching the asymptotic
  (9/7)·X₀ = 12.05 cm for silicon (X₀ = 9.37 cm).
- **Energy conservation.** `eElectron + ePositron + eRecoil + 2mₑ = eGamma` holds to a *relative*
  1.5e-9 typically and 7.6e-7 at worst, over all 9.98 M rows of the shipped 10 m run. Quote it as
  a relative bound, not an absolute one: the absolute residual scales with photon energy (8e-8 MeV
  below 20 MeV, 4e-4 MeV above 10 GeV, 7.2e-2 MeV at worst near 100 GeV). That is floating-point
  rounding through the model's boosts and rotations, not a physics defect.
- **Geometry.** `zConv == −½·blockThickness + pathInBlock` exactly — bit-for-bit, max deviation
  0.0 mm — confirming the path accumulation.
- **Triplet fraction.** 6.67 % in silicon, against the expected 1/(Z+1) = 6.67 %.
- **Angles.** Median `thetaElectron` ≈ 1.5 mrad at 1 GeV, a few times mₑc²/E = 0.51 mrad, as
  expected for the heavy-tailed Bethe–Heitler distribution; the acoplanarity is broadly
  distributed rather than fixed at π.
- **Conversion fraction.** 99.96 % at 1 GeV in 1 m of silicon. Over 2 MeV–100 GeV the shipped
  `default.cfg` gives **99.77 %** in 10 m of silicon, against 93.1 % when the block was 1 m — the
  shortfall is entirely the lowest energies, where the mean free path is metres. The end-of-run
  summary prints this number.

## Fast simulation with the flow (`simMode = fast`)

The same executable can replace Geant4's final-state sampler with the trained flow, as a
`G4VFastSimulationModel` (`ConvFastSimModel`) attached to the block, turned on with `simMode = fast`.
It is deliberately faithful in the one place that matters and fast in the other:

- **The conversion is decided exactly as Geant4 decides it.** `ModelTrigger` draws the interaction
  length from the *real* pair-production cross section — an owned, standalone `G4BetheHeitler5DModel`,
  the same parametrisation the full run uses — so the conversion rate and the vertex distribution are
  unchanged. Because the block is one uniform material and the photon loses no energy before
  converting, a single exponential draw over the traversal is exactly equivalent to Geant4's
  step-by-step sampling of that constant mean free path.
- **Only the final state comes from the flow.** When a conversion falls inside the block, `DoIt`
  passes the photon energy to `ConversionFlowInference`, which runs the exported flow through **ONNX
  Runtime** and returns the pair energies, the leading polar angle, the recoil and the triplet flag.
  The charge label, the azimuth and the sub-lepton angle (by coplanar transverse-momentum balance)
  are assigned in `DoIt`, since the flow does not model them.

Both modes fill the identical `conversions` ntuple, so the fast output is validated directly against
the full one (closure). With matched seeds the two agree on the conversion fraction, `pathInBlock`
and `zConv`, the pair energies, the median leading angle and the triplet fraction; energy
conservation is *exact* in fast mode (`from_learned` closes the algebra), against Geant4's float
rounding. The sub-lepton angle and the acoplanarity are only approximate — the flow does not model
them.

### Exporting the flow

The C++ side does not load the PyTorch checkpoint. `training/export_flow_onnx.py` writes the
deterministic sub-networks (the shared trunk and one MADE per head) to ONNX and the standardisation
constants to text, into `training/models/onnx/`; the stochastic remainder (the standard-normal draws,
the rational-quadratic spline **inverse**, the de-standardisation and `from_learned`) is
reimplemented in `ConversionFlowInference` and validated against `nflows` by the export script (spline
inverse to 1e-5; sampled distributions within sampling noise). Run it once before building:

```bash
conda run -n g4fastsim python training/export_flow_onnx.py   # writes models/onnx/
```

CMake copies `models/onnx/` next to the executable, so the default `flowModelDir = models/onnx`
resolves from the build directory. Fast mode needs ONNX Runtime (`brew install onnxruntime`); the
build fails with a clear message if it is missing.

### Benchmarking full vs fast

`benchmark/run_benchmark.sh` times both modes over many experiments. Each run prints one
machine-parseable `BENCHMARK` line (mode, events, real/user/sys seconds for the **event loop only** —
the timer excludes I/O, table building and the one-time flow load), which the script collects into
`benchmark/results/timings.csv`:

```bash
source install/geant4/bin/geant4.sh
./studies/gammaConversionSi/benchmark/run_benchmark.sh          # 100 experiments x 1e6 events, per mode
EXPERIMENTS=2 EVENTS=20000 .../run_benchmark.sh                 # quick smoke run
python3 studies/gammaConversionSi/benchmark/summarise.py <csv>  # per-mode mean/sd, throughput, ratio
```

`benchmark/{full,fast}.cfg` are `default.cfg`'s 10 m Si geometry at 1e6 events, single-threaded for a
clean per-experiment wall time, with a fixed `outputName` per mode so the ntuple is overwritten each
experiment (the last survives as the closure sample) rather than filling the disk. On this machine
the flow mode runs **≈ 2× faster** per event than the full 5D model — the 5D sampler rebuilds the
recoil nucleus through `G4IonTable` on every conversion, which costs more than the flow's ONNX call.
