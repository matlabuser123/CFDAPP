#!/usr/bin/env python3
"""P18.7 -- THE INDEPENDENT REFERENCE for the batch reactor and the steady plug flow.

NO CFDApp CODE IS IMPORTED OR CONSULTED (sections 19, 46).

AND IT IS NOT A TIME INTEGRATION. Section 19 asks for "an integration algorithm mathematically
separate from CFDApp's production update". CFDApp's update is explicit Euler stepping in time; this
reference does a QUADRATURE in composition and then a root find. There is no time-stepping loop
anywhere in it, so the two share no algorithm at all -- not merely a different step size or
tolerance of the same method.

================================================================================
THE SYSTEM, derived here from the physics rather than from CFDApp.

One irreversible first-order reaction  A -> B, equal molecular weights M, equal constant cp:

    q      = k(T) C_A = k(T) rho Y_A / M            [mol/(m^3 s)]
    omega_A = M (-1) q = -k(T) rho Y_A              [kg/(m^3 s)]
    omega_B = +k(T) rho Y_A

CLOSED ADIABATIC BATCH REACTOR (U = 0, no flux, constant volume):

    dY_A/dt = omega_A / rho = -k(T) Y_A
    rho cp dT/dt = S_E = -q Delta H_r^0 = -k(T) rho Y_A (h_f,B - h_f,A)

THE ENERGY INVARIANT MAKES T AN EXACT ALGEBRAIC FUNCTION OF Y_A, which is what collapses this to a
single ODE and then to a quadrature. From P18.6's frozen conserved quantity
E = rho cp T + rho sum_i Y_i h_f,i with Y_B = 1 - Y_A and h_f,A = 0:

    cp T + (1 - Y_A) h_f,B = cp T_0 + (1 - Y_A0) h_f,B
    =>  T(Y_A) = T_0 + (Y_A0 - Y_A) * (-h_f,B) / cp

so the remaining ODE is autonomous in Y_A alone:

    dY_A/dt = -k(T(Y_A)) Y_A

and separating,

    t(Y_A) = INTEGRAL_{Y_A}^{Y_A0}  dy / ( k(T(y)) y )            <-- A QUADRATURE

evaluated at 50 digits. To get Y_A at a GIVEN time, the monotone relation t(Y_A) is inverted by
bisection on the quadrature. Neither step resembles an explicit Euler march.

================================================================================
STEADY 1D ADIABATIC PLUG FLOW, uniform velocity U, no diffusion:

    U dY_A/dx = omega_A / rho = -k(T) Y_A

which is THE SAME SYSTEM with t replaced by x/U. The same invariant holds along the flow, so

    x(Y_A) = U * t(Y_A)

EXACTLY. One reference therefore serves both cases, and the plug-flow benchmark is referenced
against a quadrature rather than against CFDApp -- section 46's requirement.
"""

from mpmath import mp, mpf, exp, quad, findroot

mp.dps = 50


def g(x):
    """17 significant figures -- round-trips a double exactly."""
    return mp.nstr(x, 17, strip_zeros=False)


# ---------------------------------------------------------------- the case, stated as literals
# Dyadic where it matters, so the C++ side can hold the same values exactly.
M = mpf("0.0625")            # kg/mol, both species
CP = mpf("1000")             # J/(kg K), both species -- constant
RHO = mpf("1")               # kg/m^3
HF_A = mpf("0")              # J/kg
HF_B = mpf("-2000000")       # J/kg  -> exothermic
T0 = mpf("1000")             # K
YA0 = mpf("1")               # pure A

# Arrhenius, n = 0
A_PRE = mpf("5")             # 1/s  -- chosen so the reaction proceeds on an O(1 s) timescale
EA = mpf("3.0e4")            # J/mol
R_U = mpf("8.31446261815324")  # CODATA 2018, stated here rather than imported

U_FLOW = mpf("2")            # m/s, for the plug-flow mapping


def temperature(y_a):
    """T(Y_A) from the energy invariant. EXACT, not integrated."""
    return T0 + ((YA0 - y_a) * (-HF_B) / CP)


def rate_coefficient(t):
    """k(T) = A exp(-Ea/(R_u T)), n = 0."""
    return A_PRE * exp(-EA / (R_U * t))


def integrand(y):
    """1 / ( k(T(y)) y )."""
    return 1 / (rate_coefficient(temperature(y)) * y)


def time_at(y_a):
    """t(Y_A) by quadrature. NO TIME STEPPING."""
    return quad(integrand, [y_a, YA0])


def composition_at(t_target):
    """Invert t(Y_A) by bisection on the quadrature -- monotone, so the root is unique."""
    if t_target == 0:
        return YA0
    lo, hi = mpf("1e-40"), YA0
    for _ in range(300):
        mid = (lo + hi) / 2
        if time_at(mid) < t_target:
            hi = mid
        else:
            lo = mid
        if hi - lo < mpf("1e-45"):
            break
    return (lo + hi) / 2


def progress_rate(y_a):
    """q = k(T) rho Y_A / M  [mol/(m^3 s)]."""
    return rate_coefficient(temperature(y_a)) * RHO * y_a / M


def main():
    print("=== P18.7 INDEPENDENT REFERENCE: batch reactor and steady plug flow ===")
    print("mpmath dps=50, QUADRATURE + bisection. NO time integration. NO CFDApp code.")
    print()
    print(f"  M      = {M} kg/mol (both species)      cp   = {CP} J/(kg K)")
    print(f"  rho    = {RHO} kg/m^3                    T_0  = {T0} K")
    print(f"  h_f,A  = {HF_A} J/kg                     h_f,B = {HF_B} J/kg")
    print(f"  A      = {A_PRE} 1/s   n = 0             Ea   = {EA} J/mol")
    print(f"  R_u    = {R_U} J/(mol K)")
    print(f"  U      = {U_FLOW} m/s  (plug flow)")
    print()
    print(f"  Delta H_r^0 = M (h_f,B - h_f,A) = {g(M * (HF_B - HF_A))} J/mol   (exothermic)")
    print(f"  adiabatic complete-conversion T = {g(temperature(mpf(0)))} K")
    print(f"  k(T_0) = {g(rate_coefficient(T0))} 1/s")
    print()

    print("--- THE REFERENCE TABLE: Y_A, Y_B, T, q at checkpoints (section 60) ---")
    print(f"  {'t [s]':>8}  {'Y_A':>22}  {'Y_B':>22}  {'T [K]':>22}  {'q [mol/(m^3 s)]':>22}")
    checkpoints = ["0.05", "0.1", "0.2", "0.3", "0.5", "0.75", "1.0"]
    for ts in checkpoints:
        t = mpf(ts)
        ya = composition_at(t)
        print(f"  {ts:>8}  {g(ya):>22}  {g(YA0 - ya):>22}  {g(temperature(ya)):>22}"
              f"  {g(progress_rate(ya)):>22}")
    print()

    print("--- the conserved total energy, which MUST be constant along the reference ---")
    e0 = (RHO * CP * T0) + (RHO * ((YA0 * HF_A) + ((1 - YA0) * HF_B)))
    print(f"  E(t=0) = {g(e0)} J/m^3")
    for ts in ["0.1", "0.5", "1.0"]:
        ya = composition_at(mpf(ts))
        e = (RHO * CP * temperature(ya)) + (RHO * ((ya * HF_A) + ((1 - ya) * HF_B)))
        print(f"  E(t={ts:>4}) = {g(e)} J/m^3    drift = {g(e - e0)}")
    print("  (exactly constant by construction: T(Y_A) was DERIVED from this invariant)")
    print()

    print("--- STEADY PLUG FLOW: the same system with t -> x/U ---")
    print(f"  {'x [m]':>8}  {'Y_A':>22}  {'T [K]':>22}")
    for xs in ["0.1", "0.2", "0.4", "0.6", "1.0", "1.5", "2.0"]:
        x = mpf(xs)
        ya = composition_at(x / U_FLOW)
        print(f"  {xs:>8}  {g(ya):>22}  {g(temperature(ya)):>22}")
    print()
    print(f"  outlet at x = 2.0 m : Y_A = {g(composition_at(mpf(2) / U_FLOW))}")
    print(f"  integrated conversion 1 - Y_A/Y_A0 = "
          f"{g(1 - composition_at(mpf(2) / U_FLOW) / YA0)}")
    print()

    print("--- a self-check of the quadrature: t(Y_A) inverted and re-evaluated ---")
    for ts in ["0.1", "0.5", "1.0"]:
        t = mpf(ts)
        ya = composition_at(t)
        back = time_at(ya)
        print(f"  t = {ts:>5}  ->  Y_A = {g(ya)}  ->  t = {g(back)}   residual {g(back - t)}")


if __name__ == "__main__":
    main()
