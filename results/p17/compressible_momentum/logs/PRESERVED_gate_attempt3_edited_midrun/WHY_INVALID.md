# Gate attempt 3 — superseded, and a script edited while it was running

Recorded 2026-09-27. Nothing is deleted. `01_gate_attempt3.log` is the run's own log;
`attempt3_task_stdout.log` is the parent shell's stdout, which is where the damage shows.

## 1. Why attempt 3 is superseded — three reasons, all on its own merits

```text
G6 clang-format     FAILED, exit 74. Four violations in the new test code. A real failure.
L5 probe            DEFECTIVE. It reported physics::MomentumEquation TOUCHED although P17.4
                    never opened it. See AMENDMENT_L5_instrument.md.
suite incomplete    DilatationalStressTest.DefaultIsDisabledBehaviourally did not yet exist,
                    so control N6b had nothing that could observe the default.
```

A clean re-run was required regardless of anything below.

## 2. What I did wrong

While attempt 3's final GPU turbulence tests were still executing, I ran a patch script that
rewrote `tools/run_gate.sh` — **the script that was running.** I had meant to dry-run the
patch against throwaway copies; the command that was supposed to redirect it to a scratch
root died with a Python `SyntaxError` on a Windows path escape, so the redirection never took
effect and the patch hit the real file.

## 3. What that actually did — measured, not assumed

My first instinct was that the tail of the log could not be trusted. **That was wrong, and
the log disproves it**: `01_gate_attempt3.log` is coherent from its header to its `done:`
marker and every section matches the original script's text, including the P16 group and the
G8 probe. So the logged run is a complete, uncorrupted execution of the *original* gate.

The damage is somewhere else, and it is real. A purpose-built probe
(`scratchpad/reparse_probe.sh`) settled the mechanism:

```text
bash does NOT read a script into memory up front. It parses incrementally from a byte
offset. Editing the file mid-run shifts every later offset.
  case A   body wrapped in { ... } | tee, as run_gate.sh is    -> syntax error, tail lost
  case B   plain sequential body                               -> syntax error, tail lost
```

So bash re-read. In `run_gate.sh` the entire body is one `{ ... } 2>&1 | tee "$log"` compound
command, which bash had already parsed and was executing; that is why the *logged* output
stayed intact. When that pipeline finished, bash went back to the file for the next command,
resumed at an offset that now landed inside the **patched** text, and began executing the
patched gate's middle as a sequence of top-level commands. Those commands were no longer
inside the brace group, so their output bypassed `tee` and went to the parent's stdout:

```text
attempt3_task_stdout.log, after the "done:" marker
  line 105: passed: command not found          <- landed mid-token
  build exit: 0  (18 s)                        <- an UNCONTROLLED rebuild
  100% tests passed, 0 tests failed out of 19  <- an UNCONTROLLED partial re-run
  the 11 P17.4 rows ... DefaultIsDisabledBehaviourally  Passed
```

It was still running the compressible suite 21 minutes in and was stopped deliberately. It
had already overwritten `02_momentum.log`, `03_low_mach.log` and `04_compressible_suite.log`;
attempt 4 regenerates all of them.

## 4. What is and is not carried forward

```text
NOT CITED   the stray pass's green numbers, including "19 tests passed" and
            "DefaultIsDisabledBehaviourally Passed". They are encouraging and they are NOT
            evidence: nobody chose when that run started, against which sources, or in which
            order. An accident that returns a welcome answer is still an accident.
NOT CITED   attempt 3's own green numbers. Attempt 4 re-measures every one from scratch.
CARRIED     nothing. Attempt 3 is superseded in full.
```

## 5. The root cause is a repeat, for the fourth time this phase

Every one of these came from composing a shell or Python command inline instead of writing it
to a file and running the file:

```text
1  export PATH=...:$PATH   -> the outer shell expanded $PATH into Windows paths containing
                              parentheses: "syntax error near unexpected token '('"
2  $?, :(exclude) pathspecs -> mangled into plausible-but-wrong values
3  pkill -f "ctest --test-dir build/qual" -> the pattern matched its own parent shell and
                              killed it
4  this one              -> a "dry run" whose redirection silently failed became a live run
```

```text
RULES APPLIED FROM HERE ON
  1. Never edit a script while a process is executing it -- check `ps` first.
  2. Any multi-line or quote-heavy command goes into a FILE, then runs from that file.
  3. A dry run is only a dry run once its target has been VERIFIED harmless. If the
     redirection can fail silently, assert on it instead of trusting it.
  4. After stopping a background task, confirm the WSL-side process is actually gone.
```
