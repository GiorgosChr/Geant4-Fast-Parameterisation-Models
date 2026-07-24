# Conditioning the flow on `Z`

The multi-material flow adds the target atomic number `Z` as a second conditioning input, alongside
`eGamma`. This note records why that needs **almost no change** to the model — nothing beyond adding
`Z` to the input list. It reuses the single-material flow from
[`gammaConversionSi/training`](../../gammaConversionSi/training/README.md) unchanged.

## The question

The flow does not fit densities in MeV and radians; it maps the final state onto three learned
coordinates and then standardises each (zero mean, unit variance) using constants fitted once on the
training data. That standardisation is what puts the data inside the flow's usable range.

For one material this was unambiguous: one element, one set of constants. Conditioning on `Z` raises
the worry that a single pooled set might not work across the whole periodic table — that the
coordinates might **shift** or **broaden** with `Z` enough to push some elements outside the flow's
range, or that the flow might not be able to follow the `Z`-dependence at all.

## The measurement

For each element (Z = 1…82) the three coordinates were recomputed directly from the ntuple — no
trained model needed — and their mean and width measured. The finding, across the whole range:

- **The coordinates barely move.** The largest shift of any element's mean, over the entire periodic
  table, is well under one standard deviation — against a usable range of ±6. Every element sits
  comfortably inside the flow's resolved region.
- **The widths are essentially `Z`-independent**, to within ~10 %.
- **The trend is smooth and monotone in `log Z`**, which is why `log Z` (not linear `Z`) is the right
  way to feed it in.

A smooth, sub-σ shift across the periodic table is exactly the kind of dependence a *conditional*
flow represents natively: every head already sees `log Z` through the trunk, so the shift is
something the model absorbs, not something it has to fight. Pooled standardisation only has to put the
data in the window — and it does.

One `Z`-dependence is sharp rather than mild: the **triplet fraction** follows `1/(Z+1)`, from 50 %
at hydrogen down to ~1 % at lead. But that lives entirely in the Bernoulli triplet head, which
handles it directly; it is not something the standardisation has to cope with. (The recoil coordinate
is still standardised *separately for nuclear and triplet events*, as in the single-material model,
because those two modes sit far apart — but that split is independent of `Z`.)

## What changes in the code

Only the input list:

```python
INPUT_COLUMNS = ("eGamma", "Z")     # was ("eGamma",)
```

Everything else follows automatically: the trunk's first layer is sized from the input count, and the
input normalisation is already per-column. The head layout, the coordinate maps and the per-mode
recoil constants are all unchanged.

## Caveats

- **The pooled constants depend on the training mix**, so they must be refitted on whatever mix is
  actually trained on — never carried over from a different one. The margin is large enough that the
  conclusion holds regardless.
- **Outside the trained `Z` range is extrapolation.** Training covers Z = 1…82, every element in the
  composites, so this only bites if that list grows.
- **This measures the coordinates, not the fit.** It shows pooled standardisation puts every element
  in range; whether the trained flow actually reproduces each element's density is what the closure
  plots answer — especially the triplet fraction versus `Z`, the sharpest dependence in the problem.
