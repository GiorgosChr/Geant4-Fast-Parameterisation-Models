"""A self-contained DNN for gamma -> e+e- conversion kinematics.

Given the incident photon energy and the distance it travelled inside the
block before converting, the network predicts the kinematics of the pair:

    inputs   eGamma, pathInBlock
    outputs  eLead, thetaLead, eRecoil
    derived  eSub = eGamma - 2*me - eLead - eRecoil

`eLead` and `eSub` are the higher and lower of the two lepton energies, sorted
by magnitude rather than by charge, and `thetaLead` is the polar angle with
respect to the incident photon of whichever lepton carried `eLead`.

`eRecoil` is predicted rather than neglected so that the energy-conservation
step is exact. Every conversion emits three secondaries -- e-, e+ and a recoil
-- and while that recoil is the nucleus for ~93% of events and carries only
tens of eV, for the remaining ~1/(Z+1) it is an atomic electron carrying up to
two thirds of the photon energy. Dropping it would put the derived `eSub` out
by more than 100% on those.

The normalisation lives inside the model, as non-trained buffers. There is no
separate scaler object to keep in sync with the weights: a saved `state_dict`
is everything needed to run inference later.
"""

from __future__ import annotations

import numpy as np
import torch
from torch import nn

__all__ = ["ConversionDNN", "build_dataset", "INPUT_COLUMNS", "TARGET_COLUMNS", "ELECTRON_MASS"]

INPUT_COLUMNS = ("eGamma", "pathInBlock")
TARGET_COLUMNS = ("eLead", "thetaLead", "eRecoil")

#: Electron mass in MeV, matching the units of the ntuple.
ELECTRON_MASS = 0.51099895

#: Floor applied before log10, guarding against a zero-valued sample.
_TINY = 1e-30


def build_dataset(arrays):
    """Turn raw ntuple arrays into model inputs and targets.

    Sorts the pair by energy, so that the model never has to learn the
    arbitrary e-/e+ labelling, and pairs each angle with the lepton it belongs
    to.

    Parameters
    ----------
    arrays : mapping of str to numpy array
        Must provide eGamma, pathInBlock, eElectron, ePositron, thetaElectron,
        thetaPositron and eRecoil, as returned by ``tree.arrays(library="np")``.

    Returns
    -------
    x, y, extra : (N, 2), (N, 3) float32 arrays and a dict
        `extra` carries eSub, the true lower lepton energy, which the model
        derives rather than predicts and which is therefore useful only for
        validation.
    """
    electron = arrays["eElectron"]
    positron = arrays["ePositron"]
    electron_leads = electron >= positron

    e_lead = np.where(electron_leads, electron, positron)
    e_sub = np.where(electron_leads, positron, electron)
    theta_lead = np.where(electron_leads, arrays["thetaElectron"], arrays["thetaPositron"])

    x = np.stack([arrays["eGamma"], arrays["pathInBlock"]], axis=1).astype(np.float32)
    y = np.stack([e_lead, theta_lead, arrays["eRecoil"]], axis=1).astype(np.float32)
    return x, y, {"eSub": e_sub.astype(np.float32)}


class ConversionDNN(nn.Module):
    """Fully connected network with normalisation and batch norm built in.

    Both the inputs and the targets are strictly positive and span between
    five and twelve decades, so the in-model transform is ``log10`` followed by
    standardisation. Plain standardisation over that range would leave nearly
    every event squashed against zero with rare points tens of sigma away.
    Working in log space also makes the three predicted quantities positive by
    construction, since they are decoded through ``10**x``.

    This is separate from, and in addition to, the batch norm in the hidden
    layers: batch norm cannot see the fixed physical scale of the raw inputs,
    because it only ever acts after the first linear layer.

    The normalisation constants are registered as buffers rather than
    parameters, so they move with ``.to(device)``, are written to the
    ``state_dict``, and are never touched by the optimiser.

    Typical use::

        model = ConversionDNN()
        model.fit_normalisation(x_train, y_train)   # training split only
        ...
        loss = mse(model.forward_normalised(x), model.normalise_targets(y))
        ...
        pred = model(x)          # physical units, plus the derived eSub
    """

    def __init__(self, hidden=(128, 128, 64), activation=nn.ReLU):
        super().__init__()

        n_in = len(INPUT_COLUMNS)
        n_out = len(TARGET_COLUMNS)

        # Non-trained normalisation constants. Initialised to the identity so
        # the module is usable (if pointless) before fit_normalisation.
        self.register_buffer("input_mu", torch.zeros(n_in))
        self.register_buffer("input_sigma", torch.ones(n_in))
        self.register_buffer("target_mu", torch.zeros(n_out))
        self.register_buffer("target_sigma", torch.ones(n_out))
        self.register_buffer("fitted", torch.zeros((), dtype=torch.bool))

        layers = []
        size = n_in
        for width in hidden:
            layers += [nn.Linear(size, width), nn.BatchNorm1d(width), activation()]
            size = width
        layers.append(nn.Linear(size, n_out))
        self.net = nn.Sequential(*layers)

    # -- normalisation -----------------------------------------------------

    @staticmethod
    def _log10(values):
        return torch.log10(torch.clamp(values, min=_TINY))

    def fit_normalisation(self, x, y):
        """Set the normalisation constants from the training split.

        Call this once, on the training data only. Using the full sample would
        leak the validation set's scale into the model.
        """
        x = torch.as_tensor(x, dtype=torch.float32)
        y = torch.as_tensor(y, dtype=torch.float32)

        log_x, log_y = self._log10(x), self._log10(y)
        self.input_mu.copy_(log_x.mean(dim=0))
        self.input_sigma.copy_(log_x.std(dim=0).clamp(min=1e-12))
        self.target_mu.copy_(log_y.mean(dim=0))
        self.target_sigma.copy_(log_y.std(dim=0).clamp(min=1e-12))
        self.fitted.fill_(True)
        return self

    def normalise_inputs(self, x):
        return (self._log10(x) - self.input_mu) / self.input_sigma

    def normalise_targets(self, y):
        """Map physical targets into the space the loss is computed in."""
        return (self._log10(y) - self.target_mu) / self.target_sigma

    def denormalise_targets(self, y):
        return torch.pow(10.0, y * self.target_sigma + self.target_mu)

    # -- forward -----------------------------------------------------------

    def forward_normalised(self, x):
        """Raw network output, in the normalised target space.

        This is what the loss should be compared against, paired with
        `normalise_targets`, so that the three outputs contribute comparably
        despite spanning 5.5, 7.5 and 12 decades in physical units.
        """
        if not bool(self.fitted):
            raise RuntimeError(
                "ConversionDNN.fit_normalisation(x, y) must be called before the model is used"
            )
        return self.net(self.normalise_inputs(x))

    def forward(self, x):
        """Physical predictions for physical inputs.

        Returns a dict with `eLead`, `thetaLead`, `eRecoil` and the derived
        `eSub`, all in the units of the ntuple (MeV and radians).

        `eSub` is returned unclamped. Nothing constrains the network to keep
        ``eLead + eRecoil <= eGamma - 2*me``, so an untrained or poorly trained
        model can produce a negative lower lepton energy. That is worth seeing:
        the fraction of negative `eSub` is a blunt but honest convergence
        check, and clamping it at zero would hide it.
        """
        predicted = self.denormalise_targets(self.forward_normalised(x))
        e_lead, theta_lead, e_recoil = predicted[:, 0], predicted[:, 1], predicted[:, 2]

        e_gamma = x[:, 0]
        e_sub = e_gamma - 2.0 * ELECTRON_MASS - e_lead - e_recoil

        return {"eLead": e_lead, "thetaLead": theta_lead, "eRecoil": e_recoil, "eSub": e_sub}
