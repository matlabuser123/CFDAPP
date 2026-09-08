"""Plotting -- reads a ResultData (or plain arrays) and writes PNG/CSV
files. Never computes anything the C++ solver itself is responsible for
(TODO.md "P1 -- Python Tooling" section 1); velocity magnitude/contour
reconstruction are the only "postprocessing quantities" this package is
allowed to derive (section 15).

Headless by construction: every submodule forces the non-interactive
"Agg" matplotlib backend before importing pyplot, since this package is
meant to run in scripts/CI without a display.
"""

import matplotlib

matplotlib.use("Agg")
