"""Export the trained ConversionFlow to ONNX + JSON for the C++ fast-sim model.

`nflows`' ``sample()`` cannot be exported whole: it draws from a base normal and
runs the rational-quadratic spline in its *inverse* direction, so the graph is
stochastic and iterative. What *is* a plain feed-forward function of its input is

  * the trunk (eGamma -> trunk features) and the triplet logit, and
  * each head's MADE network (context -> unnormalised spline parameters).

So we export exactly those four deterministic pieces to ONNX and reimplement the
cheap remainder -- the standard-normal draw, the spline inverse, the
de-standardisation and the ``from_learned`` coordinate map -- in C++
(``ConversionFlowInference``). This script also writes every non-graph constant to
``flow_constants.json`` and validates the reimplementation against `nflows` so the
C++ contract is pinned before a line of C++ is written.

Run inside the training env (needs torch, nflows, onnx, onnxruntime, numpy)::

    python export_flow_onnx.py

Outputs into ``models/onnx/``: ``trunk.onnx``, ``recoil_head.onnx``,
``lead_head.onnx``, ``theta_head.onnx``, ``flow_constants.json``.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import torch
from torch import nn

from conversion_data import ELECTRON_MASS
from conversion_flow import ConversionFlow

HERE = Path(__file__).resolve().parent
CHECKPOINT = HERE / "models" / "conversion_flow.pt"
OUT_DIR = HERE / "models" / "onnx"
OPSET = 17
_TINY = 1e-30  # matches conversion_flow._TINY

# Spline hyper-parameters -- read back off the trained heads so this file can
# never silently disagree with the model it exports.
HEADS = ("recoil", "lead", "theta")


# --------------------------------------------------------------------------- #
# Export wrappers                                                             #
# --------------------------------------------------------------------------- #


class TrunkExport(nn.Module):
    """eGamma (MeV) -> (trunk_features, triplet_logit).

    Bakes in the log10 + min-max input normalisation, so the C++ side passes raw
    photon energy in MeV and never touches the input buffers.
    """

    def __init__(self, model: ConversionFlow):
        super().__init__()
        self.model = model

    def forward(self, e_gamma):  # e_gamma: (N, 1)
        trunk = self.model._trunk_features(e_gamma)
        logit = self.model.triplet_head(trunk)[:, 0:1]
        return trunk, logit


class HeadExport(nn.Module):
    """context (N, C) -> raw MADE parameters (N, 3*num_bins-1).

    With ``features=1`` the MADE output does not depend on the (masked) input
    feature, only on the context -- asserted in ``main`` before trusting it. The
    /sqrt(hidden_features) rescaling of widths/heights that nflows applies inside
    ``_elementwise`` is deliberately *not* done here; the C++ applies it, so the
    ONNX graph stays a verbatim copy of the trained network.
    """

    def __init__(self, flow):
        super().__init__()
        self.net = flow._transform.autoregressive_net

    def forward(self, context):  # context: (N, C)
        zeros = torch.zeros(context.shape[0], 1, dtype=context.dtype)
        return self.net(zeros, context)


# --------------------------------------------------------------------------- #
# Reference reimplementation (the exact logic the C++ will carry)             #
# --------------------------------------------------------------------------- #


def _softmax(z, axis=-1):
    z = z - z.max(axis=axis, keepdims=True)
    e = np.exp(z)
    return e / e.sum(axis=axis, keepdims=True)


def _softplus(z):
    # log1p(exp(-|z|)) + max(z, 0) -- overflow-safe
    return np.log1p(np.exp(-np.abs(z))) + np.maximum(z, 0.0)


def spline_inverse_numpy(params, z, consts):
    """Inverse of nflows' unconstrained RQ spline (noise -> data), vectorised.

    ``params`` (N, 3*num_bins-1) raw MADE output, ``z`` (N,) base-normal draws.
    Mirrors ``rational_quadratic.py`` exactly, including the linear tails, the
    derivative padding constant and the /sqrt(hidden) rescaling.
    """
    num_bins = consts["num_bins"]
    tb = consts["tail_bound"]
    min_bw = consts["min_bin_width"]
    min_bh = consts["min_bin_height"]
    min_d = consts["min_derivative"]
    scale = consts["param_scale"]

    uw = params[:, :num_bins] / scale
    uh = params[:, num_bins : 2 * num_bins] / scale
    ud = params[:, 2 * num_bins :]  # (N, num_bins-1)

    out = z.copy()
    inside = (z >= -tb) & (z <= tb)
    if not np.any(inside):
        return out

    uw, uh, ud, zi = uw[inside], uh[inside], ud[inside], z[inside]

    # widths -> cumwidths on [-tb, tb]
    widths = min_bw + (1 - min_bw * num_bins) * _softmax(uw)
    cumw = np.cumsum(widths, axis=-1)
    cumw = np.pad(cumw, ((0, 0), (1, 0)))
    cumw = 2 * tb * cumw - tb
    cumw[:, 0] = -tb
    cumw[:, -1] = tb
    widths = cumw[:, 1:] - cumw[:, :-1]

    heights = min_bh + (1 - min_bh * num_bins) * _softmax(uh)
    cumh = np.cumsum(heights, axis=-1)
    cumh = np.pad(cumh, ((0, 0), (1, 0)))
    cumh = 2 * tb * cumh - tb
    cumh[:, 0] = -tb
    cumh[:, -1] = tb
    heights = cumh[:, 1:] - cumh[:, :-1]

    # linear-tail derivative padding
    const = np.log(np.exp(1 - min_d) - 1)
    ud = np.pad(ud, ((0, 0), (1, 1)))
    ud[:, 0] = const
    ud[:, -1] = const
    derivatives = min_d + _softplus(ud)  # (N, num_bins+1)

    # bin index by searchsorted in cumheights (inverse uses the y grid)
    bin_idx = np.array(
        [np.searchsorted(cumh[i], zi[i], side="right") - 1 for i in range(len(zi))]
    )
    bin_idx = np.clip(bin_idx, 0, num_bins - 1)
    rows = np.arange(len(zi))

    in_cumw = cumw[rows, bin_idx]
    in_bw = widths[rows, bin_idx]
    in_cumh = cumh[rows, bin_idx]
    in_h = heights[rows, bin_idx]
    delta = in_h / in_bw
    d0 = derivatives[rows, bin_idx]
    d1 = derivatives[rows, bin_idx + 1]

    dz = zi - in_cumh
    a = dz * (d0 + d1 - 2 * delta) + in_h * (delta - d0)
    b = in_h * d0 - dz * (d0 + d1 - 2 * delta)
    c = -delta * dz
    disc = b * b - 4 * a * c
    disc = np.maximum(disc, 0.0)
    root = (2 * c) / (-b - np.sqrt(disc))
    out[inside] = root * in_bw + in_cumw
    return out


def from_learned_numpy(e_gamma, z_recoil, z_lead, z_theta):
    total = np.maximum(e_gamma - 2.0 * ELECTRON_MASS, _TINY)
    e_recoil = _sigmoid(z_recoil) * total
    shared = total - e_recoil
    e_lead = 0.5 * (_sigmoid(z_lead) + 1.0) * shared
    e_sub = shared - e_lead
    theta_lead = np.exp(z_theta) * ELECTRON_MASS / np.maximum(e_lead, _TINY)
    return e_lead, theta_lead, e_recoil, e_sub


def _sigmoid(z):
    return 1.0 / (1.0 + np.exp(-z))


def sample_numpy(sessions, consts, e_gamma, rng):
    """Full C++-equivalent sampling pipeline, using ONNX for the networks."""
    import onnxruntime as ort  # noqa: F401  (sessions already built)

    e = np.asarray(e_gamma, dtype=np.float32).reshape(-1, 1)
    n = e.shape[0]
    trunk, logit = sessions["trunk"].run(None, {"eGamma": e})
    p = _sigmoid(logit[:, 0])
    is_triplet = (rng.random(n) < p).astype(np.float32)

    def draw(head, cond):
        ctx = np.concatenate([trunk, cond.reshape(-1, 1).astype(np.float32)], axis=1)
        params = sessions[head].run(None, {"context": ctx})[0]
        z0 = rng.standard_normal(n)
        return spline_inverse_numpy(params, z0, consts)

    u_recoil = draw("recoil", is_triplet)
    u_lead = draw("lead", u_recoil)
    u_theta = draw("theta", u_lead)

    mode = is_triplet.astype(int)
    z_recoil = u_recoil * consts["recoil_sigma"][mode] + consts["recoil_mu"][mode]
    z_lead = u_lead * consts["lead_sigma"] + consts["lead_mu"]
    z_theta = u_theta * consts["theta_sigma"] + consts["theta_mu"]
    e_lead, theta_lead, e_recoil, e_sub = from_learned_numpy(
        e.reshape(-1), z_recoil, z_lead, z_theta
    )
    return {
        "eLead": e_lead,
        "thetaLead": theta_lead,
        "eRecoil": e_recoil,
        "eSub": e_sub,
        "isTriplet": is_triplet,
    }


# --------------------------------------------------------------------------- #
# Main                                                                        #
# --------------------------------------------------------------------------- #


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    model = ConversionFlow()
    state = torch.load(CHECKPOINT, map_location="cpu")
    model.load_state_dict(state)
    model.eval()
    if not bool(model.fitted):
        raise RuntimeError("checkpoint is not fitted; cannot export")

    transform = model.recoil_flow._transform
    consts = {
        "num_bins": int(transform.num_bins),
        "tail_bound": float(transform.tail_bound),
        "min_bin_width": float(transform.min_bin_width),
        "min_bin_height": float(transform.min_bin_height),
        "min_derivative": float(transform.min_derivative),
        # nflows rescales widths/heights by 1/sqrt(hidden) only when the
        # autoregressive net exposes a `hidden_features` attribute; MADE does
        # not, so no rescaling is applied and the scale is exactly 1.0.
        "hidden_features": int(transform.autoregressive_net.initial_layer.out_features),
        "param_scale": 1.0,
        "electron_mass": ELECTRON_MASS,
        "context_features": int(model.trunk[-2].out_features) + 1,
        "trunk_features": int(model.trunk[-2].out_features),
        "param_layout": {"widths": [0, 16], "heights": [16, 32], "derivatives": [32, 47]},
        "input_min": model.input_min.tolist(),
        "input_max": model.input_max.tolist(),
        "recoil_mu": model.recoil_mu.tolist(),
        "recoil_sigma": model.recoil_sigma.tolist(),
        "lead_mu": float(model.lead_mu),
        "lead_sigma": float(model.lead_sigma),
        "theta_mu": float(model.theta_mu),
        "theta_sigma": float(model.theta_sigma),
    }
    p = consts["num_bins"] * 3 - 1
    consts["param_layout"] = {
        "widths": [0, consts["num_bins"]],
        "heights": [consts["num_bins"], 2 * consts["num_bins"]],
        "derivatives": [2 * consts["num_bins"], p],
    }

    # -- assert MADE input-independence (the whole reason a head is a function
    #    of context alone) --------------------------------------------------
    with torch.no_grad():
        ctx = torch.randn(8, consts["context_features"])
        for head in HEADS:
            net = getattr(model, f"{head}_flow")._transform.autoregressive_net
            a = net(torch.zeros(8, 1), ctx)
            b = net(torch.randn(8, 1), ctx)
            if not torch.allclose(a, b, atol=1e-6):
                raise RuntimeError(f"{head} head depends on its input feature; export invalid")

    # -- export the four graphs -------------------------------------------- #
    dyn = {0: "batch"}
    torch.onnx.export(
        TrunkExport(model),
        (torch.randn(1, 1),),
        OUT_DIR / "trunk.onnx",
        input_names=["eGamma"],
        output_names=["trunk", "triplet_logit"],
        dynamic_axes={"eGamma": dyn, "trunk": dyn, "triplet_logit": dyn},
        opset_version=OPSET,
        dynamo=False,
    )
    for head in HEADS:
        flow = getattr(model, f"{head}_flow")
        torch.onnx.export(
            HeadExport(flow),
            (torch.randn(1, consts["context_features"]),),
            OUT_DIR / f"{head}_head.onnx",
            input_names=["context"],
            output_names=["params"],
            dynamic_axes={"context": dyn, "params": dyn},
            opset_version=OPSET,
            dynamo=False,
        )

    with open(OUT_DIR / "flow_constants.json", "w") as f:
        json.dump(consts, f, indent=2)
    write_constants_txt(consts)
    print(f"wrote {OUT_DIR}/ (trunk + 3 heads + flow_constants.{{json,txt}})")

    validate(model, consts)


def write_constants_txt(consts):
    """A whitespace-delimited copy of the constants for the C++ side to read.

    Format: one ``key`` per line followed by its value(s); ``recoil_mu`` and
    ``recoil_sigma`` carry two values (nuclear, triplet), everything else one.
    ``ConversionFlowInference`` parses this with a plain ``ifstream``, so the
    C++ needs no JSON dependency. Keep the two writers in step.
    """
    lines = [
        f"num_bins {consts['num_bins']}",
        f"tail_bound {consts['tail_bound']!r}",
        f"min_bin_width {consts['min_bin_width']!r}",
        f"min_bin_height {consts['min_bin_height']!r}",
        f"min_derivative {consts['min_derivative']!r}",
        f"param_scale {consts['param_scale']!r}",
        f"electron_mass {consts['electron_mass']!r}",
        f"context_features {consts['context_features']}",
        f"trunk_features {consts['trunk_features']}",
        f"recoil_mu {consts['recoil_mu'][0]!r} {consts['recoil_mu'][1]!r}",
        f"recoil_sigma {consts['recoil_sigma'][0]!r} {consts['recoil_sigma'][1]!r}",
        f"lead_mu {consts['lead_mu']!r}",
        f"lead_sigma {consts['lead_sigma']!r}",
        f"theta_mu {consts['theta_mu']!r}",
        f"theta_sigma {consts['theta_sigma']!r}",
    ]
    (OUT_DIR / "flow_constants.txt").write_text("\n".join(lines) + "\n")


# --------------------------------------------------------------------------- #
# Validation                                                                  #
# --------------------------------------------------------------------------- #


def validate(model, consts):
    import onnxruntime as ort

    consts_np = dict(consts)
    for k in ("recoil_mu", "recoil_sigma", "input_min", "input_max"):
        consts_np[k] = np.asarray(consts[k], dtype=np.float64)

    sessions = {
        "trunk": ort.InferenceSession(str(OUT_DIR / "trunk.onnx")),
        "recoil": ort.InferenceSession(str(OUT_DIR / "recoil_head.onnx")),
        "lead": ort.InferenceSession(str(OUT_DIR / "lead_head.onnx")),
        "theta": ort.InferenceSession(str(OUT_DIR / "theta_head.onnx")),
    }

    # (1) spline inverse: numpy vs nflows, over random contexts and z0 --------
    from nflows.transforms.splines.rational_quadratic import (
        unconstrained_rational_quadratic_spline as rqs,
    )

    nb = consts["num_bins"]
    scale = consts["param_scale"]
    rng = np.random.default_rng(0)
    ctx = rng.standard_normal((4096, consts["context_features"])).astype(np.float32)
    params = sessions["recoil"].run(None, {"context": ctx})[0]
    z0 = rng.uniform(-consts["tail_bound"] * 1.2, consts["tail_bound"] * 1.2, size=4096)
    mine = spline_inverse_numpy(params, z0, consts_np)
    with torch.no_grad():
        pt = torch.tensor(params)
        # nflows works with a feature axis: inputs (N,1), params (N,1,nb)
        ref, _ = rqs(
            torch.tensor(z0).unsqueeze(1),
            (pt[:, :nb] / scale).unsqueeze(1),
            (pt[:, nb : 2 * nb] / scale).unsqueeze(1),
            pt[:, 2 * nb :].unsqueeze(1),
            inverse=True,
            tails="linear",
            tail_bound=consts["tail_bound"],
        )
    ref = ref[:, 0].numpy()
    max_err = np.max(np.abs(mine - ref))
    print(f"[check] spline inverse vs nflows: max abs err = {max_err:.2e}")
    assert max_err < 1e-4, "spline inverse reimplementation disagrees with nflows"

    # (2) distribution: torch.sample vs the ONNX+numpy pipeline ---------------
    for e_val in (10.0, 1000.0, 50000.0):
        n = 200_000
        x = torch.full((n, 1), float(e_val))
        with torch.no_grad():
            ref_s = model.sample(x)
        my_s = sample_numpy(sessions, consts_np, np.full(n, e_val), np.random.default_rng(1))
        print(f"\n[check] eGamma = {e_val} MeV, N = {n}")
        for key in ("eLead", "eSub", "thetaLead", "eRecoil"):
            r = np.sort(np.asarray(ref_s[key]))
            m = np.sort(np.asarray(my_s[key]))
            qs = [0.1, 0.5, 0.9, 0.99]
            rq = np.quantile(r, qs)
            mq = np.quantile(m, qs)
            rel = np.max(np.abs(mq - rq) / (np.abs(rq) + 1e-9))
            flag = "OK" if rel < 0.05 else "**"
            print(f"   {key:10} quantiles ref={rq} mine={mq}  maxrel={rel:.3f} {flag}")
        tr_ref = float(np.mean(np.asarray(ref_s["isTriplet"])))
        tr_mine = float(np.mean(my_s["isTriplet"]))
        print(f"   triplet frac ref={tr_ref:.4f} mine={tr_mine:.4f}")

    print("\nvalidation done")


if __name__ == "__main__":
    main()
