# RAWR — Roadmap

RAWR is a unified wrapper removal tool. The Reflexive Arcade service is the
first target because it is the wrapper that most of the preserved 2000s
casual games still carry, and because it is the one people hit as a wall
most often.

Everything below is either "not built yet" or "not finished yet". Nothing on
this list is promised for a date.

---

## v0.0.0 — shipped

- [x] Reflexive Arcade: detect the layout
- [x] Reflexive Arcade: derive the stream seed (three strategies, in order)
- [x] Reflexive Arcade: decrypt the payload container
- [x] Reflexive Arcade: write the original program back
- [x] Reflexive Arcade: carry the icon, manifest and version info over
- [x] Reflexive Arcade: remove the wrapper leftovers
- [x] The `.FFF` wrapper generation (the one the community tooling misses)
- [x] The `RAW_001` payload layout (older builds)
- [x] Support files outside the support folder, and mixed case names
- [x] Atomic writes, staged rollback, idempotency
- [x] `selftest`, `deps-check`, `deps-install`, `--json`, `--dry-run`
- [x] Static Linux binary, static Windows binary, CI for both

---

## Next — integrated wrappers

The hardest remaining Reflexive case. In roughly a dozen titles the binding
layer is compiled into the game executable itself: there is no separate
payload, the executable loads `ReflexiveArcade.dll` through `LoadLibrary`
and calls `radll_HasTheProductBeenPurchased` and `radll_GetUnlockCode` to
decide whether to start the trial or the full game.

- [ ] Find the call sites of the two `radll_*` entry points
- [ ] Find the test that consumes the result
- [ ] Patch the check so the program behaves as if it were registered
- [ ] Patch it in place, without a code cave where one is not needed
- [ ] Verify against the same titles in the corpus

This needs a small x86 length decoder so the tool can walk the instruction
stream without a disassembler dependency. A 16 bit opcode map plus the
one byte and two byte ModRM forms is enough for the call sites that matter.

---

## Other services

Each of these is a different wrapper with a different shape. The detection
and staging machinery is reusable; the unwrapping is not.

- [ ] **GameTop** - the wrapper is the easiest of the bunch: a launcher that
  starts the real program and a support folder around it
- [ ] **MyRealGames** and **MyPlayCity** - same family, similar shape
- [ ] **GameHouse** - the retail wrappers, several generations
- [ ] **MumboJumbo** - needs research, the wrapper has not been looked at yet
- [ ] **BigFish** - the Torque Game Builder install layout, where the game
  is `TGB.exe` next to the shared engine data
- [ ] **Alawar** - the Nova disc layout with the Inno installer inside

---

## Steam

Not a wrapper in the classic sense, but the same shape of problem: a depot
that was pulled from the store leaves an install that cannot be repaired
from the client any more.

- [ ] Detect a depot that the client refuses to verify
- [ ] Recover the manifest and the chunk list from the local cache
- [ ] Reassemble the files that are still present
- [ ] Report what is genuinely gone instead of pretending

---

## The tool itself

- [ ] **A single command for a whole library** - `rawr ~/games/*/` already
  works, but a recursive mode that walks a tree of folders would be nicer
- [ ] **A progress line for large libraries**
- [ ] **A report file** - a markdown summary of what was found, what was
  removed and what could not be handled
- [ ] **Unwrapping a single file** rather than a whole folder, for the
  cases where the payload and the stub are loose
- [ ] **Undo** - the stage folder already makes this possible, a `rawr
  --restore` that walks the stage folders would make it explicit
- [ ] **Parallelism** - a library of two hundred games is slow enough to
  want more than one core
- [ ] **A progress report on stderr and the result on stdout**, so the
  output stays pipeable
- [ ] **Windows: long path support**, the wrapper paths are deep
- [ ] **Windows: the same code path for the shell and the console build**

---

## Ideas, not scheduled

- [ ] A **wrapper manager** mode: instead of removing a wrapper, restore
  the files a wrapper needs. Some wrappers are easier to satisfy than to
  remove, and for a game that is still being sold that is the right answer
- [ ] **Detection only** mode - report what a folder is without changing
  anything, for cataloguing a collection
- [ ] Porting Unity Android games to native Windows. Not a wrapper problem
  at all, and not a RAWR problem, but the same instinct: a tool that takes
  a thing that cannot run here and makes it run here
- [ ] A **format library** so that the PE, resource and packing code can be
  reused by other tools instead of living only inside RAWR

---

## Non-goals

- Generating registration keys
- Patching the game's own logic
- Anything that touches a game that is still being sold
- A GUI
