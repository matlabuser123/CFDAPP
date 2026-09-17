#!/usr/bin/env python3
"""P12-MESH-003 -- generator of the committed multi-block cases.

Writes a complete case directory (case.json, geometry.json, mesh.json,
physics.json, boundaries.json, solver.json) in the production format
(docs/user_guide/case_format.md, "Multi-block meshes"):

  curved_channel   270-degree curved channel (annular bend r = 1..2), three
                   90-degree blocks -- the P12-MESH-003 quantitative benchmark
                   (exact fully developed solution, acceptance_gate.md)
  step_channel     L-shaped channel with a backward-facing step, three blocks
  obstacle_channel channel with a square internal solid (hole), eight blocks
  sector_conduction 270-degree annular sector, three blocks, pure conduction
                   (exact T linear in theta; heat crosses both interfaces)

Every block takes its vertices from ONE set of global grid lines, so the
vertices of a shared side are the same floating-point numbers in both blocks
(the multiblock format requires bitwise-identical interface vertices).
Run under Linux (glibc libm, as the C++ tests) for bit-reproducible output:

  python3 generate_multiblock_cases.py curved_channel <dir> [--nr 12 --nt 30]
"""
import argparse
import json
import math
import os
import re


def linspace(a, b, n):
    return [a + (b - a) * k / n for k in range(n + 1)]


def block(name, xs, ys):
    """Rectilinear block over grid lines xs (i) and ys (j)."""
    return {"name": name, "nx": len(xs) - 1, "ny": len(ys) - 1,
            "vertices": [[x, y] for y in ys for x in xs]}


def polar_block(name, rs, thetas):
    """Annular block: local i = radius (left = inner arc), j = angle."""
    return {"name": name, "nx": len(rs) - 1, "ny": len(thetas) - 1,
            "vertices": [[r * math.cos(t), r * math.sin(t)] for t in thetas for r in rs]}


def side(b, s):
    return {"block": b, "side": s}


def iface(b1, s1, b2, s2, orientation="aligned"):
    return {"first": side(b1, s1), "second": side(b2, s2), "orientation": orientation}


SOLVER = {
    "type": "SIMPLE",
    "max_iterations": 8000,
    "velocity_relaxation": 0.7,
    "pressure_relaxation": 0.3,
    "velocity_tolerance": 2e-5,
    "pressure_tolerance": 5e-4,
    "continuity_tolerance": 1e-6,
    "convection_scheme": "linear_upwind",
    "gradient_scheme": "green_gauss",
    "non_orthogonal_corrections": 1,
    "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                               "relative_tolerance": 1e-8, "max_iterations": 500},
    "pressure_linear_solver": {"type": "CG", "absolute_tolerance": 1e-10,
                               "relative_tolerance": 1e-8, "max_iterations": 5000},
}

WALL = {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0}}
OUTLET = {"velocity": {"type": "outlet"}, "pressure": {"type": "fixed_value", "value": 0.0}}


def inlet(u, v):
    return {"velocity": {"type": "inlet", "value": [u, v]},
            "pressure": {"type": "fixed_gradient", "value": 0.0}}


def annulus_lines(nr, nt, blocks):
    """Global radius and angle lines of the 270-degree bend (r = 1..2)."""
    rs = linspace(1.0, 2.0, nr)
    thetas = linspace(0.0, 1.5 * math.pi, blocks * nt)
    return rs, thetas


def curved_channel(nr, nt):
    rs, th = annulus_lines(nr, nt, 3)
    names = ["bend_a", "bend_b", "bend_c"]
    blocks = [polar_block(n, rs, th[k * nt:(k + 1) * nt + 1]) for k, n in enumerate(names)]
    mesh = {
        "type": "multiblock",
        "blocks": blocks,
        "interfaces": [iface("bend_a", "top", "bend_b", "bottom"),
                       iface("bend_b", "top", "bend_c", "bottom")],
        "patches": [
            {"name": "inlet", "sides": [side("bend_a", "bottom")]},
            {"name": "outlet", "sides": [side("bend_c", "top")]},
            {"name": "inner_wall", "sides": [side(n, "left") for n in names]},
            {"name": "outer_wall", "sides": [side(n, "right") for n in names]},
        ],
    }
    boundaries = {"patches": {"inlet": inlet(0.0, 1.0), "outlet": OUTLET,
                              "inner_wall": WALL, "outer_wall": WALL}}
    physics = {"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.1}
    cells = 3 * nr * nt
    description = (
        "P12-MESH-003 quantitative benchmark: 2D laminar flow through a 270-degree curved channel "
        "(annular bend, inner wall r1 = 1, outer wall r2 = 2, centred on the origin) built from "
        "three conformal 90-degree structured blocks (bend_a, bend_b, bend_c; block-local i = "
        "radius, j = angle; two interfaces at 90 and 180 degrees). Uniform inflow U = 1 through "
        "the theta = 0 section (x in [1, 2], y = 0, velocity [0, 1]); outlet p = 0 at theta = 270 "
        "degrees; no-slip inner and outer walls; rho = 1, mu = 0.1 (Re = U (r2 - r1) rho / mu = "
        "10). The fully developed flow is an exact Navier-Stokes solution: u_theta(r) = A (r ln r "
        "+ c1 r + c2 / r), u_r = 0, dp/dtheta = 2 mu A, dp/dr = rho u_theta^2 / r, with A fixed "
        "by the flow rate U (r2 - r1). Mesh " + str(nr) + " x " + str(nt) + " per block (" +
        str(cells) + " cells). See results/p12-mesh-003/acceptance_gate.md.")
    return {"name": "Curved Channel 270 deg (3-block multiblock)", "description": description,
            "mesh": mesh, "boundaries": boundaries, "physics": physics, "solver": SOLVER}


def step_channel(n):
    # n cells per half height (0.5); dx = 2 dy.
    xs_up = linspace(0.0, 2.0, 2 * n)
    xs_down = linspace(2.0, 14.0, 12 * n)
    ys_low = linspace(0.0, 0.5, n)
    ys_up = linspace(0.5, 1.0, n)
    mesh = {
        "type": "multiblock",
        "blocks": [block("upstream", xs_up, ys_up), block("upper", xs_down, ys_up),
                   block("lower", xs_down, ys_low)],
        "interfaces": [iface("upstream", "right", "upper", "left"),
                       iface("lower", "top", "upper", "bottom")],
        "patches": [
            {"name": "inlet", "sides": [side("upstream", "left")]},
            {"name": "outlet", "sides": [side("upper", "right"), side("lower", "right")]},
            {"name": "top_wall", "sides": [side("upstream", "top"), side("upper", "top")]},
            {"name": "bottom_wall", "sides": [side("upstream", "bottom"), side("lower", "bottom")]},
            {"name": "step", "sides": [side("lower", "left")]},
        ],
    }
    boundaries = {"patches": {"inlet": inlet(1.0, 0.0), "outlet": OUTLET, "top_wall": WALL,
                              "bottom_wall": WALL, "step": WALL}}
    physics = {"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01}
    cells = 2 * n * n + 2 * 12 * n * n
    description = (
        "P12-MESH-003 geometry / boundary-condition regression case: an L-shaped channel with a "
        "backward-facing step, built from three conformal structured blocks -- upstream "
        "[0, 2] x [0.5, 1], upper [2, 14] x [0.5, 1], lower [2, 14] x [0, 0.5] (the step face "
        "x = 2, y in [0, 0.5] is the 'step' wall patch; the region x < 2, y < 0.5 is not part of "
        "the domain). Uniform inflow U = 1 on the upstream channel (height 0.5), outlet p = 0 over "
        "both downstream blocks, no-slip walls; rho = 1, mu = 0.01 (Re = U h / nu = 50 on the "
        "inlet height). Checks: conservation, a recirculation zone behind the step, and the exact "
        "Poiseuille profile u = 3 y (1 - y) (mean 0.5) far downstream. " + str(cells) + " cells.")
    return {"name": "Step Channel (3-block multiblock)", "description": description,
            "mesh": mesh, "boundaries": boundaries, "physics": physics, "solver": SOLVER}


def obstacle_channel(n):
    # n cells per 1/16 unit: x lines 0 | 2 | 2.5 | 6, y lines 0 | .375 | .625 | 1.
    xs = [linspace(0.0, 2.0, 32 * n), linspace(2.0, 2.5, 8 * n), linspace(2.5, 6.0, 56 * n)]
    ys = [linspace(0.0, 0.375, 6 * n), linspace(0.375, 0.625, 4 * n), linspace(0.625, 1.0, 6 * n)]
    names = {(0, 0): "sw", (1, 0): "s", (2, 0): "se", (0, 1): "w", (2, 1): "e",
             (0, 2): "nw", (1, 2): "n", (2, 2): "ne"}
    blocks = [block(names[(i, j)], xs[i], ys[j]) for j in range(3) for i in range(3)
              if (i, j) in names]
    interfaces = [iface("sw", "right", "s", "left"), iface("s", "right", "se", "left"),
                  iface("nw", "right", "n", "left"), iface("n", "right", "ne", "left"),
                  iface("sw", "top", "w", "bottom"), iface("w", "top", "nw", "bottom"),
                  iface("se", "top", "e", "bottom"), iface("e", "top", "ne", "bottom")]
    mesh = {
        "type": "multiblock",
        "blocks": blocks,
        "interfaces": interfaces,
        "patches": [
            {"name": "inlet", "sides": [side("sw", "left"), side("w", "left"), side("nw", "left")]},
            {"name": "outlet",
             "sides": [side("se", "right"), side("e", "right"), side("ne", "right")]},
            {"name": "bottom_wall",
             "sides": [side("sw", "bottom"), side("s", "bottom"), side("se", "bottom")]},
            {"name": "top_wall", "sides": [side("nw", "top"), side("n", "top"), side("ne", "top")]},
            {"name": "obstacle", "sides": [side("s", "top"), side("e", "left"),
                                           side("n", "bottom"), side("w", "right")]},
        ],
    }
    boundaries = {"patches": {"inlet": inlet(1.0, 0.0), "outlet": OUTLET, "bottom_wall": WALL,
                              "top_wall": WALL, "obstacle": WALL}}
    physics = {"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.05}
    cells = sum(b["nx"] * b["ny"] for b in blocks)
    description = (
        "P12-MESH-003 internal-solid case: channel [0, 6] x [0, 1] with a square solid obstacle "
        "[2, 2.5] x [0.375, 0.625] that is NOT part of the mesh -- eight conformal structured "
        "blocks surround it (a multiply connected domain; the obstacle's four sides form the "
        "'obstacle' wall patch). Uniform inflow U = 1, outlet p = 0, no-slip channel walls; rho = "
        "1, mu = 0.05 (Re = 20 on the channel height, 5 on the obstacle). Checks: no cell inside "
        "the obstacle, conservation, mirror symmetry about y = 0.5. " + str(cells) + " cells.")
    solver = dict(SOLVER)
    return {"name": "Obstacle Channel (8-block multiblock)", "description": description,
            "mesh": mesh, "boundaries": boundaries, "physics": physics, "solver": solver}


def sector_conduction(nr, nt):
    rs, th = annulus_lines(nr, nt, 3)
    names = ["sector_a", "sector_b", "sector_c"]
    blocks = [polar_block(n, rs, th[k * nt:(k + 1) * nt + 1]) for k, n in enumerate(names)]
    mesh = {
        "type": "multiblock",
        "blocks": blocks,
        "interfaces": [iface("sector_a", "top", "sector_b", "bottom"),
                       iface("sector_b", "top", "sector_c", "bottom")],
        "patches": [
            {"name": "hot_end", "sides": [side("sector_a", "bottom")]},
            {"name": "cold_end", "sides": [side("sector_c", "top")]},
            {"name": "inner_arc", "sides": [side(n, "left") for n in names]},
            {"name": "outer_arc", "sides": [side(n, "right") for n in names]},
        ],
    }

    def patch(temperature):
        p = dict(WALL)
        p["temperature"] = temperature
        return p

    boundaries = {"patches": {
        "hot_end": patch({"type": "fixed_temperature", "value": 1.0}),
        "cold_end": patch({"type": "fixed_temperature", "value": 0.0}),
        "inner_arc": patch({"type": "adiabatic"}),
        "outer_arc": patch({"type": "adiabatic"})}}
    physics = {"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 1.0,
               "thermal": {"conductivity": 1.0, "specific_heat": 1.0,
                           "initial_temperature": 0.5}}
    cells = 3 * nr * nt
    description = (
        "P12-MESH-003 interface-conservation case for a transported scalar: steady conduction in "
        "a 270-degree annular sector (r = 1..2) from three conformal 90-degree blocks; T = 1 on "
        "the theta = 0 end, T = 0 on the theta = 270 degree end, adiabatic arcs, fluid at rest. "
        "Exact solution T = 1 - theta / (3 pi / 2); the heat flow through every radial section "
        "(including both block interfaces) is k ln(r2 / r1) / (3 pi / 2) per unit depth. " +
        str(cells) + " cells.")
    solver = dict(SOLVER)
    solver["max_iterations"] = 200
    # Exactly the two-point face flux coefficient * dT on every face (the
    # annular faces are orthogonal up to round-off), so heat flows through
    # the interfaces can be accounted exactly.
    solver["non_orthogonal_corrections"] = 0
    return {"name": "Annular Sector Conduction (3-block multiblock)", "description": description,
            "mesh": mesh, "boundaries": boundaries, "physics": physics, "solver": solver}


def write_case(directory, case):
    os.makedirs(directory, exist_ok=True)

    def dump(name, obj):
        text = json.dumps(obj, indent=2)
        # One [x, y] vertex per line.
        text = re.sub(r"\[\s*(-?[0-9.eE+-]+),\s*(-?[0-9.eE+-]+)\s*\]", r"[\1, \2]", text)
        # One {"block": ..., "side": ...} reference per line.
        text = re.sub(r"\{\s*\"block\": (\"[^\"]*\"),\s*\"side\": (\"[^\"]*\")\s*\}",
                      r'{"block": \1, "side": \2}', text)
        with open(os.path.join(directory, name), "w", newline="\n") as f:
            f.write(text + "\n")

    dump("case.json", {"name": case["name"], "description": case["description"],
                       "format_version": 1, "geometry": "geometry.json", "mesh": "mesh.json",
                       "physics": "physics.json", "boundaries": "boundaries.json",
                       "solver": "solver.json"})
    dump("geometry.json", {"type": "mesh_defined"})
    dump("mesh.json", case["mesh"])
    dump("physics.json", case["physics"])
    dump("boundaries.json", case["boundaries"])
    dump("solver.json", case["solver"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("case", choices=["curved_channel", "step_channel", "obstacle_channel",
                                         "sector_conduction"])
    parser.add_argument("directory")
    parser.add_argument("--nr", type=int, default=12)
    parser.add_argument("--nt", type=int, default=30)
    parser.add_argument("--n", type=int, default=None)
    args = parser.parse_args()
    if args.case == "curved_channel":
        case = curved_channel(args.nr, args.nt)
    elif args.case == "sector_conduction":
        case = sector_conduction(args.nr, args.nt)
    elif args.case == "step_channel":
        case = step_channel(args.n or 8)
    else:
        case = obstacle_channel(args.n or 1)
    write_case(args.directory, case)


if __name__ == "__main__":
    main()
