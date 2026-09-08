"""Analytical planar-Poiseuille solution (TODO.md "P1 -- Python Tooling"
section 23), the same closed form
tests/integration/poiseuille/PoiseuilleValidationUtils.cpp already uses
on the C++ side -- this is an independent Python re-derivation, not a
port of that file, so agreement between the two is itself evidence
neither has a transcription bug.
"""

from __future__ import annotations

import numpy.typing as npt


def velocity_from_pressure_gradient(
    y: npt.ArrayLike, channel_height: float, dynamic_viscosity: float, pressure_gradient: float
) -> npt.ArrayLike:
    """u(y) = -(1/(2*mu)) * (dp/dx) * y * (H - y).

    `pressure_gradient` is dp/dx (negative for flow in +x, the usual
    convention -- see section 23's own formula).
    """
    return -(1.0 / (2.0 * dynamic_viscosity)) * pressure_gradient * y * (channel_height - y)


def velocity_from_mean_velocity(
    y: npt.ArrayLike, channel_height: float, mean_velocity: float
) -> npt.ArrayLike:
    """u(y) = 6 * mean_velocity * (y/H) * (1 - y/H) -- the same
    mean-velocity-parameterized form
    PoiseuilleValidationUtils.cpp's `analyticalPoiseuilleVelocity` uses,
    useful when the inlet's prescribed uniform velocity (the mean, by
    mass conservation) is known but dp/dx is not.
    """
    eta = y / channel_height
    return 6.0 * mean_velocity * eta * (1.0 - eta)


def max_velocity(
    channel_height: float, dynamic_viscosity: float, pressure_gradient: float
) -> float:
    """u_max = H^2/(8*mu) * (-dp/dx), at the centerline y=H/2."""
    return (channel_height**2 / (8.0 * dynamic_viscosity)) * (-pressure_gradient)


def mean_velocity_from_pressure_gradient(
    channel_height: float, dynamic_viscosity: float, pressure_gradient: float
) -> float:
    """u_mean = (2/3) * u_max."""
    return (2.0 / 3.0) * max_velocity(channel_height, dynamic_viscosity, pressure_gradient)


def pressure_gradient_from_mean_velocity(
    channel_height: float, dynamic_viscosity: float, mean_velocity: float
) -> float:
    """dp/dx = -12*mu*mean_velocity / H^2 -- the inverse of
    `mean_velocity_from_pressure_gradient`, matching
    PoiseuilleValidationUtils.cpp's `analyticalPressureGradient`.
    """
    return -12.0 * dynamic_viscosity * mean_velocity / (channel_height**2)


def flow_rate(mean_velocity: float, channel_height: float) -> float:
    """Q = mean_velocity * H (per unit depth, 2D planar flow)."""
    return mean_velocity * channel_height
