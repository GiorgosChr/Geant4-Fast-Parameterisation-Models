# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Interaction

Start every reply with "Giorgos".

## Git

**Never attribute commits to Claude.** Do not add a `Co-Authored-By: Claude …` trailer, a "Generated with Claude Code" line, or any similar marker to commit messages or PR descriptions — the user is the sole author of record. This overrides the default commit-trailer behaviour.

**Commit messages are the subject line only — no body.** Write one summary line and stop; do not follow it with a blank line and explanatory paragraphs, and do not offer to add them. If a change feels too large to summarise in one line, split it into more commits rather than writing a description.

## Repository state

The repo holds `README.md`, `LICENSE`, two upstream source trees vendored as git submodules, and `studies/` — self-contained preliminary studies, each its own CMake project. No fast-simulation model of the project's own exists yet; it will be written at the repo root, alongside (not inside) the submodules.

| Path | Upstream | Pinned at | Working tree |
| --- | --- | --- | --- |
| `geant4/` | `git@github.com:Geant4/geant4.git` | `v11.5.0.beta` | ~200 MB (+~440 MB of git objects) |
| `root/` | `git@github.com:root-project/root.git` | `master`, commit `28b3942d157` ≈ 6.41.01-dev | ~776 MB |

**Treat both as read-only vendored upstream.** They are mirrors kept as reference source + buildable toolkits. Never commit edits inside them; changes there show up as a submodule pointer bump, which is almost never intended.

Note that `root/` tracks **master, not a release** — `git describe` returns `v6-39-99-1013-g28b3942d157`, so don't go looking for a version tag or assume release-note behaviour.

Clone/refresh with:

```bash
git submodule update --init --recursive   # ~1 GB of working tree, slow first time
```

## Local environment

macOS on Apple Silicon, Homebrew prefix `/opt/homebrew`, Apple clang 17, CMake 4.0.3, 12 cores (use `-j10`). Build trees live at `build/<pkg>`, install prefixes at `install/<pkg>` (`build/geant4`, `install/geant4`, and the same pattern for `root`); all untracked.

**Homebrew is hands-off.** Never run `brew update`, `brew upgrade`, or `brew cleanup`, and never upgrade a formula that isn't strictly required for the task at hand — the machine deliberately sits on ~80 outdated formulae. Install with:

```bash
HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1 HOMEBREW_NO_INSTALLED_DEPENDENTS_CHECK=1 brew install <formula>
```

`brew install` still force-upgrades any outdated *dependencies* of what it installs, and no flag prevents that. So before installing, check the blast radius with `brew install --dry-run <formula>` against `brew outdated`, and say what would get upgraded rather than discovering it afterwards. `brew --prefix <formula>` is **not** an installation check — it prints a computed path for uninstalled formulae too; use `brew list --formula`.

**The shell is zsh, not bash.** Quote anything glob-like (`"G4*DATA"`), or zsh aborts the command with `no matches found` instead of passing it through. Scalars do not word-split: `for x in $var` iterates once over the whole string — use an array (`var=(a b c)`) or `${=var}`.

## Building

### Geant4 itself (only needed if no system/CVMFS install is available)

```bash
cmake -S geant4 -B build/geant4 -DCMAKE_INSTALL_PREFIX=$PWD/install/geant4 \
      -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase \
      -DGEANT4_INSTALL_DATA=ON -DGEANT4_BUILD_MULTITHREADED=ON -DGEANT4_USE_QT=ON
cmake --build build/geant4 -j10 && cmake --install build/geant4
source install/geant4/bin/geant4.sh   # exports GEANT4_DATA_DIR
```

This takes tens of minutes. Prefer an existing install and just `source .../bin/geant4.sh`.

**Run every one of these from the repo root**, not from inside `build/`: `cmake --install` takes the build directory as an argument, and the `source` path is relative to the root. `geant4.sh` itself self-locates (`dirname ${BASH_SOURCE[0]:-$0}`) and explicitly handles zsh, so only the path you type matters, not your working directory. Note it does **not** export `Geant4_DIR` — applications still need `-DGeant4_DIR=<prefix>/lib/cmake/Geant4`.

**Don't go looking for `G4LEDATA` and friends.** As of 11.5 `geant4.sh` exports only `GEANT4_DATA_DIR` and leaves every individual `G4*DATA` export commented out; datasets are resolved from that one directory at runtime by `G4FindDataDir.cc`. An empty `$G4LEDATA` after sourcing is correct, not a broken install.

**`-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase` is mandatory here and easy to lose.** Qt comes from Homebrew's `qtbase` formula, which CMake does not search by default; without it configure dies with `Could not find a package configuration file provided by "QT"` and every downstream step fails confusingly (no generated `Makefile`, no install tree to source). Geant4 needs only the `Core Gui Widgets OpenGL OpenGLWidgets` components (`geant4/cmake/Modules/G4InterfaceOptions.cmake`), so `qtbase` is deliberate — do not "fix" a Qt problem by installing the full `qt` formula, which adds ~50 unused packages including `qtwebengine`.

### ROOT (`root/`) — for analysis, not for building anything here

**Nothing in this repo needs ROOT to compile or run.** Geant4 writes `.root` files through the *bundled* g4tools (`geant4/source/analysis/root/` includes `tools/wroot/…`), and no example under `geant4/examples/extended/parameterisations/` calls `find_package(ROOT)`. So never build ROOT merely to obtain ROOT-format output — it is here to *analyse* and plot that output (and, if wanted later, for TMVA SOFIE, which generates C++ inference code as an alternative to the ONNX/LWTNN/Torch backends Par04 uses; needs `-Dtmva-sofie=ON` + protobuf 3).

ROOT is **not installed on this machine** — no `root`/`root-config` on `PATH`, no `root` in `brew list`. Any ROOT step therefore requires building the submodule first, which is far more expensive than building Geant4: `builtin_llvm`, `builtin_clang` and `builtin_cling` all default to ON, so the build compiles an entire LLVM/Clang toolchain. Budget hours and >10 GB, and needs CMake ≥ 3.20 (system CMake 4.0.3 is fine).

```bash
cmake -S root -B build/root -DCMAKE_INSTALL_PREFIX=$PWD/install/root \
      -Droofit=OFF -Dtmva=OFF -Dwebgui=OFF -Dxrootd=OFF -Ddavix=OFF -Dfitsio=OFF
cmake --build build/root -j10 && cmake --install build/root
source install/root/bin/thisroot.sh
root -l -b -q analysis.C     # batch macro;  `root -l` for the interpreter
```

Three things that bite:

- **`check_connection` defaults to ON**, so configure *fails* rather than degrades when a component that must be downloaded (clad, and any enabled `builtin_*`) can't be fetched. ROOT configure is not offline-safe.
- **`ROOTConfig.cmake` does not install to `lib/cmake/ROOT`.** Without `-Dgnuinstall=ON` it lands in `<prefix>/cmake` (with it, `<prefix>/share/cmake`), so downstream apps need `-DROOT_DIR=$PWD/install/root/cmake` — the conventional path just silently fails to be found.
- On macOS ROOT flips its own defaults: `cocoa` ON, `x11` OFF, `builtin_openssl` ON. Don't "fix" graphics by forcing `-Dx11=ON`.

The `-D…=OFF` list above is only for trimming build time when all that's wanted is histogramming/RDataFrame; drop it if a specific package (RooFit, TMVA) is actually needed.

### A Geant4 application (the standard pattern all examples follow)

```bash
cmake -S <appdir> -B <appdir>_build -DGeant4_DIR=<prefix>/lib/cmake/Geant4
cmake --build <appdir>_build -j8
./<appdir>_build/<exampleName> <macro>.mac        # batch
./<appdir>_build/<exampleName>                    # interactive UI+vis
```

The executable name is the basename of the `.cc` holding `main()`. Applications find Geant4 via `find_package(Geant4 ... ui_all vis_all)` + `include(${Geant4_USE_FILE})`.

## Studies (`studies/`)

Self-contained preliminary studies, one CMake project each, built exactly like any Geant4 application. They do **not** use `G4VFastSimulationModel` — they produce the reference distributions a parameterisation will later be fitted to.

- **`studies/gammaConversionSi`** — γ → e⁺e⁻ conversion in a thick block, with `G4GammaConversion` as the only process in a hand-written `G4VUserPhysicsList`. Writes one ROOT ntuple row per conversion (photon energy, path in block, pair energies/angles). See its `README.md` for the ntuple schema and validated numbers.

```bash
./studies/gammaConversionSi/build.sh        # configures + builds; JOBS/Geant4_DIR/CLEAN override
source install/geant4/bin/geant4.sh
cd build/gammaConversionSi && ./gammaConversionSi config/default.cfg   # 100k events in <1 s
```

Each study ships its own `build.sh` that resolves paths relative to itself, so it runs from any working directory; keep that pattern for new ones.

Conventions worth keeping for future studies:

- **A run is described by a `key = value` config file, not a macro.** `ConvConfig` parses it (units via `G4UIcommand::ConvertToDimensionedDouble`) and replays it as UI commands around `/run/initialize`. The argument is treated as a config file unless it ends in `.mac`.
- **Output is auto-named and collected in `ntuples/`** (git-ignored) as `<material>_<energy>_<N>.root`, so repeated runs with different settings never overwrite each other. `<N>` is photons *fired*, not rows written.
- **The full Geant4 output is teed to `logs/<same stem>.log`** (also git-ignored) by `ConvLogger`, a `G4UIsession` installed as the cout destination.
- Ntuple units are **MeV / mm / rad**, applied explicitly at fill time (`value / MeV`).

Three Geant4 traps this study hit, all relevant to any single-process EM study:

- **A worker thread's output never reaches the master's cout destination.** `G4UImanager::SetCoutDestination` on the master captures only master output; each worker's `G4MTcoutDestination` writes its `G4WT<n> > ` lines straight to the terminal. To log everything, install the same destination again from `ActionInitialization::Build()`, which runs on the worker thread — and serialise the writes, since all threads then share one stream.

- **A bare `new G4GammaConversion` uses `G4PairProductionRelModel`**, which emits an *exactly coplanar* pair (Δφ ≡ π to machine precision) — silently useless for an angular study. `G4EmStandardPhysics` (opt0) inherits this; only opt3/opt4/Livermore/LowEP call `SetEmModel(new G4BetheHeitler5DModel())`. Set the 5D model explicitly.
- **`G4BetheHeitler5DModel` emits three secondaries** — e⁻, e⁺ and a recoil — and builds the recoiling nucleus through `G4IonTable`, so `ConstructParticle()` must define `G4GenericIon` or every event warns `PART105: Can not create ions because GenericIon is not ready`. In a fraction ≈1/(Z+1) of conversions (6.7 % in Si) the recoil is a second electron (triplet conversion), so "the" pair electron is the *first* e⁻ in the secondary vector.

- **A short particle list is a false economy.** Once `GenericIon` exists, `G4RunManagerKernel::SetupPhysics()` (`geant4/source/run/src/G4RunManagerKernel.cc:566`) unconditionally calls `G4IonConstructor::ConstructParticle()`, which defines the hypernuclei; building their decay tables then emits a `G4VDecayChannel::FillDaughters ... is not defined` warning for every missing daughter — 38 lines at the head of every run. Declaring just the named daughters does not converge, since each drags in its own decay products. Construct the full standard set (`G4BosonConstructor`, `G4Lepton…`, `G4Meson…`, `G4Baryon…`, `G4Ion…`, `G4ShortLived…`) as TestEm14 does. What makes a study single-process is `ConstructProcess()`, not a short particle list; the extra particles are inert and never appear in an event.

### Testing

Geant4 applications have no unit-test framework here. Validation is by macro: each example ships `exampleXYZ.in` (input) and `exampleXYZ.out` (reference output), and regression checking means diffing a run against the reference. Use short `/run/beamOn N` macros for iteration; note that MT output ordering is non-deterministic, so run single-threaded (`/run/numberOfThreads 1` or `-DGEANT4_BUILD_MULTITHREADED=OFF`) when comparing output.

**Par03 and Par04 require `-m MACRO`** — a bare macro path is rejected with `Unknown argument` (`examplePar03.cc`, `examplePar04.cc`). Par01, Par02 and gflash take the macro positionally, as in the recipe above.

**The shipped `.out` files are not from this Geant4 version, so never diff them whole.** Most are `geant4-11-04-ref-06`; `Par03/examplePar03.out` is `geant4-10-07-ref-09` (2021) and is badly stale — Par03 also ships a second, much closer reference as `Par03/Par03.out`, which is the one to use. Differences in the version banner, available vis drivers, and FPE/G4Backtrace notices are expected noise. Compare the physics lines instead (for Par03: the fitted `sigma`, `alpha`, `beta`, `max depth`, and the deposit count).

Known-good smoke test on this machine — builds and runs in ~30 s single-threaded, reproducing `sigma = 9.87274 mm`, `alpha = 4.3593`, 100 deposits exactly as in `Par03.out`:

```bash
cmake -S geant4/examples/extended/parameterisations/Par03 -B build/Par03 \
      -DGeant4_DIR=$PWD/install/geant4/lib/cmake/Geant4
cmake --build build/Par03 -j10
source install/geant4/bin/geant4.sh
./build/Par03/examplePar03 -m <macro>.in   # writes .root files into $PWD
```

## Fast simulation architecture (what this project is about)

Geant4's fast-simulation ("parameterisation") framework replaces detailed tracking inside a geometry region with a parameterised response. The pieces, all under `geant4/source/processes/parameterisation/include/`:

- **`G4VFastSimulationModel`** — the class you subclass. Three pure virtuals define the contract: `IsApplicable(const G4ParticleDefinition&)` (particle type filter, checked once), `ModelTrigger(const G4FastTrack&)` (per-step decision — energy/geometry cuts), `DoIt(const G4FastTrack&, G4FastStep&)` (produce the parameterised result). Optional `AtRestModelTrigger`/`AtRestDoIt`, plus `Flush()` for models that buffer across tracks.
- **`G4FastTrack`** — read-only view of the track *in envelope-local coordinates*; `G4FastStep` — how you kill/modify the primary and emit secondaries and energy deposits.
- **Envelope = `G4Region`.** Constructing a model with a region attaches a `G4FastSimulationManager` to it. There is one active model per region per particle.
- **`G4FastSimHitMaker` + `G4VFastSimSensitiveDetector`** — route deposits created in `DoIt` into sensitive detectors so fast and full simulation hits land in the same collection.
- **`G4GlobalFastSimulationManager`** — registry; `/param/...` UI commands come from `G4FastSimulationMessenger`.

Wiring an application takes exactly two steps, split across two files:

1. In `main()`: `physicsList->RegisterPhysics(fastSimulationPhysics)` where a `G4FastSimulationPhysics` has had `ActivateFastSimulation("e-")` etc. called on it. This is what inserts `G4FastSimulationManagerProcess` into the particles' process managers.
2. In `DetectorConstruction::ConstructSDandField()` (**not** `Construct()` — it must be per-worker-thread): `new MyShowerModel("model", region)`.

Missing either step means the model silently never fires — check both first when debugging a model that isn't triggering.

## Reference examples in the submodule

`geant4/examples/extended/parameterisations/` is the primary reference and the closest analogue to this project's goals:

- **Par01** — baseline demonstration of the facilities (ex-novice N05).
- **Par02** — track/energy smearing from assumed detector resolutions; one model per subdetector (`Par02FastSimModelTracker/EMCal/HCal`).
- **Par03** — emits *multiple* energy deposits from a model and stores them alongside full-simulation hits. The cleanest minimal template: `Par03EMShowerModel` + `Par03DetectorConstruction` + messenger.
- **Par04** — ML-aided EM shower fast simulation. Pluggable inference backends behind `Par04InferenceInterface` (`Par04OnnxInference`, `Par04LwtnnInference`, `Par04TorchInference`), selected by optional `find_package` in its CMakeLists and guarded by `USE_INFERENCE_*` defines; uses parallel worlds for the fast-sim scoring mesh. Training code lives in `training_vae/` and `training_calodit/`.
- **gflash** (`gflash1`–`gflash3`, `gflasha`) — the GFlash parameterisation library, whose implementation is `geant4/source/parameterisations/gflash/`.

`geant4/source/parameterisations/` holds the shipped model libraries (`gflash`, `channeling`) — the reference for what a production-quality model looks like.

## Conventions

Geant4 style, enforced upstream by `geant4/.clang-format` and `geant4/.clang-tidy`: 2-space indent, 100-column limit, `G4`-prefixed types, `f`-prefixed data members, `a`-prefixed parameters, always use Geant4 unit constants (`GeV`, `mm`) rather than bare numbers. Reuse those config files for project code so it reads like the toolkit it extends.

In prose — comments, docstrings, READMEs — write **"orders of magnitude"**, never "decades", when describing the range a quantity spans. "Decade" reads as ten years to most people. This applies to wording only: leave Geant4 API names such as `SetNumberOfBinsPerDecade` exactly as they are.
