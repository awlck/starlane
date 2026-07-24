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
[x] run every matching Specific task, not just the first (honouring "continue to execute lower
    priority tasks")
[x] handle commands that name several objects at once ("take the plates and the ration bar")
[x] end the game when a task says to (win/lose/neutral)
[x] go on taking input after the game has ended, accepting restart/restore/quit/undo
[x] implement pronoun references in commands ("take the bar" ... "eat it")
[x] implement the pronoun arguments to `character.Name`/`%CharacterName%` (`Force`, `Objective`,
    `Possessive`, ...), and only pronominalise a character already mentioned this turn
[x] fall back to ADRIFT's "I don't understand what you want to do with <object>" when the input
    names a known object but matches no task
[x] let a command's %object% match nothing at all, so that "launch" answers "Launch what?" rather
    than falling through to "I didn't understand that sentence"
[x] implement subevents that override the room description (`SetLook`)
[x] implement tasks that use a loop
[x] fully implement ADRIFT's "Aggregate output" task property: a per-command response buffer
    (`Game::ResponseBuffer`/`activeResponseBuffer`, opened over `ExecuteMatchedTask`) collects
    completion messages, dedups them on unevaluated text when a task's `<Aggregate>` flag is on
    (evaluated text when off), merges the references of collapsed runs, and flushes them at
    end-of-command, rendering %objects%.Name / %TheObject[...]% as "the X and the Y" -- see
    `Game::RunTaskAndCapture`'s `emit()` and `Game::FlushResponseBuffer` (parser.cpp).
    Remaining deviation: restriction-failure ("pass/fail") messages are not routed through the
    buffer, so ADRIFT's merge of a failing subset ("You take A and B. C is too heavy.") is not
    reproduced -- failure text still prints immediately on the non-buffered path
[x] support a variable reference (not just a literal integer) as a `SetVariable`/`IncVariable`/
    `DecVariable` array index, e.g. `SetVariable cl_Buttonarra[cl_One] = "%b0%"` where `cl_One`
    is itself a variable holding the index -- `Task::Action::PerformImpl`'s array-index parsing
    (task.cpp, the `ActionType::SetVarTo`/`IncVar`/`DecVar` case) calls `ParseInt` directly on the
    bracketed text and crashes (uncaught `std::invalid_argument` from `stoll`) when it isn't a
    bare integer literal. Found via `testdata/ww2-elevator-escape/ww2-elevator-escape.taf`, whose
    `cl_Vars2array` System task does exactly this at game start; previously masked because the
    game failed to load at all (unimplemented `SetTasks` FOR loop, now fixed) before ever reaching
    `Game::Begin()`
[x] also properly support variable names on the right-hand side of a restriction regarding variables
[x] Implement task action `Move <character> ToSwitchWith <character>`. (For the player, this means
    changing perspective. "Switching" two NPC apparently just brings the second to the first? Weird.)
[x] Fixed: a completion message that ends up in the per-command response buffer (see
    Game::RunTaskAndCapture/FlushResponseBuffer) gets its expressions evaluated twice: once to
    measure whether it has anything to say (`Description::Build(false)`), once more at flush time
    to actually render it (`Build()`/`Build(true)`). A %CharacterName%/character.Name call inside
    such a message used to mention the character (see DisplayCharacterName) on the *first*
    (throwaway) evaluation, so the second (printed) evaluation saw them as already mentioned this
    turn and wrongly pronominalised them ("he" instead of the name) the first time they were
    actually shown to the player. Fixed by having `Description::Build` suppress
    `Game::MentionCharacter`'s writes for the duration of any `commit=false` pass (and anything
    nested within one), via `Game::MentionTrackingSuppressGuard` -- mirroring how `commit` already
    gates `HandleSegmentShown`. Covered by `testdata/tests/charswitchothertest.taf` (a
    MoveCharacter ... ToSwitchWith Look output) and `testdata/tests/charswitchplayertest.taf`.
[x] Implement the `.Article` built-in expression function
[x] Implement Specific Tasks with multiple objects in the same reference (cf. Race Against Time,
    `cl_PutABlueFo`). `Task::SpecificInfo::keys` (task.h/task.cpp) now holds every `<Key>` in a
    `<Specific>` block instead of just the first; loading no longer throws on a second one.
    `SpecificTaskMatches` (parser.cpp) gained a branch for `keys.size() > 1`: rather than binding a
    merged multi-object value into `currentRefs` (which would feed restriction.cpp/expression.cpp
    code paths that were never built to handle more than one object per reference -- confirmed by a
    crash when tried), it checks the *full* set a plural reference named -- held in `currentRefLists`,
    untouched by which single item `ExecuteMatchedTask`'s per-object odometer loop currently has
    bound -- against the required key set. The Specific then fires on every odometer iteration that
    set produced, each suppressing its own object's share of the parent's behavior, so "put the fob
    key and the tube in the box" is blocked as a whole while "put the fob key in the box" alone still
    falls through to the general task normally. Matches ADRIFT's own `GetReference`, which likewise
    only ever narrows a plural reference down to its first item for restriction/expression purposes.
[ ] implement the conversation system (or refuse to load games using it because it isn't very widely used)
    -- deferred: no test game uses more than the ADRIFT standard library's scaffolding, and authors
    generally roll their own with custom tasks/variables/properties (which already work).
[x] come up with a better error-handling mechanism than crashing the entire interpreter on load issues
    or when unexpectedly referencing non-existing objects.
    - `Game::GetObject` now throws `MissingObjectException` for a missing key (a probing
      `Game::TryGetObject` returns nullptr for the callers that legitimately expect one).
    - Restrictions and task actions that won't parse are marked faulty at load and logged rather than
      aborting the whole load: a faulty restriction always fails, a faulty action is skipped. The same
      happens if evaluation throws at runtime (funnel try/catch in `Restriction::PassRestrictionBlock`
      and `Task::RunActions`).
    - Top-level try/catch in `starlane-core.cpp` (CreateGame/BeginGame/ProcessInput/TimeTick) backstops
      anything else: a load/start failure is reported via `FatalError`; a failed turn is rolled back to
      its pre-turn undo snapshot. Errors are logged with a backtrace (`Starlane::Exception`).
    - TODO(debug-log): `LogError` currently writes to stderr; route it to a real debug log (see below).
[x] implement status bar support into the backend
[ ] resolve outstanding TODO markers within starlane-core
    - Fixed: loading a new game over one that was already ongoing left the old game's `startupState`
      snapshot and `undoStates` history dangling/leaked instead of being cleared, because
      `Game::LoadFromXML` deleted the old `Game::theGame` directly rather than going through
      `Game::Discard()` -- and `Discard()` itself didn't null the (freed) `startupState` pointer
      either. A subsequently loaded game's first `Begin()` would then see a non-null but dangling
      `startupState` and skip creating its own, so a later `restart` dereferenced freed memory.
      `LoadFromXML` now calls `Discard()` (gameloader.cpp), and `Discard()` nulls `startupState`
      after the delete that frees it (game.cpp).
    - Fixed: `Save::Writer::RunCompressor` (savefiles/writer.cpp) ignored `mz_deflate`'s status
      entirely; it now throws `Starlane::Exception` on failure. Since the same call also runs from
      `~Writer()` on the final flush, where throwing risks `std::terminate()`, the destructor's call
      is wrapped in its own try/catch that logs instead of propagating.
[ ] implement the status bar in the Qt frontend
[ ] fix and fully implement text formatting in the Qt frontend (including default colors and fonts)
[ ] implement dockable secondary windows in the Qt frontend
[ ] Qt frontend: redirect starlane-core debug output to a debug log window
[ ] implement `blorb` support in the Qt frontend
[ ] implement graphics support in the Qt frontend
[ ] implement sound support in the Qt frontend
[ ] ensure the Qt frontend can be compiled for the web (WASM)
[x] begin work on a Glk frontend (can probably rip off most of the implementation from FrankenDrift)
[x] implement image support in Glk frontend
[x] implement font color support in Glk frontend
[x] implement sound support in Glk frontend
[ ] wrap Glk windows in classes where it makes sense
[ ] Glk frontend: ensure real-time events can't print while input is active
[ ] implement status bar support in Glk frontend
[ ] Glk frontend: add a debug window
[ ] Glk frontend: implement secondary windows
[ ] think about implementing an automap (then probably decide against it)
