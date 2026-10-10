#pragma once

// G8 closure, step 4 -- THE STEADY ONE-DIMENSIONAL PREMIXED FLAME.
//
// This is the case G8 requires and that P18.7 could not provide: "a 1D laminar premixed flame:
// flame speed, peak temperature, species profiles". It is a DEDICATED reacting path, not a
// conditional bolted onto an existing solver (CLAUDE.md section 6), and it changes no existing
// behaviour.
//
// ===================================================================================
// WHAT IS SOLVED, and why no momentum equation appears
// ===================================================================================
//
//     mdot dY_i/dx = d/dx(rho D_i dY_i/dx) + omega_i          i = 1..N
//     mdot cp dT/dx = d/dx(lambda dT/dx)   + S_E
//
// in the frame of the unburnt gas, at CONSTANT PRESSURE. In that frame the steady continuity
// equation is d(rho u)/dx = 0, so the mass flux `mdot = rho u` [kg/(m^2 s)] is UNIFORM and known
// once its value is known. There is nothing left for a momentum equation to determine except the
// pressure, and at the Mach numbers of a laminar flame (S_L / c ~ 1e-3) the pressure is constant to
// the order of the terms retained. So this solver carries NO SIMPLE/PISO loop, NO pressure
// correction and NO velocity field: it builds the face mass flux DIRECTLY from the scalar `mdot`.
// The thermal expansion is still fully present -- rho(x) = p Mbar(x) / (R T(x)) -- and the velocity
// u(x) = mdot/rho(x) rises across the front exactly as it must; it is simply never solved for.
//
// This is the standard freely-propagating-flame formulation, and it is why the flame speed can be
// obtained without any of the flow machinery.
//
// ===================================================================================
// THE EIGENVALUE, and the degeneracy that rules out the obvious method
// ===================================================================================
//
// `mdot` is not an input: it is an EIGENVALUE. The flame speed is S_L = mdot / rho_u.
//
// The tempting way to find it is the consumption-speed integral
//
//     S_c = -1/(rho_u Y_F,u) * integral omega_F dx
//
// iterated to a fixed point against mdot. THAT METHOD IS DEGENERATE AND CANNOT WORK. Integrate the
// fuel equation over a domain in which the flame is detached from the inlet (so dY_F/dx -> 0 at
// both ends) and the fuel is fully consumed:
//
//     mdot (Y_F,outlet - Y_F,u) = [rho D dY_F/dx] + integral omega_F dx
//         =>  -mdot Y_F,u = integral omega_F dx  =>  S_c == mdot / rho_u
//
// identically, for ANY mdot that admits a steady solution. The integral is an exact consequence of
// the conservation law, so it reproduces whatever mdot it was given and contains NO information
// about the eigenvalue. A fixed-point iteration on it converges instantly from any starting guess
// and is completely vacuous -- it would have looked like a converged flame speed while measuring
// only the initial guess.
//
// WHAT ACTUALLY SELECTS THE EIGENVALUE is the cold boundary. In a finite domain with a Dirichlet
// inlet, a flame sitting at position x_f fixes mdot (too small an mdot and the front is driven
// back into the inlet, where the Dirichlet condition quenches it; too large and it is blown
// downstream), and mdot(x_f) PLATEAUS as x_f moves away from the inlet. That plateau is the
// physical eigenvalue -- the classical cold-boundary difficulty and its standard resolution.
//
// WHY THE INNER SOLVE IS UNSTEADY, which is not a performance choice but a well-posedness one.
// The obvious segregated scheme solves the STEADY equations and drives mdot from the position
// error, mdot <- mdot (1 - gain (x_f - x_anchor)/L). That was built first and it does not work,
// for a reason that is worth recording because it is the plateau itself:
//
//   * the plateau means mdot(x_f) is nearly FLAT near the eigenvalue, so its inverse x_f(mdot) is
//     nearly VERTICAL. A position-driven controller is inverting a flat function, so it must use a
//     small gain or oscillate;
//   * meanwhile EXTINCTION IS AN ABSORBING STATE. With a steady inner solve the temperature field
//     relaxes toward the solution for the current mdot, and if that mdot admits no flame the peak
//     falls below the reaction threshold within a handful of iterations; once the gas is cold the
//     Arrhenius rate is negligible and nothing reignites it.
//
// Measured on the synthetic flame: the peak temperature fell 1800 -> 1501 -> 1276 -> 1088 K while
// mdot moved by 4% per iteration. The controller lost the race by more than an order of magnitude,
// and no seed accuracy fixes that in general because the conditioning is intrinsic.
//
// An unsteady march fixes the collapse: implicit Euler stops the field falling apart while mdot is
// still wrong, and the front then DRIFTS at u_in - S_L, which is a kinematic measurement rather
// than the inverse of a flat function.
//
// A CONTINUOUS UPDATE ON THAT DRIFT STILL FAILS, and the reason is a timescale violation worth
// recording. The obvious step is mdot <- mdot - rho_u * drift, which has a gain of exactly one
// from kinematics and no tuned constant at all. But the flame's STRUCTURE responds on its own
// transit time t_f = alpha/S_L^2, while this update changes mdot every time step. Measured on the
// synthetic flame: t_f was 0.55 s against dt = 1.5 ms, so the controller was ~360x faster than the
// physics it was measuring. It overshot the eigenvalue, and past that point the drift stops
// meaning propagation at all -- with mdot driven to ~0 the measured drift stayed POSITIVE, because
// what the crossing was tracking was the conduction profile of a dying flame rather than a front.
// No gain repairs this, and t_f is not known in advance, so no schedule can be set either.
//
// WHAT IS ROBUST IS THE SIGN. At a FIXED mdot, a healthy flame is washed downstream when mdot
// exceeds the eigenvalue and flashes upstream when it falls short, and that sign does not depend on
// knowing any timescale -- only on marching long enough for the structure to have responded, which
// a fixed-length trial does. So the eigenvalue is found by BRACKET AND BISECT on sign(drift):
//
//     trial(mdot)  = march a fixed number of steps, take the mean drift over the SECOND HALF
//     bracket      = double or halve mdot until the sign changes
//     bisect       = geometric midpoint, because mdot is a positive scale and not an offset
//
// The result is BRACKETED, so what is reported as the converged residual is a genuine bound on the
// eigenvalue rather than the last step of a controller. There is no gain anywhere in it.
//
// FRONT RECENTRING is what lets a trial run as long as it needs. The profile is translated by a
// WHOLE NUMBER of cells to bring the front back to the anchor -- exact on a uniform grid, so it
// introduces no interpolation error of its own -- and the displacement is accumulated so the
// front's true travel, and therefore the drift, is unaffected.
//
// This is the segregated counterpart of the fixed-temperature-point constraint a coupled solver
// (Cantera's FreeFlame) adds as an extra row with mdot as an extra unknown.
//
// THE PLATEAU MUST BE DEMONSTRATED, NOT ASSUMED. `anchorFraction` and `domainLength` are knobs
// that the answer must be insensitive to, and a flame speed quoted without that demonstration is
// quoting the anchor position. Its verification is part of G8's evidence, not an afterthought.
//
// THE CONSUMPTION INTEGRAL IS STILL COMPUTED and reported as `consumptionSpeed`, for what it IS
// good for: because the identity above is exact for a converged, conservative discretisation,
// S_c == mdot/rho_u is a sharp CONSISTENCY check on the discrete solution -- it detects an
// unconverged profile, a leaking boundary or a stoichiometry error. It is NOT validation and it is
// NOT the eigenvalue condition. Labelled accordingly (CLAUDE.md section 4).
//
// ===================================================================================
// WHAT THIS DELIBERATELY DOES NOT DO
// ===================================================================================
//
//   * no clipping, clamping or renormalisation of Y_i or T. P18's invariant throughout. A tiny
//     negative Y_i left by a linear solve is REPORTED (`minMassFraction`), never repaired.
//   * cp is CONSTANT, and that is a modelling commitment rather than a convenience: the adiabatic
//     flame temperature of a one-step mechanism is T_u - dHr Y_F,u / (nu_F M_F cp), so cp alone
//     fixes T_ad. It must be supplied as a value derived from CITED thermodynamic data and frozen
//     before any measurement.
//   * lambda(T) is VARIABLE, supplied as a cfd::physics::TemperatureProperty. This matters for
//     integrity, not just accuracy: S_L ~ sqrt(lambda), so a single hand-picked constant lambda
//     IS a hand-picked flame speed, and it would have been the largest free parameter in a gated
//     measurement. A cited lambda(T) law removes that freedom. Note that the energy equation has
//     no overload taking per-cell properties AND a per-cell source, so the source is added to the
//     assembled RHS by the production function assembleThermalSourceContribution -- which is
//     RHS-only by construction, so this composition is exact rather than approximate.
//   * unity Lewis number for every species, so rho D_i = lambda / cp. With equal diffusivities
//     P18.2's cross-species closure correction is identically zero (sum_j J_j^0 = -rho D grad(sum_j
//     Y_j) = 0), so it neither helps nor threatens positivity here; it is left enabled regardless.
//   * no radiation, no Soret/Dufour, no pressure dependence, no dissociation (the one-step
//     mechanism oxidises completely, so T_ad carries no equilibrium dissociation deficit).
#include <cstddef>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/HeatRelease.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"

namespace cfd::species {

// The chemistry, as non-owning pointers: every one of these is a live P18 object built elsewhere
// (CaseBuilder, or a test), and this solver stores no copy so there is no second basis that can
// drift from the first.
struct PremixedFlameChemistry {
  const ReactionMechanism* mechanism{nullptr};
  const MixtureThermodynamics* mixture{nullptr};
  const FormationEnthalpies* formation{nullptr};
  const ReactionKinetics* kinetics{nullptr};
};

// The physical state and the transport properties. EVERY MEMBER OF THIS STRUCT IS AN INPUT TO THE
// FLAME SPEED, so every one must be cited and frozen before a measurement is made -- see the
// header note on lambda.
struct PremixedFlameConditions {
  Real pressure{101325.0};               // [Pa], constant
  Real unburntTemperature{298.0};        // [K]
  std::vector<Real> unburntComposition;  // Y_i,u in species-index order

  // lambda(T) [W/(m K)], non-owning and REQUIRED -- there is no default, because a default would
  // be an uncited constant standing in the most sensitive position in the whole measurement.
  // A ConstantProperty is accepted and is then an explicit, visible choice.
  const cfd::physics::TemperatureProperty* conductivity{nullptr};

  Real specificHeat{};    // cp [J/(kg K)], constant -- it fixes T_ad (header note)
  Real lewisNumber{1.0};  // rho D_i = lambda / (cp Le)
  Index fuelIndex{0};     // which species the consumption integral is taken on
};

struct PremixedFlameSettings {
  Index cells{400};
  Real domainLength{0.02};  // [m]

  // Where the front is held, as a fraction of the domain, and the temperature that defines "the
  // front". Both are method parameters the answer must be INSENSITIVE to (header note).
  Real anchorFraction{0.35};
  Real anchorTemperatureFraction{0.5};  // T_anchor = T_u + f (T_ad - T_u)

  Index maxOuterIterations{4000};

  // The width of the final bracket on mdot, relative: (high - low)/low. Because the eigenvalue is
  // BRACKETED rather than approached, this is a bound on the answer and not merely the size of the
  // last step.
  Real tolerance{1.0e-4};

  // THE IMPLICIT-EULER TIME STEP, set per trial from scales that do NOT involve S_L.
  //
  // An earlier version derived dt from the Zel'dovich seed, dt = f * alpha/S_est^2. That couples
  // dt to the SQUARE of a seed that can be several times wrong, and a seed 4x high made dt 16x too
  // small, so a fixed-length trial covered about ONE flame transit time instead of the ten or
  // twenty needed. The drift then measured the seeded ramp relaxing diffusively rather than the
  // flame propagating: +1.3 m/s at an inflow of 4.6e-5 m/s, which is the diffusive spreading rate
  // sqrt(alpha dt)/dt and is kinematically impossible for a front.
  //
  // Both scales below are MESH scales, known in advance:
  //
  //     dt = min( cellCourant * dx / u_in ,  maximumCellDiffusionNumber * dx^2 / alpha_max )
  //
  // The first keeps the front moving a fraction of a cell per step, so the drift is resolved. The
  // second caps dt where u_in is small, because an unbounded dt makes each step the STEADY solution
  // and brings back everything that made the steady formulation fail. Implicit Euler is
  // unconditionally stable, so neither is a stability limit -- they are resolution limits.
  //
  // 0 for timeStep selects this; a positive value overrides it for a deliberate study.
  Real timeStep{0.0};
  Real cellCourant{0.3};

  // THE CELL DIFFUSION NUMBER CAP, VALIDATED AGAINST A MEASURED ERROR (TD-027). It was 20.0, and
  // that value was never checked against anything.
  //
  // WHAT IT ACTUALLY CONTROLS, which is sharper than "a cap". At the eigenvalue the diffusion
  // branch of the dt expression binds -- the Courant branch binds only early, while mdot is still
  // near the Zel'dovich seed -- so dt is set to exactly `cap * dx^2 / alpha_max` and
  //
  //     D = alpha_max * dt / dx^2  ==  cap,   identically.
  //
  // This is NOT an upper bound that is loosely respected: the cap IS the realized cell diffusion
  // number. And because dt scales as dx^2, D is INDEPENDENT OF dx -- refining the mesh cannot
  // improve it, at 8x the cost for no gain in this parameter. The cap is the only lever.
  //
  // THE MEASUREMENT (TD-027 audit, from a dt-only self-similar family at fixed dx -- the first
  // such family ever run on this solver, because dt had always been refined together with dx):
  //
  //     cap     S_L cm/s     error vs the converged value
  //      20      4.86230        +3.99 %     <- the OLD default. OUTSIDE the asymptotic range:
  //                                            the error changes sign between 20 and 10.
  //      10      4.65615        -0.42 %
  //       5      4.67194        -0.08 %     <- this default
  //     2.5      4.67510        -0.02 %
  //
  //     Richardson limit over {10, 5, 2.5}, p = 2.321:  S_L = 4.67589 cm/s,  GCI 0.021 %
  //
  // So the old default put a GATED quantity 3.99 % off, against G8's acceptance band of +-2.80 % --
  // the resolution error exceeded the band it was to be judged against. Every S_L figure recorded
  // in this repository before TD-027, including the 4.86230 quoted throughout TD-004 and TD-005,
  // carries that error.
  //
  // WHY 5 AND NOT ANOTHER VALUE. Three requirements, and 5 is the largest value meeting all three:
  //   * inside the asymptotic range with margin -- the boundary is between 20 and 10, so 5 is 2x
  //     inside it and a convergence statement is meaningful;
  //   * the temporal error at most one tenth of G8's band (0.28 %), so the time discretisation is
  //     not a material contributor to a gated comparison. 5 gives 0.084 %; 10 gives 0.422 % and
  //     fails this;
  //   * COHERENT WITH THE PRECISION THE SOLVER REPORTS. `massFluxResidual` publishes the
  //     eigenvalue's uncertainty as sqrt(1+tolerance)-1, i.e. 0.05 % at tolerance 1e-3. At cap 20
  //     the temporal error was 80x that; at 10 it is 8.4x; at 5 it is 1.7x. A result reporting a
  //     bracket far finer than its own discretisation error claims a precision it cannot honour.
  //
  // 1.0 would be the textbook condition -- dt within a single cell-diffusion time -- and is ~20x
  // the cost of this default, which breaches the repository's 7200 s per-test cap. It is the right
  // value for a convergence study and not affordable as a default; set it explicitly for one.
  //
  // THE COST IS REAL AND IS NOT HIDDEN: 4x the step count. G8PremixedFlameTest goes from ~62 min
  // to ~4.1 h, longest case ~3640 s against the 7200 s cap.
  Real maximumCellDiffusionNumber{5.0};

  // A SHORT STARTUP RAMP ON dt, and nothing more.
  //
  // THERE IS NO DENSITY-CHANGE CONTROLLER HERE ANY MORE, and its removal is worth recording. One
  // was built to tame the velocity amplification of the finite-difference continuity closure, where
  // d(rho)/dt grew as 1/dt. The non-conservative time term (see PremixedFlame.cpp) removed that
  // closure altogether, which left the controller solving a problem that no longer existed -- and
  // it then DEADLOCKED the march: the flame's natural per-step density change at a Courant number
  // of 0.3 is about 2.5%, measured and stable, against a 2% limit, so dt was cut on every step,
  // never reached its target, and the drift window that requires a settled dt never opened. The
  // measured symptom was a settling curve that was empty at every march length: drift = 0,
  // measured = 0, steps = 0.
  //
  // Raising the limit until it passed would have been tuning a bound to obtain a result. Deleting a
  // mechanism whose justification had gone is the actual fix. The per-step density change is still
  // MEASURED and reported, as worstDensityChange, because 2.5% is a useful fact about the march.
  Real timeStepGrowth{1.5};
  Real startupTimeStepDivisor{100.0};

  // Further under-relaxation on top of the time step. 1.0 -- no extra damping -- is the default
  // because implicit Euler already damps the march; these remain available for a stiffer
  // mechanism. They set the convergence rate, never the converged state.
  Real temperatureRelaxation{1.0};
  Real compositionRelaxation{1.0};

  // HOW A TRIAL ENDS: when the DRIFT HAS SETTLED, not after a fixed number of steps.
  //
  // A fixed count cannot be right, because the number of steps in a flame transit time depends on
  // the flame thickness and the trial velocity, neither of which is known before the trial. So the
  // drift is measured over successive windows and the trial ends when consecutive windows agree.
  //
  // The agreement is judged against u_in rather than against the drift itself, because the drift
  // approaches ZERO at the eigenvalue and a relative test on it would become ill-conditioned
  // exactly where the search spends most of its time.
  Index driftWindow{32};
  Real driftSettleTolerance{0.02};  // |d_k - d_{k-1}| < tol * u_in
  Index settledWindowsRequired{2};
  Index maximumStepsPerTrial{200000};

  // THE MINIMUM TRIAL LENGTH IS DERIVED FROM THE STRUCTURE RELAXATION TIME, not fixed.
  //
  // A trial may not read its drift until the flame STRUCTURE has relaxed, and the time that takes
  // is set by diffusion across the preheat zone:
  //
  //     delta_preheat = alpha_burnt / S      t_relax = delta_preheat^2 / alpha_burnt
  //                                                  = alpha_burnt / S^2
  //
  // with S = mdot/rho_u for the trial. The old fixed 128 steps was about HALF a relaxation time at
  // the eigenvalue, and every outstanding symptom traced back to it:
  //
  //   * the eigenvalue came out wrong -- the final march at the "converged" mdot flashed the flame
  //     all the way back to the inlet, 206 cells upstream of the anchor;
  //   * S_L depended on the drift window at the 4.3% level, because a longer window is simply a
  //     longer trial and the structure was still relaxing;
  //   * the fuel balance sat 42% out, because what was being measured was a recentring limit cycle
  //     rather than a steady flame.
  //
  // This is a CONVERGENCE REQUIREMENT, not an accuracy tolerance, and the answer must be shown
  // insensitive to it.
  Real relaxationTimesPerTrial{3.0};
  Index minimumStepsPerTrial{128};  // a floor only; the derived value normally dominates

  // THE CHEMISTRY HALF OF THE OPERATOR SPLIT. A sub-step may consume at most this fraction of any
  // species' local mass, which is the same stability control ChemistryIntegrator uses and for the
  // same reason. It is NOT an accuracy tolerance -- accuracy is established by refinement.
  Real chemistryFractionalChange{0.1};
  std::size_t maximumChemistrySubSteps{20000000};

  // THE FINAL MARCH, which produces the state that gets REPORTED.
  //
  // The drift settling that ends a search trial is a weaker condition than steadiness: the front
  // can stop moving while the profile behind it is still filling in. Measured consequence -- a
  // state marched only until the drift settled (128 steps, about 3.6 flame transit times) gave a
  // consumption integral of 2.58 cm/s against a flame speed of 4.50 cm/s, a 43% violation of an
  // identity that is exact for a steady flame.
  //
  // So after the eigenvalue is bracketed, the state is marched again at that mass flux until the
  // STATE stops changing, and it is that state whose profiles and balances are reported.
  // THE FINAL MARCH IS BOUNDED BY RELAXATION TIMES, not by a large fixed count.
  //
  // `finalMarchSteps` is a SAFETY CEILING only. The march stops once the structure has relaxed, by
  // the same t_relax = alpha_u/S^2 criterion the trials use, because the state-change tolerance
  // below is unreachable by construction (F13): a bracketed eigenvalue leaves a residual front
  // drift, and a front creeping at that speed changes a cell by ~1.6e-5 of the rise per step,
  // four orders above 1e-9. So `steadyStateReached` could only ever be false and the march ran its
  // ENTIRE budget every time -- 40000 steps at ~56 ms, about 37 minutes of pure waste per solve,
  // which turned a three-test run into an estimated three hours.
  //
  // The tolerance is kept as a reporting threshold: if it is ever met, the state is steadier than
  // the relaxation bound requires and stopping early is correct.
  Real finalMarchRelaxationTimes{10.0};
  Index finalMarchSteps{200000};     // ceiling; the derived bound normally stops the march
  Real finalMarchTolerance{1.0e-9};  // max|dT|/(T_ad-T_u) and max|dY_i| per step

  // THE RECENTRING DEAD BAND, in cells, and it is not a cosmetic threshold.
  //
  // Recentring translates the profile by a whole number of cells to bring the front back to the
  // anchor. That translation is NOT CONSERVATIVE on a finite domain -- it discards whatever sat in
  // the cells shifted off one end -- and the loss is ASYMMETRIC:
  //
  //   shift +1  discards cell 0, which holds FRESH FUEL        -> loses rho Y_u dx of fuel
  //   shift -1  refills cell 0 with Y_u, which it already held -> loses nothing
  //
  // Without a dead band, a front sitting half a cell off the anchor is snapped back every step and
  // then oscillates +1, -1, +1, -1. Measured: recentrings == steps + 67 over marches of 1000, 4000
  // and 16000 steps, i.e. one translation EVERY step, leaking about one cell of fuel every two.
  // That showed up as a fuel-balance residual pinned at 0.4177 independent of march length -- the
  // signature of a steady leak rather than a transient -- and as a per-step temperature change of
  // 0.35 of the full rise, which made the state look permanently unsteady. One cause, both
  // symptoms.
  //
  // With a dead band the front is left alone while it stays within `recentreDeadBandCells` of the
  // anchor, so at the eigenvalue -- where it barely drifts -- recentring becomes rare instead of
  // constant. It cannot bias the eigenvalue: it only decides WHEN the frame is translated, and the
  // accumulated displacement is tracked exactly either way.
  Real recentreDeadBandCells{3.0};

  // THE SEEDED PREHEAT WIDTH, as a multiple of alpha_burnt/S, so there is far less for the march to
  // relax away.
  //
  // The flame has TWO thickness scales and the seed was built on the wrong one. The inner layer is
  // (T_ad - T_u)/max|dT/dx| ~ 0.67 mm, but the PREHEAT extent is alpha_burnt/S_L ~ 4.7 mm -- larger
  // by the expansion ratio, since alpha = lambda/(rho cp) and rho falls sixfold across the front.
  // The measured profile shows the preheat spanning 10 to 15 mm, which matches the burnt-side scale
  // and not the inner one. Seeding at 0.8 mm left a sixfold relaxation to perform on every trial.
  Real seedWidthFactor{1.0};

  // How many doublings/halvings the bracketing phase may use before giving up. Each one is a
  // factor of two, so 12 covers a seed wrong by 4000x in either direction.
  Index maximumBracketSteps{12};

  Real initialMassFlux{0.0};  // <= 0 selects the Zel'dovich seed

  // G8's FIRST NEGATIVE CONTROL, as a setting rather than an edit: with reactions off the rate is
  // an explicit zero everywhere and no flame can propagate. Production always leaves this true.
  bool reactionsEnabled{true};
};

// ONE TRIAL of the eigenvalue search, recorded. This is evidence, not debug scaffolding: a flame
// speed obtained by a bracketing search is only as good as the bracket, and the trial log is what
// shows the bracket was real -- that the sign genuinely changed, which side each trial fell on, and
// whether the decision came from a measured drift or from an extinction.
struct PremixedFlameTrial {
  Real massFlux{};       // the mdot tried [kg/(m^2 s)]
  Real drift{};          // settled front drift measured [m/s]
  Real timeStep{};       // the dt this trial marched with [s]
  Index steps{};         // how many steps it took to settle
  bool measured{false};  // a drift was actually measured
  bool settled{false};   // the drift settled, rather than the step budget running out
  bool extinguished{false};
  bool tooLarge{false};  // the side this trial assigned to its mdot
};

enum class PremixedFlameStatus {
  Converged,
  MaxIterations,
  NoFlame,  // the anchor temperature is never crossed: nothing is burning
  LinearSolveFailure,
  NonFiniteState,
  InvalidConfiguration,
};

struct PremixedFlameResult {
  PremixedFlameStatus status{PremixedFlameStatus::MaxIterations};

  Real massFlux{};          // mdot [kg/(m^2 s)]
  Real flameSpeed{};        // S_L = mdot / rho_u  [m/s]   <- THE GATED QUANTITY
  Real massFluxEstimate{};  // the Zel'dovich seed, reported so the starting point is visible
                            // and the independence of the answer from it can be judged
  Real consumptionSpeed{};  // from the omega integral: a CONSISTENCY check, not validation
  Real unburntDensity{};    // rho_u [kg/m^3]

  Real peakTemperature{};       // max T over the domain [K]
  Real adiabaticTemperature{};  // T_ad from the enthalpy balance [K], for comparison
  // x where T crosses T_anchor [m], MEASURED from the reported temperature field -- the first
  // crossing from the inlet, which is the edge the cold boundary controls. 0 if the profile never
  // crosses tAnchor. Until TD-005 this member was assigned `anchorFraction * domainLength`, an
  // INPUT, so it could not report that the front had left the anchor; see
  // results/technical_debt/TD005_flame_speed_extraction/logs/02_field_dump.log.
  //
  // Compare it against `anchorFraction * domainLength` to see whether the anchoring actually held.
  // Do NOT use `worstFinalOffsetCells` for that: it is updated only on steps where the CONTROL
  // locator accepted a crossing, so it cannot see the large offsets it appears to report.
  Real flamePosition{};
  Real flameThickness{};  // (T_ad - T_u) / max|dT/dx| [m], the diffusive thickness

  Index outerIterations{};  // bisections used
  Index bracketSteps{};     // doublings/halvings used to bracket the eigenvalue
  Index totalTimeSteps{};   // implicit-Euler steps marched in total, over every trial
  Index recentrings{};      // whole-cell translations applied to keep the front at the anchor

  // Split out from `recentrings`, which counts the SEARCH TRIALS too. Conflating the two is what
  // made the recentring rate look like one translation per step when the trials were responsible
  // for most of them -- so the two are now counted separately and the offset that drove them is
  // reported, rather than inferred from a difference of totals.
  Index finalMarchRecentrings{};
  Real worstFinalOffsetCells{};

  // TD-004 diagnostics, added while root-causing the long-march imbalance.
  //
  // The front moves S_L*dt ~ 0.09 cells per step and the recentring dead band is 3 cells, so an
  // offset of 163.96 cells -- 55x the dead band -- cannot be physical front motion. These two
  // separate the candidate explanations:
  //
  //   worstFrontJumpCells  the largest SINGLE-STEP change in the located front position. A value
  //                        of order the dead band means the front is creeping and the locator is
  //                        tracking it. A value of order 100 cells means `locateFront` JUMPED --
  //                        it takes the FIRST tAnchor crossing from c = 0, so a spurious upstream
  //                        crossing would silently report a different feature as "the front".
  //   largestShiftCells    the largest |shift| the recentring actually applied. The ledger probe
  //                        measured that ONE 164-cell shift moves 84 % of the domain's fuel, so if
  //                        shifts that large are being applied, the balance cannot survive them.
  Real worstFrontJumpCells{};
  Index largestShiftCells{};

  // TD-004: how often the front locator and the recentring REFUSED to act, and why that count has
  // to be reported rather than left silent.
  //
  // The locator takes the tAnchor crossing nearest the previous front, among those within a
  // physically admissible jump. A profile can cross tAnchor more than once -- any upstream wiggle
  // produces a spurious crossing -- and the original locator took the FIRST crossing from c = 0,
  // so it could silently report a different feature as "the front". Measured before the fix: a
  // 327.51-cell single-step jump against ~0.64 cells of physically possible motion, which the
  // recentring then executed as a 164-cell shift, moving 84 % of the domain's fuel inventory.
  //
  // A rejection is the fix working. A LOT of rejections means the profile really does have
  // multiple crossings and the flame is not what the solver thinks it is -- a different problem,
  // and one that must not hide behind a locator that quietly declines to look. So the counts are
  // reported: silence here would be the same fail-open the original locator had.
  Index rejectedFrontCrossings{};
  Index rejectedShifts{};

  // TD-004: WHERE the spurious crossings are, which is what decides what they are.
  //
  // The refusal counter established that up to 46 % of march steps present more than one tAnchor
  // crossing, but not what the extra one IS. tAnchor sits at 1050 K for this fixture -- mid-front,
  // in the steep-gradient region -- so an extra crossing there is not boundary noise in a flat
  // zone. These two localise it:
  //
  //   maxFrontCrossings        the most crossings seen in any single locate. 2 means one spurious
  //                            feature; many means an oscillation.
  //   farthestRejectedPosition [m] the rejected crossing farthest from the accepted front. Near 0
  //                            implicates the inlet, near the domain length the outlet, and
  //                            just behind the front implicates the march's own wake.
  Index maxFrontCrossings{};

  // TD-004: the DISTRIBUTION of rejected jumps, which is what separates the two live explanations.
  //
  // `maxFrontCrossings` came back as 1, so there is never more than one tAnchor crossing and the
  // "profile has multiple crossings" reading is dead. The single crossing is simply being refused.
  // Two candidates remain and they need different fixes:
  //
  //   rejections clustered at 5-10 cells  -> the admissible-jump bound is too TIGHT. A front
  //                                          sitting anywhere inside a +-3 cell dead band can jump
  //                                          6 cells between steps, which already exceeds the
  //                                          5.1-cell bound derived from S_L*dt alone.
  //   rejections at tens of cells         -> the front genuinely jumps, and there is a real
  //                                          position oscillation to chase.
  //
  // A previous attempt at this reported a position rather than a distance, and its update
  // condition compared a new distance against one recomputed from a stale position relative to the
  // CURRENT front -- incoherent, so the field never updated and read 0.0000 mm. Buckets are used
  // here instead of a single maximum because a maximum cannot show clustering.
  Real worstRejectedJumpCells{};
  // WHERE the worst rejected jump LANDED, and how many landed on the anchor. The front cannot move
  // 168 cells in a step, so something is RECONSTRUCTING the temperature field rather than moving
  // the front. `seedProfile()` puts the front exactly at the anchor, and 168 cells is ~3.4 mm
  // against an anchor at 3.50 mm -- so if these jumps terminate at the anchor, a mid-march re-seed
  // is the cause and the defect is upstream of both the locator and the recentring.
  //
  // Updated together with worstRejectedJumpCells in a single comparison, which is what the
  // previous attempt got wrong: it compared a new distance against one recomputed from a stale
  // position relative to the CURRENT front, so the field never updated.
  Real worstRejectedJumpPosition{};
  Index rejectedJumpsLandingAtAnchor{};
  Index rejectedJumpsUnder10Cells{};
  Index rejectedJumps10To50Cells{};
  Index rejectedJumpsOver50Cells{};  // max |x_front - x_anchor| / dx seen during the final march
  std::size_t chemistrySubSteps{};   // total chemistry sub-steps over the whole search: the price
                                     // the stiffness actually cost, reported rather than hidden
  Real worstLinearResidual{};        // max over every inner solve of ||r||/||b||, so the health of
                               // the linear algebra is a measured number and not an assumption
  // TD-031: THE PER-STEP STATE CHANGE, AS A BOUND AND LOCALISED.
  //
  // `temperatureResidual` below holds the LAST judged step's change, and it reads 0.989 at states
  // that three independent quantities agree are converged -- the front inside the dead band, the
  // fuel balance flat to 2.6e-04, and the thickness identical to four figures. Six configurations
  // across three meshes and two caps all return 0.989 +- 0.0002, which is a saturated quantity
  // rather than a measurement, and it is why `finalMarchTolerance` is unreachable at ANY threshold
  // and not merely at 1e-9 (the header's F13 note gives a different reason, now superseded).
  //
  // THE MECHANISM IS NOT KNOWN. `recentre()` runs before the comparison, so a frame translation is
  // included -- but the measured shifts are <= 4 cells, which at the steepest gradient accounts
  // for ~0.06 of the rise, not 0.99. These fields are therefore built to LOCALISE the change
  // rather than to assume where it comes from:
  //
  //   worstStateChange             the max over EVERY judged step, so it is a BOUND rather than
  //                                the last step's sample
  //   worstStateChangeCell         WHERE it occurred. Cell 0 or nc-1 implicates the end fill a
  //                                translation performs; the front region implicates gradient x
  //                                shift; anywhere else implicates neither
  //   worstStateChangeWasRecentred whether recentring fired on that step, which separates the
  //                                translation from the physics without guessing which matters
  //   worstStateChangeNoRecentre   the same max over steps on which recentring did NOT fire. This
  //                                is the frame-following steadiness, and it is the quantity a
  //                                convergence criterion needs (TD-031 derivation.md, clause (a))
  //
  // All four are READ-ONLY: they feed nothing, change no control flow, and the existing
  // `temperatureResidual` and the steadiness break are untouched. Verified bitwise.
  Real worstStateChange{};
  Index worstStateChangeCell{};
  bool worstStateChangeWasRecentred{};
  Real worstStateChangeNoRecentre{};

  Real worstDensityChange{};  // max over the march of |d rho|/rho in one step. Reported, not
                              // controlled -- about 2.5% is this flame's natural level

  // ---- THE CONSERVATION BALANCES ON THE REPORTED STATE, dimensionless.
  //
  // These exist because the obvious checks on this solver are VACUOUS, and that was found the hard
  // way. The seeded profile blends to the BURNT composition and the adiabatic temperature
  // downstream, so at step zero -- before any chemistry has run -- the outlet already holds
  // Y_fuel == 0.0 exactly and T == T_ad exactly. A test asserting "the fuel is consumed" or "the
  // peak reaches T_ad" therefore passes on the initial condition and measures nothing about the
  // flame. Both of those passed while the consumption integral was 43% out, which is the worst
  // possible arrangement: the vacuous checks were the reassuring ones.
  //
  // The balances below cannot be satisfied by the seed. They are integral identities that hold for
  // a converged steady flame and for nothing else:
  //
  //   fuel    mdot (Y_out - Y_u)      = integral omega_F dx  + inlet diffusive influx
  //   energy  mdot cp (T_out - T_u)   = integral S_E dx      + inlet conductive influx
  //
  // Each residual is normalised by the corresponding through-flux, so a small number means the
  // discrete solution actually conserves and not merely that the quantities are small.
  // TD-004: THE RECENTRING LEDGER -- what the frame shift added to or removed from the domain,
  // accumulated over the whole march. [kg/m^2] for fuel, [J/m^2] for energy.
  //
  // WHY IT EXISTS. Recentring translates the profile by a whole number of cells, which is exact in
  // INTERPOLATION and silent about CONSERVATION: each shift discards |s| cells of state at one end
  // and fabricates |s| at the other from a fill value. Measured: one single-cell shift moves
  // 9.97e-07 of fuel, and at the 164-cell offset the march reaches, one shift moves 84 % of the
  // domain's entire fuel inventory. The operator's own comment said it "introduces no error of its
  // own", which is true of interpolation and false of conservation -- and nothing measured the
  // difference.
  //
  // WHAT IT IS, AND WHAT IT IS NOT. In a finite window translating over an unbounded domain,
  // material MUST enter and leave -- a shift that changed no totals would not be a shift. So this
  // is not an error to be driven to zero; it is the quantity that has to be VISIBLE so that frame
  // motion is attributable to the frame instead of to the chemistry. It is computed as the content
  // integral before the shift minus after, which is exact by construction and cannot drift out of
  // step with the operator it measures.
  //
  // Gate criterion C1 compares these against the directly recomputed integrals;
  // results/technical_debt/TD004_premixed_flame/GATE.md section 3.1 derives the bound.
  Real recentringFuelLedger{};
  Real recentringEnergyLedger{};
  // The same, divided by the marched time, so it is comparable with the flux balances below.
  Real recentringFuelLedgerRate{};    // [kg/(m^2 s)]
  Real recentringEnergyLedgerRate{};  // [W/m^2]

  Real fuelBalanceResidual{};
  Real energyBalanceResidual{};

  Index finalMarchSteps{};     // steps the final steady march actually took
  bool steadyStateReached{};   // whether it met finalMarchTolerance rather than the budget
  Real timeStep{};             // the dt actually marched with [s]
  Real frontDrift{};           // last trial's mean drift [m/s]; straddles 0 at the eigenvalue
  Real massFluxResidual{};     // (high - low)/low: a genuine BRACKET on mdot, not a step size
  Real temperatureResidual{};  // final max|dT|/(T_ad - T_u)
  Real compositionResidual{};  // final max|dY_i|

  // QUALITY DIAGNOSTICS, reported because the two P18.1 invariants this solver cannot hold to
  // their state-boundary tolerance are exactly the two worth watching. Both must be small in the
  // evidence; neither is repaired.
  Real minMassFraction{};             // most negative Y_i anywhere (0 if none went negative)
  Real maxCompositionSumDeviation{};  // max_cell |sum_i Y_i - 1|

  // Every trial, in the order it ran. The bracket is only credible if the sign changed here.
  std::vector<PremixedFlameTrial> trials;

  // Set whenever the status is not Converged: which equation failed, at which cell, and with what
  // numbers. A status enum alone sends the reader back to the debugger, which is the one thing a
  // numerical failure report must not do.
  std::string diagnostic;

  // Profiles, for the L1/L2 and refinement reporting G8 asks for.
  std::vector<Real> x;                           // cell centroids [m]
  std::vector<Real> temperature;                 // [K]
  std::vector<Real> density;                     // [kg/m^3]
  std::vector<Real> velocity;                    // u = mdot/rho [m/s]
  std::vector<Real> heatRelease;                 // S_E [W/m^3]
  std::vector<std::vector<Real>> massFractions;  // [species][cell]

  [[nodiscard]] bool converged() const noexcept { return status == PremixedFlameStatus::Converged; }
};

// Solve the steady 1D freely-propagating premixed flame.
//
// Throws InvalidArgumentError for a malformed configuration (null chemistry, a composition of the
// wrong length, a non-finite or non-positive lambda/cp/pressure/temperature, a fuel index out of
// range, settings outside their valid ranges). A legitimate numerical outcome -- no flame, a
// linear-solve failure, outer max-iterations -- is REPORTED via `status`, never thrown, the same
// convention as SpeciesSolver::solve and ThermalSolver::solve.
[[nodiscard]] PremixedFlameResult solvePremixedFlame(const PremixedFlameChemistry& chemistry,
                                                     const PremixedFlameConditions& conditions,
                                                     const PremixedFlameSettings& settings);

// The adiabatic flame temperature of the given mixture under THIS SOLVER'S OWN energy equation,
// from the enthalpy balance
//
//     T_ad = T_u - Delta H_r^0 Y_F,u / (nu_F M_F cp)
//
// Exposed separately because it is what `anchorTemperatureFraction` is measured against, because it
// is the quantity the solved peak temperature must approach, and because it depends on the constant
// `cp` -- which makes it the cheapest possible check that the frozen cp is the one intended.
[[nodiscard]] Real flameAdiabaticTemperature(const PremixedFlameChemistry& chemistry,
                                             const PremixedFlameConditions& conditions);

// The fully burnt composition reached by consuming all the fuel through the mechanism's
// stoichiometry: Y_i,b = Y_i,u + (Y_F,u / (nu_F M_F)) nu_i^net M_i. Used for the initial profile
// and reported so the products the flame is marching towards can be checked independently.
[[nodiscard]] std::vector<Real> flameBurntComposition(const PremixedFlameChemistry& chemistry,
                                                      const PremixedFlameConditions& conditions);

}  // namespace cfd::species
