# P17.1 — boundary density semantics

How density is obtained at a boundary face, per boundary category, and what is verified
rather than assumed.

---

## 1. The single mechanism

There is **one** rule, in `evaluateCompressibleFaceDensity`:

```text
boundary face:   rho_f = EOS( referencePressure + interpolateFace(p_gauge, pressureBC),
                              interpolateFace(T, temperatureBC)  or  T[face.owner()] )
internal face:   rho_f = interpolateInternalFace(density)
```

So boundary density is **EOS-derived at that face's own interpolated thermodynamic
state** — not prescribed, not extrapolated, not copied from the owner cell. This is
P12-COMP-001, which superseded an earlier owner-cell-reuse simplification; that earlier
behaviour is recorded in `results/p12-comp-001/summary.md` as a documented
simplification rather than a bug.

The classification the authorization asks for:

```text
explicitly prescribed   NO
EOS-derived             YES  <- this is the mechanism
extrapolated            NO
interpolated            the STATE (p_gauge, T) is interpolated to the face; the
                        DENSITY is then evaluated from that state, not interpolated
copied from owner       NO, superseded by P12-COMP-001. The one case where the owner's
                        value IS used is the isothermal temperature (see section 3),
                        and there it is exact rather than an approximation.
solver-computed         NO
```

### Why this is the right construction

Interpolating density directly to a boundary face would produce a `rho_f` that does not
satisfy `rho_f = p_f/(R T_f)` for the face's own state — a thermodynamically
inconsistent face. Evaluating the EOS at the interpolated state cannot do that: the
face state and the face density agree by construction.

## 2. Per category

The assembler is **polymorphic**: it only ever calls
`boundaryValue(ownerValue, normalDistance)` through `ScalarBoundaryCondition`, so it has
no special case for any BC class. What differs per category is which *velocity* BC
applies, because that decides the flux.

```text
velocity BC     face velocity          resulting boundary flux         density role
------------------------------------------------------------------------------------
Inlet           the prescribed value   rho_f * dot(U_in, Sf)           FULL -- the
                                       (negative: inflow)              inflow mass rate
                                                                       is set by rho_f
Outlet          zero-gradient, so the  rho_f * dot(U_owner, Sf)        FULL
                owner's velocity       (positive: outflow)
Wall            no-slip, U_f = 0       EXACTLY 0                        NONE -- the flux
                                                                       is zero for any
                                                                       density
MovingWall      the wall's tangential  dot(U_t, Sf) = 0 for a face      NONE, same
                velocity               whose normal is not along U_t
Symmetry        mirrored, so the        0 for the normal component      NONE
                normal component is 0
FixedValue      the prescribed value   rho_f * dot(U, Sf)               FULL
```

The Wall row is why `WallBoundaryMassFluxIsZeroRegardlessOfBoundaryDensity` exists and
why it is the right test: on a wall the density is irrelevant to the flux, so a test
that checked wall faces for a density effect would be checking `0 == 0`. That is stated
in the existing test file and is worth preserving as a deliberate non-test.

```text
pressure BC     used for                                  supported
-----------------------------------------------------------------------
FixedValue      p_gauge at the face                        yes
FixedGradient   reconstructs p_gauge from the owner        yes
Symmetry        zero normal gradient                       yes

temperature BC  used for                                  supported
-----------------------------------------------------------------------
FixedTemperature / HeatFlux / Adiabatic /
ConvectiveHeatTransfer / Symmetry                          yes, polymorphically,
                                                           when thermal_coupled

Periodic        DOES NOT EXIST in this codebase -- there is no Periodic BC class at
                all, so it is not a compressible gap. Recorded so a reader does not
                infer a missing compressible feature from a missing BC.
```

## 3. The isothermal temperature path — exact, not a simplification

When `temperatureBoundaries == nullptr` (isothermal mode), the face temperature is
`temperature[face.owner()]` rather than an interpolation. The header states why, and it
is worth restating because it looks like a shortcut and is not: in isothermal mode every
cell holds the *same* configured constant, so the owner's value **is** the boundary
value exactly. No interpolation could produce a different answer.

P17.1's `RowVaryingDensityConservesMassAndScalesFluxPerRow` deliberately exercises this
path with a **non-uniform** temperature field and `nullptr` temperature boundaries. That
is a legitimate use of the API — the owner-value rule is what the code does, and the
test relies on it exactly — and it is what makes the boundary faces reproduce each row's
own density, which in turn is what makes the per-cell imbalance exactly zero.

## 4. Thermodynamic consistency at a boundary — verified

```text
criterion                                            test                                   status
--------------------------------------------------------------------------------------------------
boundary rho equals a DIRECT EOS evaluation at the    BoundaryDensityMatchesDirectEos-        PASS
face's own p and T                                   Evaluation
an outlet uses the EOS at the face pressure, NOT     OutletBoundaryUsesEosEvaluated-         PASS
the owner cell's density                             PressureNotOwnerDensity
an inlet reduces to the owner density when the       InletBoundaryReducesToOwnerDensity-     PASS
pressure gradient is zero                            UnderZeroPressureGradient
a wall carries zero flux for any boundary density    WallBoundaryMassFluxIsZero-             PASS
                                                     RegardlessOfBoundaryDensity
a uniform state gives a uniform boundary density     UniformStateGivesUniformBoundary-       PASS
equal to the interior                                DensityMatchingInterior
a non-positive boundary temperature is REJECTED      ThrowsOnNonPositiveBoundary-            PASS
                                                     Temperature
a non-positive / non-finite cell density is          RejectsNegative/Zero/NonFinite-         PASS
REJECTED before any face is evaluated                CellDensity  (P17.1, new)
```

`InletBoundaryReducesToOwnerDensityUnderZeroPressureGradient` is the one worth pointing
at: it pins down the *one* case where the EOS treatment and the superseded owner-cell
treatment agree, which is what makes the other tests' disagreement meaningful rather
than arbitrary.

## 5. An inconsistent thermodynamic state cannot be produced silently

The authorization asks that pressure and temperature boundary conditions not create an
inconsistent state. They cannot, and the reason is structural:

```text
the face state (p_f, T_f) is interpolated FIRST
rho_f is then EVALUATED from it
=> rho_f = p_f/(R T_f) holds at the face by construction, for any BC combination
```

And if a BC produces a non-physical state, the EOS rejects it rather than returning a
value: `IdealGasEOS::density` validates `p_abs`, `T` **and** the resulting `rho`, each
finite and `> 0`, throwing `InvalidArgumentError` that names the offending field. That
is the path `ThrowsOnNonPositiveBoundaryTemperature` exercises, and P17.1 deliberately
left it intact — adding an up-front temperature check would have made that test throw
earlier and stop reaching the boundary EOS at all.

## 6. Limitation recorded

```text
An invalid TEMPERATURE in a cell that is not adjacent to any boundary face is not
detected by the mass-flux path, because interior temperature is only read when
evaluating a boundary face. On the 2-cell and 3x2 meshes used in the tests every cell
is boundary-adjacent, so the tests cannot distinguish this.

Not fixed in P17.1: temperature validation is the EOS's existing job and a broader
sweep would blunt ThrowsOnNonPositiveBoundaryTemperature (see section 5). Recorded here
as a real, narrow gap rather than closed silently. It would matter to a solver that
writes temperature per cell -- i.e. P17.5.
```
