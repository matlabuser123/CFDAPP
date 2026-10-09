<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Conservation on device (E5's independent clause)

The frozen criterion is explicit that equivalence is not enough:

> `Σᵢ ωᵢ` must vanish **INDEPENDENTLY on each backend** to `32·ε·max|ω|` — §14: "A GPU result that
> matches CPU but violates mass conservation is not acceptable. Equivalence does not replace
> correctness."

## Measured on the device, independently

`Σᵢ ωᵢ` is summed **in the kernel**, in ascending species order, and compared against
`32·ε` relative to **each cell's own** largest source term — not to a global maximum, which would be
looser on every quiet cell and would stop meaning what it says.

Result: within bound for every cell, over 1 024 cells × 3 species. The test also asserts that some
cell had a nonzero source, or the bound would be vacuous, and that at least one cell was exercised —
a guard that was itself **fail-open** in its first version, since `Index` is `std::size_t` and a
`-1` sentinel wraps to `2⁶⁴−1`, making `EXPECT_GE(worstCell, 0)` trivially true. Replaced with a
boolean.

## Why the bound is round-off and nothing else

For both reactions in the equivalence mechanism, `Σᵢ M_i ν_i` is **exactly zero**: the weights
0.0625, 0.125 and 0.1875 are dyadic and sum to zero against `ν = (−1,−1,+1)`. So the residual is
pure accumulation round-off, which is what `32·ε` bounds. The choice of dyadic weights is deliberate
— it removes the coefficient representation error from the measurement.

## Elemental conservation is NOT modelled

Stated plainly because its absence is easy to miss: no P18 phase models elemental conservation. The
conservation checked here is mass conservation through `Σ M_i ν_i = 0`, which is a property of the
stoichiometry as written. A mechanism whose stoichiometry did not conserve elements would pass this
criterion.

## Global reacting balance

Not established on device. P18.7 closed the conservation and validation work on the **CPU** path
(G3/G7); the device has the thermochemistry operators and a closed reactor, not a flow solve, so
there is no global reacting balance to integrate over. See `kernel_inventory.md` §3.
