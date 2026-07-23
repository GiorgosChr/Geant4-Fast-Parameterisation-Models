"""Dataset definition for the gamma -> e+e- conversion flow.

`conversion_flow.ConversionFlow` trains on these rows, and the ntuple-to-tensor
step lives here rather than in the model:

    input    eGamma
    targets  eLead, thetaLead, eRecoil
    derived  eSub = eGamma - 2*me - eLead - eRecoil
    mode     isTriplet

`eLead` and `eSub` are the higher and lower of the two lepton energies, sorted by
magnitude rather than by charge, and `thetaLead` is the polar angle with respect
to the incident photon of whichever lepton carried `eLead`. Sorting means the
model never has to learn the arbitrary e-/e+ labelling.

`eRecoil` is kept as a target rather than neglected so that the energy-conservation
step is exact. Every conversion emits three secondaries -- e-, e+ and a recoil --
and while that recoil is the nucleus for ~93% of events and carries only tens of
eV, for the remaining ~1/(Z+1) it is an atomic electron carrying up to two thirds
of the photon energy. Dropping it would put the derived `eSub` out by more than
100% on those.

**`pathInBlock` is deliberately not an input.** Given that a conversion happened,
the final state depends on the photon energy and the material, not on where in
the block it occurred: the vertex is sampled independently, from the attenuation
length. In fast simulation Geant4 supplies that vertex itself through the
interaction length, so a model of the conversion only has to supply kinematics.
Feeding the path in adds a variable the target does not depend on, and asks the
network to learn that it should be ignored.
"""

from __future__ import annotations

import numpy as np

__all__ = [
    "build_dataset",
    "INPUT_COLUMNS",
    "TARGET_COLUMNS",
    "NTUPLE_BRANCHES",
    "ELECTRON_MASS",
]

INPUT_COLUMNS = ("eGamma",)
TARGET_COLUMNS = ("eLead", "thetaLead", "eRecoil")

#: Branches to read from the `conversions` tree to build a training set.
NTUPLE_BRANCHES = [
    "eGamma",
    "eElectron",
    "ePositron",
    "thetaElectron",
    "thetaPositron",
    "eRecoil",
    "isTriplet",
]

#: Electron mass in MeV, matching the units of the ntuple.
ELECTRON_MASS = 0.51099895


def build_dataset(arrays):
    """Turn raw ntuple arrays into model inputs and targets.

    Sorts the pair by energy, so that the model does not have to learn the
    arbitrary e-/e+ labelling, and pairs each angle with the lepton it belongs to.

    Parameters
    ----------
    arrays : mapping of str to numpy array
        Must provide every name in `NTUPLE_BRANCHES`, as returned by
        ``tree.arrays(NTUPLE_BRANCHES, library="np")``.

    Returns
    -------
    x, y, extra : (N, 1), (N, 3) float32 arrays and a dict
        `extra` carries `eSub`, the true lower lepton energy, which the model
        derives rather than samples, and `isTriplet`, the conversion mode -- 1
        when the photon converted on an atomic electron and the recoil is a
        second electron, 0 when it converted on a nucleus. Both are float32, so
        they can go straight into a tensor.
    """
    electron = arrays["eElectron"]
    positron = arrays["ePositron"]
    electron_leads = electron >= positron

    e_lead = np.where(electron_leads, electron, positron)
    e_sub = np.where(electron_leads, positron, electron)
    theta_lead = np.where(electron_leads, arrays["thetaElectron"], arrays["thetaPositron"])

    x = np.stack([arrays["eGamma"]], axis=1).astype(np.float32)
    y = np.stack([e_lead, theta_lead, arrays["eRecoil"]], axis=1).astype(np.float32)
    extra = {
        "eSub": e_sub.astype(np.float32),
        "isTriplet": arrays["isTriplet"].astype(np.float32),
    }
    return x, y, extra
