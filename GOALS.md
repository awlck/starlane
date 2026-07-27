# Starlane Development Goals List

- [x] implement a "dumb terminal" frontend for use on the console
- [x] fix the logic separating text and in-line expressions
- [x] finish the logic for matching player input to general tasks
- [x] implement the logic for specific tasks overriding general tasks
- [x] list visible objects and characters in location descriptions
- [x] Implement Direction restrictions
- [x] Implement system commands
- [x] make WAIT actually let `WaitTurns` turns pass
- [x] implement event execution
- [x] run System tasks that trigger themselves (on arriving somewhere, and at the start of the game)
- [x] implement subevents measured in seconds on turn-based events
- [x] implement missing built-in functions (LocationOf, DisplayLocation, ParentOf)
- [x] implement Character walks (they tick alongside events, see `Game::RunEventTick`)
- [x] implement disambiguation ("TAKE BALL" -- "Which do you mean, the red ball or the green ball?")
- [x] print the initial room description if the game asks for it
- [x] add short location description (aka location name) to location descriptions
- [x] run every matching Specific task, not just the first (honouring "continue to execute lower
      priority tasks")
- [x] handle commands that name several objects at once ("take the plates and the ration bar")
- [x] end the game when a task says to (win/lose/neutral)
- [x] go on taking input after the game has ended, accepting restart/restore/quit/undo
- [x] implement pronoun references in commands ("take the bar" ... "eat it")
- [x] implement the pronoun arguments to `character.Name`/`%CharacterName%` (`Force`, `Objective`,
      `Possessive`, ...), and only pronominalise a character already mentioned this turn
- [x] fall back to ADRIFT's "I don't understand what you want to do with <object>" when the input
      names a known object but matches no task
- [x] let a command's %object% match nothing at all, so that "launch" answers "Launch what?" rather
      than falling through to "I didn't understand that sentence"
- [x] implement subevents that override the room description (`SetLook`)
- [x] implement tasks that use a loop
- [x] fully implement ADRIFT's "Aggregate output" task property: a per-command response buffer
      (`Game::ResponseBuffer`/`activeResponseBuffer`, opened over `ExecuteMatchedTask`) collects
      completion messages, dedups them on unevaluated text when a task's `<Aggregate>` flag is on
      (evaluated text when off), merges the references of collapsed runs, and flushes them at
      end-of-command, rendering %objects%.Name / %TheObject[...]% as "the X and the Y" -- see
      `Game::RunTaskAndCapture`'s `emit()` and `Game::FlushResponseBuffer` (parser.cpp).
      Remaining deviation: restriction-failure ("pass/fail") messages are not routed through the
      buffer, so ADRIFT's merge of a failing subset ("You take A and B. C is too heavy.") is not
      reproduced -- failure text still prints immediately on the non-buffered path
- [x] support a variable reference (not just a literal integer) as a `SetVariable`/`IncVariable`/
      `DecVariable` array index, e.g. `SetVariable cl_Buttonarra[cl_One] = "%b0%"` where `cl_One`
      is itself a variable holding the index -- `Task::Action::PerformImpl`'s array-index parsing
      (task.cpp, the `ActionType::SetVarTo`/`IncVar`/`DecVar` case) calls `ParseInt` directly on the
      bracketed text and crashes (uncaught `std::invalid_argument` from `stoll`) when it isn't a
      bare integer literal. Found via `testdata/ww2-elevator-escape/ww2-elevator-escape.taf`, whose
      `cl_Vars2array` System task does exactly this at game start; previously masked because the
      game failed to load at all (unimplemented `SetTasks` FOR loop, now fixed) before ever reaching
      `Game::Begin()`
- [x] also properly support variable names on the right-hand side of a restriction regarding variables
- [x] Implement task action `Move <character> ToSwitchWith <character>`. (For the player, this means
      changing perspective. "Switching" two NPC apparently just brings the second to the first? Weird.)
- [x] Fixed: a completion message that ends up in the per-command response buffer (see
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
- [x] Implement the `.Article` built-in expression function
- [x] Implement Specific Tasks with multiple objects in the same reference (cf. Race Against Time,
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
- [ ] implement the conversation system (or refuse to load games using it because it isn't very widely used)
      -- deferred: no test game uses more than the ADRIFT standard library's scaffolding, and authors
      generally roll their own with custom tasks/variables/properties (which already work).
- [x] come up with a better error-handling mechanism than crashing the entire interpreter on load issues
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
- [x] implement status bar support into the backend
- [x] resolve outstanding TODO markers within starlane-core
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
      - Implemented: groups with no properties of their own are no longer written into a save file
        (mirroring how `descriptions_shown` already skipped descriptions with nothing shown);
        `Game::ContinueRestore` resets every group to that state before applying the file's
        exceptions, the same way it already did for descriptions. Bumped `currentSaveFileVer` to -993.
      - Fixed: a game with no explicit `<TaskExecution>` element (i.e. one whose editor's own default
        at the time matched what it wrote, so it never had reason to emit the element) built before
        ADRIFT 5.0.0.22 (`<Version>` encodes this as the double 5.000022, e.g. "5.000021") is missing
        `HighestPriorityPassingTask` even though it was authored and tested against it. The stock
        Runner doesn't correct for this -- it just applies its current default
        (`HighestPriorityTask`) regardless of the game's vintage, a known bug that leaves at least a
        couple of real ADRIFT games unwinnable. `Game::LoadFromXML` (gameloader.cpp) now deliberately
        does not reproduce that bug: absent `<TaskExecution>` and `<Version>` < 5.000022 defaults to
        `HighestPrioPassing`. None of the games under `testdata` are old enough to exercise this (the
        oldest is 5.000029), so this has no regression-suite coverage either way.
      - Fixed (correcting an initial misreading of this exact TODO -- a first pass wrongly concluded
        ADRIFT has no such feature, because a `head`-truncated reference-source grep missed it):
        games actually can replace ADRIFT's default English direction words wholesale via 12
        `<DirectionNorth>`/`<DirectionNorthEast>`/.../`<DirectionDown>` elements (FileIO.vb), e.g.
        `<DirectionEast>Clockwise/CW</DirectionEast>` on a circular map. The canonical direction name
        itself (what a Location's exits key on, and what a move message displays, e.g. "You move
        east") is unaffected -- only which typed words resolve to it. `Util::kDirections`
        (gamecontent/utility.cpp) is now built per-game into a `Util::DirectionTable`
        (`GameStatic::directionTable`, `Game::GetDirectionTable()`) from ADRIFT's defaults with any of
        those 12 elements substituted in, instead of a single process-wide static table. Verified
        against `testdata/tests/renamedirtest.taf`/`renamedirtest-expected.txt` (a real Runner
        transcript): "clockwise"/"cw"/"ccw" now correctly resolve to East/West.
        - That same test exposed a second, unrelated gap while verifying this: its expected transcript
          also has an "Exits are clockwise and counterclockwise." line after each location description
          that starlane never prints. `GameStatic::showExits` is loaded from `<ShowExits>` but nothing
          in starlane-core ever reads it -- automatic exit listing isn't implemented at all yet. Left
          as its own goal below rather than folded into this fix.
      - Investigated and found moot, so just removed/reworded rather than fixed: the two performance
        musings in `savefiles/parser.cpp` (`Parser::CreateNode`, `Parser::Prepare`) were already
        answered by their own comments -- reworded into plain statements of the design tradeoff.
      - Left in place (already tracked): the two `TODO: Conversation` markers in
        `gamecontent/character.cpp` and `gamecontent/restriction.cpp` -- covered by "implement the
        conversation system" above. `error.h`'s `TODO(debug-log)` -- covered by the error-handling
        item's own last bullet.
      - Promoted to their own goals below, being too large for this pass: the `EverythingWithProperty`
        comparison-value bug (`gamecontent/task.cpp`), per-item plural disambiguation and
        `ExecuteTaskByKey`'s first-Command-line-only references (both `parser.cpp`), and child-task
        control suppression (`gamecontent/walk.cpp`/`gamecontent/event.cpp`).
- [x] Fixed: `ConditionType::InPosition`/`LyingOn`/`SittingOn`/`StandingOn` (restriction.cpp) read a
      character's "CharacterPosition" property unconditionally via the throwing `GetStrProp`, on the
      assumption (per the code's own comment) that it is a "mandatory" property every character has.
      It isn't: it's runtime state a character doesn't acquire until they actually sit/stand/lie down
      somewhere, and ADRIFT itself guards every read of it with `HasProperty` rather than assuming
      it's set (`clsCharacter.vb`/`clsUserSession.vb`). A character who has never done any of those
      threw `std::out_of_range` out of `PropHolder::GetStrProp`'s `.at()`, caught by
      `PassRestrictionBlock`'s generic funnel and logged as "Restriction failed to evaluate" -- treated
      as failed either way, so no game's *output* was ever wrong, but the load-bearing bug (a
      restriction whose only conceivable pass path went through an exception on every single
      evaluation) was real and worth fixing outright rather than just tolerating the noise. Found via
      `testdata/tests/renamedirtest.taf`, whose Player never explicitly sits, stands, or lies down (a
      plain `MoveCharacter ... InDirection` game), and whose standard-library `sit`/`stand`/`lie` tasks'
      "already in that position" restrictions hit this on every turn. All four now check `HasProp`
      first, matching ADRIFT.
- [x] core: expose game info (title, author, default fonts, default colors) via a new
      `Starlane::GetGameInfo()`/`GameInfo` API in starlane-core.h, mirroring the existing
      `GetStatusBar` pattern. `GameStatic` now parses `<FontName>`/`<InputColour>`/`<OutputColour>`
      alongside `<Title>`/`<Author>` (gameloader.cpp); the colors are `std::optional<uint32_t>`
      (game.h) rather than defaulting to some baked-in RGB, since ADRIFT distinguishes "the author
      didn't say" from "the author chose black" (an XML file can legitimately contain
      `<InputColour>0</InputColour>`). A new `ParseOleColor` (valueparsers.cpp) unpacks ADRIFT's
      decimal Windows OLE_COLOR format (`0x00BBGGRR`) into the packed `0xRRGGBB` used elsewhere in
      Starlane (Qt's QColor, Glk's zcolor extension). "scoring enabled" is not yet exposed --
      nothing in the engine tracks a score at all yet.
- [ ] core: expose whether the game has scoring enabled (once scoring itself is implemented)
- [ ] core: explicitly handle divide-by-zero instead of displaying the internal error message.
- [ ] implement the status bar in the Qt frontend
- [x] fix and fully implement text formatting in the Qt frontend.
      Replaced the old insertHtml()-based approach with a hand-rolled parser (OutputFormatter) that
      applies formatting directly via QTextCursor/QTextCharFormat/QTextBlockFormat, since ADRIFT's tag
      vocabulary isn't valid HTML. Implements b/i/u/c/font/center/centre/left/right/br/del/cls/waitkey;
      unclosed tags are implicitly reset at the end of each output batch (BeginGame/ProcessInput/
      TimeTick), matching the original runner. wait/window/audio/img/bgcolor are tokenized (so their
      markup never leaks into visible output) but remain no-ops pending blorb/dockable-window support.
- [x] Qt frontend: implement default colors and fonts. `OutputFormatter::ApplyGameDefaults()`
      (called from `MainWindow::ApplyGameInfo()`, right after `CreateGame()`) seeds `baseCharFormat`
      from the game's `OutputColour`/`FontName` if it specifies either; `CommandColor()` (used for
      both the echoed input line and `<c>` tags) now returns the game's `InputColour` when present
      instead of always falling back to red. `MainWindow::ApplyGameInfo()` also sets the window
      title from the game's title/author, exercising the bibliographic half of `GetGameInfo()`.
- [x] Qt frontend: always use a dark theme, regardless of the desktop's own light/dark setting.
      The ADRIFT 5 Runner always renders against a black background (`DEFAULT_BACKGROUNDCOLOUR` is
      `Color.Black` -- see `Global.vb`), so every game author picks their `InputColour`/
      `OutputColour` assuming that background; ADRIFT's own defaults for those (a muted red and
      teal) are barely readable on white. `ApplyDarkTheme()` (starlane.cpp), called right after
      `QApplication` is constructed and before any widgets exist, switches to the Fusion style
      (needed for a custom `QPalette` to consistently apply across platforms/native styles) with a
      black-based palette. Verified against `alyas-of-starhollow.taf`, whose intro uses an explicit
      `<font color=white>` block that was previously invisible against Qt's default light
      background and now reads correctly.
- [x] Qt frontend: let the app launch with no game file on the command line, and add OS "open
      file with" support -- a `.taf` path arrives as `argv[1]` on Windows/Linux (already the case,
      but `main()` used to `return 1` before `app.exec()` if it was missing/not exactly one path,
      so the app couldn't actually launch bare) or, on macOS, as a `QFileOpenEvent` delivered to a
      new `StarlaneApplication : QApplication` (`starlane.cpp`) that stashes the path if
      `MainWindow` doesn't exist yet. Both paths, plus the new "Open Game" menu action, funnel
      through `MainWindow::LoadGameFile()`. Registered the `.taf` extension in the app bundle via
      `starlane-qt/packaging/Info.plist.in` (`CFBundleDocumentTypes`/`UTExportedTypeDeclarations`,
      wired up via the `MACOSX_BUNDLE_INFO_PLIST` target property in `starlane-qt/CMakeLists.txt`).
- [x] Qt frontend: implement menu bar (open, save, restore, transcript, replay). File > Open Game;
      Game > Save Game, Restore Game, Start/Stop Transcript, Replay Commands
      (`MainWindow::CreateMenus()`). Save/Restore call `Starlane::SaveGame()`/`RestoreGame()`
      directly rather than going through `ProcessInput("save")`, per the API's own doc comment.
      Replay reads a command file and feeds it through the same `SubmitCommand()` path as typed
      input, one line at a time; `<waitkey>` is suppressed for its duration (`isReplaying`, checked
      in the `OutputFormatter` wait-key callback) so a replay never blocks on player input that
      isn't coming. Transcript start/stop is still just a menu-label toggle, no file writing yet.
      No "restart" action (not asked for; "restore" already exists to reset in effect).
- [ ] Qt frontend: actually implement StrToSentenceCase
- [ ] implement dockable secondary windows in the Qt frontend
- [ ] Qt frontend: redirect starlane-core debug output to a debug log window
- [ ] implement `blorb` support in the Qt frontend
- [ ] implement graphics support in the Qt frontend
- [ ] implement sound support in the Qt frontend
- [ ] ensure the Qt frontend can be compiled for the web (WASM)
- [x] begin work on a Glk frontend (can probably rip off most of the implementation from FrankenDrift)
- [x] implement image support in Glk frontend
- [x] implement font color support in Glk frontend
- [x] implement sound support in Glk frontend
- [x] Glk frontend: apply the game's default input/output colors and story title, via two
      complementary mechanisms so it works whether or not the underlying library supports the
      garglk zcolor extension:
      - `stylehint_TextColor` hints for `style_Normal`/`style_Input`, set from `GetGameInfo()`'s
        `OutputColour`/`InputColour` right after `CreateGame()` -- the only way to get default
        colors on a Glk library that implements style hints but not zcolor. Per the Glk spec, a
        hint only affects windows opened *after* `glk_stylehint_set()`, so `glk_main()` (
        starlane-glk.cpp) had to be reordered to load the game -- and learn its colors -- before
        opening the main window at all, rather than up front as before. `FatalError()` (output.cpp)
        now lazily opens the window itself via `EnsureMainWindowOpen()`, for the error paths (a
        malformed/unreadable game file) that need to print before any of that has happened.
      - `OutputStyled()` (output.cpp) separately falls back to `gDefaultInputColor`/
        `gDefaultOutputColor` (also set from `GetGameInfo()`) whenever a caller passes
        `zcolor_Default` (i.e. no explicit `<font color>` is in effect), covering libraries that
        support zcolor -- including per-run overrides the static style hints can't express -- but
        also correcting the *default* zcolor libraries that don't implement stylehint_TextColor.
      `glk_main()` also calls `garglk_set_story_title()` with the game's title. No Glk stylehint
      controls font family, so `FontName` stays Qt-only.
- [ ] wrap Glk windows in classes where it makes sense
- [ ] Glk frontend: ensure real-time events can't print while input is active
- [ ] implement status bar support in Glk frontend
- [ ] Glk frontend: implement transcript support
- [ ] Glk frontend: add a debug window
- [ ] Glk frontend: implement secondary windows
- [ ] think about implementing an automap (then probably decide against it)
- [x] fix `EverythingWithProperty`/`EveryoneWithProperty` task actions (Move*/AddToGroup/RemoveFromGroup)
      for Object-, Enum-, and Map-typed properties. `Task::Action` gained a dedicated `propCmpValue`
      field (task.h) for exactly this comparison value; `Task::Action::CreateFromXML` (task.cpp) now
      populates it in the Object/Enum/Map branch instead of `result.lhs` (Bool needs no value, Int/Text
      still build `result.expr`). Because the value no longer rides in `lhs`/`rhs`, the later
      per-action-name parsing (`MoveObject`/`AddObjectToGroup`/...) that re-assigns those fields can no
      longer clobber it. `Task::Action::PerformMoveTo` and the `AddToGroup`/`RemoveFromGroup` case of
      `Task::Action::Perform` now read `propCmpValue` (directly for Object/Enum, via `ParseInt` for Map)
      rather than the collapsed `lhs`/`rhs`. No test game under `testdata` exercises this path (and a
      raw XML fixture can't be loaded -- only obfuscated/zlib'd `.taf` binaries are), so this is
      code-review-verified against ADRIFT's behavior rather than regression-covered.
- [x] disambiguate per-item within a plural reference. `Game::CaptureReferences` (parser.cpp) now
      records, for a plural reference that named several things, a `currentRefItemMatches` entry: one
      `RefMatchInfo` (raw piece text + candidate keys) per named item, in the same order as that
      reference's `currentRefLists` entry. A new `Game::FirstAmbiguousSlot` walks the matched reference
      tokens in order and returns the first slot -- a whole singular reference (from `currentRefMatches`,
      as before) or one item of a plural reference (from `currentRefItemMatches`) -- that still matches
      more than one object; both `BeginDisambiguationIfNeeded` and `ResolveDisambiguation` drive off it,
      so several ambiguous items across one or more references are asked about one after another.
      Resolving a plural item narrows that item's candidates and writes the choice back into the held
      command's `refLists` slot (`Game::SetPluralItemChoice`) that `ExecuteMatchedTask`'s per-item
      odometer reads. `PendingDisambig` carries the item-matches alongside the existing refs/refLists so
      the command survives being held across the question(s). Covered by `testdata/tests/disambigtest`
      (red ball / green ball): "take the red ball and the ball" now asks "Which ball?" for just the
      ambiguous item and then takes both; "take the ball and the ball" asks twice; the unambiguous and
      command-as-answer paths are unchanged, and the singular `disambigtest-expected.txt` transcript
      still matches.
- [x] make `Game::ExecuteTaskByKey` (parser.cpp) consider the right Command line's references, not just
      the first. When an `Execute` action supplies explicit arguments, the new `PickCommandAlternate`
      chooses the `<Command>` alternate whose `%ref%` count equals the argument count, breaking ties by
      how many arguments are of the kind the ref family expects (an Object for `%object%`, a Character
      for `%character%`; `%text%`/`%direction%`/number families accept anything) -- the arguments being
      the only signal available, since there is no typed sentence to regex against as on the `MatchInput`
      path. It falls back to the first line when nothing fits, and the no-argument case still takes the
      first line unchanged. Regression suite transcripts are byte-identical (single-Command tasks and
      first-alternate Execute calls are unaffected); the multi-alternate case has no corpus coverage.
- [x] model ADRIFT's child-task control suppression. Rather than thread "currently completing" state
      through the notification paths, this mirrors ADRIFT's actual mechanism (clsUserSession.vb's
      sTriggeringTask): an Event/Walk remembers the last task whose *completion* triggered one of its
      controls this cycle, and a later completion control is ignored when that remembered task is one of
      the completing task's own direct Specific children. Because a child task completes first (deep in
      the cascade) and claims the trigger, the parent's identical control on the same Event/Walk is then
      skipped instead of re-firing. Implemented as a `triggeringTask` field on `Event`/`Walk`
      (gamecontent/event.{h,cpp}, walk.{h,cpp}), checked and set in each `ReceiveTaskNotification` via
      the new `Game::TaskIsSpecificChildOf` (which reads the existing `specificChildren` index -- note
      ADRIFT's `task.Children(True)` is *direct* children, the `True` only meaning "include completed").
      The memory is cleared when the queued command is applied in `IncrementTimer`, exactly where ADRIFT
      resets sTriggeringTask; it rides undo automatically (Events/Walks are deep-cloned) and is now saved
      as `triggering_task` (save version bumped to -992). Only completion controls carry the guard, as
      in ADRIFT; uncompletion controls are untouched. No test game under `testdata` pairs a parent and
      child task on one control, so this is code-review-verified against ADRIFT rather than
      regression-covered; the full regression suite still shows no new crashes and save/restore round
      trips cleanly.
- [x] implement automatic exit listing ("Exits are north and east.") when `<ShowExits>` is on.
      `Location::GetExitsLine` (gamecontent/location.cpp) now builds the sentence
      `Location::GetDescription` appends last (gated on `Game::ShowExits()`), mirroring
      clsLocation.ViewLocation: it walks the exits in ADRIFT's compass order (N, E, S, W, U, D, In,
      Out, NE, SE, SW, NW), skips any whose restrictions currently fail (like
      clsCharacter.HasRouteInDirection), and names each per the game's own direction table --
      "An exit leads <dir>." for a lone exit, "Exits are <...>." for several. The displayed word is
      the direction's first synonym (ADRIFT's DirectionName), now stored as
      `DirectionTable::canonicalToDisplay` (gamecontent/utility.{h,cpp}) and lowercased for display.
      Verified against `testdata/tests/renamedirtest-expected.txt` ("Exits are clockwise and
      counterclockwise.") -- output now matches the real Runner transcript exactly.
