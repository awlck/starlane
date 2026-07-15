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
[ ] implement subevents that override the room description (`SetLook`)
[ ] implement subevents measured in seconds on turn-based events
[ ] implement Character walks (they tick alongside events, see `Game::RunEventTick`)
[ ] implement disambiguation
[ ] implement the conversation system (or refuse to load games using it because it isn't very widely used)
[ ] implement tasks that use a loop
[ ] resolve outstanding TODO markers within starlane-core
[ ] implement the status bar in the Qt frontend
[ ] fix and fully implement text formatting in the Qt frontend (including default colors and fonts)
[ ] implement dockable secondary windows in the Qt frontend
[ ] Qt frontend: redirect starlane-core debug output to a debug log window
[ ] implement `blorb` support in the Qt frontend
[ ] implement graphics support in the Qt frontend
[ ] implement sound support in the Qt frontend
[ ] think about implementing an automap (then probably decide against it)
[ ] ensure the Qt frontend can be compiled for the web (WASM)
[ ] begin work on a Glk frontend
