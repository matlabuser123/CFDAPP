from mpmath import mp, mpf, sqrt
mp.dps = 50
R, cp = mpf("287.05"), mpf("1005")
cv = cp - R; g = cp/cv
rho, T = mpf("1.2"), mpf("320")
p = rho*R*T
a = sqrt(g*p/rho)
un = mpf("120")            # interior normal velocity
ke = mpf("0.5")*rho*(un**2 + mpf("40")**2 + mpf("15")**2)
E  = p/(g-1) + ke          # rhoE

# mirrored wall: right state has -un, same rho/p, same |U| so same rhoE
unL, unR = un, -un
dp = (p+p)/2 - mpf("0.125")*(unR-unL)*(rho+rho)*(a+a)
pstar_pvrs = max(dp, mpf(0))
def q(pk):
    if pstar_pvrs <= pk: return mpf(1)
    return sqrt(1 + (g+1)/(2*g)*(pstar_pvrs/pk - 1))
sL = unL - a*q(p); sR = unR + a*q(p)
qL = rho*(sL-unL); qR = rho*(sR-unR)
sStar = (p - p + qL*unL - qR*unR)/(qL-qR)

print("S_L  =", mp.nstr(sL,12), "  S_R =", mp.nstr(sR,12), "  S* =", mp.nstr(sStar,12))

rhoStarL = rho*(sL-unL)/(sL-sStar)
# the p* the Rankine-Hugoniot momentum condition gives
pStar = p + rho*(sL-unL)*(sStar-unL)
print("p       =", mp.nstr(p,12), "   p* (RH) =", mp.nstr(pStar,12), "  (compression => p* > p)")

FL_energy = unL*(E+p)
for name, sign in (("MINUS (as implemented)", -1), ("PLUS  (RH-derived)", +1)):
    X = E/rho + (sStar-unL)*(sStar + sign*p/(rho*(sL-unL)))
    EstarL = rhoStarL*X
    flux = FL_energy + sL*(EstarL - E)
    # the defining property: F*_energy must equal S*(E* + p*)
    target = sStar*(EstarL + pStar)
    print("%-24s E*_L = %-16s  F*_energy = %-16s  S*(E*+p*) = %s"
          % (name, mp.nstr(EstarL,10), mp.nstr(flux,10), mp.nstr(target,10)))
