"""Automation: launches the compiled cfdapp CLI and measures it. Never
imports or reimplements any C++ solver mathematics (TODO.md "P1 --
Python Tooling" section 31) -- these modules only start a subprocess and
interpret its exit code / exported files.
"""
