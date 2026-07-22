"""A self-contained DNN for gamma -> e+e- conversion kinematics.

Given the incident photon energy, the network predicts the kinematics of the
pair:

    input    eGamma
    outputs  eLead, thetaLead, eRecoil
    derived  eSub = eGamma - 2*me - eLead - eRecoil

The dataset itself is defined in `conversion_data`, shared with the flow.

**This model does not solve the problem, and is kept as a baseline.** Trained
with MSE it plateaus from the first epoch, because the pair kinematics are not a
function of `eGamma`: `G4BetheHeitler5DModel` samples them from a distribution
over the full five-dimensional final state, so the best a regression can do is
the conditional mean. `conversion_flow.ConversionFlow` is the model that samples
instead of regressing. What survives here is the scaffolding -- the in-model
normalisation, the head layout and the conservation step.

The normalisation lives inside the model, as non-trained buffers. There is no
separate scaler object to keep in sync with the weights: a saved `state_dict`
is everything needed to run inference later.
"""

from __future__ import annotations

import torch
from torch import nn

from conversion_data import ELECTRON_MASS, INPUT_COLUMNS, TARGET_COLUMNS, build_dataset

__all__ = [
    "ConversionDNN",
    "build_dataset",
    "INPUT_COLUMNS",
    "TARGET_COLUMNS",
    "ELECTRON_MASS",
    "WEIGHT_DECAY",
]

#: Floor applied before log10, guarding against a zero-valued sample.
_TINY = 1e-30

#: Default L2 penalty, applied by `ConversionDNN.make_optimiser` to the linear
#: weights only. Small because the network is already regularised by batch norm
#: and by ~8M training rows against ~30k parameters; raise it if the train and
#: validation curves separate.
WEIGHT_DECAY = 1e-4


class ConversionDNN(nn.Module):
    """Shared trunk over `eGamma`, then one separate head per predicted quantity.

    `eLead`, `thetaLead` and `eRecoil` are unrelated in shape -- an energy
    sharing, an angle and a recoil that is bimodal over orders of magnitude -- so
    each gets its own branch rather than one shared projection.

    Note that a *single* linear layer per target would be no different from the
    old 3-wide output layer: the rows of that matrix are already independent, so
    splitting it changes nothing about the function. The heads therefore carry a
    hidden layer of their own, which is what actually gives each target its own
    nonlinearity rather than only its own last row of weights.

    Both the input and the targets are strictly positive and span between five
    and twelve orders of magnitude, so the in-model transform is ``log10``
    followed by a min-max rescaling onto ``[0, 1]``. Rescaling the raw values
    would leave nearly every event squashed against zero, with rare points far
    up the range. Working in log space also makes the three predicted quantities
    positive by construction, since they are decoded through ``10**x``.

    The range is fitted on the training split, so validation and inference
    values falling just outside ``[0, 1]`` are expected, not a bug -- nothing
    clamps them, and clamping would silently bias the extremes of the spectrum.

    This is separate from, and in addition to, the batch norm in the trunk:
    batch norm cannot see the fixed physical scale of the raw input, because it
    only ever acts after the first linear layer.

    The normalisation constants are registered as buffers rather than
    parameters, so they move with ``.to(device)``, are written to the
    ``state_dict``, and are never touched by the optimiser.

    L2 regularisation comes from ``make_optimiser``, which is the only place
    that knows which tensors should be penalised. Building the optimiser here
    rather than at the call site is what keeps that decision with the layers it
    applies to.

    Typical use::

        model = ConversionDNN()
        model.fit_normalisation(x_train, y_train)   # training split only
        optimiser = model.make_optimiser(lr)        # Adam, with the L2 penalty
        ...
        loss = mse(model.forward_normalised(x), model.normalise_targets(y))
        ...
        pred = model(x)          # physical units, plus the derived eSub
    """

    def __init__(self, hidden=(128, 128, 64), head_hidden=32, activation=nn.ReLU):
        super().__init__()

        n_in = len(INPUT_COLUMNS)
        n_out = len(TARGET_COLUMNS)

        # Non-trained normalisation constants. Initialised to the identity so
        # the module is usable (if pointless) before fit_normalisation.
        self.register_buffer("input_min", torch.zeros(n_in))
        self.register_buffer("input_max", torch.ones(n_in))
        self.register_buffer("target_min", torch.zeros(n_out))
        self.register_buffer("target_max", torch.ones(n_out))
        self.register_buffer("fitted", torch.zeros((), dtype=torch.bool))

        layers = []
        size = n_in
        for width in hidden:
            layers += [nn.Linear(size, width), nn.BatchNorm1d(width), activation()]
            size = width
        self.trunk = nn.Sequential(*layers)

        # One head per target, keyed by name so the state_dict reads as
        # `heads.thetaLead.…` rather than by index.
        self.heads = nn.ModuleDict(
            {
                name: nn.Sequential(
                    nn.Linear(size, head_hidden), activation(), nn.Linear(head_hidden, 1)
                )
                for name in TARGET_COLUMNS
            }
        )

    # -- optimisation ------------------------------------------------------

    def make_optimiser(self, lr=1e-3, weight_decay=WEIGHT_DECAY):
        """Adam with an L2 penalty on the linear weights.

        The penalty is applied to the weight matrices only. Biases and the
        `BatchNorm1d` affine parameters are put in a second group with
        ``weight_decay = 0``: shrinking a bias just displaces the layer's
        output, and shrinking a batch-norm scale towards zero fights the
        normalisation the layer exists to provide.

        This is Adam's coupled ``weight_decay``, which adds ``wd * w`` to the
        gradient and so is L2 regularisation in the literal sense. If the
        penalty ever needs to act independently of Adam's per-parameter step
        sizes, swap in ``torch.optim.AdamW`` here -- decoupled decay, and the
        only change needed is on this line.
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

    # -- normalisation -----------------------------------------------------

    @staticmethod
    def _log10(values):
        return torch.log10(torch.clamp(values, min=_TINY))

    @staticmethod
    def _span(low, high):
        """Denominator of the rescaling, safe for a constant column."""
        return (high - low).clamp(min=1e-12)

    def fit_normalisation(self, x, y):
        """Set the normalisation constants from the training split.

        Call this once, on the training data only. Using the full sample would
        leak the validation set's range into the model.
        """
        x = torch.as_tensor(x, dtype=torch.float32)
        y = torch.as_tensor(y, dtype=torch.float32)

        log_x, log_y = self._log10(x), self._log10(y)
        self.input_min.copy_(log_x.min(dim=0).values)
        self.input_max.copy_(log_x.max(dim=0).values)
        self.target_min.copy_(log_y.min(dim=0).values)
        self.target_max.copy_(log_y.max(dim=0).values)
        self.fitted.fill_(True)
        return self

    def normalise_inputs(self, x):
        return (self._log10(x) - self.input_min) / self._span(self.input_min, self.input_max)

    def normalise_targets(self, y):
        """Map physical targets into the space the loss is computed in."""
        return (self._log10(y) - self.target_min) / self._span(self.target_min, self.target_max)

    def denormalise_targets(self, y):
        log_y = y * self._span(self.target_min, self.target_max) + self.target_min
        return torch.pow(10.0, log_y)

    # -- forward -----------------------------------------------------------

    def forward_normalised(self, x):
        """Raw network output, in the normalised target space.

        The heads are concatenated in `TARGET_COLUMNS` order, so the result
        lines up column for column with `normalise_targets(y)`.

        This is what the loss should be compared against, paired with
        `normalise_targets`, so that the three outputs each cover [0, 1] and
        contribute comparably despite spanning 5.5, 7.5 and 12 orders of
        magnitude in physical units.
        """
        if not bool(self.fitted):
            raise RuntimeError(
                "ConversionDNN.fit_normalisation(x, y) must be called before the model is used"
            )
        features = self.trunk(self.normalise_inputs(x))
        return torch.cat([self.heads[name](features) for name in TARGET_COLUMNS], dim=1)

    def forward(self, x):
        """Physical predictions for physical inputs.

        Returns a dict with `eLead`, `thetaLead`, `eRecoil` and the derived
        `eSub`, all in the units of the ntuple (MeV and radians).

        `eSub` is returned unclamped. Nothing constrains the network to keep
        ``eLead + eRecoil <= eGamma - 2*me``, so an untrained or poorly trained
        model can produce a negative lower lepton energy. That is worth seeing:
        the fraction of negative `eSub` is a blunt but honest convergence
        check, and clamping it at zero would hide it. (`ConversionFlow` removes
        the failure mode entirely, by sampling energy *fractions* instead.)
        """
        predicted = self.denormalise_targets(self.forward_normalised(x))
        e_lead, theta_lead, e_recoil = predicted[:, 0], predicted[:, 1], predicted[:, 2]

        e_gamma = x[:, 0]
        e_sub = e_gamma - 2.0 * ELECTRON_MASS - e_lead - e_recoil

        return {"eLead": e_lead, "thetaLead": theta_lead, "eRecoil": e_recoil, "eSub": e_sub}
