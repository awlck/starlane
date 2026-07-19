# Starlane Development Goals List

[x] implement a "dumb terminal" frontend for use on the console
[x] fix the logic separating text and in-line expressions
[x] finish the logic for matching player input to general tasks
[x] implement the logic for specific tasks overriding general tasks
[x] list visible objects and characters in location descriptions
[x] Implement Direction restrictions
[x] Implement system commands
[x] make WAIT actually let `WaitTurns` turns pass
[x] implement event execution
[x] run System tasks that trigger themselves (on arriving somewhere, and at the start of the game)
[x] implement subevents measured in seconds on turn-based events
[x] implement missing built-in functions (LocationOf, DisplayLocation, ParentOf)
[x] implement Character walks (they tick alongside events, see `Game::RunEventTick`)
[x] implement disambiguation ("TAKE BALL" -- "Which do you mean, the red ball or the green ball?")
[x] print the initial room description if the game asks for it
[x] add short location description (aka location name) to location descriptions
[ ] implement subevents that override the room description (`SetLook`)
[ ] implement tasks that use a loop
[ ] implement the conversation system (or refuse to load games using it because it isn't very widely used)
[ ] come up with a better error-handling mechanism than crashing the entire interpreter on load issues
[ ] implement status bar support into the backend
[ ] resolve outstanding TODO markers within starlane-core
[ ] implement the status bar in the Qt frontend
[ ] fix and fully implement text formatting in the Qt frontend (including default colors and fonts)
[ ] implement dockable secondary windows in the Qt frontend
[ ] Qt frontend: redirect starlane-core debug output to a debug log window
[ ] implement `blorb` support in the Qt frontend
[ ] implement graphics support in the Qt frontend
[ ] implement sound support in the Qt frontend
[ ] ensure the Qt frontend can be compiled for the web (WASM)
[x] begin work on a Glk frontend (can probably rip off most of the implementation from FrankenDrift)
[ ] implement image support in Glk frontend
[ ] implement font color support in Glk frontend
[ ] implement sound support in Glk frontend
[ ] implement status bar support in Glk frontend
[ ] think about implementing an automap (then probably decide against it)
