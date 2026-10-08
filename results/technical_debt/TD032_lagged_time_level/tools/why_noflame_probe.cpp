// TD-032 -- WHY does driftWindow 64 return NoFlame? The 14-second answer.
//
// Code reading (PremixedFlame.cpp:1746 and :1789) shows `NoFlame` is set when
// `trialSaysTooLarge(trial, tooLarge)` returns FALSE -- i.e. a trial could not be assigned a SIDE.
// That is NOT the condition the enum documents ("the anchor temperature is never crossed: nothing
// is burning"), so the status is mislabelled on this path and the previous probe's reading of it
// was wrong.
//
// This prints the per-trial record so the cause is a MEASUREMENT rather than an inference: a trial
// needs more than `driftWindow` steps of history before a drift can be read, so a trial that dies
// earlier than that yields no side.
#include <cstdio>
#include <string>
#include <vector>
#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/PremixedFlame.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"
using cfd::Index; using cfd::Real; using namespace cfd::species;
namespace {
constexpr Real kW=0.029, kCp=1200.0, kFuel=0.05, kTu=300.0, kP=101325.0, kCond=0.05;
constexpr Real kEa=1.2e5, kA=3.0e7, kHf=-1.044e6;
SpeciesThermodynamics sp(const std::string& n){return SpeciesThermodynamics(kW,
  cfd::thermal::SensibleEnthalpy(cfd::thermal::TemperatureDependentProperty::constant(kCp,n),298.15,0.0));}
struct Problem{SpeciesRegistry r;MixtureThermodynamics m;FormationEnthalpies f;
  ReactionMechanism me;ReactionKinetics k;};
Problem mk(){SpeciesRegistry r({"F","P"});std::vector<SpeciesThermodynamics> t;
  t.push_back(sp("cp_F"));t.push_back(sp("cp_P"));MixtureThermodynamics m(r,std::move(t));
  FormationEnthalpies f=FormationEnthalpies::fromMolar(m,{0.0,kHf});std::vector<Reaction> rx;
  rx.push_back(Reaction("F->P",r,{{"F",1.0}},{{"P",1.0}}));ReactionMechanism me(r,std::move(rx),m);
  std::vector<ReactionRateParameters> p;
  p.push_back(ReactionRateParameters{"F->P",ArrheniusParameters(kA,0.0,kEa),std::nullopt});
  ReactionKinetics k(me,std::move(p));
  return Problem{r,std::move(m),std::move(f),std::move(me),std::move(k)};}
}
int main(){
  const Problem pb=mk(); const cfd::physics::ConstantProperty cond(kCond);
  const PremixedFlameChemistry ch{&pb.me,&pb.m,&pb.f,&pb.k};
  PremixedFlameConditions c; c.pressure=kP; c.unburntTemperature=kTu;
  c.unburntComposition={kFuel,1.0-kFuel}; c.conductivity=&cond; c.specificHeat=kCp;
  c.lewisNumber=1.0; c.fuelIndex=0;
  for (const Index w : {Index(32), Index(64)}) {
    PremixedFlameSettings s; s.cells=500; s.domainLength=0.010; s.anchorFraction=0.35;
    s.tolerance=1.0e-3; s.maxOuterIterations=40; s.driftSettleTolerance=0.05;
    s.maximumStepsPerTrial=20000; s.driftWindow=w;
    const PremixedFlameResult f=solvePremixedFlame(ch,c,s);
    std::printf("=============== driftWindow %lld\n", (long long)w);
    std::printf("  status      %d   (0=Converged 1=MaxIterations 2=NoFlame)\n",(int)f.status);
    std::printf("  diagnostic  \"%s\"\n", f.diagnostic.c_str());
    std::printf("  trials recorded: %zu   bracketSteps %lld\n", f.trials.size(),
                (long long)f.bracketSteps);
    std::printf("  %4s %14s %14s %8s %9s %8s %13s %9s\n","#","mdot","drift","steps",
                "measured","settled","extinguished","tooLarge");
    for (std::size_t i=0;i<f.trials.size();++i){const auto& t=f.trials[i];
      std::printf("  %4zu %14.6g %14.6g %8lld %9s %8s %13s %9s%s\n", i, t.massFlux, t.drift,
        (long long)t.steps, t.measured?"yes":"NO", t.settled?"yes":"no",
        t.extinguished?"YES":"no", t.tooLarge?"yes":"no",
        (t.steps <= (Index)w) ? "   <- FEWER STEPS THAN THE WINDOW" : "");}
    std::printf("\n");
  }
  std::printf("A trial needs MORE than driftWindow steps of history before a drift can be read.\n");
  std::printf("Any trial marked above as having fewer steps than the window cannot be given a\n");
  std::printf("side, and the search then reports NoFlame -- whatever the field looks like.\n");
  return 0;
}
