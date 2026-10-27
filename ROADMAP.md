# RAWR — Roadmap

RAWR is a unified wrapper removal tool. The Reflexive Arcade service is the
first target because it is the wrapper that most of the preserved 2000s
casual games still carry, and because it is the one people hit as a wall
most often.

Everything below is either "not built yet" or "not finished yet". Nothing on
this list is promised for a date.

---

## v0.0.0-1 — shipped

- [x] Reflexive Arcade: detect the layout
- [x] Reflexive Arcade: derive the stream seed (three strategies, in order)
- [x] Reflexive Arcade: decrypt the payload container
- [x] Reflexive Arcade: write the original program back
- [x] Reflexive Arcade: carry the icon, manifest and version info over,
      **without ever shadowing the game's own icon**
- [x] Reflexive Arcade: remove the wrapper leftovers
- [x] Reflexive Arcade: the **integrated** layout, where the binding layer
      is compiled into the game and the purchase check is patched
- [x] The `.FFF` wrapper generation (the one the community tooling misses)
- [x] The `RAW_001` payload layout (older builds)
- [x] Support files outside the support folder, and mixed case names
- [x] Games whose payload lives in a subfolder (`bin/`) with its own support
      directory
- [x] **PopCap**: a trial stub fronts the real game, which PopCap drops in
      as a hidden `popcapgame1.exe`. RAWR puts the real game back under the
      game's own name and flips `NoReg` in `partner.xml`
- [x] Atomic writes, staged rollback, idempotency, refuses to guess
- [x] Crawling a whole library from one path
- [x] Parallel by default, with a progress line on stderr and the result on
      stdout
- [x] A markdown report next to the executable
- [x] `detect` and `wrappers-list`
- [x] `selftest`, with the script tests living in a `tests/` folder
- [x] Static Linux binary, static Windows binary, CI for both

---

## The rule about publishers that still trade

The research turned up something worth stating plainly, because it decides
what RAWR is allowed to do:

**Five of the services below are still selling games.** GameHouse, iWin,
Zylom, GameTop and FreeGamePick all sell a full version today, and their
"time limit" is the product they sell against. RAWR does not touch their
trials. The games that are worth preserving - the ones whose publisher is
gone - are the ones RAWR is for.

A "Deluxe", "Collector's Edition" or "Gourmet Edition" is a **separate
product**, not a trial lifted build. RAWR does not turn a demo into a
deluxe edition.

## Next — the remaining wrappers

| Publisher | Wrapper | Publisher status | Priority |
|---|---|---|---|
| PopCap | trial stub + hidden payload | dead | **done in v0.0.0-1** |
| WildTangent | COM in-proc `params.dll` + `GamesAppService` | dead | high |
| PlayFirst | Director projector, trial inside the bundled `Dirapi.dll` | dead | high |
| Shockwave | browser plugin for the web titles | dead | medium |
| MumboJumbo / GameHouse | `.rgs` / `.rga` / `.rfs` / `.mez` containers | **active** | low, and only the restore mode |
| iWin | `iWinGames.exe` + `iWinInfo.dll` + `host.cfg` | **active** | low |
| Zylom | `npzylomgamesplayer.dll` plugin + RGA/RFS containers | **active** | low |
| Alawar | none - a plain shareware exe with an in-game timer | dead | the in-game key only |
| Rovio | none - the archived builds are demo builds | dead | the in-game key only |
| Interaction Studios | none - a plain exe with a name and key | dead | the in-game key only |
| GameTop / FreeGamePick / MyPlayCity | **none at all** | active | nothing to do |

**GameTop, FreeGamePick and MyPlayCity ship no wrapper.** They redirect to
their website, which is why they looked like a wrapper in the first place.
RAWR has nothing to remove there and should say so rather than guess.

### WildTangent

The fingerprint is `params.dll` under
`WildTangent\Apps\GameChannel\Games\<GUID>\`, registered as CLSID
`{EE13CBE7-1144-4A03-BB60-D3510840C6A3}` with the ProgID
`Params.Props<hex>.1`. The unlock is a matter of re-registering the COM
object, so this is a *restore* job rather than a removal job.

### PlayFirst

`Dirapi.dll` (which carries the string "Macromedia Director MX 2004
Trial") next to a small game stub, plus `gamelab_splash.dcr` and the
768 byte `hsrg.raw` licence file. The trial lives inside the bundled
runtime, so the work is to find where the runtime decides and patch that.

### The noCD patch

The oldest and simplest target of all: a game that refuses to start
because its CD is not in the drive. A small set of well known patches
exist for this. It needs many tests, because every game does it
differently, but it is very manageable and it is the best possible
on-ramp for the tool.

---

## The tool itself

- [x] **A single command for a whole library** - RAWR walks the tree,
      finds every executable, and keeps the ones that are actually a
      wrapped game
- [x] **A progress line for large libraries** - `n/total`, the game name
      and the result, on stderr
- [x] **A report file** - a markdown summary next to the executable, named
      with a timestamp to the second
- [x] **Parallelism** - on by default, sized to the machine, leaving a core
      free so a big library does not lock the machine
- [x] **Progress on stderr, result on stdout**, so the output stays
      pipeable
- [x] **Windows: long path support**
- [x] **Windows: the same code path for the shell and the console build**
- [x] **Detection only mode** - `rawr detect <path>` reports what a folder
      is without changing anything
- [x] **`rawr wrappers-list`** - the publishers and wrappers RAWR knows
      about
- [ ] **A single file** - the user points at an executable and RAWR works
      out the folder around it
- [ ] **A recursive mode with an explicit depth**, for libraries that are
      too big to walk blindly
- [ ] **The uninstaller detection** - the Reflexive volumes ship an
      `unins000.exe` and a `.stg` that are not the game
- [ ] **A library catalogue** - a report that lists every game it found,
      what it is and whether it is playable

---

## Ideas, not scheduled

- [ ] A **wrapper manager** mode: instead of removing a wrapper, restore
      the files a wrapper needs. For the publishers that are still trading
      this is the *only* acceptable answer, and for a game whose wrapper
      is easier to satisfy than to remove it is the right one anyway
- [ ] Porting Unity Android games to native Windows. Not a wrapper problem
      at all, and not a RAWR problem, but the same instinct: a tool that
      takes a thing that cannot run here and makes it run here
- [ ] A **format library** so the PE, resource and packing code can be
      reused. The source is already open and MIT, so this is only worth
      doing when a second tool actually needs it

---

## Non-goals

- Generating registration keys
- Patching a game's own logic beyond the purchase check
- Touching a trial belonging to a publisher that is still selling the game
- Turning a demo build into a product that is sold separately
- A GUI
