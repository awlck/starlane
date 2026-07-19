// All of this frontend's actual logic lives in starlane-glk-base (linked in below); this file
// exists only because CMake's add_executable() requires at least one source of its own, and in
// this configuration main() itself comes from the external Glk library (e.g. Gargoyle's
// libgarglkmain), not from any source file in this repo.
