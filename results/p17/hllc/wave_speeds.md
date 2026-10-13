# P17.7 — wave speeds, the contact speed, and non-degeneracy

Covers §9, §10 and §25 of the authorization. This is the document that records P17.7's most
consequential finding: **the wave-speed estimator the authorization suggested is not signal-bounding,
and that was found before any code existed.**

---

## 1. The implemented formulation, stated exactly

§9 says *"Do not merely write 'standard HLLC'. State the actual formulas in evidence."*

```text
OUTER WAVE SPEEDS -- Toro 3rd ed. eqs. 10.59-10.61, via the PVRS pressure estimate (eq. 9.28):

  p_pvrs = 0.5 (p_L + p_R) - 0.125 (u_nR - u_nL)(rho_L + rho_R)(a_L + a_R)
  p*     = max(0, p_pvrs)

  q_K    = 1                                                  if p* <= p_K   (rarefaction side)
         = sqrt( 1 + (gamma+1)/(2 gamma) (p*/p_K - 1) )        if p*  > p_K   (shock side)

  S_L    = u_nL - a_L q_L
  S_R    = u_nR + a_R q_R

CONTACT SPEED -- Toro 3rd ed. eq. 10.37:

              p_R - p_L + rho_L u_nL (S_L - u_nL) - rho_R u_nR (S_R - u_nR)
  S*  =  -----------------------------------------------------------------------
                  rho_L (S_L - u_nL) - rho_R (S_R - u_nR)

implemented as  qL = rho_L (S_L - u_nL),  qR = rho_R (S_R - u_nR),
                S* = (p_R - p_L + qL u_nL - qR u_nR) / (qL - qR)
```

`a` is never computed here: it arrives in `PrimitiveState` from P17.2's
`ThermodynamicProperties::speedOfSound(T) = sqrt(gamma R T)`. `gamma` is passed in from the same
object. There is no thermodynamics in the flux layer.

## 2. WHY NOT DAVIS — the finding, measured before implementation

The authorization names Davis's direct bounds (Toro eq. 10.48) as *"a common admissible baseline"*:

```text
  S_L = min(u_nL - a_L, u_nR - a_R)        S_R = max(u_nL + a_L, u_nR + a_R)
```

Measured against the independent 60-digit exact Riemann solver in `tools/exact_riemann.py`, **before
`HllcFlux.hpp` existed** (`logs/01_reference.log`):

```text
                         Davis (10.48)              Toro PVRS (10.59)
case                     S_L<=left  S_R>=right      S_L<=left  S_R>=right
uniform_x                OK         OK              OK         OK
uniform_oblique          OK         OK              OK         OK
sod_literature           OK         *** FAILS       OK         OK
sod_atmospheric          OK         *** FAILS       OK         OK
sod_y                    OK         *** FAILS       OK         OK
sod_z                    OK         *** FAILS       OK         OK
stationary_contact       OK         OK              OK         OK
moving_contact           OK         OK              OK         OK
supersonic_right         OK         *** FAILS       OK         OK
supersonic_left          OK         *** FAILS       OK         OK
strong_pressure_ratio    OK         *** FAILS       OK         OK
strong_density_ratio     OK         OK              OK         OK
near_sonic_left          OK         OK              OK         OK
low_pressure             OK         *** FAILS       OK         OK
--------------------------------------------------------------------------
NOT signal-bounding:     8 of 14                    0 of 14
```

The concrete failure, for Sod:

```text
Davis gives     S_R = 1.1831394376381719746
the true right shock travels at
                      1.7521 (exact solution, 60 digits)
```

```text
WHY THIS MATTERS AND WHY IT IS NOT AN ACCURACY QUESTION. The requirement on a wave-speed estimator is
not that it be accurate; it is that it ENCLOSE the true signal speeds. S_R < (true rightmost wave)
means the right-going shock outruns the estimated fan, so signal leaves the control volume that the
flux was built for. That is a STABILITY defect in any scheme built on top, and P17.8 is exactly such
a scheme. Shipping a flux layer with a known non-bounding estimator would have handed P17.8 a defect
dressed as a foundation.
```

The authorization anticipated this: §9 says *"or a stronger established estimate"* and *"Do not copy
this blindly if another verified formulation is selected. Document the exact formulation and
reference/derivation used."* That is what this document does.

### Davis is kept — as the control

Because the failing estimator is real, the non-vacuity control for the bounding criterion is a real
defect rather than a hypothetical one. `WaveSpeedEstimator::Davis` remains selectable and
`HllcWaveSpeedTest.W4_NonVacuity_DavisFailsTheSameCriterion` asserts that it **still fails in at
least 5 cases** — so if W4 ever became vacuous, that test breaks.

```text
measured, in the implementation: Davis fails W4 in 8 of 14 cases, matching the reference exactly,
and the implementation's Davis speeds agree with the reference's to 8 eps.
```

## 3. Measured agreement with the reference

`logs/03_hllc_measured.log`, criterion W1:

```text
worst |S - S_reference| / max(|S_reference|, a_max)  over S_L, S*, S_R, 15 cases
    2.172e-16      against the derived bound 8 eps = 1.776e-15
```

Criterion W2, the strict ordering `S_L < S* < S_R`: holds in **15 of 15** cases.

```text
  case                            S_L            S*            S_R    |den|/bound
  uniform_x                  -313.798            30        373.798     1.000000
  uniform_oblique            -324.084       19.7143        363.512     1.000000
  sod_literature             -1.18314      0.676167        1.75209     1.000000
  sod_atmospheric            -376.612       215.235        557.719     1.000000
  stationary_contact         -1065.22            -0        1065.22     2.350835
  moving_contact              -1015.22            50        1115.22     2.350835
  tangential_shear           -376.612             0        376.612     1.000000
  supersonic_right            523.388       989.681        1336.68     1.000000
  supersonic_left            -1336.68      -810.319       -523.388     1.000000
  strong_pressure_ratio      -3766.12       2687.72        10935.4     1.000000
  strong_density_ratio       -11909.5             0        11909.5   999.001998
  near_sonic_left                1e-09       376.612        753.224     1.000000
  low_pressure               -10.8314       7.91535        20.0198     1.000000
```

(Values as printed by the test; the Sod `S_R = 1.75209` is the pressure-based estimate correctly
enclosing the true shock speed of 1.7521, where Davis gave 1.1831.)

## 4. §10 — the degenerate denominator, and why NO epsilon is used

§10 asks for the contact denominator's conditioning to be inspected, and forbids *"an arbitrary large
epsilon without justification"*.

**The justification is that no epsilon is needed.** The denominator is provably bounded away from
zero:

```text
Both estimators satisfy  q_K >= 1, hence
      S_L = u_nL - a_L q_L   =>   (S_L - u_nL) = -a_L q_L  <=  -a_L  <  0
      S_R = u_nR + a_R q_R   =>   (S_R - u_nR) = +a_R q_R  >=  +a_R  >  0
therefore
      denom = rho_L (S_L - u_nL) - rho_R (S_R - u_nR)
            <= -(rho_L a_L + rho_R a_R)   <   0
so     |denom| >= rho_L a_L + rho_R a_R > 0  for ANY admissible pair of states.
```

The bound is **tight and attained**: when `u_nL = u_nR` and `a_L = a_R`, the min/max pick each side's
own value and the inequality becomes an equality.

```text
MEASURED, criterion W3: the smallest |denom| / (rho_L a_L + rho_R a_R) over every case is
    1.000000000000000
i.e. the bound is reached exactly, confirming the derivation rather than merely not contradicting it.
The largest ratio is 999.002 (the 1e6 density-ratio case).
```

```text
A FIRST VERSION OF THE REFERENCE SCRIPT GOT THIS WRONG, and the error is instructive: it tested the
bound with a strict `>=` on 60-digit values and reported FALSE for the two supersonic cases. The bound
does not fail there -- it is ATTAINED there, and the last digits of two different summation orders
disagree. The script now reports the RATIO instead of a boolean, which cannot be fooled that way, and
the C++ criterion carries a 4 eps slack for exactly the equality case.
```

### Consequently there is NO fallback flux

§15 permits a fallback *"only if justified by actual numerical failure cases or established design"*.
Since the denominator cannot degenerate for admissible inputs, there is no failure to fall back from,
and adding a silent switch to a more diffusive flux would be precisely the *"silently change solver
type"* that §10 forbids.

```text
WHAT IS DONE INSTEAD:
  * inadmissible INPUTS are refused, with a typed reason and a named message (11 cases tested)
  * the star states are checked anyway, in hllcFluxChecked(), and a violation throws NumericalError
    describing itself as a DEFECT IN THIS LAYER rather than as bad input -- because under the proof
    above it can only be that. A proof that is never tested is an assumption.
  * there are no fallback counters, because there is no fallback. Criterion S5 asks for them to read
    zero; it is reported as NOT APPLICABLE with this reason rather than as a satisfied criterion.
HLL exists in the header but is NOT reachable from hllcFlux() -- it is the V2 control, and its header
comment says so.
```

## 5. §25 — sonic points and near-zero wave speeds

```text
near_sonic_left: u_n is placed so that u_n - a = 1e-9 exactly, i.e. S_L is within 1e-9 of zero.
  S_L    = 1.000000e-09
  branch = F_L                (S_L >= 0, so the supersonic selection is taken)
  S*, S_R, flux: all finite
```

Branch selection is by `>=`/`<=` comparison against zero, so it is continuous in the sense that
matters: as `S_L` passes through zero the returned flux passes between `F_L` and `F*_L`, and at
`S_L = 0` the star state reduces to the left state (`rho*_L = rho_L (S_L-u_nL)/(S_L-S*)`), so both
branches agree in the limit. No entropy fix from another solver family is introduced; §25 forbids
that unless the chosen formulation requires one, and Toro's HLLC does not.

```text
A NOTE ON NEGATIVE ZERO. For the stationary contact the numerator is exactly 0 and the denominator is
strictly negative, so S* evaluates to -0.0. IEEE gives -0.0 >= 0.0 == true, so the LeftStar branch is
taken, and (S_L - (-0.0)) == S_L exactly, so rho*_L == rho_L exactly and the mass flux is exactly
zero. The sign of the zero is therefore visible in the printed S* but changes nothing. Recorded
because it looks like an anomaly in the log and is not one.
```

## 6. What is NOT claimed about the wave speeds

```text
NOT CLAIMED   that the PVRS estimate bounds the true signal speeds for EVERY conceivable state. It is
              measured to do so for all 14 reference cases, which span pressure ratios to 1e5,
              density ratios to 1e6, Mach to ~2.4 and a near-sonic point. It is an ESTIMATE, and a
              proof of universal bounding is not offered and would not be true of any cheap estimator.
NOT CLAIMED   that S* approximates the exact contact speed u* accurately. It does not, for strong
              jumps: measured |S* - u*|/|u*| is 0.271 for Sod and 0.705 for the strong pressure ratio.
              S* is a WAVE-SPEED ESTIMATE inside an approximate Riemann solver, not a prediction of
              u*, and the criteria treat it accordingly.
NOT CLAIMED   anything about a time step. CFL selection belongs to P17.8.
```
