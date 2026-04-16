# AGENTS.md

## Purpose
- This repository is a ZMK/Zephyr board module for the `agentickbd` board.
- Most custom work happens in `boards/xmstudio/agentickbd/`.
- Treat it as hardware/configuration code, not a typical app or library.

## Repository Layout
- `boards/xmstudio/agentickbd/agentickbd.dts`: main board devicetree.
- `boards/xmstudio/agentickbd/agentickbd-pinctrl.dtsi`: pinctrl definitions.
- `boards/xmstudio/agentickbd/agentickbd-layouts.dtsi`: physical layout metadata.
- `boards/xmstudio/agentickbd/agentickbd.keymap`: default keymap.
- `boards/xmstudio/agentickbd/agentickbd.conf`: board config defaults for users.
- `boards/xmstudio/agentickbd/agentickbd_defconfig`: board default Kconfig values.
- `boards/xmstudio/agentickbd/Kconfig*`: board symbols and defaults.
- `boards/xmstudio/agentickbd/board.cmake`: flashing runner configuration.
- `config/west.yml`: manifest pinned to ZMK `v0.3`.
- `zephyr/module.yml`: exposes this repo as a Zephyr module.
- `build_agentic.sh`: local helper for Studio-enabled builds.

## External Agent Rules
- No `.cursor/rules/` directory was found.
- No `.cursorrules` file was found.
- No `.github/copilot-instructions.md` file was found.
- There are no extra Cursor/Copilot rules to merge beyond this file.

## Environment Assumptions
- Local workflow expects a sibling checkout at `../zmk`.
- `build_agentic.sh` activates `../zmk/.venv/bin/activate`.
- This repo also contains a local `.zmk/` checkout, but `.gitignore` ignores `.zmk/` and `build/`.
- Keep generated build outputs out of Git.

## Primary Build Commands
- Preferred local build from repo root:
  - `./build_agentic.sh`
- Pristine rebuild:
  - `./build_agentic.sh -p`
- The helper wraps this `west build` shape:
  - `west build -d <repo>/build -b agentickbd -S studio-rpc-usb-uart -- -DZMK_CONFIG="<repo>/config" -DZMK_EXTRA_MODULES="<repo>" -DCONFIG_ZMK_STUDIO=y`
- Use the helper when validating board/module changes because it wires in the correct module and Studio flags.

## Manual Build Equivalent
- From a ZMK checkout, the equivalent command is:
  - `west build -d /home/xm1994/Projects/AgenticKbd-config/build -b agentickbd -S studio-rpc-usb-uart /home/xm1994/Projects/zmk/app -- -DZMK_CONFIG="/home/xm1994/Projects/AgenticKbd-config/config" -DZMK_EXTRA_MODULES="/home/xm1994/Projects/AgenticKbd-config" -DCONFIG_ZMK_STUDIO=y`
- Add `-p` if you need a pristine rebuild after changing Kconfig, DTS, or overlays.

## Flash Commands
- `board.cmake` enables UF2 and `nrfjprog` runners.
- Common flash path after build:
  - `west flash -d build -r nrfjprog`
- Alternative: copy `build/zephyr/zmk.uf2` to the UF2 bootloader drive.
- Only claim flashing was verified if you actually tested on hardware.

## CI Build Signal
- GitHub Actions uses `zmkfirmware/zmk/.github/workflows/build-user-config.yml@v0.3`.
- `build.yaml` is still the stock commented template; it does not define a custom matrix today.

## Lint / Format Reality
- No repo-local lint or format config was found.
- No `Makefile`, `package.json`, `.editorconfig`, `.clang-format`, ESLint, Prettier, Ruff, or yamllint config exists here.
- In practice, validation means successful builds plus syntax that follows Zephyr/ZMK conventions.
- Do not invent formatter or lint commands unless you confirm the tooling exists.

## Recommended Validation Commands
- Build firmware: `./build_agentic.sh`
- Pristine build: `./build_agentic.sh -p`
- Inspect worktree: `git status --short`
- Review board-only diff: `git diff -- boards/xmstudio/agentickbd`

## Test Commands
- This repo has no standalone local test suite of its own.
- Upstream ZMK tests are available through `.zmk/zmk/app/run-test.sh` when that checkout exists.
- Run all upstream keymap tests:
  - `.zmk/zmk/app/run-test.sh all`
- Run one test case:
  - `.zmk/zmk/app/run-test.sh .zmk/zmk/app/tests/toggle-layer/normal`
- Another valid single-test example:
  - `.zmk/zmk/app/run-test.sh .zmk/zmk/app/tests/wpm/1-single_keypress`
- The runner builds for `native_posix_64` and compares output against snapshot files.

## Single-Test Guidance
- Prefer a single test while iterating on behavior changes.
- Pass the test case directory, not the `native_posix_64.keymap` file.
- Generic pattern:
  - `.zmk/zmk/app/run-test.sh .zmk/zmk/app/tests/<feature>/<case>`
- To refresh snapshots intentionally:
  - `ZMK_TESTS_AUTO_ACCEPT=1 .zmk/zmk/app/run-test.sh .zmk/zmk/app/tests/<feature>/<case>`
- Only auto-accept snapshots when the behavior change is intentional and reviewed.

## When To Run Checks
- DTS, pinctrl, defconfig, or `board.cmake` changes: run at least `./build_agentic.sh`.
- Keymap or behavior changes: run a relevant single ZMK test if one exists.
- Cross-cutting board/config changes: use a pristine build and one targeted test.
- Docs-only changes: no build needed unless commands or paths changed.

## Editing Rules
- Keep diffs narrow and localized.
- Preserve file responsibilities; do not move board metadata into unrelated files.
- Avoid touching generated/template comments unless they become wrong or distracting.
- Never commit `build/` or `.zmk/` outputs.

## Imports and Includes
- In DTS/keymap files, keep system includes in angle brackets first.
- Keep local board includes in quotes after system includes.
- Preserve existing include order unless a dependency requires a change.
- Follow the repo's current pattern in `agentickbd.dts`.

## Formatting Conventions
- Default to 4-space indentation in DTS, Kconfig, YAML, and CMake.
- Match surrounding style if a file already contains mixed whitespace.
- Avoid unrelated reformatting.
- In multi-line DTS property lists, keep one logical item per line and preserve leading-comma continuation.
- Favor readable lines over aggressive wrapping.

## Naming Conventions
- Board identifier stays lowercase: `agentickbd`.
- Human-readable name stays `AgenticKbd` where metadata expects it.
- Kconfig symbols stay uppercase with `CONFIG_` prefixes.
- Devicetree labels use lowercase snake_case like `default_transform` and `led_strip`.
- Compatible strings stay lowercase and vendor-prefixed where required.
- New files should follow the existing `agentickbd*` naming pattern.

## Devicetree Style
- Prefer Zephyr macros such as `DT_SIZE_K(...)`, `PWM_USEC(...)`, and GPIO flag macros when applicable.
- Keep `chosen` entries together near the top of the root node.
- Group related peripherals and attached devices logically.
- Keep comments only when they explain hardware intent or non-obvious constraints.
- Do not invent property names or compatible strings; follow Zephyr/ZMK bindings.

## Keymap Style
- Keep layers inside the `keymap` block.
- Preserve the `bindings = < ... >;` matrix formatting.
- Use official ZMK behaviors and keycodes from the included headers.
- Keep matrix shape aligned with the board transform unless the hardware layout is intentionally changing.
- Do not casually rename existing layer nodes.

## Kconfig / Defconfig Style
- Put board defaults in `Kconfig.defconfig` or `agentickbd_defconfig` according to current usage.
- Use `default`, `select`, and `imply` carefully; prefer the weakest mechanism that works.
- Group related symbols together, for example USB, display, RGB, storage.
- Remove accidental duplication when it is safe and clearly redundant.
- Keep comments concise and only for non-obvious settings.

## YAML / Metadata Style
- Keep YAML keys lowercase unless the schema requires otherwise.
- Preserve stable identifiers like `id`, `identifier`, and `arch`.
- Do not add speculative features, URLs, or capabilities.
- If hardware capability changes, update matching metadata files consistently.

## CMake / Shell Style
- Keep CMake minimal and declarative.
- For shell edits, prefer existing POSIX/Bash patterns already used by the file.
- New shell scripts should fail fast unless there is a clear reason not to.
- Quote paths that may contain spaces.

## Types, Safety, and Error Handling
- There is little conventional typed code here; safety mainly means correct bindings, symbols, and schemas.
- Verify names against nearby files or upstream conventions before adding new ones.
- For scripts, surface failures instead of hiding them.
- For config changes, prefer explicit build failure over silent fallback behavior.

## Agent Workflow Tips
- Check `git status --short` before editing so you do not overwrite user work.
- Ignore unrelated dirty files unless your task explicitly involves them.
- Read neighboring DTS/Kconfig files before making structural changes; board settings are split across files.
- In your final response, state exactly which validation you ran and what you could not run.

## Good Final Checks
- `git diff -- boards/xmstudio/agentickbd`
- `./build_agentic.sh`
- Optional: `.zmk/zmk/app/run-test.sh <case>`

## What Not To Do
- Do not commit generated build outputs.
- Do not rely on nonexistent lint/format tooling.
- Do not rewrite large template-generated sections only for style.
- Do not rename identifiers or devicetree labels without updating every reference.
