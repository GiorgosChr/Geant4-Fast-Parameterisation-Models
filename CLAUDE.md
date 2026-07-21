# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Interaction

Start every reply with "Giorgos".

## Repository state

This repo is a project shell: `README.md`, `LICENSE`, and the upstream **Geant4 source tree vendored as a git submodule at `geant4/`** (currently `v11.5.0.beta`). None of the project's own fast-parameterisation code exists yet — it will be written at the repo root, alongside (not inside) the submodule.

**Treat `geant4/` as read-only vendored upstream.** It is a mirror of `git@github.com:Geant4/geant4.git` and exists as reference source + a buildable toolkit. Never commit edits inside it; changes there show up as a submodule pointer bump, which is almost never intended.

Clone/refresh with:

```bash
git submodule update --init --recursive   # ~2 GB, slow first time
```

## Building

### Geant4 itself (only needed if no system/CVMFS install is available)

```bash
cmake -S geant4 -B build/geant4 -DCMAKE_INSTALL_PREFIX=$PWD/install/geant4 \
      -DGEANT4_INSTALL_DATA=ON -DGEANT4_BUILD_MULTITHREADED=ON -DGEANT4_USE_QT=ON
cmake --build build/geant4 -j8 && cmake --install build/geant4
source install/geant4/bin/geant4.sh   # sets Geant4_DIR and the G4*DATA vars
```

This takes tens of minutes. Prefer an existing install and just `source .../bin/geant4.sh`.

### A Geant4 application (the standard pattern all examples follow)

```bash
cmake -S <appdir> -B <appdir>_build -DGeant4_DIR=<prefix>/lib/cmake/Geant4
cmake --build <appdir>_build -j8
./<appdir>_build/<exampleName> <macro>.mac        # batch
./<appdir>_build/<exampleName>                    # interactive UI+vis
```

The executable name is the basename of the `.cc` holding `main()`. Applications find Geant4 via `find_package(Geant4 ... ui_all vis_all)` + `include(${Geant4_USE_FILE})`.

### Testing

Geant4 applications have no unit-test framework here. Validation is by macro: each example ships `exampleXYZ.in` (input) and `exampleXYZ.out` (reference output), and regression checking means diffing a run against the reference. Use short `/run/beamOn N` macros for iteration; note that MT output ordering is non-deterministic, so run single-threaded (`/run/numberOfThreads 1` or `-DGEANT4_BUILD_MULTITHREADED=OFF`) when comparing output.

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
