# G10 — exclusions audit and qualification test registration

Generated 2026-09-25T14:48:03Z. `ctest -N` only; nothing was executed.

## Registry sizes

```text
release_cuda   2550 registered tests
debug_gui      2612 registered tests
cpu_only       2348 registered tests
```

## Step 5 — the documented exclusions, enumerated

Frozen set, copied verbatim from `results/p16/turbulence/tools/regress_both.sh`
(p16 mode) and matching `PLAN_2026-09-24.md`. Five items; not widened.

```text
test                                                           REGISTERED  EXCLUDED  config
----                                                           ----------  --------  ------
SSTAutomaticWallChannel.TheDefaultCaseFileIsBitwiseTheHighRePath YES         YES       release_cuda
SSTAutomaticWallChannel.HighReSweep                            YES         YES       release_cuda
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate     YES         YES       release_cuda
SSTAutomaticWallChannel.LowReStressSweep                       YES         YES       release_cuda
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedLowReGate      YES         YES       release_cuda
SSTAutomaticWallChannel.IsDeterministic                        YES         YES       release_cuda
SSTAutomaticWallChannel.StaysWellBehavedOnMeshesTheHighRePathRejects YES         YES       release_cuda
SSTCompositeWallChannel.DiscriminatingMeshesAtReTau590         YES         YES       release_cuda
SSTCompositeWallChannel.HighRePathIsStillBitwiseUnchanged      YES         YES       release_cuda
SSTMomentumFluxChannel.FourWayComparisonAtReTau590             YES         YES       release_cuda
WallArchitectureDiagnostic.LowReSSTOnTheMeshesTheAutomaticTreatmentFailed YES         YES       release_cuda
WallArchitectureDiagnostic.Ny16IsTheDecidingCase               YES         YES       release_cuda
DriverSeegmiller.CorrectedMeshSequence                         YES         YES       release_cuda
SSTAutomaticWallChannel.TheDefaultCaseFileIsBitwiseTheHighRePath YES         YES       debug_gui
SSTAutomaticWallChannel.HighReSweep                            YES         YES       debug_gui
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate     YES         YES       debug_gui
SSTAutomaticWallChannel.LowReStressSweep                       YES         YES       debug_gui
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedLowReGate      YES         YES       debug_gui
SSTAutomaticWallChannel.IsDeterministic                        YES         YES       debug_gui
SSTAutomaticWallChannel.StaysWellBehavedOnMeshesTheHighRePathRejects YES         YES       debug_gui
SSTCompositeWallChannel.DiscriminatingMeshesAtReTau590         YES         YES       debug_gui
SSTCompositeWallChannel.HighRePathIsStillBitwiseUnchanged      YES         YES       debug_gui
SSTMomentumFluxChannel.FourWayComparisonAtReTau590             YES         YES       debug_gui
WallArchitectureDiagnostic.LowReSSTOnTheMeshesTheAutomaticTreatmentFailed YES         YES       debug_gui
WallArchitectureDiagnostic.Ny16IsTheDecidingCase               YES         YES       debug_gui
DriverSeegmiller.CorrectedMeshSequence                         YES         YES       debug_gui
SSTAutomaticWallChannel.TheDefaultCaseFileIsBitwiseTheHighRePath YES         YES       cpu_only
SSTAutomaticWallChannel.HighReSweep                            YES         YES       cpu_only
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate     YES         YES       cpu_only
SSTAutomaticWallChannel.LowReStressSweep                       YES         YES       cpu_only
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedLowReGate      YES         YES       cpu_only
SSTAutomaticWallChannel.IsDeterministic                        YES         YES       cpu_only
SSTAutomaticWallChannel.StaysWellBehavedOnMeshesTheHighRePathRejects YES         YES       cpu_only
SSTCompositeWallChannel.DiscriminatingMeshesAtReTau590         YES         YES       cpu_only
SSTCompositeWallChannel.HighRePathIsStillBitwiseUnchanged      YES         YES       cpu_only
SSTMomentumFluxChannel.FourWayComparisonAtReTau590             YES         YES       cpu_only
WallArchitectureDiagnostic.LowReSSTOnTheMeshesTheAutomaticTreatmentFailed YES         YES       cpu_only
WallArchitectureDiagnostic.Ny16IsTheDecidingCase               YES         YES       cpu_only
DriverSeegmiller.CorrectedMeshSequence                         YES         YES       cpu_only
```

### Reasons and evidence

| suite | frozen reason | evidence |
|---|---|---|
| `SSTAutomaticWallChannel` | deferred automatic wall treatment, attempt 2 (4th-power blend of two friction velocities) | `results/p16/turbulence/automatic_wall/SUMMARY.md` |
| `SSTCompositeWallChannel` | deferred automatic wall treatment, attempt 4 (Spalding composite law) | same |
| `SSTMomentumFluxChannel` | deferred automatic wall treatment, attempt 5 (F_U = -rho u_tau u*) | same |
| `WallArchitectureDiagnostic` | diagnostic accompanying the three deferred treatments | same |
| `DriverSeegmiller.CorrectedMeshSequence` | preserved failing separated-flow benchmark, ~1100 s alone | `results/p16/turbulence/` + `run_p20_known_failures.sh` |

All five are deferred to **P20**, which is NOT AUTHORIZED. G9 re-ran all four
automatic-wall suites directly and confirmed the failure set is exactly the
documented four -- neither grown nor shrunk -- with the 8 passing tests in
those same suites still passing:

```text
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate            366 s  FAILED
SSTAutomaticWallChannel.StaysWellBehavedOnMeshesTheHighRePathRejects  375 s  FAILED
SSTCompositeWallChannel.DiscriminatingMeshesAtReTau590                377 s  FAILED
SSTMomentumFluxChannel.FourWayComparisonAtReTau590                    594 s  FAILED
```

So the exclusion audit rests on a live re-measurement, not a citation.

## Step 6 — qualification-specific tests, verified BY NAME

Added or repaired during G2-G8. Total count growth is not evidence that a
particular row survived, so each is checked by name per configuration.

```text
expected test                                              release_cuda  debug_gui   cpu_only  origin
-------------                                              ------------  ---------   --------  ------
CliGuiEquivalenceTest.TwoDimensionalCaseAgreesBitwise      --            YES         --        G4
CliGuiEquivalenceTest.ThreeDimensionalCaseAgreesBitwise    --            YES         --        G4
CliGuiEquivalenceTest.ACaseEditedInTheGuiStillAgrees       --            YES         --        G4
CliGuiEquivalenceTest.AThermalCaseAgreesBitwise            --            YES         --        G4 F2 NEW
CliGuiCheckpointTest.TheGuiCanCheckpointAndTheFileMatchesTheClis --            YES         --        G4
CliGuiCheckpointTest.ACheckpointCrossesBetweenTheClients   --            YES         --        G4 CLI->GUI
CliGuiCheckpointTest.ACheckpointWrittenByTheGuiResumesInTheCli --            YES         --        G4 F1 NEW GUI->CLI
CliGuiCheckpointTest.AnIntervalWithoutAPathIsRejectedAsTheCliRejectsIt --            YES         --        G4
CliGuiCheckpointTest.ARunWithoutCheckpointOptionsReportsNothing --            YES         --        G4
ExportWriteFailureTest.EveryWriterReportsAFailedWrite      YES           YES         YES       G5
ExportWriteFailureTest.TheFailureNamesTheFile              YES           YES         YES       G5
ExportWriteFailureTest.AHealthyWriteStillSucceeds          YES           YES         YES       G5 N3
CFDAppCliLogLinesNeverReachStdout                          YES           YES         YES       G6 N5
CFDAppCliLogLevelIsDocumented                              YES           YES         YES       G6
CFDAppCliLogLevelUnknownIsRejected                         YES           YES         YES       G6 V2
CFDAppPackageNaming                                        YES           YES         YES       G7
CFDAppPackageNamingRefusesUnknownSystem                    YES           YES         YES       G7 NV5
CFDAppBenchmarkSuiteSmoke                                  YES           YES         YES       G8 S3
CheckpointProductionCaseTest.CheckpointingATurbulentCaseIsRefusedByName YES           YES         YES       G2 P16
CheckpointProductionCaseTest.ResumingATurbulentCaseIsRefusedByName YES           YES         YES       G2 P16
CFDAppCliCheckpointFlagsAreDocumented                      YES           YES         YES       G4 V3
CFDAppCliCheckpoint_every_not_a_number                     YES           YES         YES       G4 V3
CFDAppCliCheckpoint_every_zero                             YES           YES         YES       G4 V3
CFDAppCliCheckpoint_resume_missing_file                    YES           YES         YES       G4 V3
```

`--` in the cpu_only column for a CUDA test is EXPECTED and is not a silent
exclusion: the target is not built when `CFDAPP_ENABLE_CUDA=OFF`. The
cpu_only column is reported so that absence is visible rather than assumed.

## GPU turbulence gates (G9 / P16), by name

```text
expected test                                              release_cuda  debug_gui   cpu_only 
-------------                                              ------------  ---------   -------- 
DeviceTurbulenceAlgebra.StrainRateProductionAndViscosityAreBitwise YES           YES         --       
DeviceTurbulenceAlgebra.ExactlyThreeOperationsReportGpu    YES           YES         --       
DeviceTurbulenceAlgebra.BothFloorsActivateAndMatchTheHost  YES           YES         --       
DeviceTurbulenceAlgebra.AForcedFallbackIsDetectedForEachOperation YES           YES         --       
DeviceTurbulenceAlgebra.PerturbingOneInputCellChangesOnlyThatOutput YES           YES         --       
DeviceTurbulenceAlgebra.TheSameInputsProduceTheSameBytesTwice YES           YES         --       
DeviceTurbulenceAlgebra.PromotedViscosityMatchesTheProductionModel YES           YES         --       
DeviceKTransport.EffectiveDiffusivityIsBitwise             YES           YES         --       
DeviceKTransport.EffectiveDiffusivityFailsClosedLikeTheHost YES           YES         --       
DeviceKTransport.SourceCoefficientsAreBitwise              YES           YES         --       
DeviceKTransport.MatrixStructureCoefficientsAndRhsMatchTheHost YES           YES         --       
DeviceKTransport.SolvedKMatchesTheProductionSolve          YES           YES         --       
```

## Unexpected exclusions

An unexpected exclusion would be a test present in the registry that the
invocation filter removes WITHOUT being one of the five documented items.
Computed directly:

```text
release_cuda   tests matched by the filter: 13   all of them documented: YES
debug_gui      tests matched by the filter: 13   all of them documented: YES
cpu_only       tests matched by the filter: 13   all of them documented: YES
```
