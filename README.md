# Geant4-Fast-Parameterisation-Models
Geant4 fast parameterisation examples and utilities for implementing, testing, and benchmarking fast detector simulation techniques using G4FastSimulationModel.

## Dependencies

The Geant4 toolkit itself is vendored as a git submodule at `geant4/` (currently `v11.5.0.beta`), so the dependencies below are those needed to *build* it and any application in this repo.

### Required

| Dependency | Minimum | Notes |
| --- | --- | --- |
| C++ compiler | C++17 | Apple clang (Xcode Command Line Tools) or GCC 9+. Also supplies the system `expat`, which Geant4 uses for its GDML/physics data parsing. |
| CMake | 3.16 | Geant4 declares `cmake_minimum_required(VERSION 3.16...3.27)`. |
| git | any | Needed for the submodule: ~200 MB of working tree plus ~440 MB of git objects. |
| Disk space | 15 GB free | The physics datasets pulled in by `-DGEANT4_INSTALL_DATA=ON` are several GB on their own, on top of the build tree and the install prefix. |

```bash
git submodule update --init --recursive
```

### Optional

| Dependency | Enables | CMake flag |
| --- | --- | --- |
| Qt 6 (`qtbase` only) | Interactive UI and the OpenGL/ToolsSG viewers — the practical GUI option on macOS | `-DGEANT4_USE_QT=ON` |
| Xerces-C | GDML geometry import/export | `-DGEANT4_USE_GDML=ON` |
| ONNX Runtime / LibTorch / LWTNN | ML inference backends, only if following the Par04 ML-shower example | selected by `find_package` in that example |

Geant4 requires only the `Core`, `Gui`, `Widgets`, `OpenGL` and `OpenGLWidgets` Qt 6 components (see `geant4/cmake/Modules/G4InterfaceOptions.cmake`), all of which live in Homebrew's `qtbase` formula. Installing the full `qt` formula instead pulls in ~50 extra packages (including `qtwebengine`) that are never used:

```bash
brew install qtbase xerces-c
```

Because `qtbase` is not on CMake's default search path, pass its prefix when configuring:

```bash
cmake -S geant4 -B build/geant4 -DCMAKE_INSTALL_PREFIX=$PWD/install/geant4 \
      -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase \
      -DGEANT4_INSTALL_DATA=ON -DGEANT4_BUILD_MULTITHREADED=ON -DGEANT4_USE_QT=ON
```

Omitting `-DCMAKE_PREFIX_PATH` produces `Could not find a package configuration file provided by "QT"`. Building without Qt at all (`-DGEANT4_USE_QT=OFF`) still works, leaving the terminal UI and the file-based vis drivers (VRML, HepRep, ASCIITree).

On Linux the equivalents are `qt6-base-dev` / `qt6-qtbase-devel`, `libxerces-c-dev`, plus `libexpat1-dev` and X11/OpenGL development headers.
