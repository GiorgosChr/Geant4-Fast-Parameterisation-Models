# Why `Z` needs no change to the flow's coordinates

Reference for one decision in `conversion_flow.py`: adding `Z` as a second input changes
`INPUT_COLUMNS` and nothing else. The coordinate maps, the per-mode standardisation, the head
layout and the layer sizes all carry over unmodified from the single-material
`gammaConversionSi` flow.

This file records the measurement that justifies that, so the claim in the module docstring can be
checked rather than taken on trust.

## The question

The flow does not fit densities in MeV and radians. `ConversionFlow.to_learned` maps the final
state onto three coordinates, with `S = eGamma − 2mₑ` the kinetic energy the three secondaries
share:

```
f_recoil = eRecoil / S                 z_recoil = logit(f_recoil)
f_lead   = eLead / (S − eRecoil)       z_lead   = logit(2·f_lead − 1)
t        = thetaLead · eLead / mₑ      z_theta  = log(t)
```

Each of the three is then **standardised** by constants fitted once on the training split
(`fit_normalisation`), because each head's base distribution is a standard normal and its spline
has `tail_bound = 6.0` in those units. Beyond ±6 the transform is linear — no bins, no resolution.
`z_recoil` gets its constants *per conversion mode*, indexed by `isTriplet`; `z_lead` and `z_theta`
get one pair each.

In the single-material study that was unambiguous: one element, one set of constants. Conditioning
on `Z` raises the question of whether one pooled set still works across the periodic table, or
whether the constants have to become functions of `Z` — which would mean either a per-element
lookup in the state dict, or a learned scale-and-shift network in front of each head.

Concretely, three things could go wrong:

1. the coordinate **shifts** with `Z` far enough that some elements sit outside `tail_bound` when
   standardised with pooled constants;
2. the coordinate's **width** changes with `Z` enough that one pooled σ over- or under-fills the
   spline's bins at the ends of the range;
3. the shift is small but the flow cannot follow it, so the density is right on average and wrong
   for every individual element.

## The measurement

29 pure-element ntuples, one per element in `config/elements.dat`, 10⁶ photons each over
1 GeV–100 GeV, ~1.0 M conversions per file. For each file the three coordinates were recomputed
exactly as `to_learned` does — in float64 rather than float32, which changes nothing at these
magnitudes — and their mean and standard deviation taken, with `z_recoil` split by `isTriplet`.

Pooled constants for the standardised columns come from all 29 files with equal weight, which is
what a training set built from `element_files()` gives.

> The earlier pass over this question saw only the 14 files that existed at the time (Z = 1…23) and
> reported the nuclear `z_recoil` mean moving by ~2.0. Over the full range now available it moves by
> 3.1, which is what the module docstring quotes. The conclusion is unchanged — 3.1 against
> `tail_bound = 6` in units of σ ≈ 1.7 is still a shift of well under one σ per order of magnitude
> in Z.

### Raw coordinates, element by element

`r0` is nuclear `z_recoil`, `r1` triplet `z_recoil`. `f_trip` is the measured triplet fraction,
against `1/(Z+1)` for reference.

| el | Z | f_trip | 1/(Z+1) | r0 μ | r0 σ | r1 μ | r1 σ | lead μ | lead σ | theta μ | theta σ |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| H  |  1 | 0.4993 | 0.5000 | −17.590 | 1.889 | −14.907 | 2.966 | 0.171 | 1.797 | 0.196 | 0.929 |
| Be |  4 | 0.1999 | 0.2000 | −18.586 | 1.558 | −14.679 | 2.920 | 0.206 | 1.817 | 0.536 | 1.040 |
| B  |  5 | 0.1665 | 0.1667 | −18.680 | 1.543 | −14.642 | 2.910 | 0.215 | 1.816 | 0.580 | 1.052 |
| C  |  6 | 0.1426 | 0.1429 | −18.736 | 1.533 | −14.599 | 2.899 | 0.217 | 1.818 | 0.610 | 1.058 |
| N  |  7 | 0.1250 | 0.1250 | −18.818 | 1.525 | −14.558 | 2.891 | 0.218 | 1.819 | 0.641 | 1.068 |
| O  |  8 | 0.1113 | 0.1111 | −18.892 | 1.517 | −14.522 | 2.882 | 0.220 | 1.820 | 0.668 | 1.076 |
| F  |  9 | 0.1001 | 0.1000 | −18.988 | 1.514 | −14.496 | 2.886 | 0.219 | 1.821 | 0.697 | 1.088 |
| Na | 11 | 0.0834 | 0.0833 | −19.100 | 1.509 | −14.440 | 2.872 | 0.223 | 1.823 | 0.733 | 1.100 |
| Mg | 12 | 0.0771 | 0.0769 | −19.133 | 1.509 | −14.407 | 2.866 | 0.224 | 1.821 | 0.745 | 1.104 |
| Al | 13 | 0.0714 | 0.0714 | −19.197 | 1.508 | −14.397 | 2.870 | 0.224 | 1.823 | 0.763 | 1.111 |
| Si | 14 | 0.0667 | 0.0667 | −19.222 | 1.508 | −14.370 | 2.855 | 0.226 | 1.821 | 0.770 | 1.113 |
| Ar | 18 | 0.0529 | 0.0526 | −19.450 | 1.511 | −14.292 | 2.844 | 0.228 | 1.821 | 0.821 | 1.136 |
| Ti | 22 | 0.0437 | 0.0435 | −19.573 | 1.516 | −14.236 | 2.833 | 0.230 | 1.821 | 0.849 | 1.148 |
| V  | 23 | 0.0418 | 0.0417 | −19.617 | 1.517 | −14.222 | 2.829 | 0.230 | 1.821 | 0.857 | 1.151 |
| Cr | 24 | 0.0402 | 0.0400 | −19.631 | 1.517 | −14.210 | 2.829 | 0.231 | 1.822 | 0.861 | 1.152 |
| Mn | 25 | 0.0385 | 0.0385 | −19.668 | 1.520 | −14.197 | 2.841 | 0.230 | 1.825 | 0.869 | 1.156 |
| Fe | 26 | 0.0373 | 0.0370 | −19.682 | 1.520 | −14.183 | 2.827 | 0.231 | 1.822 | 0.870 | 1.157 |
| Ni | 28 | 0.0347 | 0.0345 | −19.717 | 1.523 | −14.155 | 2.829 | 0.231 | 1.822 | 0.877 | 1.160 |
| Cu | 29 | 0.0336 | 0.0333 | −19.774 | 1.526 | −14.143 | 2.829 | 0.232 | 1.822 | 0.886 | 1.165 |
| Zn | 30 | 0.0325 | 0.0323 | −19.795 | 1.527 | −14.131 | 2.830 | 0.232 | 1.823 | 0.889 | 1.167 |
| Ru | 44 | 0.0223 | 0.0222 | −20.124 | 1.549 | −14.025 | 2.821 | 0.234 | 1.823 | 0.936 | 1.191 |
| Pd | 46 | 0.0214 | 0.0213 | −20.165 | 1.551 | −14.001 | 2.820 | 0.234 | 1.823 | 0.941 | 1.194 |
| Ag | 47 | 0.0209 | 0.0208 | −20.175 | 1.551 | −13.992 | 2.822 | 0.234 | 1.823 | 0.943 | 1.195 |
| Sn | 50 | 0.0198 | 0.0196 | −20.252 | 1.556 | −13.970 | 2.819 | 0.235 | 1.823 | 0.951 | 1.199 |
| Ba | 56 | 0.0176 | 0.0175 | −20.370 | 1.563 | −13.930 | 2.826 | 0.235 | 1.823 | 0.963 | 1.209 |
| Ta | 73 | 0.0135 | 0.0135 | −20.600 | 1.577 | −13.831 | 2.825 | 0.235 | 1.823 | 0.983 | 1.219 |
| W  | 74 | 0.0134 | 0.0133 | −20.613 | 1.578 | −13.832 | 2.825 | 0.235 | 1.824 | 0.984 | 1.220 |
| Au | 79 | 0.0125 | 0.0125 | −20.668 | 1.583 | −13.808 | 2.831 | 0.235 | 1.827 | 0.989 | 1.223 |
| Pb | 82 | 0.0121 | 0.0120 | −20.716 | 1.584 | −13.803 | 2.816 | 0.236 | 1.824 | 0.991 | 1.224 |

Pooled over all 29: `recoil0` (−19.594, 1.714), `recoil1` (−14.481, 2.899), `lead` (0.223, 1.821),
`theta` (0.808, 1.153).

Two things worth noting in passing, neither of them about standardisation:

- **`f_trip` reproduces `1/(Z+1)` to within the statistics at every element**, which is a clean
  check that the study's `isTriplet` flag and the per-conversion `Z` in the ntuple are what they
  claim to be. It is also the sharpest `Z` dependence in the whole problem, and it lives entirely in
  the triplet head — a Bernoulli logit, not a spline.
- **The two recoil modes sit 5.1 logit units apart** (−19.6 against −14.5) with widths 1.7 and 2.9.
  That is the gap the per-mode split exists to avoid spanning, and it is roughly constant in `Z`;
  nothing about multi-material training weakens the case for splitting.

### Standardised with the pooled constants

Applying the pooled (μ, σ) above, per mode for the recoil, gives each element's mean position on
the axis the spline actually sees. The last column is the fraction of that element's rows landing
outside `tail_bound = 6` in *any* of the three coordinates.

| el | Z | u(r0) | u(r1) | u(lead) | u(theta) | outside ±6 |
|---|---:|---:|---:|---:|---:|---:|
| H  |  1 | +1.175 | −0.148 | −0.028 | −0.526 | 5.5e−4 |
| Be |  4 | +0.587 | −0.068 | −0.005 | −0.236 | 1.9e−4 |
| C  |  6 | +0.500 | −0.035 | −0.006 | −0.173 | 2.1e−4 |
| O  |  8 | +0.409 | −0.020 | −0.006 | −0.121 | 1.9e−4 |
| Si | 14 | +0.217 | +0.024 | +0.001 | −0.032 | 1.5e−4 |
| Ti | 22 | +0.013 | +0.073 | −0.000 | +0.034 | 1.0e−4 |
| Fe | 26 | −0.051 | +0.086 | +0.003 | +0.055 | 8.0e−5 |
| Cu | 29 | −0.103 | +0.117 | +0.000 | +0.070 | 6.5e−5 |
| Ag | 47 | −0.340 | +0.169 | +0.003 | +0.117 | 6.5e−5 |
| Ba | 56 | −0.454 | +0.200 | +0.004 | +0.136 | 7.5e−5 |
| W  | 74 | −0.595 | +0.206 | +0.005 | +0.155 | 4.5e−5 |
| Pb | 82 | −0.653 | +0.225 | +0.005 | +0.160 | 6.0e−5 |

(Abridged; the full 29 rows follow the same monotone trend.)

Across all 29 elements:

| quantity | range over Z = 1…82 | span | in units of the pooled σ |
|---|---|---:|---:|
| nuclear `z_recoil` μ | −20.72 … −17.59 | 3.13 | **1.83** |
| nuclear `z_recoil` σ | 1.508 … 1.889 | 0.38 | ±11 % of 1.714 |
| triplet `z_recoil` μ | −14.91 … −13.80 | 1.10 | **0.38** |
| triplet `z_recoil` σ | 2.816 … 2.966 | 0.15 | ±3 % of 2.899 |
| `z_lead` μ | 0.171 … 0.236 | 0.066 | **0.035** |
| `z_lead` σ | 1.797 … 1.827 | 0.030 | ±1 % of 1.821 |
| `z_theta` μ | 0.196 … 0.991 | 0.79 | **0.69** |
| `z_theta` σ | 0.929 … 1.224 | 0.29 | ±13 % of 1.153 |

## The conclusion, against the three failure modes

**1. Nothing falls outside the tails.** The largest whole-element displacement is hydrogen's
nuclear recoil at +1.18 σ, against a bound of 6. Every element's bulk sits inside the middle third
of the spline's support, so no element is being fitted in the linear tail region.

Tail *occupancy* is not zero, but it is a per-row extreme rather than a Z effect: the worst element
puts 5.5e−4 of its rows beyond ±6 (hydrogen, essentially all of it in the recoil coordinate), the
average is 1.2e−4, and the elements holding the single most extreme value in each coordinate are
scattered across the range — O for the recoil, Pb for `z_lead`, Zn for `z_theta` — which is what a
sampling tail looks like, not a systematic shift. Those rows land in the linear tail and are still
assigned a finite density; they are simply not resolved. A per-`Z` standardisation would not
recover them, because they are outliers relative to their *own* element's mean too.

**2. The widths barely move.** `z_lead` is Z-independent to 1 %, the two recoil modes to 11 % and
3 %, `z_theta` to 13 %. A pooled σ therefore fills the bins to within about a tenth of their
spacing everywhere. A per-element σ would buy a ~10 % change in effective bin density at the ends
of the range — nothing, next to the 16 bins each head has.

**3. The flow can absorb what shift remains.** This is the part that makes the small numbers
sufficient rather than merely reassuring. Every head is conditioned on a trunk that sees
`log10 Z`, and the spline's parameters — knot positions, widths, derivatives — are functions of
that context. A shift of 1.8 σ across the entire periodic table is exactly the kind of smooth
context dependence a conditional spline represents natively; it is not an offset the model has to
work around. What the standardisation has to do is put the data *in the window*, and it does.

The monotone, near-logarithmic trend of every column in `Z` is also why `log10` is the right input
map. `u(r0)` moves +1.18 → +0.50 between H and C, and only −0.60 → −0.65 between W and Pb: on a
linear `Z` axis the entire steep part would be compressed into the first 6 % of the trunk's input
range.

**Nothing needs to be indexed by element.** Two constants per conversion mode, pooled over `Z`, are
enough.

## What this means for the code

Only `INPUT_COLUMNS` changes:

```python
INPUT_COLUMNS = ("eGamma", "Z")     # was ("eGamma",)
```

and `conversion_flow.py` already handles that without further edits:

- `n_in = len(INPUT_COLUMNS)` sizes the first trunk layer;
- `input_min` / `input_max` are registered as `torch.zeros(n_in)` / `torch.ones(n_in)`, so they are
  vectors, and `fit_normalisation` fills them with `log_x.min(dim=0).values`, a per-column min;
- `_trunk_features` applies `(log10(x) − input_min) / (input_max − input_min)` column-wise, which is
  the same line for one input or two.

`recoil_mu` / `recoil_sigma` stay length-2 buffers indexed by `isTriplet`, and `lead_*` / `theta_*`
stay scalars. The head layout, `num_bins = 16`, `tail_bound = 6.0` and the trunk widths are all
unchanged.

## Caveats

- **The pooled constants depend on the training mix.** The numbers above weight the 29 elements
  equally, matching a training set assembled from `element_files()`. Training on a composite-heavy
  mix would move the pooled μ toward whichever `Z` dominates the rows. The margin is large enough
  that this does not threaten the conclusion — even the most extreme single-element constants leave
  every other element inside ±2 σ — but `fit_normalisation` must still be called on the training
  split of the mix actually being used, never on a different one.
- **Outside the fitted `Z` range is extrapolation.** `input_min` / `input_max` come from the
  elements present in the training split, so a `Z` beyond them normalises outside `[0, 1]` and the
  trunk is being asked for a value it never saw. Training covers Z = 1…82, which is every element
  in `config/materials.dat`, so this only matters if the composite list grows.
- **Composites were not used in this measurement.** In a composite file `Z` is a spectrum rather
  than a constant, so per-file statistics mix elements and say nothing about the per-`Z` behaviour.
  The right closure test for composites is whether the model reproduces a held-out composite it was
  never trained on, which is a training-notebook question, not a coordinate one.
- **This measures the coordinates, not the fit.** It establishes that pooled standardisation puts
  every element in the spline's resolved range. Whether the trained flow actually reproduces each
  element's density is what the closure plots answer — `closure_isTriplet_vs_Z` in particular, since
  the triplet fraction is the steepest `Z` dependence in the problem.

## Reproducing it

The coordinates are recomputed straight from the ntuple branches — no model instance, no trained
weights:

```python
import numpy as np, uproot
from conversion_data import ELECTRON_MASS, NTUPLE_BRANCHES, NTUPLE_TREE, element_files

for name, path in element_files("build/gammaConversion/ntuples",
                                "studies/gammaConversion/config/elements.dat"):
    a = uproot.open(path)[NTUPLE_TREE].arrays(NTUPLE_BRANCHES, library="np")
    e_lead = np.maximum(a["eElectron"], a["ePositron"])
    e_sub  = np.minimum(a["eElectron"], a["ePositron"])
    total  = a["eGamma"] - 2.0 * ELECTRON_MASS
    shared = total - a["eRecoil"]

    z_recoil = np.log(a["eRecoil"]) - np.log(shared)
    z_lead   = np.log(e_lead - e_sub) - np.log(2.0 * e_sub)
    z_theta  = np.log(a["theta"] * e_lead / ELECTRON_MASS)

    nuclear = a["isTriplet"] == 0
    print(name, z_recoil[nuclear].mean(), z_recoil[nuclear].std(),
          z_lead.mean(), z_lead.std(), z_theta.mean(), z_theta.std())
```

Note the logits are formed as differences of logs, as in `to_learned`, and for the same reason: a
nuclear `f_recoil` of 1e−8 to 1e−14 cannot survive an epsilon-clamped `logit`.

## Further reading

Background on normalising flows themselves, collected under `resources/`:

- Kobyzev, Prince & Brubaker, *Normalizing Flows: An Introduction and Review of Current Methods*
  (arXiv:1908.09257) — `resources/1908.09257v4.pdf`.
- *Normalizing Flows* — <https://www.youtube.com/watch?v=YPsIq_f_ihQ>.
