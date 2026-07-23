"""A conditional normalising flow for gamma -> e+e- conversion kinematics.

Given the incident photon energy, this *samples* the kinematics of the pair
rather than predicting them:

    input    eGamma
    sampled  isTriplet, eRecoil, eLead, thetaLead
    derived  eSub = eGamma - 2*me - eLead - eRecoil

which is the whole point. The pair kinematics are not a function of `eGamma`:
`G4BetheHeitler5DModel` draws them from a distribution over the full
five-dimensional final state, so a regression trained with MSE could only ever
learn the conditional mean and collapse onto it. A flow learns the conditional
density itself, and generating an event means drawing from it.

Separate heads, chained
-----------------------

Each quantity gets its own network, all reading a shared trunk over `eGamma`::

    eGamma -> log10 -> [0,1] -> trunk -+-> triplet_head   Bernoulli(isTriplet | E)
                                       +-> recoil_flow    p(z_recoil | E, isTriplet)
                                       +-> lead_flow      p(z_lead   | E, z_recoil)
                                       +-> theta_flow     p(z_theta  | E, z_lead)

The later heads also see the already-sampled quantities, so the product of the
four is the exact joint density by the chain rule -- not an approximation that
assumes independence. That conditioning is not decoration: at fixed `eGamma` the
angular scale is ``me / E`` of *that lepton*, so p(theta) genuinely depends on
`eLead`, and heads that could not see it would reproduce each one-dimensional
marginal while generating wrong (eLead, thetaLead) pairs.

`isTriplet` comes first because `eRecoil` is bimodal: the recoil is the nucleus
for ~93% of conversions and carries tens of eV, and an atomic electron for the
remaining ~1/(Z+1), carrying up to two thirds of the photon energy. Sampling the
mode first leaves each head fitting one smooth unimodal shape, instead of one
spline having to bridge ~10 orders of magnitude of empty space between two peaks.

Each head is an `nflows` `Flow` over a *one-dimensional* rational-quadratic
spline transform. With ``features=1`` the autoregressive masks inside MADE
degenerate and the spline parameters come purely from the context path, so the
transform is exactly a conditional head -- built from library code that is
already tested, rather than a hand-rolled `Transform`.

Coordinates
-----------

The flow does not work in MeV and radians. With ``S = eGamma - 2*me`` the
kinetic energy available to share::

    f_recoil = eRecoil / S                    z_recoil = logit(f_recoil)
    f_lead   = eLead / (S - eRecoil)          z_lead   = logit(2*f_lead - 1)
    t        = thetaLead * eLead / me         z_theta  = log(t)

Two things fall out of this, both structural rather than learned:

- ``f_lead <= 1`` makes ``eSub >= 0``, and energy conservation exact, for every
  sample -- a constraint removed by construction rather than left to training.
- ``f_lead >= 0.5`` keeps the leading lepton leading, so the sorted-pair
  convention of the dataset cannot be violated by a sample.

`t = theta * E / me` is the natural angular variable, since the Bethe-Heitler
angular scale is `me / E`. Working in it strips most of the `eGamma` dependence
out of the target, so the flow learns one nearly universal shape rather than a
different one at every energy.

Normalisation
-------------

The `eGamma` input uses ``log10`` then a min-max rescaling onto ``[0, 1]``, from
buffers fitted on the training split.

The three learned coordinates are **standardised** instead, which is a
deliberate difference. The base distribution here is a standard normal and
``tail_bound`` is expressed in its units, so anything beyond it lands in the
spline's linear tails where there is no resolution left; zero mean and unit
variance is what puts the data inside the bins. `z_recoil` gets its constants
*per mode*, indexed by `isTriplet`, because the two modes sit ~20 logit units
apart and one shared sigma would spend the spline's bins on the gap between them.

All of it lives in buffers, so a saved `state_dict` is self-contained: there is
no scaler to reload alongside the weights.
"""

from __future__ import annotations

import torch
from nflows.distributions.normal import StandardNormal
from nflows.flows.base import Flow
from nflows.transforms.autoregressive import (
    MaskedPiecewiseRationalQuadraticAutoregressiveTransform,
)
from torch import nn
from torch.nn import functional as F

from conversion_data import ELECTRON_MASS, INPUT_COLUMNS, TARGET_COLUMNS, build_dataset

__all__ = [
    "ConversionFlow",
    "build_dataset",
    "INPUT_COLUMNS",
    "TARGET_COLUMNS",
    "ELECTRON_MASS",
    "LOSS_TERMS",
    "LOSS_WEIGHTS",
    "WEIGHT_DECAY",
]

#: Floor applied before log10 and log, guarding against a zero-valued sample.
#: The only floor in the coordinate maps: both logits are built from logs of
#: energies rather than from a fraction, so nothing needs clamping away from
#: the ends of the unit interval. See `ConversionFlow.to_learned`.
_TINY = 1e-30

#: Order the loss terms are reported in: the order they are sampled in.
LOSS_TERMS = ("isTriplet", "eRecoil", "eLead", "thetaLead")

#: Per-head weights for the training loss. Each head's negative log-likelihood
#: is averaged over the batch on its own and the four means are then summed, so
#: no head can dominate the total through sheer batch-to-batch scale.
#:
#: With every weight at 1.0 this is *numerically* the joint NLL, because a mean
#: of sums equals a sum of means. What the split buys is visibility -- the four
#: numbers are reported separately every epoch -- and a place to intervene: if
#: one head's NLL is orders of magnitude larger than the others and drags the
#: shared trunk around, lower its weight here rather than rescaling the data.
LOSS_WEIGHTS = {name: 1.0 for name in LOSS_TERMS}

#: Default L2 penalty, applied by `ConversionFlow.make_optimiser` to the linear
#: weights only.
WEIGHT_DECAY = 1e-4


class ConversionFlow(nn.Module):
    """Conditional flow over the conversion final state, one head per quantity.

    Typical use::

        model = ConversionFlow()
        model.fit_normalisation(x_train, y_train, triplet_train)   # train split
        optimiser = model.make_optimiser(lr)
        ...
        loss, per_head = model.loss(x, y, is_triplet)
        loss.backward()
        ...
        events = model.sample(x)     # physical units, energy-conserving
    """

    def __init__(
        self,
        trunk_hidden=(128, 128),
        trunk_features=64,
        head_hidden=64,
        head_blocks=2,
        num_bins=16,
        tail_bound=6.0,
        activation=nn.ReLU,
    ):
        super().__init__()

        n_in = len(INPUT_COLUMNS)

        # -- non-trained constants, fitted from the training split ----------
        self.register_buffer("input_min", torch.zeros(n_in))
        self.register_buffer("input_max", torch.ones(n_in))
        # eRecoil is standardised per conversion mode: index 0 nuclear, 1 triplet
        self.register_buffer("recoil_mu", torch.zeros(2))
        self.register_buffer("recoil_sigma", torch.ones(2))
        self.register_buffer("lead_mu", torch.zeros(()))
        self.register_buffer("lead_sigma", torch.ones(()))
        self.register_buffer("theta_mu", torch.zeros(()))
        self.register_buffer("theta_sigma", torch.ones(()))
        self.register_buffer("fitted", torch.zeros((), dtype=torch.bool))

        # -- shared trunk ---------------------------------------------------
        # No batch norm in the trunk. It feeds heads that parameterise a
        # density, and batch norm would make the log-likelihood of one row
        # depend on which other rows happened to share its batch.
        layers = []
        size = n_in
        for width in trunk_hidden:
            layers += [nn.Linear(size, width), activation()]
            size = width
        layers += [nn.Linear(size, trunk_features), activation()]
        self.trunk = nn.Sequential(*layers)

        # -- heads ----------------------------------------------------------
        # Every head sees the trunk plus exactly one already-sampled quantity,
        # hence trunk_features + 1 context features throughout.
        self.triplet_head = nn.Sequential(
            nn.Linear(trunk_features, head_hidden), activation(), nn.Linear(head_hidden, 1)
        )

        def spline_head():
            transform = MaskedPiecewiseRationalQuadraticAutoregressiveTransform(
                features=1,
                hidden_features=head_hidden,
                context_features=trunk_features + 1,
                num_bins=num_bins,
                tails="linear",
                tail_bound=tail_bound,
                num_blocks=head_blocks,
                use_residual_blocks=True,
                use_batch_norm=False,
            )
            return Flow(transform, StandardNormal([1]))

        self.recoil_flow = spline_head()
        self.lead_flow = spline_head()
        self.theta_flow = spline_head()

        # `StandardNormal` registers its log-normaliser as float64, and MPS has
        # no float64 at all -- without this, `ConversionFlow().to("mps")` dies
        # with "Cannot convert a MPS Tensor to float64 dtype". It matters on CPU
        # too: that one float64 constant would otherwise promote every
        # log-density, and the loss with it, to double precision.
        self.float()

    # -- optimisation ------------------------------------------------------

    def make_optimiser(self, lr=1e-3, weight_decay=WEIGHT_DECAY):
        """Adam with an L2 penalty on the linear weights.

        Weight matrices are penalised, biases are not. MADE's `MaskedLinear`
        subclasses `nn.Linear`,
        so the heads are covered without naming them. Its masked-out entries are
        decayed along with the rest, which is harmless -- they are multiplied by
        a zero mask before they ever reach the output.
        """
        decayed, undecayed = [], []
        for module in self.modules():
            if isinstance(module, nn.Linear):
                decayed.append(module.weight)
                if module.bias is not None:
                    undecayed.append(module.bias)
            elif isinstance(module, nn.BatchNorm1d) and module.affine:
                undecayed += [module.weight, module.bias]

        return torch.optim.Adam(
            [
                {"params": decayed, "weight_decay": weight_decay},
                {"params": undecayed, "weight_decay": 0.0},
            ],
            lr=lr,
        )

    # -- coordinates -------------------------------------------------------

    @staticmethod
    def _log10(values):
        return torch.log10(torch.clamp(values, min=_TINY))

    @staticmethod
    def _log(values):
        return torch.log(torch.clamp(values, min=_TINY))

    @staticmethod
    def _available_energy(e_gamma):
        """Kinetic energy the three secondaries share, ``eGamma - 2*me``."""
        return torch.clamp(e_gamma - 2.0 * ELECTRON_MASS, min=_TINY)

    def to_learned(self, x, y):
        """Physical targets -> the coordinates the flow works in.

        Returns the three unstandardised coordinates ``(z_recoil, z_lead,
        z_theta)``, each of shape (N,).

        Both logits are formed as a *difference of logs* rather than by
        computing the fraction and taking ``log(f) - log1p(-f)``. That is not
        cosmetic. A nuclear recoil of tens of eV against an available energy of
        up to 100 GeV gives ``f_recoil`` between 1e-8 and 1e-14, so any
        epsilon-clamped logit large enough to be safe near ``f = 1`` flattens
        the entire nuclear mode -- 93% of the sample -- onto one value, leaving
        it with no width for the per-mode standardisation to measure. Working
        from the energies keeps every one of those rows distinct, and needs no
        floor beyond the shared `_TINY`.
        """
        e_gamma = x[:, 0]
        e_lead, theta_lead, e_recoil = y[:, 0], y[:, 1], y[:, 2]

        total = self._available_energy(e_gamma)
        shared = torch.clamp(total - e_recoil, min=_TINY)  # eLead + eSub
        e_sub = torch.clamp(shared - e_lead, min=_TINY)

        # logit(eRecoil / S) == log(eRecoil) - log(S - eRecoil)
        z_recoil = self._log(e_recoil) - self._log(shared)
        # eLead >= eSub by construction of the dataset, so eLead / (eLead + eSub)
        # lives in [0.5, 1) and 2f - 1 maps that onto the unit interval; its
        # logit is log(eLead - eSub) - log(2*eSub).
        z_lead = self._log(e_lead - e_sub) - self._log(2.0 * e_sub)
        z_theta = self._log(theta_lead * e_lead / ELECTRON_MASS)

        return z_recoil, z_lead, z_theta

    def from_learned(self, x, z_recoil, z_lead, z_theta):
        """The inverse of `to_learned`, used to decode a sample.

        Every constraint the physical final state has to satisfy is restored
        here by construction, not by clamping: the recoil takes a fraction of
        the available energy, the two leptons split what is left, and the
        remainder *is* `eSub`, so conservation closes exactly and no energy can
        come out negative.
        """
        total = self._available_energy(x[:, 0])

        e_recoil = torch.sigmoid(z_recoil) * total
        shared = total - e_recoil
        e_lead = 0.5 * (torch.sigmoid(z_lead) + 1.0) * shared
        e_sub = shared - e_lead
        theta_lead = torch.exp(z_theta) * ELECTRON_MASS / torch.clamp(e_lead, min=_TINY)

        return e_lead, theta_lead, e_recoil, e_sub

    # -- normalisation -----------------------------------------------------

    def fit_normalisation(self, x, y, is_triplet):
        """Set every non-trained constant from the training split.

        Call this once, on the training data only. Using the full sample would
        leak the validation set's range into the model.
        """
        x = torch.as_tensor(x, dtype=torch.float32)
        y = torch.as_tensor(y, dtype=torch.float32)
        is_triplet = torch.as_tensor(is_triplet, dtype=torch.float32)

        log_x = self._log10(x)
        self.input_min.copy_(log_x.min(dim=0).values)
        self.input_max.copy_(log_x.max(dim=0).values)

        z_recoil, z_lead, z_theta = self.to_learned(x, y)
        self.lead_mu.copy_(z_lead.mean())
        self.lead_sigma.copy_(z_lead.std().clamp(min=1e-12))
        self.theta_mu.copy_(z_theta.mean())
        self.theta_sigma.copy_(z_theta.std().clamp(min=1e-12))

        # One (mu, sigma) per conversion mode. A split that happens to hold a
        # single row has no width to measure, so it falls back to the pooled
        # constants rather than to a NaN.
        mode = is_triplet.long()
        for value in (0, 1):
            selected = z_recoil[mode == value]
            if selected.numel() > 1:
                self.recoil_mu[value] = selected.mean()
                self.recoil_sigma[value] = selected.std().clamp(min=1e-12)
            else:
                self.recoil_mu[value] = z_recoil.mean()
                self.recoil_sigma[value] = z_recoil.std().clamp(min=1e-12)

        self.fitted.fill_(True)
        return self

    def _standardise(self, z_recoil, z_lead, z_theta, is_triplet):
        mode = is_triplet.long()
        return (
            (z_recoil - self.recoil_mu[mode]) / self.recoil_sigma[mode],
            (z_lead - self.lead_mu) / self.lead_sigma,
            (z_theta - self.theta_mu) / self.theta_sigma,
        )

    def _destandardise(self, u_recoil, u_lead, u_theta, is_triplet):
        mode = is_triplet.long()
        return (
            u_recoil * self.recoil_sigma[mode] + self.recoil_mu[mode],
            u_lead * self.lead_sigma + self.lead_mu,
            u_theta * self.theta_sigma + self.theta_mu,
        )

    # -- density -----------------------------------------------------------

    def _context(self, trunk, conditioner):
        """Trunk features plus the one quantity this head is conditioned on."""
        return torch.cat([trunk, conditioner.unsqueeze(1)], dim=1)

    def _trunk_features(self, x):
        if not bool(self.fitted):
            raise RuntimeError(
                "ConversionFlow.fit_normalisation(x, y, is_triplet) must be called "
                "before the model is used"
            )
        normalised = (self._log10(x) - self.input_min) / (
            (self.input_max - self.input_min).clamp(min=1e-12)
        )
        return self.trunk(normalised)

    def log_prob_terms(self, x, y, is_triplet):
        """Per-head log-densities, each of shape (N,).

        Summing the four gives the joint log-density of the event by the chain
        rule. They are returned separately because that is how the loss is
        formed, and because a head that has stopped learning is invisible in the
        total.

        The three continuous terms are densities in the *standardised* learned
        coordinates, so the number is not a likelihood in MeV and radians and is
        not comparable to anything computed in physical units.
        """
        trunk = self._trunk_features(x)
        is_triplet = is_triplet.to(trunk.dtype)

        z_recoil, z_lead, z_theta = self.to_learned(x, y)
        u_recoil, u_lead, u_theta = self._standardise(z_recoil, z_lead, z_theta, is_triplet)

        triplet_logit = self.triplet_head(trunk)[:, 0]
        log_p_triplet = -F.binary_cross_entropy_with_logits(
            triplet_logit, is_triplet, reduction="none"
        )

        return {
            "isTriplet": log_p_triplet,
            "eRecoil": self.recoil_flow.log_prob(
                u_recoil.unsqueeze(1), context=self._context(trunk, is_triplet)
            ),
            "eLead": self.lead_flow.log_prob(
                u_lead.unsqueeze(1), context=self._context(trunk, u_recoil)
            ),
            "thetaLead": self.theta_flow.log_prob(
                u_theta.unsqueeze(1), context=self._context(trunk, u_lead)
            ),
        }

    def loss(self, x, y, is_triplet, weights=None):
        """Training loss: each head averaged on its own, then summed.

        Returns ``(total, per_head)``, where `per_head` maps each name in
        `LOSS_TERMS` to that head's mean negative log-likelihood as a scalar
        tensor -- print them, and a head that is not learning shows up as its
        own flat curve rather than being hidden inside the total.

        See `LOSS_WEIGHTS` for what the per-head weights do and do not change.
        """
        weights = LOSS_WEIGHTS if weights is None else weights
        terms = self.log_prob_terms(x, y, is_triplet)
        per_head = {name: -log_prob.mean() for name, log_prob in terms.items()}
        total = sum(weights.get(name, 1.0) * value for name, value in per_head.items())
        return total, per_head

    def forward(self, x, y, is_triplet):
        """Joint log-density of the given events, shape (N,).

        Training goes through `loss`, which keeps the heads separate; this is
        the plain chain-rule sum, for evaluating a density.
        """
        return sum(self.log_prob_terms(x, y, is_triplet).values())

    # -- sampling ----------------------------------------------------------

    def sample(self, x):
        """Draw one final state per row of `x`, in the units of the ntuple.

        Returns a dict with `eLead`, `thetaLead`, `eRecoil`, `eSub` (MeV) and
        `isTriplet`, each of shape (N,). Energy conservation and ``eSub >= 0``
        hold by construction -- see `from_learned`.

        The heads are drawn in their conditioning order, each one taking the
        previous sample as context. Call it inside `torch.no_grad()`.
        """
        trunk = self._trunk_features(x)

        triplet_probability = torch.sigmoid(self.triplet_head(trunk)[:, 0])
        is_triplet = torch.bernoulli(triplet_probability)

        # Flow.sample(1, context=(N, C)) returns (N, 1, 1): one draw per row.
        u_recoil = self.recoil_flow.sample(1, context=self._context(trunk, is_triplet))[:, 0, 0]
        u_lead = self.lead_flow.sample(1, context=self._context(trunk, u_recoil))[:, 0, 0]
        u_theta = self.theta_flow.sample(1, context=self._context(trunk, u_lead))[:, 0, 0]

        z_recoil, z_lead, z_theta = self._destandardise(u_recoil, u_lead, u_theta, is_triplet)
        e_lead, theta_lead, e_recoil, e_sub = self.from_learned(x, z_recoil, z_lead, z_theta)

        return {
            "eLead": e_lead,
            "thetaLead": theta_lead,
            "eRecoil": e_recoil,
            "eSub": e_sub,
            "isTriplet": is_triplet,
        }
