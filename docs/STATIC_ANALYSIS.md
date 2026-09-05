# Static analysis

How to prepare the tooling and run the static inspection suite for Shot
Stopper on macOS, Linux (Ubuntu) and Windows. Walkthrough from clone to
flash: [Build environment](BUILD.md). Script reference: [Build scripts](SCRIPTS.md).

## 1. What the suite runs

Every tool reads the ESP-IDF compilation database (`build-idf/<arch>/compile_commands.json`)
produced by a normal firmware build. The analysis scripts **never build the
firmware themselves**; they only read the database and the sources. Each tool
deletes and recreates its own reports directory on every run.

| Tool | Script | Reports directory | Contract |
|------|--------|-------------------|----------|
| Cppcheck | `./scripts/static-idf` | `reports/static-analysis/` | Fails (exit 1) on any finding |
| GCC `-fanalyzer` | `./scripts/gcc_analyzer` | `reports/gcc-analyzer/` | Fails on diagnostics in versioned code (builds with the analyzer enabled) |
| clang-tidy | `./scripts/static-tidy` | `reports/static-tidy/` | Fails on in-scope diagnostics or parse errors |
| Include-What-You-Use | `./scripts/iwyu` | `reports/iwyu/` | Advisory: never fails on suggestions, only when tooling is missing or nothing parses |

Analysis scope (identical for every tool): `shotStopper/`,
`libraries/EspressoScaleBLE/`, `idf/main/`, and `idf/components/` (the
project-owned components). ESP-IDF internals, `idf/managed_components/`,
`idf/third_party/` and Arduino cores are out of scope.

Prepare the database once, then run any tool:

```sh
./scripts/build-idf --arch n16r8     # also produces compile_commands.json
./scripts/static-idf --arch n16r8
```

`--arch` is `n8r4` or `n16r8`; see [Build scripts](SCRIPTS.md) for how
parameters resolve. `--build-dir` and `--output-dir` are one-shot overrides on
the analysis scripts and are never persisted to `.shotstopper`.

## 2. macOS prerequisites

```sh
brew install cppcheck
brew install include-what-you-use   # optional; IWYU only
```

`include-what-you-use` from Homebrew is built against the Homebrew `llvm`
formula, so the clang version always matches — do not mix it with another
clang. Xcode Command Line Tools (`xcode-select --install`) provide the host
compiler used by the host tests; IWYU does not need it.

## 3. Linux (Ubuntu) prerequisites

```sh
sudo apt-get update
sudo apt-get install git python3 python3-pip python3-venv cmake ninja-build \
  build-essential cppcheck
```

IWYU is version-locked to the clang it was compiled against, and the Ubuntu
`iwyu` package is built against the distro clang, which may not be the clang
you have. Build it from source instead (replace the versions with your
installed clang):

```sh
sudo apt-get install clang-18 libclang-18-dev llvm-18-dev cmake ninja-build
git clone --branch clang_18 --depth 1 \
  https://github.com/include-what-you-use/include-what-you-use.git
cmake -S include-what-you-use -B iwyu-build -G Ninja \
  -DCMAKE_PREFIX_PATH=/usr/lib/llvm-18
cmake --build iwyu-build
sudo cp iwyu-build/bin/include-what-you-use /usr/local/bin/
```

Verify the pairing before running the analysis:

```sh
include-what-you-use --version   # must report the same clang as: clang --version
```

## 4. Windows prerequisites (native, no WSL)

1. **Git for Windows** — the repo scripts are bash; run them from **Git Bash**
   (`winget install Git.Git`).
2. **ESP-IDF 5.5.x** — use the [ESP-IDF Tools Installer](https://dl.espressif.com/dl/esp-idf/)
   (or the VS Code extension backend). Clone/check out `v5.5.5` to
   `%USERPROFILE%\esp\esp-idf`; the scripts find it via `IDF_PATH` or the
   default `%USERPROFILE%\esp\esp-idf`. The installer also provides CMake,
   Ninja and the Xtensa GCC toolchain that produce the compilation database.
3. **cppcheck** — `winget install Cppcheck.Cppcheck` or the installer from
   [cppcheck.sourceforge.io](https://cppcheck.sourceforge.io/). Make sure its
   install directory is on `PATH` (visible from Git Bash too).
4. **Node.js** — needed by the firmware image checks: `winget install OpenJS.NodeJS.LTS`.

Windows notes:

- The analysis scripts resolve ESP-IDF and toolchain paths the same way as on
  macOS/Linux; inside Git Bash, `%USERPROFILE%` is `$HOME`.
- ESP-IDF's own `export.bat`/`export.ps1` are for CMD/PowerShell. The repo
  scripts do not source them; they only need the paths above to exist. For
  interactive `idf.py` work, use the **ESP-IDF 5.5 CMD**/PowerShell shortcuts
  the installer creates.
- IWYU is optional on Windows: there are no official prebuilt binaries, so
  build it from source with Visual Studio Build Tools against your clang, or
  skip `./scripts/iwyu` (the other three tools work without it).

## 5. ESP-IDF and esp-clang

The firmware needs ESP-IDF **v5.5.x** (project validated with v5.5.5):

```sh
mkdir -p "$HOME/esp" && cd "$HOME/esp"
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3 && . ./export.sh   # macOS/Linux
```

clang-tidy must understand the `xtensa-esp32s3-elf` target. The stock clang
from Homebrew or apt does **not** — use Espressif's LLVM fork (`esp-clang`),
which ships a clang-tidy with Xtensa support:

```sh
python "$HOME/esp/esp-idf/tools/idf_tools.py" install esp-clang
```

This installs into `~/.espressif/tools/esp-clang/` (all OSes, including the
Windows `x86_64-w64-mingw32` package); `./scripts/static-tidy` finds it there
automatically. To use a specific build instead, point the script at it:

```sh
export ESP_CLANG_TIDY=/path/to/esp-clang/bin/clang-tidy
```

## 6. Running clang-tidy

```sh
./scripts/build-idf --arch n16r8          # once, to (re)generate the database
./scripts/static-tidy --arch n16r8
```

What the script does:

1. Rewrites the GCC compilation database into a clang-friendly one
   (`reports/static-tidy/compile_commands.json`): splices CMake `@response`
   files, drops GCC-only flags (`-fno-tree-*`, `-mdisable-hardware-atomics`,
   `-fanalyzer`, …), adds the explicit `-target xtensa-esp32s3-elf` plus the
   GCC sysroot and libstdc++ include paths that GCC resolves internally.
2. Runs esp-clang clang-tidy per translation unit, using the check set from
   `.clang-tidy` at the repository root.
3. Reports to `reports/static-tidy/tidy.txt` (full output, including
   third-party diagnostics) and `reports/static-tidy/README.txt` (summary).

Exit code: **1** when any in-scope file has diagnostics or failed to parse,
**0** when clean. Diagnostics inside ESP-IDF/managed-component headers are
written to the report but never fail the run — the scope is defined by the
`HeaderFilterRegex` in `.clang-tidy` and mirrored by the script.

### Configuration and suppressions policy

`.clang-tidy` starts deliberately small: `bugprone-*`, `performance-*`,
`clang-analyzer-core.*`/`cplusplus.*` and a few `modernize-*` checks, each
clean on the current codebase. Every disabled check has a comment in the file
saying why (third-party noise vs. pending project debt). To grow it:

1. Remove a check from the disable list (or add a new one).
2. Run `./scripts/static-tidy -a n16r8` and fix or triage every finding.
3. Prefer fixing code over `// NOLINT(...)`. When a NOLINT is unavoidable,
   add a comment explaining why — same rule as `scripts/cppcheck-suppressions.txt`.
   Note that compiler diagnostics surfaced as `clang-diagnostic-*` are not
   reliably suppressed by NOLINT; use a local
   `#pragma GCC diagnostic ignored "-Wformat-security"` instead — the
   GCC-style pragma is accepted by both compilers (see
   `ShotStopperWebhook.cpp` for an example).

**Caution with `clang-tidy -fix`:** fixes are applied for every file that
contributed diagnostics, including headers outside this repository (the
ESP-IDF checkout itself). If you use `-fix`, review `git status` in **both**
this repository and `$HOME/esp/esp-idf` afterwards and revert any changes
under `esp-idf/`.

## 7. Running Include-What-You-Use

```sh
./scripts/build-idf --arch n16r8
./scripts/iwyu --arch n16r8
```

IWYU is **advisory**: the script sanitizes the database for the host compiler
(no Xtensa target in stock IWYU), runs it per translation unit, and collects
suggestions into `reports/iwyu/iwyu.txt`. Translation units that reach
Xtensa-specific headers may fail to parse on the host; they are skipped and
listed, which is expected. The run only fails when IWYU is missing or nothing
parsed at all.

Workflow for the suggestions:

1. Review `reports/iwyu/iwyu.txt` — do not apply blindly: embedded builds care
   about include weight, and ESP-IDF headers sometimes need the C spelling.
2. Apply selected fixes with the `fix_includes.py` shipped next to your IWYU
   build, or by hand.
3. Rebuild (`./scripts/build-idf --arch n16r8`) and re-run the host tests
   (`shotStopper/tests/run_host_tests.sh`) before committing.

The mapping file `scripts/iwyu-shotstopper.imp` is intentionally empty: stock
IWYU already maps the C standard headers to their C++ wrappers, and
re-mapping them aborts IWYU. Add entries there only when IWYU repeatedly
suggests something wrong for ESP-IDF/Arduino headers in this codebase, and
document each one — note that `<stdint.h>` must **not** be mapped to
`<cstdint>` (libstdc++ ships both as mutual wrappers and IWYU aborts on the
double visibility).

## 8. Running Cppcheck and the GCC analyzer

```sh
brew install cppcheck        # macOS
./scripts/static-idf --arch n16r8
./scripts/gcc_analyzer --arch n16r8
```

`static-idf` never builds and fails on any finding; project suppressions live
in `scripts/cppcheck-suppressions.txt` with a documented reason per entry.
`gcc_analyzer` **builds** the firmware with `-fanalyzer` enabled and keeps
only diagnostics whose primary location is versioned code.

## 9. Recommended order for a release check

```sh
./scripts/build-idf --arch n16r8 && ./scripts/build-idf --arch n8r4
./scripts/static-idf -a n16r8
./scripts/static-tidy -a n16r8
./scripts/gcc_analyzer -a n16r8
./scripts/iwyu -a n16r8          # advisory report
shotStopper/tests/run_host_tests.sh
```

## 10. Troubleshooting

- **`unknown argument '-mlongcalls'` / `unknown target`** — you are using a
  stock clang-tidy (Homebrew/apt) against the Xtensa database. Install
  esp-clang (section 5) or point `ESP_CLANG_TIDY` at it.
- **`clang-tidy (esp-clang) not found`** — run
  `python "$HOME/esp/esp-idf/tools/idf_tools.py" install esp-clang`, or set
  `ESP_CLANG_TIDY`.
- **IWYU version mismatch** (instant assertion or `unsupported option` on
  ordinary code) — IWYU was built against a different clang than the one it
  runs with. Rebuild IWYU against your clang or point `IWYU_BINARY` at a
  matching build.
- **`'new' / 'algorithm' file not found`** in the clang-tidy report — the
  GCC sysroot or libstdc++ include paths could not be resolved from the
  compilation database. Rebuild the firmware for that architecture first so
  the database and the toolchain on disk agree; if it persists, compare
  `xtensa-esp32s3-elf-g++ -print-sysroot` output with the sysroot embedded in
  the sanitized database under `reports/static-tidy/`.
- **A whole architecture fails in clang-tidy after changing build flags** —
  stale `CMakeCache.txt` in the build tree can leak analyzer-only flags into
  the database (e.g. `-fanalyzer`). Delete `build-idf/<arch>/CMakeCache.txt`
  and rebuild.
- **Windows: `command not found` inside Git Bash** — the tool was installed
  but its directory is not on the Git Bash `PATH`. Prefer `winget`/installer
  paths that update the system `PATH`, then reopen Git Bash.
