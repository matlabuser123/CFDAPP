# Superseded: gate rerun with the first version of the BiCGSTAB fix

These logs and data come from the unchanged-gate rerun, backward-compatibility comparison and
focused tests made with the **first** version of the solver-robustness fix: the norm-relative
breakdown test |(x, y)| ≤ ε‖x‖‖y‖, without restart. The gate passed with it, but the full
regression then failed 16 tests ([../logs/27a](../logs/27a_FAILED_full_regression_release_norm_criterion.log)).
The criterion was replaced by the term-magnitude test plus a bounded restart
(`summary.md` §23), and everything was rerun with the final code (`../logs/15`–`29`, `../data/`).
They are kept as a record, not as evidence for the final state.
