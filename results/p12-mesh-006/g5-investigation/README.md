# P12-MESH-006 — investigation of the failed G5.1 / G6.2 square-duct thresholds

**Evidence only.** No solver, test, gate or case change was made. [acceptance_gate.md](../acceptance_gate.md)
and the original failed results are unchanged (§15, sha256 before and after). MESH-006 remains
`[ ]` **BLOCKED / FAILED GATE**. MESH-007 was not started. Nothing was committed or pushed.

**Classification: D — PRE-REGISTERED THRESHOLD DESIGN DEFECT** (§12).

The failed values, as measured by the gate:

| item | measured | pre-registered limit |
| --- | --- | --- |
| n = 16, velocity L∞/U | 0.023357 | 0.020 |
| n = 24, velocity L∞/U | 0.010577 | 0.010 |
| n = 24, velocity RMS/U | 0.0052021 | 0.005 |

They are the exact discretization error of CFDApp's cell-centred finite-volume scheme on this
problem. An independent solver of the same discrete equations reproduces them to 7–8 significant digits
(CFDApp − discrete ≤ 8.1e-8 U). The analytical reference, the error norms and the benchmark
configuration are all verified. The error converges at order 2 and lies on the asymptotic curve.
The limits sit below that curve because the derivation in G5.1 carried the 2D channel error over
to the square duct without deriving the duct's own velocity error, which is 4.1× the channel's
(§10.4).

---

## 0. Method and independence

| tool (tools/) | computes | uses CFDApp? |
| --- | --- | --- |
| `duct_reference.py` | analytical truth: two independent closed forms (double sine series; single series, 20 001 terms), exact cell averages, K, G, u_max, flow rate by quadrature, PDE/BC residuals | no |
| `duct_discrete.py` | the fully developed discrete problem, solved exactly (1D eigen-decomposition, cross-checked by a sparse direct solve), n = 8 … 256 | no |
| `channel_1d_reference.py` | the same scheme on the 2D channel (the gate derivation's premise) | no |
| `gen_duct_case.py`, `run_cli_ducts.sh` | production CLI runs of cases/duct_3d at n = 8, 12, 16, 24, 32 (x) and n = 16 (y, z), plus audit variants | runs the CLI |
| `compare_cfdapp.py` | Python norms of the CLI's fields.csv (17 digits) against truth, cell averages, the discrete solution and the gate's C++ numbers; the x/y/z comparison | reads CLI output |
| `analysis.py` | refinement tables, observed orders, Richardson/GCI (Celik et al. 2008), required resolution, grid-location decomposition, truncation error | reads the above |
| `run_analysis.sh` | regenerates data/*.json, data/analysis.md and logs/*.log | — |

CLI binary: `build/release/apps/cli/cfdapp`, sha256 `3ce47396…43a51f8a4` (every logs/cli_*.log),
not rebuilt. It was built from the same solver sources as the frozen gate binary. The only later
source edit is the CaseReader refusal order for invalid 3D cases. The CLI reproduces the gate's
ProjectRunner runs: same iteration counts, u_max equal to the bit, dp/dx within 3e-15 (§3).

## 1. The failed values and where they come from

- The gate measures the error of CFDApp's cell-centre streamwise velocity on the cell plane at
  x = 4a − h/2 of a 6a-long duct. The reference is the exact fully developed solution at the cell
  centres (A2.2).
- That plane is fully developed to ≤ 8.1e-8 U: the axis-velocity variation over x ∈ [3a, 4.5a] is
  1.6e-6, and the downstream plane agrees to 6 digits.
- There, every cell's momentum balance reduces to the cross-section finite-volume Poisson problem
  of CFDApp's diffusion discretization, with the flow rate fixed by mass conservation.
- So the gate measures the discretization error of that scheme, and nothing else.
- An independent exact solution of the same discrete equations gives:
  - n = 16: L∞ 0.023357;
  - n = 24: L∞ 0.010577 and RMS 0.0052021.

  These are the failed values to 5 digits. CFDApp's own errors differ from them by < 1e-7.

## 2. Analytical reference verification (item 3) — [logs/reference.log](logs/reference.log)

| check | result |
| --- | --- |
| K = Qμ/(Ga⁴), single series (mpmath, 40 digits) | 0.035144253738788 |
| K, double series m, n ≤ 399 (the gate) | 0.035144253311625 (rel. 1.2e-8) |
| G = −dp/dx = Uμ/(Ka²) | 2.845415376956 |
| Fanning f·Re = 1/(2K), D_h = a | 14.227077 (Shah & London 1978: 14.227); Darcy 56.908 (56.91) |
| u_max = u(a/2, a/2) | 2.096256014684 (Shah & London: 2.0962); double series 399: rel. 2.7e-8 |
| Reynolds number | Re = ρUa/μ = 10; D_h = 4A/P = a, so Re_Dh = 10 (reference length a = D_h, reference velocity U = mean velocity) |
| mean velocity / flow rate (400×400 Gauss–Legendre of the single series) | 1.00000000000004, i.e. Q = ρUa² = 1 |
| PDE: \|∇²u + G/μ\|/(G/μ) at 40 random points (4th-order FD) | 1.7e-10 (FD round-off) |
| walls: max \|u\| on y, z = 0, a; symmetry u(y,z) = u(z,y) | 6.0e-13; 7.7e-13 |
| coordinates (CFDApp fields.csv) | cell centres from h/2 to a − h/2 in y and z; walls at 0 and a; origin at the duct corner (0, 0) of the cross-section, as in the reference |
| truncation of the gate's reference (double series 399 vs 20 001-term truth, at the cell centres) | ≤ 2.9e-7 (n = 8), 4.4e-8 (16), 5.1e-7 (24), 9.2e-7 (32): negligible against 1e-2 errors |

**Quantities compared.** For a cell of a cell-centred FV solution there are four candidate
"analytical values":

- the **cell-centre point value** u(y_c, z_c), which the gate uses;
- the **cell average** (1/h²)∬_cell u, computed exactly here by integrating the single series;
- the **face value**: for the x-normal faces that carry the section mass flow, the exact face
  average equals the cell average of the cross-section, because the flow is fully developed;
- a **pointwise value elsewhere**, e.g. the axis value u(a/2, a/2) for u_max (A2.3: the bilinear
  interpolant of the four axis cells against the exact axis value).

In the developed region the discrete x-face flux is ρh²(u_P + u_N)/2 plus a Rhie–Chow term that
is zero for linear pressure, so it equals ρh²u_P. The cell-average comparison is therefore also
the face-flux (mass-flux) comparison. The centre value and the cell average differ by
(h²/24)∇²u + O(h⁴) = −(h²/24)(G/μ), a near-uniform shift (§8):

| n | mean(point values) − U | (h²/24)(G/μ) | mean(cell averages) − U |
| --- | --- | --- | --- |
| 8 | +1.848e-2 | 1.852e-2 | 0 (±2e-16) |
| 16 | +4.628e-3 | 4.631e-3 | 0 |
| 24 | +2.058e-3 | 2.058e-3 | 0 |
| 32 | +1.158e-3 | 1.158e-3 | 0 |

The point values at the cell centres carry a flow rate 0.46 % (n = 16) and 0.21 % (n = 24) above
the true one. A mass-conserving discrete solution cannot match them exactly.

## 3. Norm verification (item 4) — [logs/compare.log](logs/compare.log)

The gate's C++ norms (`tests/integration/case/test_3d_production_cases.cpp:346`, `measureDuct`):

- L∞ = max over the n² cells of the measurement plane of |u − u_exact|/U.
- RMS = √(Σe²/n²): all cross-section cells have the same area h², so this is the area-weighted L2.
- The reference is the cell-centre point value. There are no boundary, ghost or wall values; only
  cell-centre values are used.
- U = 1 is the inlet and mean velocity, so the norms are dimensionless.
- Samples: n² per plane. The plane is x = 4a − h/2 (A2.2).
- The measurement also defines u_max (A2.3) and dp/dx (the least-squares slope of the plane-mean
  pressure over x ∈ [2.5a, 4.5a]).

The independent Python norms of the same CLI solution:

| n | iterations CLI / gate | L∞ Python (truth) | L∞ gate C++ | rel. diff, same reference (D399) | RMS Python (truth) | RMS gate C++ | rel. diff, same reference | dp/dx rel. diff | u_max rel. diff |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8 | 67 / 67 | 8.49967165e-02 | 8.49967998e-02 | 3.7e-14 | 4.19097947e-02 | 4.19098097e-02 | 1.6e-13 | 1.7e-15 | 0.0e+00 |
| 16 | 183 / 183 | 2.33567039e-02 | 2.33567293e-02 | 1.1e-12 | 1.14830395e-02 | 1.14830553e-02 | 1.5e-12 | 3.3e-15 | 0.0e+00 |
| 24 | 362 / 362 | 1.05773206e-02 | 1.05773173e-02 | 1.2e-12 | 5.20206593e-03 | 5.20210579e-03 | 3.0e-12 | 1.7e-14 | 0.0e+00 |

With the gate's own reference (the 399-term series with G from its own truncated K), Python and
C++ agree to 1e-12 relative or better, i.e. numerical precision. Against the 20 001-term truth
they differ by about 1e-6 relative. That is the gate reference's G normalization (K₃₉₉ is 1.2e-8
off), not a norm error, and it is 10⁴ below the margins in question.

## 4. CFDApp vs analytical vs independent discrete (item 5) — [data/analysis.md](data/analysis.md)

| n | source | L∞ (point) | RMS = L2 (point) | L1 (point) | u_max err | dp/dx (G) err | mean u (plane) | mass flow in / out |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8 | CFDApp | 8.49967e-02 | 4.19098e-02 | 3.56883e-02 | 6.69071e-02 | 5.55121e-02 | 1.0000000001 | 1.000000000000 / 1.000000000002 |
| 8 | discrete | 8.49967e-02 | 4.19098e-02 | 3.56882e-02 | 6.69071e-02 | 5.55134e-02 | 1.0000000000 | 1 (imposed) |
| 8 | CFDApp − discrete | max\|Δu\| 3.55e-08 (downstream plane 2.04e-08; planes x∈[3,4.5] 2.31e-06) | | | Δu_max 1.70e-08 | ΔG 1.39e-06 | | |
| 12 | CFDApp | 4.04838e-02 | 1.99068e-02 | 1.67542e-02 | 3.10654e-02 | 2.57817e-02 | 1.0000000000 | 1.000000000000 / 1.000000000004 |
| 12 | discrete | 4.04838e-02 | 1.99068e-02 | 1.67542e-02 | 3.10654e-02 | 2.57832e-02 | 1.0000000000 | 1 (imposed) |
| 12 | CFDApp − discrete | max\|Δu\| 5.92e-08 (downstream plane 4.29e-08; planes x∈[3,4.5] 3.10e-06) | | | Δu_max 2.82e-08 | ΔG 1.49e-06 | | |
| 16 | CFDApp | 2.33567e-02 | 1.14830e-02 | 9.53078e-03 | 1.77605e-02 | 1.47420e-02 | 1.0000000000 | 1.000000000000 / 1.000000000003 |
| 16 | discrete | 2.33566e-02 | 1.14830e-02 | 9.53077e-03 | 1.77605e-02 | 1.47435e-02 | 1.0000000000 | 1 (imposed) |
| 16 | CFDApp − discrete | max\|Δu\| 6.38e-08 (downstream plane 5.11e-08; planes x∈[3,4.5] 3.39e-06) | | | Δu_max 3.04e-08 | ΔG 1.54e-06 | | |
| 24 | CFDApp | 1.05773e-02 | 5.20207e-03 | 4.35890e-03 | 7.98965e-03 | 6.63232e-03 | 1.0000000000 | 1.000000000000 / 1.000000000004 |
| 24 | discrete | 1.05772e-02 | 5.20205e-03 | 4.35890e-03 | 7.98962e-03 | 6.63384e-03 | 1.0000000000 | 1 (imposed) |
| 24 | CFDApp − discrete | max\|Δu\| 7.08e-08 (downstream plane 6.27e-08; planes x∈[3,4.5] 3.55e-06) | | | Δu_max 3.38e-08 | ΔG 1.53e-06 | | |
| 32 | CFDApp | 5.99034e-03 | 2.94702e-03 | 2.47456e-03 | 4.51400e-03 | 3.74685e-03 | 1.0000000000 | 1.000000000000 / 0.999999999988 |
| 32 | discrete | 5.99026e-03 | 2.94700e-03 | 2.47455e-03 | 4.51397e-03 | 3.74835e-03 | 1.0000000000 | 1 (imposed) |
| 32 | CFDApp − discrete | max\|Δu\| 8.08e-08 (downstream plane 7.51e-08; planes x∈[3,4.5] 3.58e-06) | | | Δu_max 3.85e-08 | ΔG 1.51e-06 | | |
| 48 | discrete | 2.67568e-03 | 1.31679e-03 | 1.10409e-03 | 2.01272e-03 | 1.67149e-03 | 1.0000000000 | 1 (imposed) |
| 64 | discrete | 1.50778e-03 | 7.42151e-04 | 6.22815e-04 | 1.13348e-03 | 9.41347e-04 | 1.0000000000 | 1 (imposed) |
| 96 | discrete | 6.71013e-04 | 3.30330e-04 | 2.77191e-04 | 5.04200e-04 | 4.18749e-04 | 1.0000000000 | 1 (imposed) |
| 128 | discrete | 3.77624e-04 | 1.85911e-04 | 1.56002e-04 | 2.83700e-04 | 2.35622e-04 | 1.0000000000 | 1 (imposed) |
| 192 | discrete | 1.67892e-04 | 8.26604e-05 | 6.93614e-05 | 1.26117e-04 | 1.04746e-04 | 1.0000000000 | 1 (imposed) |
| 256 | discrete | 9.44509e-05 | 4.65033e-05 | 3.90213e-05 | 7.09468e-05 | 5.89244e-05 | 1.0000000000 | 1 (imposed) |

## 5. Refinement table (independent discrete, every grid)

Exact solution of the discrete equations (`tools/duct_discrete.py`) vs the analytical truth. The eigen-decomposition and a sparse direct solve agree to ≤ 7e-14 (n ≤ 64, logs/discrete.log).

| n | L∞ point | RMS point | L1 point | L∞ cell-average | RMS cell-average | \|G err\|/G | \|u_max err\|/u_max | u_axis | G |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8 | 8.49967e-02 | 4.19098e-02 | 3.56882e-02 | 6.64930e-02 | 3.75869e-02 | 5.55134e-02 | 6.69071e-02 | 1.95600154 | 2.68745658 |
| 12 | 4.04838e-02 | 1.99068e-02 | 1.67542e-02 | 3.22547e-02 | 1.81220e-02 | 2.57832e-02 | 3.10654e-02 | 2.03113504 | 2.77205155 |
| 16 | 2.33566e-02 | 1.14830e-02 | 9.53077e-03 | 1.87267e-02 | 1.05067e-02 | 1.47435e-02 | 1.77605e-02 | 2.05902554 | 2.80346388 |
| 24 | 1.05772e-02 | 5.20205e-03 | 4.35890e-03 | 8.51919e-03 | 4.77725e-03 | 6.63384e-03 | 7.98962e-03 | 2.07950773 | 2.82653934 |
| 32 | 5.99026e-03 | 2.94700e-03 | 2.47455e-03 | 4.83254e-03 | 2.70994e-03 | 3.74835e-03 | 4.51397e-03 | 2.08679359 | 2.83474975 |
| 48 | 2.67568e-03 | 1.31679e-03 | 1.10409e-03 | 2.16111e-03 | 1.21206e-03 | 1.67149e-03 | 2.01272e-03 | 2.09203685 | 2.84065930 |
| 64 | 1.50778e-03 | 7.42151e-04 | 6.22815e-04 | 1.21834e-03 | 6.83370e-04 | 9.41347e-04 | 1.13348e-03 | 2.09387996 | 2.84273685 |
| 96 | 6.71013e-04 | 3.30330e-04 | 2.77191e-04 | 5.42369e-04 | 3.04249e-04 | 4.18749e-04 | 5.04200e-04 | 2.09519908 | 2.84422386 |
| 128 | 3.77624e-04 | 1.85911e-04 | 1.56002e-04 | 3.05262e-04 | 1.71249e-04 | 2.35622e-04 | 2.83700e-04 | 2.09566131 | 2.84474494 |
| 192 | 1.67892e-04 | 8.26604e-05 | 6.93614e-05 | 1.35730e-04 | 7.61470e-05 | 1.04746e-04 | 1.26117e-04 | 2.09599164 | 2.84511733 |
| 256 | 9.44509e-05 | 4.65033e-05 | 3.90213e-05 | 7.63602e-05 | 4.28401e-05 | 5.89244e-05 | 7.09468e-05 | 2.09610729 | 2.84524771 |

## 6. Observed convergence orders (item 6)

| discrete | 8→12 | 12→16 | 16→24 | 24→32 | 32→48 | 48→64 | 64→96 | 96→128 | 128→192 | 192→256 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Linf point | 1.829 | 1.912 | 1.954 | 1.976 | 1.988 | 1.994 | 1.997 | 1.998 | 1.999 | 2.000 |
| RMS point | 1.836 | 1.913 | 1.953 | 1.975 | 1.987 | 1.993 | 1.996 | 1.998 | 1.999 | 1.999 |
| L1 point | 1.865 | 1.961 | 1.929 | 1.968 | 1.990 | 1.990 | 1.997 | 1.998 | 1.999 | 2.000 |
| Linf average | 1.784 | 1.890 | 1.943 | 1.971 | 1.985 | 1.992 | 1.996 | 1.998 | 1.999 | 1.999 |
| RMS average | 1.799 | 1.895 | 1.944 | 1.971 | 1.984 | 1.992 | 1.996 | 1.998 | 1.999 | 1.999 |
| G error | 1.891 | 1.943 | 1.970 | 1.984 | 1.992 | 1.996 | 1.998 | 1.999 | 1.999 | 2.000 |
| u_max error | 1.892 | 1.944 | 1.970 | 1.985 | 1.992 | 1.996 | 1.998 | 1.999 | 1.999 | 2.000 |

| CFDApp | 8→12 | 12→16 | 16→24 | 24→32 |
| --- | --- | --- | --- | --- |
| Linf point | 1.829 | 1.912 | 1.954 | 1.976 |
| RMS point | 1.836 | 1.913 | 1.953 | 1.975 |
| L1 point | 1.865 | 1.961 | 1.929 | 1.968 |
| Linf average | 1.784 | 1.890 | 1.943 | 1.971 |
| RMS average | 1.799 | 1.895 | 1.944 | 1.971 |
| G error | 1.891 | 1.943 | 1.970 | 1.985 |
| u_max error | 1.892 | 1.944 | 1.970 | 1.985 |

## 7. Richardson / GCI (item 7) and required resolution

| study | quantity | p | φ_ext | true err of φ_ext | fine-grid true err | GCI_fine | GCI_coarse | asymptotic indicator | exact inside GCI_fine |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| CFDApp 8/16/24 (gate grids) | G | 1.881 | 2.84672189 | 4.59e-04 | 6.632e-03 | 8.924e-03 | 1.929e-02 | 1.0082 | yes |
| CFDApp 8/16/24 (gate grids) | u_axis | 1.881 | 2.09740640 | 5.49e-04 | 7.990e-03 | 1.076e-02 | 2.330e-02 | 1.0099 | yes |
| CFDApp 8/16/32 | G | 1.891 | 2.84630713 | 3.13e-04 | 3.747e-03 | 5.094e-03 | 1.910e-02 | 1.0112 | yes |
| CFDApp 8/16/32 | u_axis | 1.891 | 2.09703939 | 3.74e-04 | 4.514e-03 | 6.137e-03 | 2.308e-02 | 1.0135 | yes |
| CFDApp 16/24/32 | G | 1.954 | 2.84563515 | 7.72e-05 | 3.747e-03 | 4.798e-03 | 8.443e-03 | 1.0029 | yes |
| CFDApp 16/24/32 | u_axis | 1.955 | 2.09644492 | 9.01e-05 | 4.514e-03 | 5.781e-03 | 1.018e-02 | 1.0035 | yes |
| discrete 8/16/24 | G | 1.880 | 2.84671770 | 4.58e-04 | 6.634e-03 | 8.924e-03 | 1.929e-02 | 1.0082 | yes |
| discrete 8/16/24 | u_axis | 1.881 | 2.09740648 | 5.49e-04 | 7.990e-03 | 1.076e-02 | 2.330e-02 | 1.0099 | yes |
| discrete 16/32/64 | G | 1.970 | 2.84547493 | 2.09e-05 | 9.413e-04 | 1.204e-03 | 4.729e-03 | 1.0028 | yes |
| discrete 16/32/64 | u_axis | 1.970 | 2.09630803 | 2.48e-05 | 1.133e-03 | 1.450e-03 | 5.699e-03 | 1.0034 | yes |
| discrete 32/64/128 | G | 1.992 | 2.84541936 | 1.40e-06 | 2.356e-04 | 2.963e-04 | 1.180e-03 | 1.0007 | yes |
| discrete 32/64/128 | u_axis | 1.992 | 2.09625946 | 1.64e-06 | 2.837e-04 | 3.568e-04 | 1.421e-03 | 1.0009 | yes |
| discrete 64/128/256 | G | 1.998 | 2.84541564 | 9.32e-08 | 5.892e-05 | 7.378e-05 | 2.947e-04 | 1.0002 | yes |
| discrete 64/128/256 | u_axis | 1.998 | 2.09625624 | 1.08e-07 | 7.095e-05 | 8.883e-05 | 3.549e-04 | 1.0002 | yes |

### Required resolution

| limit | metric | smallest n | smallest even n (the gate's u_max needs even n) | value there |
| --- | --- | --- | --- | --- |
| linf_020 | point | 18 | 18 | 1.85843e-02 |
| linf_010 | point | 25 | 26 | 9.03312e-03 |
| rms_005 | point | 25 | 26 | 4.44302e-03 |
| linf_020 | average | 16 | 16 | 1.87267e-02 |
| linf_010 | average | 23 | 24 | 8.51919e-03 |
| rms_005 | average | 24 | 24 | 4.77725e-03 |

Asymptotic power-law fit E = C h^p (discrete, point metric, n = 64…256): L∞: p = 1.9984, C = 6.1381, n(L∞ ≤ 0.020) = 17.56, n(L∞ ≤ 0.010) = 24.84; RMS: p = 1.9982, C = 3.0189, n(RMS ≤ 0.005) = 24.64.

## 8. Grid-location hypothesis (item 8)

The full per-grid table is in [data/analysis.md](data/analysis.md) (section "Grid location"). All
of it uses the independent discrete solution, which is identical to CFDApp's to ≤ 8.1e-8.

**Near-wall sampling does not cause the miss.**

- The L∞ error sits at the **duct axis** on every grid: the four axis-adjacent cells, n/2 − 1
  cells from the nearest wall. It is the core of the flattened discrete profile (−0.0234 U at
  n = 16).
- The wall-adjacent ring's largest error is only 41 % (n = 16) to 49 % (n = 32) of it, and has the
  opposite sign (+0.0087 U at n = 16).
- The ring (60 of 256 cells at n = 16) carries 15 % of Σe² (13 % at n = 24). The interior RMS
  (1.21e-2) is larger than the ring RMS (0.91e-2).
- [logs/grid_location_ring_share.log](logs/grid_location_ring_share.log)

**What is a sampling effect is uniform, not wall-localized.** The cell-centre point value exceeds
the cell average by (h²/24)(G/μ), nearly uniformly (§2). So the point-value error carries a
constant offset: mean(u − point) = −4.628e-3 at n = 16 and −2.058e-3 at n = 24, against
(h²/24)(G/μ) = 4.631e-3 and 2.058e-3. The cell-average error has mean 0 (both carry the exact flow
rate), and its RMS equals the RMS of (point error − its mean) to 4 digits (1.0507e-2 vs 1.0509e-2
at n = 16).

| n | L∞ point | L∞ average | RMS point | RMS average | against the limits |
| --- | --- | --- | --- | --- | --- |
| 16 | 0.023357 | 0.018727 | 0.011483 | 0.010507 | L∞ ≤ 0.020: point fails, average passes |
| 24 | 0.010577 | 0.008519 | 0.0052021 | 0.0047772 | L∞ ≤ 0.010, RMS ≤ 0.005: point fails, average passes |

**Conclusion.** The miss is not caused by where the cell-centre values sit relative to the walls.
Its size depends on a legitimate O(h²) choice of reference quantity: point versus cell average,
where the cell average is also the mass-flux-consistent one. The metric is not changed here (the
investigation brief and the stop rules forbid it).

## 9. Directional symmetry (item 9)

The original gate result is preserved:
[../logs/06_g6_directional_symmetry.log](../logs/06_g6_directional_symmetry.log) and
[../data/duct3d_symmetry_gate.txt](../data/duct3d_symmetry_gate.txt), unchanged (§15). Here the n = 16
duct was re-run along x, y and z through the production CLI (logs/cli_x16, cli_y16, cli_z16).
Every cell was compared in canonical coordinates by the independent Python code, not by the gate's
C++:

| pair | max \|Δ streamwise\|/U | max \|Δ cross 1\|/U | max \|Δ cross 2\|/U | max \|Δp\|/(p_max − p_min) | \|ΔG\|/G |
| --- | --- | --- | --- | --- | --- |
| x vs y | 2.346e-12 | 2.106e-13 | 2.085e-13 | 1.736e-11 | 2.253e-11 |
| x vs z | 3.436e-12 | 1.156e-13 | 1.156e-13 | 2.127e-11 | 6.692e-11 |

| duct | L∞ (point) | RMS (point) | dp/ds error | u_max error | CFDApp − discrete max\|Δu\| |
| --- | --- | --- | --- | --- | --- |
| x | 2.33567039e-02 | 1.14830395e-02 | 1.47420253e-02 | 1.77604922e-02 | 6.38e-08 |
| y | 2.33567039e-02 | 1.14830395e-02 | 1.47420253e-02 | 1.77604922e-02 | 6.38e-08 |
| z | 2.33567039e-02 | 1.14830395e-02 | 1.47420254e-02 | 1.77604922e-02 | 6.38e-08 |

- The failed n = 16 value (L∞ 0.0233567 U) occurs identically in all three orientations, to 9
  printed digits.
- The three fields agree to 3.4e-12 (streamwise), 2.1e-13 (cross-flow) and 2.1e-11 (pressure /
  range), which is round-off and Krylov-path level.
- This reproduces the gate's C++ G6.1 values exactly.
- A defect specific to w, to the z-direction, or to any one axis would make these differ at O(h²)
  (≈ 1e-2) or at O(1). So the directional result also rules out an axis-specific implementation
  defect.

## 10. Numerical implementation audit (item 10)

### 10.1 Code review (nothing rewritten)

| item | where | finding |
| --- | --- | --- |
| momentum diffusion coefficients | `src/physics/MomentumEquation.cpp:107-150` | wall faces μ\|S_f\|/\|x_P − x_f\| = μh²/(h/2); interior faces μ\|S_f\|/d_PN = μh²/h. These are exactly the discrete model's coefficients. No factor error: an O(1) coefficient error would change the solution at O(h) or more, and CFDApp matches the model to ≤ 8.1e-8 |
| wall BC assembly | same, boundary branch: A_PP += μ\|S\|/(h/2), RHS += that × 0 (wall value) | the standard half-cell two-point wall gradient; its truncation error is §10.3 |
| pressure gradient | Green–Gauss (the duct case uses the default `green_gauss`), boundary pressure from the BCs | exact for the linear developed pressure. CFDApp's least-squares dp/dx is within 1.5e-6 of G_disc, which is the development inside the fit range |
| Rhie–Chow | `src/pressure_velocity/RhieChow.cpp:17-51` | `−(D_f/α_u)·((p_N − p_P) − (∇p)_f·d)` vanishes for linear p (G2.2). Converged result independent of relaxation to 1.4e-10 (§10.2) |
| face areas, cell volumes | `MeshGeometry::createCartesian3D` | exact (MESH-005 §24a: ≤ 8.8e-12) |
| pressure correction, velocity correction | `PressureCorrectionEquation.cpp` | hand-derived 2×1×1 / 1×1×2 / 2×2×2 systems reproduced to 1e-13 (G3.1). At convergence p′ → 0, so they do not enter the converged solution |
| under-relaxation | implicit (A_P/α with the lagged (1 − α)/α A_P u term) | converged solution unchanged by 0.5/0.2 vs 0.7/0.3 (1.2e-10–1.4e-10) |
| convergence criterion | initial residuals of the freshly assembled systems ≤ 1e-9 (absolute) | iterative error 8.5e-9 (tolerance 1e-11 vs 1e-9) |
| convection (central) | — | zero in developed flow; cross-flow on the plane ≤ 1.5e-8 U |

Nothing capable of producing an O(h) error or a wrong coefficient was found, and the experiments
below would expose one.

### 10.2 Audit experiments (production CLI, [logs/cli_*.log](logs/))

| variant vs base | iterations | max \|Δu\|/U | max \|Δ cross-flow\|/U | \|ΔG\|/G | L∞ variant / base |
| --- | --- | --- | --- | --- | --- |
| x8relax vs x8 | 142 / 67 | 1.35e-10 | 1.06e-10 | 5.92e-11 | 0.0849967166 / 0.0849967165 |
| x8long vs x8 | 69 / 67 | 8.49e-10 | 7.25e-11 | 1.30e-10 | 0.0849967156 / 0.0849967165 |
| x8linear vs x8 | 20000 / 67 | 2.52e-04 | 4.28e-07 | 4.25e-02 | 0.0848107111 / 0.0849967165 |
| x16tight vs x16 | 228 / 183 | 8.54e-09 | 5.98e-10 | 1.19e-09 | 0.0233566954 / 0.0233567039 |
| x16relax vs x16 | 413 / 183 | 1.18e-10 | 1.13e-10 | 1.00e-10 | 0.0233567040 / 0.0233567039 |

Linear face flux (`face_flux: linear`, n = 8): **did not converge**. It stalls at 20 000 iterations
([logs/cli_x8linear.log](logs/cli_x8linear.log), exit 3) with P residual 8.7e-6 and U residual
3.3e-7. The unconverged plane differs from the Rhie–Chow solution by 2.5e-4 U, and its dp/dx by
4.2 %, because the pressure field keeps the undamped odd-even mode. This is the documented
limitation of the explicit linear flux on open domains, the reason 3D uses Rhie–Chow (summary.md
§1). It is kept as a failed audit experiment, not a gate item.

### 10.3 Truncation error: where the error constant comes from

The exact solution is inserted into the discrete equations (τ per unit volume, independent
discrete code; table in [data/analysis.md](data/analysis.md)):

| n | wall-ring max | wall cell at mid-edge | second ring max (corner-adjacent) | central half max |
| --- | --- | --- | --- | --- |
| 16 | 0.9159 | 0.7100 | 0.0672 | 9.98e-3 |
| 64 | 0.9159 | 0.7113 | 0.0669 | 6.89e-4 |
| 256 | 0.9159 | 0.7114 | 0.0669 | 4.43e-5 |

**Wall cells: O(1), exactly G/4 = 0.7114.**

- The wall flux μ u_P/(h/2) expands to μ(u_y + u_yy h/4 + O(h²)).
- On the wall, u_yy = −G/μ, because u ≡ 0 along the wall gives u_zz = 0.
- So the flux error is −G h/4, and per unit volume that is G/4.

**Corner-adjacent cells: O(1).** The exact solution has the r² log r singularity of a right-angle
Dirichlet corner with a constant source.

**Interior: O(h²).**

The global error is nevertheless O(h²) (orders → 2.000, §6). This is the classical
supraconvergence of cell-centred schemes with a half-cell boundary distance. The O(1) wall
truncation sets a large error constant, and it is a property of the chosen, standard scheme.
CFDApp's 2D channels use the same scheme. It is not an implementation defect, and it is not O(h).

### 10.4 The premise of the G5.1 derivation ([logs/channel_vs_duct.log](logs/channel_vs_duct.log))

Same scheme, independent code, fully developed flow:

| cells | 2D channel: \|G err\|/G | channel L∞/U | channel RMS/U | square duct: \|G err\|/G | duct L∞/U | duct RMS/U | duct / channel: G, L∞ |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 16 | 7.752e-3 | 5.769e-3 | 3.950e-3 | 1.474e-2 | 2.336e-2 | 1.148e-2 | 1.90, 4.05 |
| 18 | 6.135e-3 | 4.573e-3 | 3.129e-3 | 1.170e-2 | 1.858e-2 | 9.137e-3 | 1.91, 4.06 |
| 24 | 3.460e-3 | 2.586e-3 | 1.768e-3 | 6.634e-3 | 1.058e-2 | 5.202e-3 | 1.92, 4.09 |
| 32 | 1.949e-3 | 1.459e-3 | 9.973e-4 | 3.748e-3 | 5.990e-3 | 2.947e-3 | 1.92, 4.11 |

- The derivation's premise, the 2D dp/dx error of about 0.74 % at 16 and 0.33 % at 24 cells, is
  confirmed (0.775 %, 0.346 %).
- The velocity limits correspond to 3.5× (L∞, n = 16), 3.9× (L∞, n = 24) and 2.8× (RMS, n = 24)
  the channel's velocity errors.
- The duct's actual ratios are 4.05×, 4.09× and 2.94×.
- The transfer from the channel to the duct was never computed. That is the design defect.

## 11. Already-running G7 jobs (item 2, item 11)

The 48³ and 64³ lid-driven-cube levels had been launched before G5 failed. They were allowed to
finish and are preserved unchanged. The G7 gate test then ran automatically (the same launched job
group), and no new job was started for G7. Logs:

- [../logs/07a](../logs/07a_g7_cube_32.log), [07b](../logs/07b_g7_cube_48.log),
  [07c](../logs/07c_g7_cube_64.log), gate [07d](../logs/07d_g7_cube_gate.log);
- data [../data/cavity3d_re1000_n{32,48,64}.json](../data/),
  [../data/cavity3d_re1000_gate.txt](../data/cavity3d_re1000_gate.txt).

| grid | status | iterations | wall time (under concurrent load) | max \|Δ\| vs Albensoeder & Kuhlmann | RMS | extrema \|Δ\| (T5 min / T6 min / T6 max) | spanwise symmetry | net boundary flux |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 32³ | Converged | 249 | 195 s | 0.1040 | 0.0486 | 0.0843 / 0.0428 / 0.1040 | 9.2e-8 | 0 |
| 48³ | Converged | 441 | 1525 s | 0.0938 | 0.0370 | 0.0394 / 0.0158 / 0.0502 | 4.8e-7 | 0 |
| 64³ | Converged | 680 | 6175 s | 0.0532 | 0.0210 | 0.0176 / 0.0088 / 0.0289 | 5.6e-7 | 0 |

**G7 gate: every item passes (14/14).**

- G7.1 at 64³:
  - max \|Δ\| 0.0532 ≤ 0.06;
  - RMS 0.0210 ≤ 0.03;
  - extrema 0.0176, 0.0088 and 0.0289, each ≤ 0.03. The Table 6 maximum is within 0.0011 of its
    limit.
- G7.2: max \|Δ\| and RMS decrease monotonically.
- G7.3: symmetry 5.6e-7 ≤ 1e-5.
- G7.4: all three grids Converged, with net flux 0.

As instructed, this result is **not** used to bypass G5. MESH-006 stays BLOCKED on G5.1 / G6.2
regardless.

## 12. Decision

**D — PRE-REGISTERED THRESHOLD DESIGN DEFECT.**

**A — implementation defect: excluded.**

- CFDApp reproduces the independent solution of the same discrete equations to ≤ 8.1e-8 U on the
  measurement plane at every grid run (n = 8, 12, 16, 24, 32; §4). Its observed orders are
  identical to the discrete ones (§6).
- The remaining ~6e-8 is the inlet development tail. It does not change at a 100× tighter
  tolerance, and it grows to 3.5e-6 at x = 3a.
- The converged solution does not depend on:
  - under-relaxation, which also covers the Rhie–Chow D_f/α_u term (1.4e-10);
  - the outer tolerance (8.5e-9);
  - the duct length / outlet (8.5e-10).
- The linear-flux variant: §10.2.
- The code audit finds the coefficients of the discrete model (§10.1).
- No O(h) error exists anywhere: the global order tends to 2.000. The O(1) truncation error
  confined to the wall cells is G/4. That is an analytical property of the standard half-cell
  two-point wall gradient (§10.3), the one CFDApp also uses in 2D.
- x, y and z agree to 3.4e-12 (§9), so there is no w- or z-specific defect.

**B — reference / error-metric defect: excluded.**

- The series is verified by two independent closed forms, the literature constants, the PDE, the
  wall conditions and the flow rate (§2).
- It is evaluated at CFDApp's actual cell centres.
- The gate's norm arithmetic is reproduced by an independent implementation to ≤ 3e-12 (§3).
- The metric (point values at cell centres) is standard, well defined and implemented as
  specified.
- *Finding, not a defect:* the equally legitimate cell-average reference removes a uniform
  offset of (h²/24)(G/μ) (§8). That makes L∞ 20 % and RMS 8 % smaller, and it would flip all
  three failed items. The outcome therefore depends on an O(h²) choice of reference quantity,
  which shows the limits had no margin. Switching to the metric that passes after seeing the
  result is exactly what the stop rules forbid, and it is not proposed as the fix.

**C — benchmark configuration defect: excluded.**

- Re = 10 on a = D_h, the side, length, U, μ, inlet, outlet and walls are as intended (A2).
- The measurement plane is fully developed:
  - axis variation 1.6e-6 over x ∈ [3a, 4.5a];
  - the downstream plane is identical to 6 digits;
  - doubling the duct length changes it by 8.5e-10;
  - CFDApp matches the *fully developed* discrete solution to ≤ 8.1e-8.

**D — threshold design defect: confirmed.**

- The independent discrete errors are computed without CFDApp, and a correct implementation of
  the scheme must produce them. They are L∞ 0.023357 (n = 16), L∞ 0.010577 and RMS 0.0052021
  (n = 24), all above the limits. No correct implementation of this second-order scheme can meet
  G5.1 on these grids.
- The gate's own derivation (acceptance_gate.md G5.1) started from the 2D channel's dp/dx error
  (0.585 % at 18 cells), scaled it to 16 and 24 cells (0.74 %, 0.33 %) and allowed "≈ 4× that".
  - That premise is correct for the scheme: the 1D channel gives 0.775 % and 0.346 % (§10.4).
  - But the square duct's dp/dx error is 1.9× the channel's, and its velocity L∞ is 4.05–4.11×
    the channel's (RMS 2.9×): two walls, corners, and a peak velocity of 2.096 U instead of 1.5 U.
  - The L∞ and RMS limits were never derived from a velocity error. They allow only 3.5× and
    3.9× (L∞) and 2.8× (RMS) of the channel values.
  - The derived dp/dx and u_max limits pass (0.66 % ≤ 1.5 %, 0.80 % ≤ 1.0 % at n = 24).

**E — inconclusive: no.** Every alternative has been tested directly.

## 13. Proposed replacement for G5.1 / G6.2 (NOT installed)

**Not installed.** Installing it needs the user's explicit authorization. It would be recorded as a
new amendment (A3) before any re-run, and the original G5.1/G6.2 result stays on record as
failed.

**Basis.** Everything below comes from CFDApp-free calculations:

- the analytical series;
- the independent exact solution of the scheme's discrete equations;
- the formal properties of the scheme.

The only CFDApp-dependent quantities (the agreement tolerances of G5.1′(a)) are argued from
error sources that can be bounded without CFDApp. Their CFDApp values are already known from this
investigation (≤ 8.1e-8 and ≤ 1.54e-6), and they are stated so the user can judge.

**Proposed G5.1′** (replaces G5.1; G5.2, G5.3 and G5.4 stay as written; same grids n = 8, 16, 24;
same point-value metric):

| id | criterion | justification |
| --- | --- | --- |
| G5.1′(a) implementation | measurement plane: max\|u_CFDApp − u_disc\|/U ≤ 1e-5 on every grid; \|dp/dx_CFDApp − G_disc\|/G ≤ 1e-4 (u_disc, G_disc: `tools/duct_discrete.py`, the exact solution of the same discrete equations) | The difference can only come from inexact iteration and incomplete development. Both are measured in this investigation, from CFDApp, not derived independently. The iterative part is 8.5e-9 at tolerance 1e-9 (§10.2). The development tail from the inlet decays by about 50× per duct width: 3.5e-6 at x = 3a, ≤ 8e-8 at 4a, so an estimated 2.5e-5 at x = 2.5a, where the dp/dx fit starts. The tolerances allow 100× (velocity) and about 60× (dp/dx) over those estimates. They sit ≥ 3 orders below the discretization error at n ≤ 24 (≥ 5e-3), so an error in any discretization coefficient ≥ 0.1 % of a term would fail. |
| G5.1′(b) convergence | L∞, RMS, \|dp/dx err\|/G and \|u_max err\|/u_max each decrease monotonically 8 → 16 → 24 | as G5.2, extended to all four |
| G5.1′(c) order | the observed order of the 16 → 24 pair is in [1.8, 2.2] for L∞, RMS, dp/dx and u_max | The scheme is formally second order (supraconvergent at the walls, §10.3). An O(h) defect would give about ≤ 1.2. The ±0.2 band covers the pre-asymptotic approach from below. The independent discrete values for this pair are 1.953–1.970. |
| G5.1′(d) accuracy | at n = 24: \|dp/dx err\|/G ≤ 0.015 and \|u_max err\|/u_max ≤ 0.010, the original engineering limits of G5.1, kept. Also L∞ ≤ F_s·E_L∞(24) and RMS ≤ F_s·E_RMS(24), where E are the independent discrete errors (0.010577, 0.0052021) and F_s = 1.25 (the three-grid GCI safety factor), i.e. L∞ ≤ 0.0132 and RMS ≤ 0.0065 | The limits now follow from the scheme's own computed error, with the standard GCI margin, instead of from an analogy with another geometry and another quantity. |
| G5.1′(e) Richardson | Celik et al. GCI on CFDApp's 8/16/24 sequence for G and u_max: asymptotic-range indicator in [0.95, 1.05], and the exact value inside the fine-grid GCI band | This verifies convergence to the right continuum solution, not just to a discrete fixed point. The independent discrete gives 1.008 and 1.010, with the exact value inside the band. |

**Proposed G6.2′:** the z-directed n = 16 duct meets G5.1′(a) at n = 16. G6.1 is unchanged (it
passed at 1e-11–1e-13).

**Also recommended:**

- Report both the point-value and the cell-average norms (not gated), because §8 shows the
  metric choice moves the error constant by 8–20 %.
- Any future absolute velocity limit must be derived from the scheme's computed error on the
  same geometry and metric.

**Alternative, if absolute per-grid limits are preferred:** keep the original limits but move
them to grids where the scheme meets them with margin (independent discrete):

- L∞ ≤ 0.020 at n = 24: 0.0106, 47 % margin.
- L∞ ≤ 0.010 and RMS ≤ 0.005 at n = 32: 0.0060 and 0.0029, 40 % and 41 % margin.

This needs a CFDApp n = 32 gate run (about 1 h). The scan in §7 gives the smallest passing grids:
n = 18, 25 and 25.

## 14. Pending MESH-006 closeout work (not part of this investigation)

- clang-format: 14 of 56 new or modified C++ files would change
  ([../logs/08](../logs/08_clang_format_dry_run.log)). Nothing was reformatted.
- Everything listed in summary.md §29: the GUI (G9.4), CLI 3D fixtures, CPU baseline, final G10,
  G11 and docs.

## 15. Confirmations

- **Original gate unchanged.** `acceptance_gate.md` and the 12 original G5/G6 evidence files (logs
  05a–05e and 06; data/duct3d_n8/n16/n24.json, duct3d_gate.txt, duct3d_symmetry*.{json,txt}) were
  hashed at the start of the investigation
  ([logs/original_evidence_sha256_start.txt](logs/original_evidence_sha256_start.txt)) and re-verified
  at the end ([logs/original_evidence_sha256_check.txt](logs/original_evidence_sha256_check.txt)):
  all 13 OK.
- **No code, test, case or document change.** No file under src/, include/, apps/, tests/, cases/,
  cmake/, nor CMakeLists.txt, TODO.md, ROADMAP.md or README.md, was modified after the investigation
  started (2026-09-15 11:05Z). No binary was rebuilt. Every CLI output went to `$HOME/m6g5` (WSL),
  outside the repository. Written:
  - this directory (`results/p12-mesh-006/g5-investigation/`);
  - the four G7 result placeholders of `../summary.md` (§19, §20, §22, §25), filled with the
    preserved G7 results once those jobs finished.

  summary.md's G5/G6 text and its §30 decision are unchanged.
- **MESH-006 remains `[ ]` BLOCKED / FAILED GATE** (TODO.md lines 61 and 312). No checkbox was
  marked.
- **MESH-007 was not started.** TODO.md: "P12-MESH-007 onward — NOT AUTHORIZED".
- **Nothing was committed or pushed.** HEAD is still `b66310c`, with 0 commits since.
- The gate amendment proposed in §13 was **not** installed.
