"""cfdapp-tools: validation, plotting, and automation for CFDApp.

Consumes files CFDApp's C++ ResultExporter writes (fields.csv,
residuals.csv, metadata.json -- see docs/user_guide/case_format.md and
TODO.md "P1 -- Result Export"). This package never reimplements or
reinterprets the CFD numerical core: it reads already-computed results,
it does not solve anything itself (TODO.md "P1 -- Python Tooling"
section 1).
"""

from cfdapp.results import ResultData, ResultLoadError, load_case_results

__all__ = ["ResultData", "ResultLoadError", "load_case_results"]
