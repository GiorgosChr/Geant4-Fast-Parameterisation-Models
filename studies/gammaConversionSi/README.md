# Study: γ → e⁺e⁻ conversion in silicon

Fires photons into a thick silicon block with **pair conversion as the only physics process** and
writes one ROOT ntuple row per conversion — the photon energy, where it converted, and the energies
and angles of the produced pair.

Because nothing competes with conversion (no Compton, no photoelectric effect, no energy loss on the
e±), the recorded final state is exactly what the conversion model produced, undisturbed. That makes
it clean ground truth to train and validate a fast-simulation model against.

## Build and run

```bash
./build.sh                                    # configures + builds into build/gammaConversionSi/
source ../../install/geant4/bin/geant4.sh     # from the repo root
cd ../../build/gammaConversionSi
./gammaConversionSi config/default.cfg        # batch run from a config file (100k photons, ~1 s)
./gammaConversionSi                           # interactive UI + vis.mac
```

`build.sh` resolves paths relative to itself, so it runs from anywhere; `JOBS`, `Geant4_DIR` and
`CLEAN` override its defaults. The run argument is a config file unless it ends in `.mac`.

## Configuration

A run is fully described by a `key = value` file in `config/`; numeric values may carry any Geant4
unit. `config/default.cfg` documents every key.

| Key | Default | Meaning |
| --- | --- | --- |
| `material` | `G4_Si` | Any NIST material name |
| `blockThickness` | `10 m` | Only the thickness matters — the photon is on axis and the pair is killed once recorded |
| `minEnergy`, `maxEnergy` | `2 MeV`, `10 GeV` | Photon energy, sampled **log-uniformly**; set equal for a mono-energetic run |
| `model` | `BetheHeitler5D` | Conversion model (see below) |
| `conversionType` | `mixed` | `mixed`, `nuclear` or `triplet` |
| `simMode` | `full` | `full` runs Geant4's sampler; `fast` runs the trained flow (see below) |

Output is auto-named from the material, energy range and photon count and collected under `ntuples/`
(git-ignored), so runs never overwrite each other; the full Geant4 log is teed to `logs/` under the
same stem.

### Choice of conversion model

A bare `G4GammaConversion` falls back to a model that emits an **exactly coplanar** pair, so the
azimuthal correlation is unusable. This study sets `G4BetheHeitler5DModel` explicitly — the accurate
model that samples the full five-dimensional final state, so the angles and energy sharing are all
physical.

The 5D model always emits three secondaries: e⁻, e⁺ and a recoil. Usually the recoil is the nucleus;
in a fraction ≈ 1/(Z+1) of cases (~6.7 % in silicon) the photon converts on an atomic electron and
the recoil is a second electron. These **triplet** events are flagged by `isTriplet`.

## Ntuple `conversions`

One row per converting event. Energies in **MeV**, lengths in **mm**, angles in **radians**.

| Column | Meaning |
| --- | --- |
| `eGamma` | Energy of the incident photon |
| `pathInBlock`, `zConv` | Path travelled before converting, and the vertex z |
| `eElectron`, `ePositron` | Kinetic energy of the pair |
| `thetaElectron`, `thetaPositron` | Polar angle w.r.t. the initial photon direction |
| `phiElectron`, `phiPositron`, `openingAngle` | Azimuth and e⁻–e⁺ opening angle |
| `eRecoil` | Kinetic energy of the recoil (nucleus, or electron for a triplet) |
| `isTriplet` | 1 for conversion on an atomic electron, 0 for nuclear |

Energy is conserved row by row: `eElectron + ePositron + eRecoil + 2mₑc² = eGamma`.

Read it with `uproot`, since ROOT is not installed here:

```python
import uproot
d = uproot.open("ntuples/Si_1GeV_50000.root")["conversions"].arrays(library="np")
```

`training/explore_ntuple.ipynb` writes one histogram PDF per branch into `training/plots/` in the
ATLAS style.

## Validation

The reference behaviour reproduced on this machine, in brief:

- **Mean free path** approaches the expected (9/7)·X₀ ≈ 12 cm for silicon.
- **Energy conservation** holds to floating-point rounding (relative ~1e-9).
- **Triplet fraction** matches 1/(Z+1) = 6.7 % in silicon.
- **Angles** follow the heavy-tailed Bethe–Heitler distribution around the mₑc²/E scale, and the
  acoplanarity is broadly distributed rather than pinned at π.

## Training a fast-simulation model

Given a photon energy, the pair kinematics are **not a function of it**: Geant4 draws them from a
distribution. A useful model must therefore *sample* from that conditional distribution, not
regress its mean — the same reason you would reach for a VAE or GAN rather than an MSE-trained
network.

The model used here is a **normalising flow** (`training/conversion_flow.py`, built on `nflows`).
Like a VAE or GAN it turns a simple base distribution (a standard normal) into a complex data
distribution, but instead of an encoder/decoder or a generator/discriminator it uses a single
*invertible* network. Invertibility lets it evaluate the exact likelihood of any sample by change of
variables, so it is trained directly by maximum likelihood — no reconstruction bound, no adversarial
game.

The final state is factorised by the chain rule, one flow "head" per quantity reading a shared
trunk over `eGamma`, each head also seeing the quantity sampled before it:

```
eGamma → trunk ─┬─► isTriplet   (Bernoulli)
                ├─► eRecoil     p(· | eGamma, isTriplet)
                ├─► eLead       p(· | eGamma, eRecoil)
                └─► thetaLead   p(· | eGamma, eLead)
```

The model works in physics-scaled coordinates (energy *fractions* and `θ·E/mₑc²`), which makes exact
energy conservation and the energy ordering of the pair hold **by construction** rather than being
learned. Training is standard maximum likelihood with early stopping on a validation split.

**[`training/README.md`](training/README.md)** is the detailed reference for the coordinates, the
architecture and the loss.

```bash
conda activate g4fastsim
jupyter lab training/train_flow.ipynb
```

The notebook ends in a **closure test** — the right way to score a sampler, since there is no
correct answer per event, only a correct distribution. It draws one sample per validation row and
checks that the marginals, the key correlations and the triplet fraction match Geant4.

## Fast simulation with the flow (`simMode = fast`)

The same executable can replace Geant4's final-state sampler with the trained flow, wired in as a
`G4VFastSimulationModel`. It is faithful where it matters and fast where it can be:

- **When and where the conversion happens is still decided by the real physics** — the true
  pair-production cross section — so the conversion rate and vertex distribution are unchanged.
- **Only the final state comes from the flow**, evaluated through **ONNX Runtime**.

Both modes fill the identical ntuple, so fast output is validated directly against full (closure).
On this machine the flow runs **≈ 2× faster per event** than the full 5D model. `training/
export_flow_onnx.py` exports the trained flow to ONNX for the C++ side; `benchmark/` times the two
modes and summarises the throughput.
