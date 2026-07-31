# Starlane

Starlane is a C++ reimplementation of the [ADRIFT 5](http://www.adrift.co) interactive
fiction engine. It loads and plays ADRIFT 5 `.taf` game files, aiming to reproduce the
original VB.NET runner's behavior as closely as possible.

**Status: Alpha.** Starlane is under active development and has not reached a stable
release. Expect rough edges, missing features, and behavior that occasionally diverges
from the original ADRIFT 5 runner. See [Known limitations](#known-limitations) below.

## Structure

* `starlane-core/`: the interpreter core, built as a static or dynamic library. Exposes
  a C++ API (`starlane-core/starlane-core.h`) for frontends to drive.
* `starlane-console/`: a minimal text frontend, mainly useful for development and
  debugging.
* `starlane-qt/`: a UI frontend built with Qt6, aiming to replicate ADRIFT 5's
  multimedia presentation.
* `starlane-glk/`: a frontend targeting the [Glk](https://www.eblong.com/zarf/glk/) API
  used by many interactive fiction interpreters, so Starlane can run inside any
  Glk-compatible shell (e.g. Gargoyle).
* `slc-capi/`: a plain C wrapper over the core's C++ API.
* `starlane-utils/`: assorted small development utilities.

## Building

### Dependencies/Prerequisites

Starlane is a C++17 project and as such requires a C++17-compliant compiler and
standard library. Dependencies are minimal, and most are included in the repository.
Make sure to clone with submodules:

```shell
git clone --recursive https://github.com/awlck/starlane.git
```

The Qt frontend additionally requires Qt6 development libraries to be installed. The console
and Glk frontends have no further dependencies of their own.

Development currently happens on macOS 26 and Fedora 44, so those are the platforms that get
the most testing, but any platform that supports the above requirements *should* work in
theory.

### Building (for real, this time)

All targets are built through a single top-level CMake project. From the repository
root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The following CMake options control which frontends get built (all default to `ON`
except where noted):

* `SL_BUILD_CONSOLE`: the `starlane-console` text frontend.
* `SL_BUILD_QT`: the Qt6 frontend. (This has ambitions of becoming the primary frontend
  one day, and so its executable is just called `starlane`.) Requires Qt6 (`Core`, `Gui`,
  `Widgets`, `Multimedia`); point CMake at your Qt installation with `-DCMAKE_PREFIX_PATH`, e.g.
  `-DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.1/macos`.
* `SL_BUILD_GLK`: the `starlane-glk` Glk frontend. By default, this only builds a static
  library (`SLGLK_LIB_ONLY=ON`) without linking against any particular Glk
  implementation. Set `SLGLK_USE_BUILTIN_CHEAPGLK=ON` to build a standalone
  `starlane-glk` executable against the vendored `cheapglk` reference library, or set
  `SLGLK_LIB_ONLY=OFF` and point `SLGLK_GLK_LIB_NAME` at an installed Glk library's
  pkg-config module (e.g. `garglk`, for Gargoyle) to link against a real one.
* `SL_BUILD_C_WRAPPER`: the `slc-capi` C API wrapper library.
* `SL_BUILD_TOOLS`: the `starlane-utils` development utilities.
* `BUILD_SHARED_LIBS`: build `starlane-core` as a shared library instead of static
  (default `OFF`).

Individual targets can also be built on their own, e.g. `ninja starlane-console` or
`ninja starlane` (the Qt frontend) when using the Ninja generator.

### Running the console frontend

`starlane-console` is the simplest way to play a game or exercise the interpreter core:

```bash
starlane-console [--quit] [--input <commands.txt>] [--seed <n>] <game.taf>
```

* `<game.taf>`: path to the ADRIFT 5 TAF file to load (required).
* `--quit`: terminate immediately after loading, without prompting for input.
* `--input <commands.txt>`: read player input from a file instead of stdin.
* `--seed <n>`: seed the RNG for reproducible output (`0`, the default, seeds
  nondeterministically from the OS).

## Known limitations

The core interpreter library handles ADRIFT 5 `.taf` game files (games without bundled
multimedia assets) only; `blorb` archives (used to bundle a game together with its
images and sounds) are unpacked at the frontend level instead:

* `.tas` save files produced by the original ADRIFT 5 runner are not supported;
  Starlane uses its own, still-evolving save file format instead.
* `blorb` files are handled by the Qt and Glk frontends -- the Glk frontend via
  whichever Glk library hosts it, the Qt frontend with its own small Blorb parser --
  but not by the console frontend, which is unlikely to ever gain this ability.

Within that scope, most core game mechanics are implemented and working: parsing and
matching player input against tasks, task and restriction evaluation, events, character
walks, disambiguation, pronoun references, aggregated output messages, save/restore/undo,
and status bar support in the core library. The [ADRIFT conversation system](GOALS.md)
is not implemented, and games that rely on it will not work correctly.

Frontend support currently looks like this:

* **starlane-console**: functional for plain-text play and scripted testing; does not
  interpret HTML formatting, and has no `blorb`/multimedia support at all.
* **starlane-glk**: the most complete frontend, with image, font color, sound, `blorb`,
  and status bar support. Secondary output windows, transcript support, and a debug
  output window are not yet implemented.
* **starlane-qt**: now has `blorb`, image, and sound support (including `<img>`/`<audio>`
  markup) alongside its status bar, as well as text formatting in line with the original
  ADRIFT 5 implementation. Still outstanding: transcript support, dockable secondary
  windows, debug log output, and WASM builds.

See [GOALS.md](GOALS.md) for the detailed, up-to-date development to-do list.

## Reporting issues

This project is still finding and fixing behavioral differences from the original
ADRIFT 5 runner. Issues describing specific games or commands where Starlane's output
diverges from the original runner's are very welcome; please include the game (or a
minimal reproduction) and the expected vs. actual output where possible.

## Licensing and AI disclosure

The Starlane project is licensed under the Apache License 2.0 ([LICENSE.txt](LICENSE.txt)).
Third-party components vendored under `starlane-core/deps/` (and, optionally,
CheapGlk under `reference/cheapglk/`) are separately licensed; see
[NOTICE.txt](NOTICE.txt) for the full rundown, including a note on how this
project relates to the original ADRIFT 5 runner.

The project started in mid-2022, but by early 2024 I had thoroughly burned
myself out / lost steam / realized I had bitten off more than I could chew (pick
whichever you find most relatable), and development fizzled out. (For a while
it also looked like we could just [connect the ADRIFT codebase to Glk](https://github.com/awlck/frankendrift),
but then that also kinda fizzled out.) But in mid-2026 I remembered that AI
was a thing, and I started wondering whether Claude Code would be able to keep
enough of the original codebase in its virtual brain to make the port happen,
and it turned out that it could, so that's where we are now.

Refer to the commit history or `git blame` to figure out which parts of the
codebase are all mine and which were made by AI. My very rough guesstimate is
that I was about a third (or at most halfway) done by the time I gave up in 2024.
