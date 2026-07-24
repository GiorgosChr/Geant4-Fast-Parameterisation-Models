# Study: γ → e⁺e⁻ conversion across materials

Fires photons into a thick block with **pair conversion as the only physics process** and writes one
ROOT ntuple row per conversion, tagged with the atomic number `Z` of the element it converted on. It
produces the ground truth for a **`(eGamma, Z)`-conditioned** fast-simulation model of the final
state.

This is the sibling of [`gammaConversionSi`](../gammaConversionSi), generalised from one silicon
block to a scan over materials so the model can be conditioned on `Z`. As there, nothing competes
with conversion, so the recorded final state is undisturbed.

The data comes in two sets, both filling the same ntuple:

- **Training — pure single elements** (`config/elements.cfg`). One block per element, so every
  conversion in a run is on the same `Z`. This gives a clean, even sample of the final state across
  the periodic table (Z = 1…82) — the right data to learn the `Z`-dependence (the triplet fraction
  `1/(Z+1)`, screening, the Coulomb correction).
- **Validation — real detector composites** (`config/composites.cfg`). Each block is a blend of
  elements (the ATLAS tracker materials); conversions happen on many different `Z` within one run.
  Use it to check that the model, trained on pure elements, reproduces a real detector's blended
  response.

**`Z` is a property of the nucleus, not the block.** Pair production happens on one atom at a time,
and the final state is characteristic of that atom's `Z`. So in a composite the recorded `Z` varies
event to event — it is the element that particular conversion happened on, never a single effective
`Z` of the material.

## Build and run

```bash
./build.sh                                    # configures + builds into build/gammaConversion/
source ../../install/geant4/bin/geant4.sh     # from the repo root
cd ../../build/gammaConversion
./gammaConversion config/elements.cfg         # training set: pure elements
./gammaConversion config/composites.cfg       # validation set: composites
./gammaConversion config/elements.cfg materials=Pb   # one material, overriding the list
```

`build.sh` resolves paths relative to itself. The run argument is a config file unless it ends in
`.mac`; trailing `key=value` tokens override the config. Each material runs in its own process and
produces its own auto-named ntuple under `ntuples/` (git-ignored), with the log teed to `logs/`.

## Configuration

A run is described by a `key = value` file in `config/`; `config/default.cfg` documents every key.
The keys mirror those in `gammaConversionSi`, with one difference: `materials` takes a list, and a
name can be a NIST material (`G4_Pb`), a pure element from `config/elements.dat` (`Pb`), or a
composite from `config/materials.dat` (`pix::Chip`, given as `Z=massFraction` lines). The experiment
runs once per material.

Densities are irrelevant to what this study records (only kinematics and the target `Z`); a 10 m
block converts essentially every photon regardless.

### Choice of conversion model

Same as `gammaConversionSi`: `G4BetheHeitler5DModel` explicitly, for a physical five-dimensional
final state; three secondaries (e⁻, e⁺ and a recoil), with the ≈ 1/(Z+1) **triplet** events — the
photon converting on an atomic electron — flagged by `isTriplet`.

## Ntuple `conversions`

One row per converting event. Energies in **MeV**, angles in **radians**.

| Column | Meaning |
| --- | --- |
| `eGamma` | Energy of the incident photon |
| `Z` | Atomic number of the element it converted on (constant per file for a pure element; a spectrum for a composite) |
| `isTriplet` | 1 for conversion on an atomic electron, 0 for nuclear |
| `eRecoil` | Kinetic energy of the recoil (nucleus, or electron for a triplet) |
| `eElectron`, `ePositron` | Kinetic energy of the pair |
| `theta` | Polar angle of the **leading** (higher-energy) lepton w.r.t. the photon |

Energy is conserved row by row: `eElectron + ePositron + eRecoil + 2mₑc² = eGamma`.

Read it with `uproot` (ROOT is not installed here):

```python
import uproot
d = uproot.open("ntuples/Pb_1GeV-100GeV_1000000.root")["conversions"].arrays(library="np")
```

In a composite the `Z` mix is weighted by the per-atom cross section (≈ `Z²`), so high-`Z` trace
elements are over-represented relative to their mass fraction.

## Training

The training code lives in [`training/`](training) and builds a **`Z`-conditioned normalising
flow** on top of the single-material model — see [`gammaConversionSi/training/README.md`](../gammaConversionSi/training/README.md)
for the flow itself. Conditioning on `Z` turns out to need almost no change to that model; the
reasoning is in [`training/Z-conditioning.md`](training/Z-conditioning.md), and background reading on
normalising flows is collected under [`training/resources/`](training/resources).
