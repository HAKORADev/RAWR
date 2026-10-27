# CHANGELOG

- All notable changes to RAWR will be documented in this file.
- This project does not use version names like v1.2.3; it just timestamps changes. It will always be updated every time I notice something wrong.
- Reflexive Arcade is a dead service, so the tool is not going to be version pumped either. The release is `v0.0.0-1`, `v0.0.0-2` and so on: the first release of a build, then the next one, without ever inventing a new number.

## 10/28/2026
- Status: v0.0.0-1
- **The icon is fixed properly.** The first version copied the wrapper's icon group into the recovered program. That was wrong: the Windows shell looks for the group called "MAINICON" *first*, so the wrapper's group **shadowed the icon the game shipped with** and the game ended up with the wrong artwork, or with none. Now the game's own icon always wins. The wrapper's icon is only used for the games that never had one, and then its image ids are remapped onto a free range so they can never collide with an icon the game already carries. The group is registered under the name "MAINICON" *and* under id 1, so the shell finds it either way.
- **The metadata is carried over as well**, including the version information that some Delphi builds keep under a resource type called "IMAGE".
- **The "integrated" Reflexive layout now works.** In a small number of titles the binding layer is compiled into the game executable instead of being a separate stub. Such a game loads ReflexiveArcade.dll at run time and asks `radll_HasTheProductBeenPurchased` whether the product was bought. The library is load bearing - it also draws the frames - so the support folder stays. The purchase check is patched instead: the indirect call is replaced with `mov eax, 1; ret`, which is the same six bytes, so nothing moves and the stack stays balanced. 18 of the 18 titles in the corpus that use this layout have a clean patch site.
- **PopCap.** A PopCap trial install is a stub executable in front of the real game, which PopCap drops in the same folder as a hidden `popcapgame1.exe` and deletes again when the stub exits. The trial state also lives in `partner.xml` as `NoReg="true"`. RAWR puts the real game back under the game's own name, flips `NoReg` to `false`, and leaves the uninstaller and CDProtect alone.
- **A whole library in one command.** RAWR takes a folder, walks it, finds every executable, works out which ones are really games, and processes them. Point it at a library and it crawls it. A folder whose subfolders hold no files is descended into automatically, and a game that lives in `bin/` is found there.
- **Parallel by default.** The work is spread over the cores, sized to the machine with one core left free so a big library does not lock the machine up. One bad folder can no longer take the whole run down.
- **Progress on stderr, result on stdout**, so the output can be piped into other tools. The progress line shows `n/total`, the game and the result.
- **A report file.** After a run a markdown report is written next to the executable, named with a timestamp to the second: what was cleaned, what was marked registered, what was already clean and what failed.
- **`rawr detect <path>`** reports what a folder is without changing anything, for cataloguing a collection.
- **`rawr wrappers-list`** shows the publishers and wrappers RAWR knows about, and which of them are supported right now.
- **The Windows build is now two binaries**: `rawr.exe` is the shell build with no console window, and `rawr-console.exe` is the same program with a terminal attached. They share the same code path. The shell build writes its output to a log file next to the executable when it is started from Explorer. Both carry an application manifest that turns on **long path support**, which the deeply nested Reflexive volumes need.
- **The logo is new**: a grey R with a flame burnt into its upper right and three claw cuts through it. It is drawn from signed distance fields by `scripts/make_logo.py`, so the artwork is reproducible rather than a binary blob somebody drew.
- **`deps-check` and `deps-install` are gone.** They were about rebuilding RAWR from source, which is a different job, and having them in the shipped binary was only confusing.
- **`rawr selftest` now takes the path to the tests folder**, so the script tests live in the repository next to the code they test instead of inside the executable.

### Fixed

- The resource data directory was written at a hard coded offset that is only right for 32 bit images, so a 64 bit game would have had its resource directory corrupted
- Named resources were sorted after the numbered ones, which the resource format does not allow
- The data entry array overlapped the name strings by one slot, which is why the icon group came back with a garbled name
- The JSON output printed a human readable line before the JSON, which broke every parser as soon as a folder failed
- The report file was built from the iterators of two different temporary strings, which is undefined behaviour and crashed the tool at the end of a run
- The combination walk in the seed search decremented instead of incrementing, which walked off the end of the pool

### Verified

- **334 game folders** across two corpora were run through RAWR: 303 from the Reflexive Arcade retail volumes (the old builds) and 31 from single game retail installers
- **317 unwrap cleanly, 17 are the integrated layout and are now marked registered, and there are no unexpected failures** - every folder either comes out clean or comes out explained
- Wrapper build coverage, oldest to newest: **123, 124, 131, 142, 143, 167, 172, 173**
- A sanitizer build (address and undefined behaviour) runs the whole corpus with no findings
- The icon is checked over the whole corpus: for every game that shipped with an icon, the shell still resolves that same icon after the unwrap
- The program recovered from the `.FFF` generation is byte identical to the same program recovered from the retail installer, apart from the PE header fields that have to change when a section is added
- The Windows build is exercised end to end in CI against the synthetic fixture, so the binary that ships has actually unwrapped a game

## 10/27/2026
- Status: v0.0.0 - first public build
- **Reflexive Arcade wrapper removal** - the core feature. RAWR finds an installed Reflexive Arcade game folder, decrypts the encrypted payload container, writes the original game program back over the launcher stub, carries the icon and the application manifest across, and removes the wrapper's leftovers. The game code itself is never patched.
- **The wrapper generation that the community tooling cannot handle** - a later Reflexive wrapper generation moved the binding code out of `.text` and into its own `.FFF` section. The seed derivation has to skip that section, otherwise the CRC is wrong and the configuration never decrypts. RAWR skips it, and the tool that this was written next to does not. A game preserved from that generation now comes out byte identical to the same game recovered from the retail installer.

**Wrapper layouts that are covered:**

| Layout | Shape |
|---|---|
| `rwg_with_config` | `<Game>.exe` stub + `<Game>.RWG` payload + `ReflexiveArcade/` support folder |
| `.FFF` generation | as above, but the stub carries an extra `.FFF` section that has to be left out of the seed CRC |
| `RAW_001` payload | older builds that shipped the encrypted program as `RAW_001.dat` instead of a `.RWG` container |
| top level support | packages where `RAW_002.wdt` and the support images sit next to the game instead of inside `ReflexiveArcade/` |
| mixed case support | installs that ship `Background.jpg` where the seed derivation expects `background.jpg` |
| nested game root | games whose payload lives in a subfolder (`bin/`) with its own support directory |

**Seed derivation, in the order RAWR tries it:**

1. `original_crc32_sections_and_sizes` - CRC32 over the launcher's section bytes, the `RAW_003` section bytes and the sizes of the four support images. This is what the wrapper itself did.
2. `original_crc32_full_files` - the same idea over whole files, for packages whose sections do not line up.
3. `repack_additive_sizes` - the payload size plus a search over combinations of file sizes, for installs that were repacked after the wrapper was built.

Every candidate seed is verified by decrypting the configuration and checking that it really is a configuration before it is used.

**Safety** - every file that is about to change is staged into a hidden `.rawr-stage-<signature>` folder first, so a tool that is killed half way through can be recovered by the next run. Writes are atomic. The tool refuses to touch a folder it does not recognise and refuses to write a payload that does not parse as a program.

**Build:**
- `src/core` - the platform independent code: PE reader and writer, resource tree builder, the Reflexive stream cipher, the seed derivation, the CLI
- `src/linux` - the Linux entry point and the POSIX platform layer
- `src/windows` - the Windows entry point, the Win32 platform layer and the application manifest
- Single static binary, no third party libraries, 300 KB on Linux
- GitHub Actions builds and tests both platforms, and publishes the release artifacts
