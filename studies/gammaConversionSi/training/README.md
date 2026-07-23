# `ConversionFlow` — data structure and architecture

Reference for `conversion_flow.py` and the dataset it consumes from `conversion_data.py`. The
companion notebook is `train_flow.ipynb`.

## What it is

A **conditional normalising flow** over the γ → e⁺e⁻ final state: given the incident photon
energy it *samples* the kinematics rather than predicting them.

```
input    eGamma
sampled  isTriplet, eRecoil, eLead, thetaLead
derived  eSub = eGamma − 2mₑc² − eLead − eRecoil
```

Sampling is necessary because the pair kinematics are **not a function** of `eGamma`.
`G4BetheHeitler5DModel` draws them from a distribution over the full five-dimensional final state,
so a network trained with MSE could only learn the conditional *mean* and its loss would plateau at
the conditional variance. A flow learns the conditional density itself, and generating an event
means drawing from it.

## The data

### Source

The `conversions` tree of a run of this study — by default
`build/gammaConversionSi/ntuples/Si_2MeV-100GeV_10000000.root`, which holds **9,977,028** rows
(one per converting photon, from 10 M fired). Units throughout are **MeV**, **mm** and **radians**,
applied at fill time in the C++ side.

`conversion_data.NTUPLE_BRANCHES` lists the seven branches the models read:

| branch | meaning |
| --- | --- |
| `eGamma` | energy of the incident photon |
| `eElectron`, `ePositron` | kinetic energy of the pair |
| `thetaElectron`, `thetaPositron` | polar angle w.r.t. the **initial** photon direction |
| `eRecoil` | kinetic energy of the recoiling nucleus, or of the recoil electron for a triplet |
| `isTriplet` | 1 for conversion on an atomic electron, 0 for nuclear |

The other columns in the ntuple (`phiElectron`, `phiPositron`, `openingAngle`, `zConv`,
`pathInBlock`) are not read.

### `build_dataset(arrays)`

Turns those arrays into tensors-in-waiting. All outputs are `float32`.

| returned | shape | contents |
| --- | --- | --- |
| `x` | `(N, 1)` | `eGamma` |
| `y` | `(N, 3)` | `eLead`, `thetaLead`, `eRecoil` |
| `extra["eSub"]` | `(N,)` | the lower lepton energy — derived by the model, kept for validation |
| `extra["isTriplet"]` | `(N,)` | conversion mode, as a float so it can be concatenated into a context |

**The pair is sorted by energy, not by charge.** `eLead` and `eSub` are the higher and lower of
`eElectron`/`ePositron`, and `thetaLead` is the angle of whichever lepton carried `eLead`. Sorting
means the model never has to learn the arbitrary e⁻/e⁺ labelling, and — because the sort is by
magnitude — it also fixes `eLead ≥ eSub`, a constraint the coordinates below exploit.

### Why `pathInBlock` is not an input

Given that a conversion happened, the final state depends on the photon energy and the material,
not on *where* in the block it occurred: the vertex is sampled independently, from the attenuation
length. In fast simulation Geant4 supplies that vertex itself through the interaction length, so a
model of the conversion has to supply kinematics and nothing else. Feeding the path in would add a
variable the target does not depend on and ask the network to learn to ignore it.

### What `isTriplet` is

Pair production needs a charged body to absorb momentum, and the 5D model always emits **three**
secondaries — e⁻, e⁺ and a recoil. There are two cases:

| | recoil is | fraction in Si | recoil energy |
| --- | --- | --- | --- |
| nuclear | the silicon nucleus | ~93.3 % | tens of eV |
| **triplet** | an atomic electron | ~6.7 % = 1/(Z+1) | up to ⅔ of `eGamma` |

"Triplet" because the final state is three light particles. It is set in
`../src/ConvSteppingAction.cc` from the PDG code of the third secondary. It matters twice over: it
is what makes `eRecoil` bimodal across ~10 orders of magnitude, and it is part of the final state a
fast-simulation model must produce — 6.7 % of the time there is a third electron carrying real
energy rather than a nucleus absorbing none.

## Learned coordinates

The flow does not work in MeV and radians. With `S = eGamma − 2mₑc²`, the kinetic energy the three
secondaries share:

| coordinate | forward (physical → learned) | inverse (sampled → physical) |
| --- | --- | --- |
| `z_recoil` | `logit(eRecoil / S)` | `eRecoil = sigmoid(z_recoil) · S` |
| `z_lead` | `logit(2·eLead/(S − eRecoil) − 1)` | `eLead = ½(sigmoid(z_lead) + 1) · (S − eRecoil)` |
| `z_theta` | `log(thetaLead · eLead / mₑc²)` | `thetaLead = exp(z_theta) · mₑc² / eLead` |
| — | — | `eSub = S − eRecoil − eLead` |

Implemented as `to_learned()` and `from_learned()`. Two properties follow **structurally, not from
training**:

- `sigmoid(z_lead) ≤ 1` ⟹ `eLead ≤ S − eRecoil` ⟹ **`eSub ≥ 0`, with energy conservation exact**
  for every sample. Energy conservation is enforced by construction rather than left to training.
- `sigmoid(z_lead) ≥ 0` ⟹ `eLead ≥ ½(S − eRecoil) ≥ eSub`, so a sample can never violate the
  sorted-pair convention the dataset is built on.

`θ·E/mₑc²` is the natural angular variable because the Bethe–Heitler angular scale *is* `mₑc²/E`.
Working in it strips most of the energy dependence out of the target, so the flow learns one nearly
universal shape instead of a different one at every energy.

### Why the logits are differences of logs

The table states the maps as logits of fractions, but `to_learned()` never forms the fraction. It
computes

```
z_recoil = log(eRecoil) − log(S − eRecoil)
z_lead   = log(eLead − eSub) − log(2·eSub)
```

which is the same quantity, evaluated from energies instead. The reason is the nuclear mode. A
recoil of tens of eV against an available energy of up to 100 GeV gives `f_recoil` between 1e-8 and
1e-14, so a `logit(f)` guarded by an epsilon big enough to be safe near `f = 1` — anything around
1e-7, which is already float32's resolution there — clamps **every nuclear row onto one value**.
The mode then has no width for the per-mode standardisation to measure, `recoil_sigma[0]` falls to
its 1e-12 floor, and 93 % of the sample carries no recoil information at all. Nothing crashes; the
model simply trains on a delta function. Working from the energies keeps those rows distinct and
needs no floor beyond `_TINY = 1e-30`, the single guard applied before every log.

## Normalisation buffers

Non-trained constants, registered as **buffers** — so they move with `.to(device)`, are written to
the `state_dict`, and are never touched by the optimiser. A saved checkpoint is therefore
self-contained: there is no scaler to reload alongside the weights.

| buffer | shape | fitted from |
| --- | --- | --- |
| `input_min`, `input_max` | `(1,)` | `log10(eGamma)` over the training split |
| `recoil_mu`, `recoil_sigma` | `(2,)` | `z_recoil`, **separately per mode**: index 0 nuclear, 1 triplet |
| `lead_mu`, `lead_sigma` | scalar | `z_lead` |
| `theta_mu`, `theta_sigma` | scalar | `z_theta` |
| `fitted` | scalar `bool` | set by `fit_normalisation`; using the model before it raises |

`fit_normalisation(x, y, is_triplet)` is called **once, on the training split only** — fitting on
everything would leak the validation set's range into the model.

Two scaling choices, deliberately different from each other:

- **The `eGamma` input** uses `log10`, then min-max onto `[0, 1]`.
- **The three learned coordinates are standardised** (zero mean, unit variance). The base
  distribution is a standard normal and `tail_bound` is expressed in its units, so anything beyond
  it lands in the spline's linear tails where there is no resolution left; standardising is what
  puts the data inside the bins.

`z_recoil` gets its constants **per mode** because the nuclear and triplet peaks sit roughly 20
logit units apart. One shared σ would spend the spline's 16 bins covering the empty gap between
them; conditioned on the mode, each is a narrow unimodal shape. A split holding one row or fewer
falls back to the pooled constants rather than to a `NaN`.

## Architecture

```
eGamma ─► log10 ─► [0,1] ─► trunk ─┬─► triplet_head   Bernoulli(isTriplet | E)
                                   ├─► recoil_flow    p(z_recoil | E, isTriplet)
                                   ├─► lead_flow      p(z_lead   | E, z_recoil)
                                   └─► theta_flow     p(z_theta  | E, z_lead)
```

Every head owns its own network and reads the shared trunk. Each later head *also* sees the
quantity sampled before it, so the product of the four is the **exact joint density by the chain
rule**, not an independence approximation:

```
p(isTriplet, z_recoil, z_lead, z_theta | E)
    = p(isTriplet | E) · p(z_recoil | E, isTriplet) · p(z_lead | E, z_recoil) · p(z_theta | E, z_lead)
```

That chaining is not decoration. At fixed `eGamma` the angular scale is `mₑc²/E` *of that lepton*,
so `p(θ)` genuinely depends on `eLead`; heads that could not see it would reproduce every
one-dimensional marginal while generating wrong (`eLead`, `thetaLead`) pairs. `isTriplet` comes
first because it is what makes `eRecoil` bimodal.

### Layer sizes

Constructor defaults, `ConversionFlow(trunk_hidden=(128, 128), trunk_features=64, head_hidden=64,
head_blocks=2, num_bins=16, tail_bound=6.0, activation=nn.ReLU)`:

| component | structure |
| --- | --- |
| trunk | `Linear(1,128) → ReLU → Linear(128,128) → ReLU → Linear(128,64) → ReLU` |
| `triplet_head` | `Linear(64,64) → ReLU → Linear(64,1)` — a logit, not a probability |
| `recoil_flow`, `lead_flow`, `theta_flow` | one `MaskedPiecewiseRationalQuadraticAutoregressiveTransform` each, over a `StandardNormal([1])` base |

**No batch norm in the trunk.** The trunk feeds heads that parameterise a
density, and batch norm would make the log-likelihood of one row depend on which other rows
happened to share its batch. (It is also why the notebook's `DataLoader` needs no `drop_last`.)

### Inside a spline head

| setting | value | note |
| --- | --- | --- |
| `features` | 1 | one coordinate per head |
| `context_features` | 65 | `trunk_features` (64) + the one conditioning quantity |
| `hidden_features` | 64 | width of the MADE blocks |
| `num_blocks` | 2 | residual blocks (`use_residual_blocks=True`, `use_batch_norm=False`) |
| `num_bins` | 16 | rational-quadratic spline bins |
| `tails` | `"linear"` | linear extrapolation beyond the bound |
| `tail_bound` | 6.0 | in standardised units, so ±6σ |

With `features=1` the autoregressive masks inside MADE **degenerate**: the single input has no
predecessors, so its mask is all-zero and the spline parameters come purely from the context path
(`MADE.forward` adds `context_layer(context)` to the initial layer's output regardless). The
transform is therefore exactly a conditional head — assembled from library code that is already
tested, rather than a hand-rolled `Transform`.

The MADE output width is `3·num_bins − 1 = 47` for linear tails: 16 bin widths, 16 bin heights and
15 interior knot derivatives. The notebook's split cell prints the total parameter count.

## Loss

`loss(x, y, is_triplet)` returns `(total, per_head)`. Each head's negative log-likelihood is
**averaged over the batch on its own**, and the four means are then summed:

```
total = Σ  LOSS_WEIGHTS[name] · mean(−log p_name)
```

`LOSS_TERMS = ("isTriplet", "eRecoil", "eLead", "thetaLead")` is the reporting order — the order
they are sampled in. `LOSS_WEIGHTS` is `1.0` for all four by default.

Worth being straight about what the split does and does not do: with unit weights this is
**numerically identical** to the joint NLL, because a mean of sums equals a sum of means. What it
buys is *visibility* — the four numbers are printed every epoch, so a head that has stopped
learning shows up as its own flat curve instead of hiding inside the total — and a single place to
intervene if one head's NLL is orders of magnitude larger than the others and drags the shared
trunk around. Lower its weight there rather than rescaling the data.

The `isTriplet` term is a Bernoulli log-likelihood via
`binary_cross_entropy_with_logits(..., reduction="none")`; the other three come from
`Flow.log_prob` on the standardised coordinate, conditioned on the trunk plus one previous
quantity.

`forward(x, y, is_triplet)` returns the plain chain-rule sum, shape `(N,)`, for evaluating a
density. Training goes through `loss()`.

## Regularisation

**L2 with a constant of `1e-4`**, from `WEIGHT_DECAY` in the module, applied by
`make_optimiser(lr, weight_decay=WEIGHT_DECAY)`.

The optimiser is Adam with **two parameter groups**:

| group | tensors | `weight_decay` |
| --- | --- | --- |
| decayed | every `nn.Linear` weight matrix | `1e-4` |
| exempt | every bias, and any batch-norm affine parameter | `0.0` |

Biases are exempt because shrinking one only displaces a layer's output rather than simplifying the
function it computes; batch-norm scales are exempt because pulling them towards zero fights the
normalisation the layer exists to provide. The split is keyed on `isinstance(module, nn.Linear)`,
and MADE's `MaskedLinear` subclasses `nn.Linear` — so the three spline heads are covered without
being named. Their masked-out entries are decayed along with the rest, which is harmless: they are
multiplied by a zero mask before reaching the output.

This is Adam's **coupled** `weight_decay`, which adds `wd · w` to the gradient and so is L2
regularisation in the literal sense. If the penalty ever needs to act independently of Adam's
per-parameter step sizes, swapping `torch.optim.Adam` for `torch.optim.AdamW` on that one line
gives decoupled decay instead.

## Training configuration

Current globals in `train_flow.ipynb`:

| global | value | effect |
| --- | --- | --- |
| `MAX_EVENTS` | `None` | all 9,977,028 converted rows |
| `VAL_FRACTION` | `0.4` | 3,990,811 validation / 5,986,217 training rows |
| `BATCH_SIZE` | `1024` | 5,846 optimiser steps per epoch |
| `EPOCHS` | `100` | ≈584,600 steps if it runs to the end |
| `PATIENCE` | `10` | early stopping — see below |
| `LEARNING_RATE` | `1e-5` | passed to `make_optimiser`, which supplies the `1e-4` decay |
| `SEED` | `0` | seeds `torch` and the NumPy generator that makes the split |

The split is a single shuffle: `rng.permutation(N)`, first 40 % validation, remainder training.
`fit_normalisation` sees the training part only.

Validation runs in chunks of 131,072 rows (`evaluate(..., chunk=)`) — the validation split here is
~4 M rows, and one batch of that would allocate gigabytes of hidden activations at once.

### Early stopping and what a run leaves behind

The loop tracks the **summed validation NLL**. Every epoch that improves on the best so far writes
`models/conversion_flow.pt` immediately, so an interrupted run still leaves the best model behind
rather than whatever the last epoch drifted to. After `PATIENCE = 10` epochs with no improvement
the loop breaks, and the best weights are loaded back into `model` — so the closure test and
everything else downstream run on the best epoch, not the last one.

`EPOCHS` is therefore a ceiling, not a target: the run ends at whichever comes first.

Both splits' per-head NLLs are recorded every epoch, and the **validation** ones are shown in
brackets on the epoch line, so a stalling head is visible while the run is going rather than only
afterwards. The train-side per-head values are recorded but not printed. At the end, `history` is
pickled to `models/flow_history.pkl` with these keys:

| key | contents |
| --- | --- |
| `train`, `val` | summed NLL per epoch, one list each |
| `train_terms`, `val_terms` | dict of `LOSS_TERMS → list`, per epoch, for both splits |
| `seconds` | wall-clock per epoch, training plus validation |
| `best_epoch`, `best_val` | which epoch the saved weights come from, and its score |
| `epochs_run` | how many epochs actually ran, ≤ `EPOCHS` |

Six figures go to `plots/`: `flow_loss_total.pdf` and one `flow_loss_<head>.pdf` per head, each
with train in black and validation in red, plus `flow_loss_overview.pdf` carrying both totals and
all four validation curves on one pair of axes — which is what shows a head going flat while the
total still falls because another head is carrying it. All mark the best epoch with a dotted rule.

## Sampling

`sample(x)` draws one final state per row of `x`, in the heads' conditioning order:

1. `p = sigmoid(triplet_head(trunk))`, then `isTriplet ~ Bernoulli(p)`
2. `u_recoil` from `recoil_flow`, conditioned on `[trunk, isTriplet]`
3. `u_lead` from `lead_flow`, conditioned on `[trunk, u_recoil]`
4. `u_theta` from `theta_flow`, conditioned on `[trunk, u_lead]`
5. de-standardise, then `from_learned()` back to MeV and radians

`Flow.sample(1, context=(N, C))` returns `(N, 1, 1)` — one draw per row — hence the `[:, 0, 0]`
indexing. Call the whole thing inside `torch.no_grad()`.

Returns a dict of `(N,)` tensors: `eLead`, `thetaLead`, `eRecoil`, `eSub`, `isTriplet`. Energy
conservation and `eSub ≥ 0` hold by construction, so a violation means the inverse map is wrong,
not that the model is undertrained.

## Caveats

- **The NLL is in the scaled coordinates.** It is not a likelihood in MeV and radians. Only
  compare it with itself.
- **Checkpoints are tied to the buffer shapes.** Changing the coordinate set or the per-mode recoil
  constants invalidates every saved `*.pt`.
- **`nflows` bases are float64, and MPS has no float64.** `StandardNormal` registers its
  log-normaliser as a float64 buffer, so `ConversionFlow().to("mps")` would die with *"Cannot
  convert a MPS Tensor to float64 dtype"*. The constructor ends with `self.float()` to prevent it;
  that also stops the one double-precision constant promoting every log-density on CPU.
- **Do not use `nflows`' LU, QR or `NaiveLinear` transforms.** They call `torch.solve` /
  `torch.triangular_solve`, removed in torch ≥ 2.0. The calls sit inside method bodies, so
  importing the package is safe — only *using* those transforms breaks. Nothing here needs them:
  with one-dimensional per-head flows there is no permutation to apply.
- If `nflows` ever stops importing, the fallback is
  `nflows.transforms.splines.rational_quadratic.unconstrained_rational_quadratic_spline`, a plain
  function that can be driven from an ordinary MLP head.
