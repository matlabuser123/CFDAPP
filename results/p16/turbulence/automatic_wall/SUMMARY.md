# P16b -- automatic / mesh-insensitive SST wall treatment: five attempts

Adopted Re_tau = 590 SST gate, UNCHANGED throughout:
E_Re_tau <= 0.059 AND E_u+,Linf <= 1.4, evaluated on the G1-valid subset.
The HighRe path was verified byte-identical after every attempt.

## Results at ny16 (y+ = 37.1), the deciding mesh

| attempt | wall-shear formulation          | E_Re_tau | u+ Linf | verdict            |
|---------|---------------------------------|----------|---------|--------------------|
| --      | HighRe Launder-Spalding         | 0.0059   | 1.044   | PASS (baseline)    |
| 1       | u_tau,log = C_mu^1/4 sqrt(k)    | 0.2950*  | 9.04*   | catastrophic       |
| 2       | u_tau,log = sqrt(tau_LS/rho)    | 0.0698   | 2.565   | FAIL both          |
| 3       | low-Re PDE in the first cell    | 0.5966   | 38.48   | hypothesis refuted |
| 4       | Spalding composite law          | 0.0464   | 1.924   | E passes, u+ fails |
| 5       | F_U = -rho u_tau u* (two-scale) | 0.0698   | 2.561   | FAIL both          |

(*) attempt 1 figures are ny06/ny32; it failed everywhere.

Full Re_tau = 590 family, E_Re_tau:

| mesh | y1+   | HighRe | a2 blend | a4 composite | a5 flux |
|------|-------|--------|----------|--------------|---------|
| ny06 | 92.81 | 0.0556 | 0.0347   | 0.0304       | 0.0342  |
| ny12 | 49.13 | 0.0003 | 0.0483   | 0.0482       | 0.0483  |
| ny16 | 37.06 | 0.0059 | 0.0698   | 0.0464       | 0.0698  |
| ny32 | 18.41 | 0.0018 | 0.1199   | 0.0524       | 0.1243  |

## Why attempt 5 did not move the result

The two-scale flux is measurably distinct from the single-scale blend --
proved at unit level, including at exact equilibrium, where y* is built from u*
rather than u_tau* and leaves the scales 0.35% apart. But 0.35% is the size of
the effect, and the channel confirms it: ny16 gives 0.0698 under both, to four
decimal places.

## The structural obstruction

Any automatic treatment that is continuous into the sublayer must add a viscous
contribution wherever y+ is finite. In a 4th-power blend that contribution is
(u_vis/u_log)^4. At ny16, y+ = 37 and u_vis/u_log ~ 0.72, so the blend raises
u_tau by ~6% and tau_w by ~12% BY CONSTRUCTION, whatever the branches are.

The adopted gate was derived from the HighRe path, which uses the PURE log law
and is near-exact on these meshes (E = 0.0003 at ny12). So on a mesh family
sitting at y+ = 18-95, "continuous to the wall" and "matches the pure log law"
are in direct conflict, and u+ Linf <= 1.4 is the metric that detects it.

Attempt 4 came closest precisely because Spalding's law is a single composite
relation rather than a blend -- but it only becomes this repository's log law
for y+ >~ 70, and it is 1.96% off at y+ = 36.

## Consequence for SST closure

Neither available treatment can close k-omega SST on its own:

  HighRe    : passes the channel, but is structurally inapplicable to the
              separated-flow benchmark -- tau_w = 0 at reattachment BY
              DEFINITION, so its [30,300] validity band can never hold at the
              points the benchmark measures.
  Automatic : any variant tried regresses canonical attached channel flow.

This is a scope question, not a tuning question.

## Scope decision (end of the investigation)

Mesh-insensitive automatic SST wall treatment is **deferred out of P16**. The
five attempts established that the obstruction is structural rather than a
missing constant, a coding defect or one bad blend:

* the conflict is between the **pure-log-law acceptance basis** on which the
  current channel family was validated and an automatic treatment that must
  deliberately depart from that law in the buffer and sublayer region;
* on a mesh family at y+ = 18-95 those two requirements cannot both be met.

Development of `AutomaticWallTreatment` stops here for this milestone. No
further tuning. All five attempts, including every failure, are preserved in
this directory and remain reachable from the case path for future work.

`wall_treatment` values `automatic`, `automatic_spalding` and
`automatic_momentum_flux` are RETAINED but NOT VALIDATED. Only `high_re` --
the default -- is validated. See `../known_limitations.md`.

The best automatic candidate on record is **attempt 4 (Spalding composite)**,
which satisfied E_Re_tau on every mesh and failed only u+ Linf, at ny12 (1.578)
and ny16 (1.924). Anyone resuming this work should start there, and should
first settle the acceptance basis rather than the formulation.
