#!/usr/bin/env python3
"""P18.5 -- THE INDEPENDENT ARBITRARY-PRECISION REFERENCE that G6 names by name.

G6's `measured` clause:
    "omega_i for a known mechanism against an INDEPENDENT arbitrary-precision implementation
     (Python/mpmath, no CFDApp code), over a matrix of T, p and composition spanning the intended
     range."

G6's `evidence` clause requires this script to be PRESERVED under tools/. It is.

NO CFDApp CODE IS IMPORTED OR CONSULTED. Everything below is written from the physics:

    rho   = p / (R_mix T)                  ideal gas, with R_mix = sum_i Y_i R_i
    R_i   = R_u / M_i
    C_i   = rho Y_i / M_i                  [mol/m^3]
    k     = A T^n exp(-Ea/(R_u T))
    q     = k Pi_i C_i^(nu'_i)             elementary mass action, over REACTANTS
    omega_i = M_i nu_i q                   [kg/(m^3 s)]

Computed at 60 decimal digits and printed at 17 significant figures, which round-trips a double
exactly. The C++ side is then compared against these literals -- it never generates its own
expected values.

The mechanism is  2 H2 + O2 -> 2 H2O  with molecular weights chosen so that
2 M_H2 + M_O2 - 2 M_H2O == 0 exactly in binary, which P18.4 already established.
"""

from mpmath import mp, mpf, exp, log, power

mp.dps = 60

# ---------------------------------------------------------------- constants
# CODATA 2018, exact by the 2019 SI redefinition. Stated here as a literal rather than imported,
# so this script shares nothing with CFDApp.
R_U = mpf("8.31446261815324")  # J/(mol K)

# ---------------------------------------------------------------- the mechanism
SPECIES = ["H2", "O2", "H2O"]
M = {  # kg/mol
    "H2": mpf("0.002016"),
    "O2": mpf("0.031998"),
    "H2O": mpf("0.018015"),
}
# 2 H2 + O2 -> 2 H2O
NU_REACTANT = {"H2": mpf(2), "O2": mpf(1), "H2O": mpf(0)}
NU_PRODUCT = {"H2": mpf(0), "O2": mpf(0), "H2O": mpf(2)}
NU_NET = {s: NU_PRODUCT[s] - NU_REACTANT[s] for s in SPECIES}

# ---------------------------------------------------------------- kinetics
A = mpf("1.5e8")  # units are order-dependent; total reactant order here is 3
N = mpf("0.5")
EA = mpf("1.25e5")  # J/mol


def rate_coefficient(a, n, ea, t):
    """k = A T^n exp(-Ea/(R_u T)), in arbitrary precision."""
    if a == 0:
        return mpf(0)
    return a * power(t, n) * exp(-ea / (R_U * t))


def mixture_gas_constant(y):
    """R_mix = sum_i Y_i R_i, with R_i = R_u / M_i."""
    return sum(y[s] * (R_U / M[s]) for s in SPECIES)


def concentrations(y, rho):
    """C_i = rho Y_i / M_i."""
    return {s: rho * y[s] / M[s] for s in SPECIES}


def progress_rate(a, n, ea, t, y, rho):
    """q = k Pi_i C_i^(nu'_i), over the reactants."""
    k = rate_coefficient(a, n, ea, t)
    c = concentrations(y, rho)
    product = mpf(1)
    for s in SPECIES:
        order = NU_REACTANT[s]
        if order == 0:
            continue
        if c[s] == 0:
            return mpf(0)
        product *= power(c[s], order)
    return k * product


def species_sources(q):
    """omega_i = M_i nu_i q."""
    return {s: M[s] * NU_NET[s] * q for s in SPECIES}


def g(x):
    """17 significant figures -- round-trips a double exactly."""
    return mp.nstr(x, 17, strip_zeros=False)


def main():
    print("=== P18.5 INDEPENDENT ARBITRARY-PRECISION REFERENCE (mpmath, dps=60) ===")
    print("NO CFDApp code involved. G6's named reference.")
    print()
    print(f"R_u = {R_U}  J/(mol K)")
    print(f"mechanism: 2 H2 + O2 -> 2 H2O")
    print(f"  M_H2 = {M['H2']}  M_O2 = {M['O2']}  M_H2O = {M['H2O']}  [kg/mol]")
    print(f"  mass residual sum_i M_i nu_i = "
          f"{g(sum(M[s] * NU_NET[s] for s in SPECIES))}")
    print(f"kinetics: A = {A}  n = {N}  Ea = {EA} J/mol")
    print()

    # ---------------- k(T) alone, over the intended temperature range ----------------
    print("--- k(T) = A T^n exp(-Ea/(R_u T)) ---")
    for t in ["300", "500", "800", "1200", "1800", "2500", "3000"]:
        tt = mpf(t)
        k = rate_coefficient(A, N, EA, tt)
        x = -EA / (R_U * tt)
        print(f"  T = {t:>5}  x = -Ea/(R_u T) = {g(x):>22}   k = {g(k)}")
    print()

    # ---------------- the reduced analytical cases ----------------
    print("--- reduced cases (sections 9, 10, 11) ---")
    t = mpf("1500")
    print(f"  at T = {t}")
    print(f"  Ea = 0  ->  k = A T^n            = {g(rate_coefficient(A, N, mpf(0), t))}")
    print(f"             A T^n                 = {g(A * power(t, N))}")
    print(f"  n  = 0  ->  k = A exp(-Ea/(R T)) = {g(rate_coefficient(A, mpf(0), EA, t))}")
    print(f"             A exp(-Ea/(R T))      = {g(A * exp(-EA / (R_U * t)))}")
    print(f"  n=0,Ea=0 -> k = A                = {g(rate_coefficient(A, mpf(0), mpf(0), t))}")
    print(f"  A  = 0  ->  k = 0                = {g(rate_coefficient(mpf(0), N, EA, t))}")
    print()

    # ---------------- dk/dT ----------------
    print("--- dk/dT = k [ n/T + Ea/(R_u T^2) ] (section 14) ---")
    for t in ["600", "1500", "2400"]:
        tt = mpf(t)
        k = rate_coefficient(A, N, EA, tt)
        dk = k * (N / tt + EA / (R_U * tt * tt))
        print(f"  T = {t:>5}  k = {g(k):>24}   dk/dT = {g(dk)}")
    print()

    # ---------------- the Arrhenius plot, n = 0 (section 35) ----------------
    print("--- Arrhenius plot, n = 0: ln k = ln A - (Ea/R_u)(1/T) (section 35) ---")
    print(f"  expected slope     = -Ea/R_u = {g(-EA / R_U)}")
    print(f"  expected intercept = ln A    = {g(log(A))}")
    for t in ["800", "1000", "1250", "1600", "2000"]:
        tt = mpf(t)
        k = rate_coefficient(A, mpf(0), EA, tt)
        print(f"  1/T = {g(1 / tt):>24}   ln k = {g(log(k))}")
    print()

    # ---------------- THE MATRIX: T x p x composition, through to omega_i ----------------
    print("--- THE G6 MATRIX: omega_i over T, p and composition ---")
    print("    rho = p/(R_mix T) with R_mix = sum_i Y_i R_i, all in mpmath")
    # THE LAST SPECIES IS THE EXACT REMAINDER, so sum_i Y_i == 1 EXACTLY in binary.
    # Writing three decimal literals does NOT give an exact sum -- the first attempt used
    # {0.05, 0.90, 0.05} and the assertion below caught it. The remainder form is also what
    # P18.1's MassFractionState requires on the C++ side, so the two agree by construction.
    def mix(h2, o2):
        return {"H2": mpf(h2), "O2": mpf(o2), "H2O": mpf(1) - mpf(h2) - mpf(o2)}

    compositions = [
        ("stoich-ish", mix("0.10", "0.80")),
        ("H2-rich", mix("0.60", "0.30")),
        ("O2-rich", mix("0.05", "0.90")),
        ("no-H2", mix("0", "0.50")),
    ]
    for name, y in compositions:
        assert sum(y.values()) == 1, name
        for p in ["50000", "101325", "1000000"]:
            for t in ["800", "1500", "2200"]:
                pp, tt = mpf(p), mpf(t)
                r_mix = mixture_gas_constant(y)
                rho = pp / (r_mix * tt)
                q = progress_rate(A, N, EA, tt, y, rho)
                om = species_sources(q)
                total = sum(om.values())
                print(f"  {name:<11} p={p:>8} T={t:>5}  rho={g(rho):>22}  q={g(q)}")
                print(f"      omega_H2  = {g(om['H2'])}")
                print(f"      omega_O2  = {g(om['O2'])}")
                print(f"      omega_H2O = {g(om['H2O'])}")
                print(f"      sum omega = {g(total)}")
    print()

    # ---------------- the units control (sections 5, 33) ----------------
    print("--- the mol/kmol control (sections 5, 33) ---")
    tt = mpf("1000")
    correct = exp(-EA / (R_U * tt))
    wrong = exp(-EA / (R_U * 1000 * tt))  # R in J/(kmol K)
    print(f"  T = 1000, Ea = {EA}")
    print(f"  correct exp(-Ea/(R_u T))          = {g(correct)}")
    print(f"  with R 1000x too large            = {g(wrong)}")
    print(f"  ratio wrong/correct               = {g(wrong / correct)}")
    print()

    # ---------------- the sign control (section 32, G6's own) ----------------
    print("--- the exponent-sign control (G6's own N1) ---")
    print(f"  correct exp(-Ea/(R_u T)) at T=1500 = {g(exp(-EA / (R_U * mpf('1500'))))}")
    print(f"  flipped exp(+Ea/(R_u T)) at T=1500 = {g(exp(EA / (R_U * mpf('1500'))))}")
    ratio = exp(EA / (R_U * mpf("1500"))) / exp(-EA / (R_U * mpf("1500")))
    print(f"  ratio                              = {g(ratio)}")


if __name__ == "__main__":
    main()
