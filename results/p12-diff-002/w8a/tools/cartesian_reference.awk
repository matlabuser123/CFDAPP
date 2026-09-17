# RMS over the ny cell rows of (discrete - exact), exactly as cartesianVelocityL2 does,
# for the superseded two-point Cartesian solution (what the helper computes) and for the
# DIFF-002 Cartesian solution (UC-001 gate section 1; PoiseuilleValidationUtils.cpp:185).
BEGIN {
  H = 1.0; U = 1.0
  split("8 12 18", nys, " ")
  printf "%4s %22s %22s %10s\n", "ny", "two-point (helper)", "DIFF-002 Cartesian", "ratio"
  for (k = 1; k <= 3; ++k) {
    ny = nys[k]; n2 = ny * ny; dy = H / ny
    g2 = 12.0 * U / (H * H) * n2 / (n2 + 2.0)
    gd = 12.0 * U / (H * H) * (2.0 * n2) / (2.0 * n2 + 1.0)
    s2 = 0; sd = 0
    for (j = 0; j < ny; ++j) {
      y = (j + 0.5) * dy
      ex = 6.0 * U * (y / H) * (1.0 - y / H)
      d2 = 0.5 * g2 * y * (H - y) + g2 * dy * dy / 8.0
      dd = 0.5 * gd * y * (H - y)
      s2 += (d2 - ex) ^ 2; sd += (dd - ex) ^ 2
    }
    a = sqrt(s2 / ny); b = sqrt(sd / ny)
    printf "%4d %22.10e %22.10e %10.4f\n", ny, a, b, a / b
  }
}
