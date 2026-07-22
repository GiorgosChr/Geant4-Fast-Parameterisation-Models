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

Several branches span five to ten decades — `thetaElectron` covers 1e-7 to 3 rad, `eRecoil` 2e-9
to 29 MeV — so those calls pass `logx=True`; on a linear axis they collapse into a single bin.

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

`training/conversion_dnn.py` holds `ConversionDNN`, a fully connected network, and
`build_dataset()`, which turns the raw ntuple arrays into its inputs and targets. The pair is
sorted by energy rather than by charge, so the network never has to learn the arbitrary e⁻/e⁺
labelling:

| | |
| --- | --- |
| inputs (2) | `eGamma`, `pathInBlock` |
| predicted (3) | `eLead`, `thetaLead`, `eRecoil` |
| derived in-model | `eSub = eGamma − 2mₑc² − eLead − eRecoil` |

`eRecoil` is predicted rather than assumed negligible, which is what makes the conservation step
exact. It is only tens of eV for the ~93 % of conversions that happen on a nucleus, but for the
remaining ≈1/(Z+1) the recoil is an atomic electron carrying up to two thirds of the photon
energy; dropping it would put `eSub` out by more than 100 % on those.

**The normalisation is part of the model**, not the notebook. `input_mu`, `input_sigma`,
`target_mu`, `target_sigma` are registered as *buffers*, so they are never touched by the
optimiser, move with `.to(device)`, and are written into the `state_dict` — a saved checkpoint is
self-contained, with no scaler to reload alongside it. `fit_normalisation(x, y)` is called once on
the training split only; calling the model before that raises rather than silently training on
unscaled data.

The transform is `log10` then standardise, because every quantity here is strictly positive and
spans 4.7–12 decades. Plain standardisation would leave almost every event squashed against zero
with rare points tens of sigma out. Working in log space also makes `eLead` and `eRecoil` positive
by construction, since they are decoded through `10**x`. This is *in addition to* the
`BatchNorm1d` in each hidden layer, which cannot see the raw physical scale because it only ever
acts after the first linear layer.

`training/train_dnn.ipynb` imports the module and trains it. Feature histograms are written to a
subdirectory of `plots/` named by the `INPUT_PLOT_SUBDIR` global (`training_inputs` by default);
weights go to `training/models/` and are git-ignored along with any other `*.pt`.

```bash
conda activate g4fastsim
jupyter lab studies/gammaConversionSi/training/train_dnn.ipynb
```

A full run is 20 epochs over ~8 M training rows, about 13 minutes on an M-series GPU. Set
`MAX_EVENTS` to a few hundred thousand to iterate quickly.

### A plain regression network does not solve this problem

Worth knowing before spending time on it. Trained as above, the loss is flat from the first epoch
and the median fractional errors on the validation set are:

| target | median fractional error |
| --- | --- |
| `eLead` | 0.16 |
| `thetaLead` | 0.53 |
| `eRecoil` | 1.00 |

That is not a bug in the network or the normalisation — energy conservation still closes to
0.0 MeV and no validation event has `eSub < 0`. It is the model class being wrong for the task.
Given `eGamma` and `pathInBlock`, the pair kinematics are **not a deterministic function**:
Geant4 samples the energy sharing and the angles from a distribution, and `G4BetheHeitler5DModel`
does so over the full five-dimensional final state. A network trained with MSE can only learn the
conditional *mean*, so it collapses onto it and the loss plateaus at the conditional variance —
which is exactly what the flat curve shows. `eRecoil` is hit worst because its conditional
distribution is the broadest, spanning roughly twelve decades and being bimodal between the
nuclear and triplet cases.

Reproducing these distributions needs a model that **samples** rather than regresses — a
normalising flow, VAE, GAN, or a quantile/density regression. `nflows` is already in the
`g4fastsim` environment for that. `ConversionDNN` is still useful as the scaffolding: the
normalisation buffers, the sorted-pair dataset builder and the in-model conservation step all
carry over unchanged.

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
