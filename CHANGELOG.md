# CHANGELOG

- All notable changes to RAWR will be documented in this file.
- This project does not use version names like v1.2.3; it just timestamps changes. It will always be updated every time I notice something wrong.
- Reflexive Arcade is a dead service, so the tool is not going to be version pumped either. It ships as `rawr.exe` and stays that way.

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

**Resource restoration** - the icon, the application manifest and the version information live in the launcher stub, not in the game. Without carrying them over the recovered game shows a blank default icon in Explorer, which is the most visible sign that something went wrong. RAWR rebuilds the resource tree and appends it to the recovered program, keeping the original resource identifiers so the icon group still resolves.

**Integrated wrappers are detected and refused** - in a small number of titles the Reflexive binding layer is compiled into the game executable itself. There is no separate payload to recover in that case, so the executable has to be patched instead. RAWR detects this layout, says so, and leaves the game alone rather than pretending.

**Safety** - every file that is about to change is staged into a hidden `.rawr-stage-<signature>` folder first, so a tool that is killed half way through can be recovered by the next run. Writes are atomic. The tool refuses to touch a folder it does not recognise and refuses to write a payload that does not parse as a program.

**Tooling:**
- `rawr selftest` - built in checks: CRC32 vectors, keystream vectors for five known seeds, a resource tree write/read round trip and the PE reader
- `rawr deps-check` / `rawr deps-install` - reports the build toolchain and installs what is missing through apt-get, winget or chocolatey
- `rawr --json` - machine readable output for batch scripts
- `rawr --dry-run` - shows what would happen without touching anything

**Build:**
- `src/core` - the platform independent code: PE reader and writer, resource tree builder, the Reflexive stream cipher, the seed derivation, the CLI
- `src/linux` - the Linux entry point and the POSIX platform layer
- `src/windows` - the Windows entry point and the Win32 platform layer
- Single static binary, no third party libraries, 250 KB on Linux
- GitHub Actions builds and tests both platforms, and publishes the release artifacts

### Verified

- **334 game folders** across two corpora were run through RAWR
  - 303 from the Reflexive Arcade retail volumes (the old builds)
  - 31 from single game retail installers
- **317 unwrap cleanly. 17 are the integrated wrapper layout** and are
  reported as such instead of being damaged. There are **no unexpected
  failures** - every single folder either came out clean or came out
  explained.
- Wrapper build coverage, oldest to newest:

  | build | folders | result |
  |---|---|---|
  | 123 | 1 | clean |
  | 124 | 2 | clean |
  | 131 | 1 | clean |
  | 142 | 1 | clean |
  | 143 | 2 | clean |
  | 167 | 1 | clean |
  | 172 | 3 | clean |
  | 173 | 271 | clean |
  | not stamped | 21 | 5 clean, 16 integrated wrapper |

- A sanitizer build (address and undefined behaviour) runs the whole corpus
  with no findings
- The program recovered from the `.FFF` generation is byte identical to the
  same program recovered from the retail installer, apart from the PE header
  fields that have to change when a section is added
- The Windows build is exercised end to end in CI against the synthetic
  fixture, so the binary that ships has actually unwrapped a game
