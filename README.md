# Geant4-Fast-Parameterisation-Models

Geant4 fast-simulation ("parameterisation") examples and utilities: replacing the detailed,
step-by-step tracking inside a detector region with a learned model of its response, then testing
and benchmarking it.

## Layout

| Path | Contents |
| --- | --- |
| `studies/` | Self-contained preliminary studies, each its own CMake project |
| `geant4/`, `root/` | Vendored upstream toolkits (git submodules, read-only) |

### Studies

Each study generates clean physics ground truth and then fits a fast-simulation model to it.

- [`studies/gammaConversionSi`](studies/gammaConversionSi) — γ → e⁺e⁻ conversion in a silicon
  block, with pair conversion as the *only* physics process. Trains a **normalising flow** on the
  pair kinematics and runs it back inside Geant4 as a fast-simulation model.
- [`studies/gammaConversion`](studies/gammaConversion) — the same idea generalised to a scan over
  materials, so the flow can be conditioned on the target atomic number `Z`.

## Analysis environment

`environment.yml` describes a conda environment for reading the `.root` output and training the
models:

```bash
conda env create -f environment.yml
conda activate g4fastsim
```

Nothing here needs it to build or run — Geant4 writes its ntuples through bundled g4tools, and the
studies are plain C++ applications. It carries ROOT, the Scikit-HEP stack (uproot, awkward, …) and
**PyTorch as the only deep-learning framework**.

## Dependencies

Geant4 is vendored as a git submodule at `geant4/` (`v11.5.0.beta`); the dependencies below build it
and any application here.

```bash
git submodule update --init --recursive
```

### Required

| Dependency | Minimum | Notes |
| --- | --- | --- |
| C++ compiler | C++17 | Apple clang or GCC 9+; also supplies the `expat` Geant4 uses to parse its data |
| CMake | 3.16 | Geant4 requires `3.16…3.27` |
| Disk space | ~15 GB | The physics datasets pulled in by `-DGEANT4_INSTALL_DATA=ON` dominate |

### Optional

| Dependency | Enables | CMake flag |
| --- | --- | --- |
| Qt 6 (`qtbase` only) | Interactive UI and OpenGL viewers | `-DGEANT4_USE_QT=ON` |
| Xerces-C | GDML geometry import/export | `-DGEANT4_USE_GDML=ON` |
| ONNX Runtime | Running an exported flow inside Geant4 (fast-sim mode) | detected by `find_package` |

Geant4 needs only the `Core Gui Widgets OpenGL OpenGLWidgets` Qt components, all in Homebrew's
`qtbase` — the full `qt` formula pulls in ~50 unused packages. Because `qtbase` is off CMake's
default search path, pass its prefix when configuring:

```bash
brew install qtbase xerces-c
cmake -S geant4 -B build/geant4 -DCMAKE_INSTALL_PREFIX=$PWD/install/geant4 \
      -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase \
      -DGEANT4_INSTALL_DATA=ON -DGEANT4_BUILD_MULTITHREADED=ON -DGEANT4_USE_QT=ON
```

Building without Qt (`-DGEANT4_USE_QT=OFF`) still works, leaving the terminal UI and file-based vis
drivers. On Linux the equivalents are `qt6-base-dev`, `libxerces-c-dev` and X11/OpenGL headers.
