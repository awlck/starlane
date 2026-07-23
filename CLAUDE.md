# Starlane

Starlane is a C++ reimplementation of the ADRIFT 5 interactive fiction engine.

## Structure

The code is structured as follows:

* `starlane-core/`: the main interpreter core. Can be compiled to either a
  static or dynamic library. Communicates with a frontend through the API
  defined in `starlane-core/starlane-core.h`.
  * `starlane-core/gamecontent`: classes representing the various types of
    objects that make up a game: Characters, Locations, Objects, Events,
    Tasks, and so on.
  * `starlane-core/savefiles`: code dealing with savefiles in a Starlane-specific
    format (we do not aim for savefile compatibility with the ADRIFT 5 runner)
  * `starlane-core/expressions`: code and grammar definitions for parsing and
    evaluating expressions.
  * `starlane-core/deps`: vendored dependencies.
  * `starlane-core/game.{cpp,h}`: main class encompassing the entire game state.
  * `starlane-core/gameloader.cpp`: code to break an ADRIFT 5 XML document into
    its constituent parts and call out to relevant object constructors
  * `starlane-core/taffile.cpp`: code to decode the ADRIFT 5 TAF file format into
    an XML string for further processing.
  * `starlane-core/valueparsers.{cpp,h}`: code to parse various textual values within
    the XML into our internal representation
  * `starlane-core/parser.cpp`: code to deal with player input.
* `starlane-qt/`: the main UI built using Qt6. The aim for this is to
  replicate the multimedia capabilities of ADRIFT 5 as closely as possible.
* `starlane-console/`: an as-simple-as-possible frontend used for testing purposes.
* `starlane-utils/`: a collection of small utilities that I found
  occasionally useful during development. Not terribly important.
* `slc-capi/`: A plain C wrapper over starlane-core's C++ API.
* `starlane-glk/`. Draft of an interface targeting the Glk API commonly used by
  interactive fiction interpreters.

The following references are available:

* `reference/ADRIFT-5/ADRIFT`: original VB.NET implementation of ADRIFT 5. (Beware that
  some files here use encodings other than UTF-8; use `grep -a` when searching here.)
* `reference/ifarchive-if-specs/Glk-Spec.md`: reference for the Glk API
* `reference/cheapglk`: a very basic Glk library implementation
* `reference/frankendrift/FrankenDrift.GlkRunner/FrankenDrift.GlkRunner`: an adapter connecting
  the ADRIFT 5 code to a Glk library via .NET P/Invoke with working HTML parsing and all. 

## Basic Instructions

* We are targetting the C++17 standard.
* Do not introduce new dependencies besides what is already included:
  * The UI is built using Qt6
  * The interpreter core uses `pugixml`, `packcc`, `miniz`, and `magic_enum`.
* Keep headers slim; prefer to put implementations into `.cpp` files.
* For reference, the original VB.NET implementation of ADRIFT 5 can be
  found in the `reference/ADRIFT-5/ADRIFT` directory. (Beware that some files here use
  encodings other than UTF-8; use `grep -a` when searching here.)
* We are only dealing with ADRIFT 5 TAF files (game files without bundled multimedia
  assets) for now.
  * TAS files (save files produced by the original ADRIFT 5 runner) are not supported.
  * BLORB files (the de facto standard way in the Interactive Fiction world to bundle
    multimedia assets with a game) will be handled through the frontend, not the core
    interpreter library.
* Use negative numbers for the savefile versions while we are still undergoing major
  development. You will be told when it is time to transition to v1.

## Development Goals

Reference GOALS.md for a to-do list. Keep this up to date.

## Building

Starlane can be built with the following commands in the `build` directory:

```
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$HOME/Documents/Development/Qt/6.11.1/macos -GNinja ..
ninja starlane
```

## Console frontend

`starlane-console` (built with `ninja starlane-console`) is the simplest way to exercise the
interpreter core, e.g. under a debugger. It prints all output text verbatim, without
interpreting any HTML, and otherwise takes the following parameters:

```
starlane-console [--quit] [--input <commands.txt>] [--seed <n>] <game.taf>
```

* `<game.taf>`: path to the ADRIFT 5 TAF file to load. Required.
* `--quit`: terminate as soon as the game has loaded, without prompting for input.
* `--input <commands.txt>`: read player input (and prompt responses, e.g. yes/no
  questions or save/restore file paths) from the given file instead of stdin. Useful
  when running under a debugger (e.g. LLDB), where redirecting stdin via the shell is
  awkward.
* `--seed <n>`: seed the RNG with `n` for reproducible output. A seed of `0` (the
  default) means "seed from the OS", i.e. nondeterministic — games with randomized
  text (e.g. Skybreak's intro) will produce different output on every run unless a
  nonzero seed is given.

## Test data

There is no formal test suite yet, since we are still working towards making most files
load properly (or at all). Test files in the appropriate format can be foud in the
`testdata` directory.

`scripts/run_regression.py` is a parallel regression harness that feeds a fixed command
sequence into every game under `testdata` via `starlane-console` and records how each
one exits (clean, crashed, or timed out), plus a hash of its transcript. It uses a
worker pool sized to the number of CPU cores, so a full pass finishes in seconds rather
than minutes. Typical before/after comparison workflow:

```
python3 scripts/run_regression.py --output /tmp/before.txt
# make your change, rebuild starlane-console
python3 scripts/run_regression.py --output /tmp/after.txt
python3 scripts/run_regression.py --diff /tmp/before.txt /tmp/after.txt
```
