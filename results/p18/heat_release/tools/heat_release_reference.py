#!/usr/bin/env python3
"""P18.6 -- THE INDEPENDENT ARBITRARY-PRECISION THERMOCHEMICAL REFERENCE.

NO CFDApp CODE IS IMPORTED OR CONSULTED. Everything below is written from the frozen convention at
results/p18/heat_release/energy_accounting.md:

    hbar_f,i   = M_i h_f,i                                  [J/mol]
    Delta H_r^0 = sum_i nu_i hbar_f,i                        [J/mol]   formation only -- THE SOURCE
    Delta H_r(T) = sum_i nu_i M_i [ h_f,i + h_i(T) ]         [J/mol]   thermodynamic, NOT the source
    Delta Cp_r(T) = sum_i nu_i M_i cp_i(T)                   [J/(mol K)]  = d(Delta H_r)/dT
    omega_i    = M_i nu_i q                                  [kg/(m^3 s)]
    S_E        = -q Delta H_r^0 = -sum_i h_f,i omega_i        [W/m^3]
    u_sens     = rho cp(T,Y) T                               [J/m^3]
    E          = u_sens + rho sum_i Y_i h_f,i                [J/m^3]

h_i(T) is the SENSIBLE integral  integral_{Tref}^{T} cp_i dtheta, computed here in closed form from
the polynomial coefficients -- the same quantity P15 integrates, derived independently.

THE ADIABATIC FINAL TEMPERATURE IS DERIVED ANALYTICALLY, not iterated: for species sharing one
CONSTANT cp, cp_mix is composition-independent and conservation of E gives

    T_final = T_0 + ( sum_i Y_i^0 h_f,i - sum_i Y_i^f h_f,i ) / cp

which is G7's "adiabatic flame temperature computed from an INDEPENDENT equilibrium/enthalpy
balance". With unequal cp_i the same balance is solved for T, still without CFDApp.

Computed at 60 decimal digits and printed at 17 significant figures, which round-trips a double
exactly.
"""

from mpmath import mp, mpf

mp.dps = 60


def g(x):
    """17 significant figures -- round-trips a double exactly."""
    return mp.nstr(x, 17, strip_zeros=False)


# ---------------------------------------------------------------- the mechanism
# Equal, DYADIC molecular weights, so sum_i M_i nu_i == 0 EXACTLY in binary for a 1:1 conversion.
# P18.4 established that this is what makes the mass-closure criterion sharp rather than vacuous.
M = mpf("0.0625")  # 2^-4 kg/mol, every species
SPECIES = ["A", "B", "C"]
WEIGHT = {s: M for s in SPECIES}

# h_f,i [J/kg]. A is the reference element (0); B is lower (exothermic A->B); C is higher.
HF = {
    "A": mpf("0"),
    "B": mpf("-2000000"),  # -2e6 J/kg
    "C": mpf("3000000"),  # +3e6 J/kg
}

# nu_i for each reaction, net = products - reactants
REACTIONS = {
    "A->B": {"A": mpf(-1), "B": mpf(1), "C": mpf(0)},  # EXOTHERMIC
    "A->C": {"A": mpf(-1), "B": mpf(0), "C": mpf(1)},  # ENDOTHERMIC
    "B->C": {"A": mpf(0), "B": mpf(-1), "C": mpf(1)},  # endothermic, larger
}

# ---------------------------------------------------------------- a SECOND mechanism, on purpose
# THE 1:1 SET ABOVE CANNOT TEST MOLAR NON-INVARIANCE, and the first version of this script was
# vacuous because of it: for A->B, sum_i nu_i = -1 + 1 = 0, so shifting the MOLAR hbar_f,i by a
# common constant cancelled too -- for the wrong reason. The invariance under a MASS shift is the
# physical statement (it rests on sum_i M_i nu_i = 0); invariance under a MOLAR shift is NOT, and a
# mechanism that conserves mole count cannot tell them apart.
#
# A + B -> C conserves mass and does NOT conserve moles:
#     sum_i M_i nu_i = -M_A - M_B + M_C = 0        with M_C = M_A + M_B
#     sum_i nu_i     = -1 - 1 + 1 = -1  != 0
# All three weights are dyadic, so the mass residual is exactly 0 in binary.
COMBINE_SPECIES = ["A", "B", "C"]
COMBINE_WEIGHT = {"A": mpf("0.0625"), "B": mpf("0.125"), "C": mpf("0.1875")}  # 2^-4, 2^-3, 3*2^-4
COMBINE_HF = {"A": mpf("0"), "B": mpf("0"), "C": mpf("-4000000")}
COMBINE_NU = {"A": mpf(-1), "B": mpf(-1), "C": mpf(1)}


def molar_hf(name):
    """hbar_f,i = M_i h_f,i [J/mol]."""
    return WEIGHT[name] * HF[name]


def standard_reaction_enthalpy(nu):
    """Delta H_r^0 = sum_i nu_i hbar_f,i [J/mol]. THE SOURCE QUANTITY."""
    return sum(nu[s] * molar_hf(s) for s in SPECIES)


def mass_residual(nu):
    """sum_i M_i nu_i -- must be exactly 0 for a mass-balanced reaction (P18.4's invariant)."""
    return sum(WEIGHT[s] * nu[s] for s in SPECIES)


def omega(nu, q):
    """omega_i = M_i nu_i q [kg/(m^3 s)]."""
    return {s: WEIGHT[s] * nu[s] * q for s in SPECIES}


def source_molar(nu, q):
    """S_E = -q Delta H_r^0 [W/m^3]."""
    return -q * standard_reaction_enthalpy(nu)


def source_mass(nu, q):
    """S_E = -sum_i h_f,i omega_i [W/m^3] -- G7's own formula, the independent route."""
    om = omega(nu, q)
    return -sum(HF[s] * om[s] for s in SPECIES)


# ---------------------------------------------------------------- sensible enthalpy
def cp_poly(coefficients, t):
    """cp(T) from ASCENDING coefficients {a0, a1, ...}, by Horner."""
    out = mpf(0)
    for c in reversed(coefficients):
        out = (out * t) + c
    return out


def sensible_enthalpy(coefficients, t_ref, t):
    """integral_{t_ref}^{t} cp dtheta, in closed form: sum a_k (t^{k+1} - t_ref^{k+1})/(k+1)."""
    out = mpf(0)
    for k, c in enumerate(coefficients):
        power = k + 1
        out += c * ((t ** power) - (t_ref ** power)) / power
    return out


def main():
    print("=== P18.6 INDEPENDENT ARBITRARY-PRECISION THERMOCHEMICAL REFERENCE ===")
    print("mpmath dps=60. NO CFDApp code involved.")
    print()
    print(f"M_i = {M} kg/mol for every species (2^-4, dyadic)")
    for s in SPECIES:
        print(f"  h_f,{s} = {g(HF[s]):>22} J/kg    hbar_f,{s} = {g(molar_hf(s)):>22} J/mol")
    print()

    # ---------------- reaction enthalpies and the mass invariant ----------------
    print("--- Delta H_r^0 and the P18.4 mass invariant ---")
    for name, nu in REACTIONS.items():
        dh = standard_reaction_enthalpy(nu)
        kind = "EXOTHERMIC" if dh < 0 else ("ENDOTHERMIC" if dh > 0 else "THERMONEUTRAL")
        print(f"  {name:<6} Delta H_r^0 = {g(dh):>22} J/mol   {kind}")
        print(f"         sum_i M_i nu_i = {g(mass_residual(nu))}   (must be exactly 0)")
    print()

    # ---------------- a thermoneutral reaction, built deliberately ----------------
    print("--- a THERMONEUTRAL reaction (section 12): Delta H_r^0 = 0 with q != 0 ---")
    # A -> B where the two share ONE h_f: the conversion releases nothing, at any rate.
    # The h_f value is deliberately NON-ZERO (-7.5e5) so the result is 0 because the two CANCEL,
    # not because every term is 0 -- a thermoneutral test with h_f = 0 everywhere would pass for an
    # implementation that ignores h_f entirely.
    nu_neutral = {"A": mpf(-1), "B": mpf(1), "C": mpf(0)}
    hf_neutral = {"A": mpf("-750000"), "B": mpf("-750000"), "C": mpf("1234567")}
    dh_neutral = sum(nu_neutral[s] * WEIGHT[s] * hf_neutral[s] for s in COMBINE_SPECIES)
    print(f"  h_f,A = h_f,B = {g(hf_neutral['A'])} J/kg (equal and NON-ZERO, so the result is 0")
    print(f"  by cancellation rather than by every term being 0)")
    print(f"    Delta H_r^0 = {g(dh_neutral)}   (exactly 0)")
    for q in ["1", "1000", "1e6"]:
        print(f"    q = {q:>6}  ->  S_E = {g(-mpf(q) * dh_neutral)}   (exactly 0)")
    print()

    # ---------------- the source, both routes ----------------
    print("--- S_E, the MOLAR and MASS routes (sections 6, 7) ---")
    for name in ["A->B", "A->C"]:
        nu = REACTIONS[name]
        print(f"  {name}")
        for qs in ["0", "1", "2", "7.5", "1000"]:
            q = mpf(qs)
            sm = source_molar(nu, q)
            sx = source_mass(nu, q)
            print(f"    q = {qs:>7}  S_E(molar) = {g(sm):>22}  S_E(mass) = {g(sx):>22}"
                  f"  identical: {sm == sx}")
        om = omega(nu, mpf(1))
        print(f"    at q = 1:  " + "  ".join(f"omega_{s} = {g(om[s])}" for s in SPECIES))
        print(f"               sum_i omega_i = {g(sum(om.values()))}   (must be exactly 0)")
    print()

    # ---------------- linearity ----------------
    print("--- source linearity (section 27): S_E(c q) = c S_E(q) ---")
    nu = REACTIONS["A->B"]
    q1 = mpf("3.25")
    for cs in ["2", "0.5", "1e6", "-1"]:
        c = mpf(cs)
        lhs = source_molar(nu, c * q1)
        rhs = c * source_molar(nu, q1)
        print(f"  c = {cs:>6}  S_E(c q) = {g(lhs):>22}  c S_E(q) = {g(rhs):>22}"
              f"  exact: {lhs == rhs}")
    print()

    # ---------------- superposition ----------------
    print("--- multiple reactions (section 28): S_E,total = sum_r S_E,r ---")
    q_ab, q_ac = mpf("2"), mpf("0.5")
    s_ab = source_molar(REACTIONS["A->B"], q_ab)
    s_ac = source_molar(REACTIONS["A->C"], q_ac)
    print(f"  A->B at q = {q_ab}:  S_E = {g(s_ab)}   (exothermic)")
    print(f"  A->C at q = {q_ac}:  S_E = {g(s_ac)}   (endothermic)")
    print(f"  total                S_E = {g(s_ab + s_ac)}")
    print()

    # ---------------- reference-state invariance (section 33) ----------------
    print("--- reference-state invariance (section 33) ---")
    nu = REACTIONS["A->B"]
    base = standard_reaction_enthalpy(nu)
    print(f"  Delta H_r^0 unshifted                      = {g(base)}")
    for cs in ["1e5", "-2.5e6", "0.125"]:
        c = mpf(cs)
        shifted = sum(nu[s] * WEIGHT[s] * (HF[s] + c) for s in SPECIES)
        print(f"  shift EVERY h_f,i (mass) by {cs:>9}  = {g(shifted)}"
              f"   invariant: {shifted == base}")
    print("  WHY: the shift contributes c * sum_i M_i nu_i, and P18.4 makes that EXACTLY 0.")
    print()
    print("  THE MOLAR SHIFT MUST *NOT* CANCEL -- and a 1:1 reaction CANNOT SHOW THAT, because")
    print("  sum_i nu_i = 0 for it too. Using A + B -> C, which conserves mass but not moles:")
    cb_base = sum(COMBINE_NU[s] * COMBINE_WEIGHT[s] * COMBINE_HF[s] for s in COMBINE_SPECIES)
    cb_mass_res = sum(COMBINE_WEIGHT[s] * COMBINE_NU[s] for s in COMBINE_SPECIES)
    cb_mole_res = sum(COMBINE_NU[s] for s in COMBINE_SPECIES)
    print(f"    sum_i M_i nu_i = {g(cb_mass_res)}   (exactly 0 -- mass conserved)")
    print(f"    sum_i nu_i     = {g(cb_mole_res)}   (NOT 0 -- moles not conserved)")
    print(f"    Delta H_r^0 unshifted                   = {g(cb_base)}")
    for cs in ["1e5", "-2.5e6"]:
        c = mpf(cs)
        mass_shift = sum(COMBINE_NU[s] * COMBINE_WEIGHT[s] * (COMBINE_HF[s] + c)
                         for s in COMBINE_SPECIES)
        molar_shift = sum(COMBINE_NU[s] * ((COMBINE_WEIGHT[s] * COMBINE_HF[s]) + c)
                          for s in COMBINE_SPECIES)
        print(f"    shift h_f,i (MASS)  by {cs:>8}  = {g(mass_shift):>22}"
              f"   invariant: {mass_shift == cb_base}")
        print(f"    shift hbar_f,i (MOLAR) by {cs:>8} = {g(molar_shift):>22}"
              f"   invariant: {molar_shift == cb_base}   <- MUST be False")
    single = sum(nu[s] * WEIGHT[s] * (HF[s] + (mpf("1e5") if s == 'B' else mpf(0)))
                 for s in SPECIES)
    print(f"  shift ONLY h_f,B by 1e5                    = {g(single)}"
          f"   invariant: {single == base}")
    print()

    # ---------------- temperature-dependent reaction enthalpy + Kirchhoff ----------------
    print("--- Delta H_r(T) and KIRCHHOFF d(Delta H_r)/dT = Delta Cp_r (sections 31, 32) ---")
    # linear cp_i(T) = a0 + a1 T, DIFFERENT per species so Delta Cp_r != 0
    CP = {
        "A": [mpf("900"), mpf("0.25")],
        "B": [mpf("1100"), mpf("-0.15")],
        "C": [mpf("1000"), mpf("0.05")],
    }
    T_REF = mpf("298.15")
    print(f"  cp_i(T) = a0 + a1 T, T_ref = {T_REF}")
    for s in SPECIES:
        print(f"    cp_{s}(T) = {CP[s][0]} + {CP[s][1]} T")
    for name in ["A->B", "A->C"]:
        nu = REACTIONS[name]
        print(f"  {name}")
        for ts in ["400", "800", "1500", "2500"]:
            t = mpf(ts)
            dh_t = sum(nu[s] * WEIGHT[s] * (HF[s] + sensible_enthalpy(CP[s], T_REF, t))
                       for s in SPECIES)
            dcp = sum(nu[s] * WEIGHT[s] * cp_poly(CP[s], t) for s in SPECIES)
            print(f"    T = {ts:>5}  Delta H_r(T) = {g(dh_t):>22}"
                  f"  Delta Cp_r = {g(dcp):>22}")
    print()

    # ---------------- the closed adiabatic reactor (sections 23, 24) ----------------
    print("--- CLOSED ADIABATIC REACTOR: the analytic final temperature (sections 23, 24) ---")
    print("    equal CONSTANT cp for every species, so cp_mix is composition-independent and")
    print("    T_final = T_0 + ( sum Y^0 h_f - sum Y^f h_f ) / cp   EXACTLY")
    cp_const = mpf("1000")
    rho = mpf("1")
    t0 = mpf("1000")
    print(f"    cp = {cp_const} J/(kg K), rho = {rho} kg/m^3, T_0 = {t0} K")
    for label, y0, yf in [
        ("A->B complete", {"A": mpf(1), "B": mpf(0), "C": mpf(0)},
         {"A": mpf(0), "B": mpf(1), "C": mpf(0)}),
        ("A->B half", {"A": mpf(1), "B": mpf(0), "C": mpf(0)},
         {"A": mpf("0.5"), "B": mpf("0.5"), "C": mpf(0)}),
        ("A->C complete", {"A": mpf(1), "B": mpf(0), "C": mpf(0)},
         {"A": mpf(0), "B": mpf(0), "C": mpf(1)}),
        ("A->C 20%", {"A": mpf(1), "B": mpf(0), "C": mpf(0)},
         {"A": mpf("0.8"), "B": mpf(0), "C": mpf("0.2")}),
    ]:
        chem0 = sum(y0[s] * HF[s] for s in SPECIES)
        chemf = sum(yf[s] * HF[s] for s in SPECIES)
        tf = t0 + ((chem0 - chemf) / cp_const)
        e0 = (rho * cp_const * t0) + (rho * chem0)
        ef = (rho * cp_const * tf) + (rho * chemf)
        flag = "" if tf > 0 else "   <-- T <= 0: PHYSICALLY IMPOSSIBLE, must be REFUSED (section 42)"
        print(f"    {label:<16} T_final = {g(tf):>22} K   E_0 = {g(e0):>20}"
              f"  E_f = {g(ef):>20}  drift = {g(ef - e0)}{flag}")
    print()

    # ---------------- one-step energy balance (section 26) ----------------
    print("--- one-step balance (section 26): Delta u_sens = dt * S_E ---")
    nu = REACTIONS["A->B"]
    for qs, dts in [("1", "1e-3"), ("100", "1e-5"), ("0.5", "2")]:
        q, dt = mpf(qs), mpf(dts)
        se = source_molar(nu, q)
        print(f"    q = {qs:>5}  dt = {dts:>6}  S_E = {g(se):>20}"
              f"  dt*S_E = {g(dt * se)}")
    print()

    # ---------------- the mol/kmol control (section 43) ----------------
    print("--- the mol/kmol control (section 43) ---")
    nu = REACTIONS["A->B"]
    correct = standard_reaction_enthalpy(nu)
    print(f"    Delta H_r^0 correct [J/mol]        = {g(correct)}")
    print(f"    with M in kg/kmol (1000x too big)  = {g(correct * 1000)}")
    print(f"    ratio                              = 1000 exactly")


if __name__ == "__main__":
    main()
