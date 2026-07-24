# `ConversionFlow` — the fast-simulation model

Reference for `conversion_flow.py` and the dataset it consumes from `conversion_data.py`. The
companion notebook is `train_flow.ipynb`.

## What a normalising flow is

A **normalising flow** is a generative model, in the same family as VAEs and GANs but with a
different trick. It learns an *invertible* map between a simple base distribution (here a standard
normal) and the data distribution. Because the map is invertible and its Jacobian is tractable, the
change-of-variables formula gives the **exact likelihood** of any data point — so the flow is trained
by plain maximum likelihood, with none of the machinery VAEs and GANs need (no variational bound, no
discriminator). Sampling is just: draw from the normal, push it forward through the map.

Here the map is a **conditional** one: it depends on the photon energy `eGamma`, so the flow models
`p(final state | eGamma)`.

## Why sampling, not regression

Given `eGamma`, the pair kinematics are **not a function** of it — `G4BetheHeitler5DModel` draws
them from a distribution over the full five-dimensional final state. A network trained with MSE could
only learn the conditional *mean*; its loss would plateau at the conditional variance. A flow learns
the whole conditional density, and generating an event means drawing from it.

## The data

The `conversions` tree of a run of this study (~10 M rows, one per converting photon). `build_dataset`
turns the raw branches into:

| returned | contents |
| --- | --- |
| `x` | `eGamma` — the conditioning input |
| `y` | `eLead`, `thetaLead`, `eRecoil` — the sampled targets |
| `isTriplet` | conversion mode (kept as context) |

Two modelling choices matter:

- **The pair is sorted by energy, not charge.** `eLead`/`eSub` are the higher/lower of the two, so
  the model never has to learn the arbitrary e⁻/e⁺ labelling, and the ordering `eLead ≥ eSub` is
  fixed for free.
- **`pathInBlock` is not an input.** Given that a conversion happened, the final state depends on the
  energy and material, not on *where* in the block it occurred — Geant4 supplies the vertex itself.
  Feeding the path in would only ask the network to learn to ignore it.

`isTriplet` (≈ 1/(Z+1) of events) is part of the final state the model must produce: those events
carry a real third electron instead of a near-massless nuclear recoil, which makes `eRecoil` span
many orders of magnitude.

## Learned coordinates

The flow does not work in MeV and radians. Each target is mapped to a coordinate the flow can model
comfortably, and the map is inverted after sampling:

| coordinate | idea |
| --- | --- |
| `z_recoil` | logit of the recoil's *fraction* of the shared energy |
| `z_lead`   | logit of the leading lepton's *fraction* of what remains |
| `z_theta`  | log of `thetaLead · eLead / mₑc²` |

Two things follow **by construction, not from training**: energy is conserved exactly (the fractions
sum to one), and `eLead ≥ eSub` always holds. Using energy *fractions* rather than absolute energies
is what buys these guarantees. And `θ·E/mₑc²` is the natural angular variable because the
Bethe–Heitler angular scale *is* `mₑc²/E`, so working in it strips most of the energy dependence out
of the target — the flow then learns one nearly universal shape instead of a different one per energy.

Each coordinate is finally standardised (zero mean, unit variance) so it sits inside the flow's
usable range. These constants are fitted **once, on the training split only**, and stored with the
weights so a checkpoint is self-contained.

## Architecture

```
eGamma → log10 → [0,1] → trunk ─┬─► triplet_head   Bernoulli(isTriplet | E)
                                ├─► recoil_flow    p(z_recoil | E, isTriplet)
                                ├─► lead_flow      p(z_lead   | E, z_recoil)
                                └─► theta_flow     p(z_theta  | E, z_lead)
```

A shared trunk (a small MLP over `eGamma`) feeds four heads. Each head models one quantity, and each
later head *also* sees the quantity sampled before it, so the product of the four is the **exact joint
density by the chain rule** — not an independence approximation:

```
p(isTriplet, z_recoil, z_lead, z_theta | E)
    = p(isTriplet | E) · p(z_recoil | E, isTriplet) · p(z_lead | E, z_recoil) · p(z_theta | E, z_lead)
```

The chaining is essential: at fixed energy the angular scale depends on `eLead`, so heads that could
not see it would reproduce every 1-D marginal while generating wrong (`eLead`, `thetaLead`) pairs.
`isTriplet` comes first because it is what makes `eRecoil` bimodal.

The three continuous heads are each a **rational-quadratic spline flow** (a
`MaskedPiecewise…AutoregressiveTransform` from `nflows`) over a standard-normal base; `triplet_head`
is a plain Bernoulli logit. There is deliberately **no batch norm** in the trunk — it would make one
row's likelihood depend on the others in its batch.

## Training

Standard maximum likelihood. Each head's negative log-likelihood is averaged separately and the four
are summed; with equal weights this is exactly the joint NLL, but keeping them separate makes a head
that has stopped learning visible as its own flat loss curve. Adam with light L2 weight decay
(biases and any norm parameters exempt).

The notebook trains with **early stopping** on the summed validation NLL: it rewrites the checkpoint
on every improving epoch and reloads the best weights before scoring, so an interrupted run still
leaves its best model behind. Per-head loss curves and the history are written to `plots/` and
`models/` (git-ignored).

The run ends in a **closure test** — the right way to score a sampler, since there is no correct
answer per event, only a correct distribution. It draws one sample per validation row and compares:
the four marginals, the same marginals inside narrow `eGamma` slices, the `eLead`–`thetaLead`
correlation the chained heads exist to reproduce, the sampled triplet fraction, and that `eSub ≥ 0`
on every sample.

## Sampling

`sample(x)` draws one final state per row, in the heads' conditioning order (triplet → recoil → lead
→ theta), then de-standardises and inverts the coordinate maps back to MeV and radians. Energy
conservation and `eSub ≥ 0` hold by construction, so a violation would mean the inverse map is
wrong, not that the model is undertrained.

## Notes

- **The NLL is in the scaled coordinates**, not a likelihood in MeV and radians — only compare it
  with itself and with later runs of this same model.
- **Checkpoints are tied to the coordinate/standardisation choices**; changing them invalidates saved
  `*.pt` files.
- **On Apple Silicon (MPS)** the constructor forces `float()`, because `nflows` registers a float64
  base-distribution constant that MPS cannot hold.
