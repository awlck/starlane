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
- [x] core: expose whether the game has scoring enabled. `Starlane::StatusBar` gained `score`
      (`int32_t`) and `scoringUsed` (`bool`); `Game::GetStatusBar` sets `scoringUsed` from whether
      a positive `MaxScore` variable exists (matching the Runner's own `Adventure.MaxScore > 0`
      check in `clsUserSession.UpdateStatusBar`), independent of whether `Score` itself is present.
- [ ] core: explicitly handle divide-by-zero instead of displaying the internal error message.
- [x] implement the status bar in the Qt frontend. `MainWindow` adds three widgets to
      `QMainWindow::statusBar()`: `locationLabel` and `userStatusLabel` (the latter with stretch
      factor 1, so it fills the remaining space) via `addWidget`, and `scoreLabel` -- shown only
      when `Starlane::StatusBar::scoringUsed` is true -- via `addPermanentWidget` (the right-hand,
      "Score: N" segment). All three use `Qt::PlainText`, not `OutputFormatter`'s tag parsing,
      matching the original Runner's own `StatusBarPanel`-based `UpdateStatusBar`, which has no
      rich-text support either. Refreshed via `MainWindow::UpdateStatusBar()` at every call site
      that already calls `UpdateActionState()` (`RunBeginGame`, `HandleTimeTick`, `SubmitCommand`,
      `RestoreGameTriggered`), matching `GetStatusBar()`'s own "call after every
      Begin()/ProcessInput()/TimeTick()" doc comment. Verified against two test games, one with
      scoring (score segment shown and updates) and one without (segment absent).
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
      `starlane-qt/packaging/Info.plist.in` (`CFBundleDocumentTypes`/`UTImportedTypeDeclarations`,
      wired up via the `MACOSX_BUNDLE_INFO_PLIST` target property in `starlane-qt/CMakeLists.txt`).
      Uses the shared `public.adrift` UTI (confirmed via the installed Gargoyle/Spatterlight
      bundles' own `Info.plist`s) rather than a Starlane-specific one, and `LSHandlerRank` is
      `Default` rather than `Owner` -- both to play nicely with other .taf handlers rather than
      have Launch Services arbitrate three different apps each claiming the extension via a
      different identifier. Imports (doesn't (re-)export) the UTI, matching Spatterlight's own
      choice, since Gargoyle's declaration is the one that already exports/originates it.
- [x] Qt frontend: implement menu bar (open, save, restore, transcript, replay). File > Open Game;
      Game > Save Game, Restore Game, Start/Stop Transcript, Replay Commands
      (`MainWindow::CreateMenus()`). Save/Restore call `Starlane::SaveGame()`/`RestoreGame()`
      directly rather than going through `ProcessInput("save")`, per the API's own doc comment.
      Replay reads a command file and feeds it through the same `SubmitCommand()` path as typed
      input, one line at a time; `<waitkey>` is suppressed for its duration (`isReplaying`, checked
      in the `OutputFormatter` wait-key callback) so a replay never blocks on player input that
      isn't coming. Transcript start/stop is still just a menu-label toggle, no file writing yet.
      No "restart" action (not asked for; "restore" already exists to reset in effect).
- [x] Qt frontend (macOS): fix the app not quitting when its window is closed, leaving an inert
      Dock icon that did nothing when clicked. Root cause: `MainWindow::LoadGameFile()` runs
      synchronously from `main()` *before* `app.exec()`, and almost every game's intro ends in a
      `<waitkey>`, which blocks in its own nested `QEventLoop` (`WaitForKeyOrClick()`) -- so
      closing the window during that very first `<waitkey>` (extremely easy to hit) unwinds and
      exits that nested loop, but `app.exec()`'s own loop hasn't started yet to be quit; `main()`
      then entered it anyway a moment later regardless, starting an unrelated indefinitely-running
      session with no window left. Fixed by checking `theWin->isVisible()` before calling
      `app.exec()` and returning immediately if the window is already gone (`starlane.cpp`).
      Separately added `MainWindow::closeEvent()` to release a `<waitkey>` blocked on a *later*
      close (once the real main loop is already running, not just the initial one) before letting
      `quitOnLastWindowClosed` proceed, so it isn't left holding on to input that will never come.
      Verified both paths, plus the ordinary idle-close case, by launching the built app directly
      and confirming via `ps` that the process actually exits each time.
- [x] Qt frontend (macOS): when a `QFileOpenEvent` arrives for a game while another is already
      ongoing in this process, spawn a fresh instance of ourselves for it
      (`QProcess::startDetached(QCoreApplication::applicationFilePath(), {path})`, in
      `StarlaneApplication::event()`) rather than showing the "discard current game?"
      confirmation and stealing focus -- `Game::Get()` (game.h) is a single global instance, so
      the two can never coexist in one process. This isn't a Launch Services violation: "reuse
      the running instance" is just its default routing for an open-document request (what
      Option-double-click / `open -n` opt out of for the user), not a hard constraint against
      multiple processes sharing a bundle identifier, and bypassing it like this doesn't confuse
      the Dock or process bookkeeping. The confirmation dialog is kept for the "Open Game" menu
      action (a deliberate, current-window-scoped choice) and for a FileOpen arriving with no
      game ongoing, where loading in place is still correct. Verified with two real processes:
      one already playing a game, `open -a` handed it a second file, and a second independent
      process (own window, own game, no dialog) appeared while the first kept running untouched.
- [x] Qt frontend: give the window a reasonable initial size and remember the last one used.
      `MainWindow`'s constructor restores `QWidget::saveGeometry()`'s blob from `QSettings` if one
      was saved; otherwise (first launch, or a corrupt/unusable blob) it sizes itself to half the
      available screen's width and two-thirds its height and centers itself, rather than a fixed
      pixel size that would be cramped on a laptop display and tiny on a 4K monitor.
      `closeEvent()` saves the current geometry back before quitting. `QCoreApplication`'s
      organization/application metadata (`main()`, `starlane.cpp`) is set explicitly so
      `QSettings`'s default constructor resolves a stable location -- macOS: verified this lands
      in `~/Library/Preferences/de.diepixelecke.Starlane.plist` under the `mainWindowGeometry`
      key. Verified end-to-end: resized and moved the window, closed it, relaunched, and confirmed
      the exact same geometry came back.
- [x] core: work through the recorded transcripts of Bug Hunt on Menelaus, Race Against Time and
      Alyas of Starhollow, command by command, and fix what made our output differ. The big ones
      were: a task's "before" completion message has to be evaluated *ahead* of its actions and
      keep that reading if the actions change it (this is what "(from %objects%.Parent.Name)" is
      for, and it was naming the player instead of the container); description parts are chosen in
      one left-to-right pass with `DisplayOnce` ending it, not by hunting for the rightmost
      eligible part; an ambiguous reference is narrowed by the task's own restrictions before the
      player is ever asked "Which pin?", and a task that stays ambiguous is passed over rather than
      stopping the search; `{a/the}` and friends must absorb the following space even when the
      preceding block already became an optional group; and `object.Children(Objects, On)` read its
      second argument from the wrong node and validated it with an inverted test, so every capacity
      check built on it threw. Also implemented: the ALL keyword, `%ListExits%`/`%ListObjectsOn%`/
      `%ListCharactersIn%`/`%ListObjectsAtLocation%`/`%ObjectName%`/`%PropertyValue%`, and
      per-turn "has been seen by" bookkeeping. Race Against Time now runs to its winning ending
      with 3 of 131 commands differing (was 103); Bug Hunt 11 of 75 (was 26); Alyas 388 of 607
      (was 538), with the first divergence moved from command 5 to command 43.
- [x] core: chase the remaining Alyas of Starhollow divergences. Event `<Control>` elements are
      loaded for every event, not only "after a task" ones -- Alyas writes `<WhenStart>0</WhenStart>`
      (a value ADRIFT's own enum has no name for, so the event never starts by itself) for events
      that exist purely to be switched on by a task, and reading their controls conditionally left
      the whole temple sequence waiting on a notification nobody was subscribed to. Also: AGAIN/G;
      the `NoObject`/`NoCharacter` quantifier ("is the player carrying nothing at all?"), which
      until now failed every restriction that used it; `EverythingInGroup`/`EveryoneInGroup` move
      actions, which matched nothing because `ObjIsAppropriate` had no case for them; `CharEnters`/
      `CharExits` are Text properties and so have to be built as descriptions rather than read as
      strings (reading them threw, and the throw took the rest of the turn with it); and ALL is now
      recorded under every spelling of its reference, since the library keeps a task out of a
      sweeping command with "ReferencedObjects MustNot BeExactText All" -- by the generic name,
      while the task's own Command called it "%objects%". Alyas: 538 of 607 commands differing
      before this work, 48 after.
- [x] core: chase the last of the three test transcripts. Task completion is now marked after a
      Before-placement message is read, not before, so a message part gated on "this task must be
      complete" belongs to the next run; a task whose only output comes from an `Execute` action
      reports having spoken, which stops the search through its Specific siblings; failing items
      are dropped from any plural reference, not only from ALL; location-triggered and
      run-immediately System tasks run in file order, as ADRIFT walks htblTasks; `%PropertyValue%`
      builds a Text property's description instead of printing the handle behind it; and the walk
      clock decrements before stepping, so a 15-step walk comes round every 15 turns rather than
      every 16 (which is what had Alyas's patrolling guard permanently out of phase). `%Turns%`
      counts submitted input lines, as ADRIFT's frontend-driven Adventure.Turns does. Alyas now
      differs on one command of 607, Bug Hunt on none but the harness's own end-of-game line.
- [ ] core: Alyas finishes on 615 points where ADRIFT gives 620 -- one 5-point award among its 166
      scoring tasks, with no difference in any of the text. Needs an ADRIFT-side score trace to
      pin down; a console harness over the FrankenDrift engine assembly would do it.
- [x] core: the `AppendToPreviousDescription` menus were never really wrong. FrankenDrift's
      transcript recorder was stripping the `<>` seam ADRIFT inserts between appended parts as if
      it were an HTML tag, taking the following part with it; fixed upstream, and the regenerated
      transcripts agree with us. The one genuine bug the hunt turned up was in the restriction
      parser: `BeLessThan` was mapped to `LessOrEqual`, so Bug Hunt's character menu still offered
      a choice on the turn the last of them was used up.
- [x] core: a complete `%Function[...]%` call written inside a quoted string in an expression is
      evaluated (Alyas's "There is no route <dir>, only %ListExits[%Player%]%."). ADRIFT replaces
      functions across the raw text before the expression is parsed, so a call that appears whole
      in the source is substituted wherever it sits -- but one *assembled* out of pieces
      (`"%LCase" & "[x]%"`) is not, and neither is `%<#...#>[x]%`. Handled in InterpolateRefs,
      not by re-scanning our own output, so both non-cases stay literal as they should.
- [x] core: a command that runs the same task twice now prints both room descriptions. The
      already-said-this-turn set that suppressed the second was turn-wide; ADRIFT clears its
      response tables on entering any top-level task execution, so a location-triggered task
      running after a command may repeat what the command said. Scoped by Game::ResponseScope.
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
- [x] optimization pass over the interpreter core. Roughly 2.4x faster per turn and about half the
      memory, measured over the whole `testdata` corpus; transcripts are byte-identical to before
      except for the exit-ordering fix noted below, and the run is clean under ASan/UBSan.
      * Snapshotting the world for UNDO (`Game::SaveUndo`, once per turn) was over a third of all
        CPU time, because it deep-copied every object, description and property table in the game.
        The parts of that state which do not change now share storage between snapshots instead:
        `PropHolder`'s two property tables are copy-on-write (propholder.h), `Description` shares
        one immutable segment list and keeps only the "which have been shown" bits per copy
        (description.{h,cpp}), `Location::exits` is shared outright (nothing writes it after load),
        and `GameObj::nouns` is copy-on-write (only a player switch changes it).
      * `Game::descriptions` became a dense `std::vector` (DescrRefs are handed out sequentially, so
        the hash map bought nothing), and `taskCompletedStorage` a `std::vector<uint8_t>` indexed by
        the new `Task::StateIndex()` rather than a map keyed by task key -- that map was thousands
        of string copies per snapshot.
      * `AutoCapitalize` (game.cpp) no longer goes through `std::regex`. It rescanned the whole
        message from the start after every letter it raised, which is quadratic; a single
        left-to-right pass gives the same answer because raising a letter can only destroy a later
        match, never create an earlier one.
      * Matching player input against a game's several thousand `<Command>` patterns now runs a
        substring test before the regex: each pattern carries the longest run of text any match must
        contain (`LongestRequiredLiteral`, task.cpp; deliberately conservative -- it gives up rather
        than risk excluding a pattern that would have matched).
      * `dynamic_cast<Character *>`/`<Location *>`, used throughout as a type test on every object
        in the game several times a turn, is now a stored `GameObj::Kind` tag behind
        `AsCharacter`/`AsLocation`.
      * `Util::SplitString` splits on a plain delimiter directly instead of building a `std::regex`
        per call (nearly every delimiter in the codebase is a fixed string), and
        `Description::Segment::Build` no longer compiles a regex per text snippet.
- [x] fixed: saving a game with a lot of state failed partway through, leaving a truncated file.
      `Save::Writer::RunCompressor` asked miniz for more output whenever the previous `mz_deflate`
      call had produced any at all, so a chunk that happened to fill the output buffer to the byte
      led to a call with no input left and no flush -- which is `MZ_BUF_ERROR`, thrown as "Save file
      compression failed: buf error". It now stops once the compressor has taken all the input
      (mid-stream) or reported `MZ_STREAM_END` (on the final flush). Lost Coastlines and all three
      Skybreak versions could not be saved at all before this; their save files are now complete.
      Property tables are also written in key order now (`Writer::WriteSortedMap`), so a save file's
      bytes depend only on the game state and not on hash-table iteration order.
- [x] fixed: `location.Exits` (`Location::GetListOfExits`) listed exits in hash-map order, which is
      not merely arbitrary but unstable -- copying the table reverses it, so the same location could
      report its exits in a different order after an undo or an internal-error rollback. It now walks
      ADRIFT's compass order, as `Global.vb`'s own `Case "Exits"` does (and as `GetExitsLine`
      already did). Changes the wording of "There is no route up, only east and west." style
      messages in `alien-diver`, `be-there` and `tests/renamedirtest`.
- [x] index the mutable game state by position rather than by key. `Game::objects`, `events`,
      `variables` and `groups` were `unordered_map<string, T *>`, so each of the ~2000-2500 entries
      a game has cost a hash node and a copied key string every time the world was snapshotted for
      UNDO -- once a turn. They are now flat `std::vector`s in load order, with the key -> slot
      tables living in the immutable `GameStatic` (`objectIndex` and friends), which is what makes a
      slot a stable name for a thing: nothing is ever added to or removed from these tables after
      load. About 11% off the corpus benchmark on top of the previous pass.
      * The keyed lookups (`GetObject`, `TryGetObject`, `GetEvent`, `GetGroup`, `GetVariable`,
        `ObjectExists`, `GroupExists`) keep their signatures; only their bodies change, to
        `IndexedGet` (game.h). `varNames` maps a variable's name straight to its slot, so looking one
        up by name costs the same single lookup as by key.
      * This reverses the previous arrangement, where a load-order vector of *keys* was kept
        alongside the map and every ordered walk paid a hash lookup per element. `objectLoadOrder`
        and `eventLoadOrder` are gone, and `GetObjectLoadOrder()`/`GetAllObjects()` -- which were
        the same set of things in two different orders -- collapse into one accessor.
      * Save files gained the same guarantee the events section already had: every section is now
        written in load order, and `seen`/`groups` (unordered sets) are written sorted
        (`Writer::WriteSortedKV`), so a save file's bytes depend only on the game state and not on
        how the game arrived at it. Save contents are unchanged.
      * For the record, the previous entry's "over a third of all CPU time" for `SaveUndo` was an
        undercount: profiles attribute the copy and the matching destruction to two separate
        frames, and it is closer to 70%. After both passes it is still the largest single cost;
        what remains is the per-object `Clone()` and the per-variable copy, since a Variable carries
        its value arrays by value.
- [x] fixed: `AloneWithChar` returned whichever character it happened to find first in the player's
      location. ADRIFT's `clsCharacter.AloneWithChar` counts them and answers only when there is
      exactly one -- being "alone with" two people is not a thing -- so with more than one present
      it returns nothing. Ours now does the same, which also makes the answer independent of the
      order the world is walked in.
- [x] a Variable now holds its values in one `std::variant<vector<int64_t>, vector<string>>` rather
      than a vector of each, and shares them copy-on-write. A game has hundreds of variables and
      the whole lot was deep-copied into an undo snapshot every turn while a turn changes a handful
      at most; a text variable's array of strings was the single most expensive thing in that copy.
      The variant is the natural fit for "numbers or text, never both": a subclassing scheme would
      need a virtual clone (Game's copy constructor builds a Variable with `new Variable(*v)`) and
      virtual dispatch on every read, to express a distinction every caller already switches on at
      run time -- and `Variable` stays a concrete type this way, so nothing outside variable.h
      changes. Asking for the kind a variable hasn't got now throws `std::bad_variant_access` where
      indexing the empty unused vector threw `std::out_of_range`; neither is a `std::runtime_error`,
      so both land in the same handlers. About 15% off peak memory for a variable-heavy game;
      the CPU saving is smaller (~2%), since what is left of a snapshot is the per-object and
      per-variable allocation itself rather than what they contain.
- [x] fixed: RESTORE had never worked. Four bugs, each hidden behind the one before it, so nothing
      downstream of the first had ever run: `Game::Restore`'s meta check read
      `if (!gameRevNode || gameChecksumNode) return false;` (missing a `!`) and the writer always
      emits `game_checksum`, so every valid save file was rejected; `ContinueRestore` read the
      `tasks_completed` entries via `myName`/`sv.Bool` when that section is a bare string *list*,
      whose members carry their text in `Str` and have no name; `GameObj::RestoreState` added to
      `groupMembership` without clearing it, so an object that had left a group kept it; and
      `Writer::WriteLiteralString` wrote any alphanumeric string unquoted, while the lexer reads a
      bare digit-leading word as an integer -- Skybreak has text variables holding "0", which came
      back as an int and made the restore reject the whole file. Such text is now quoted (save
      version -991). `scripts/check_save_roundtrip.py` checks this without needing a baseline: play,
      save, play more, restore, save again, and require the two saves to agree.
- [x] fixed: the top-level backstop's "did this turn record a state?" test compared undo *depth*,
      but `SaveUndo` pushes before trimming to `kMaxUndoStates` -- so past the hundredth turn the
      depth was identical before and after and a turn that threw was never rolled back. Replaced
      with `Game::TopUndoGeneration()`, compared with `>`, which also gets the two awkward cases
      right: a mid-turn UNDO or RESTART leaves the newest generation lower, not higher.
- [x] UNDO records what a turn changed rather than a copy of the world. `Game::SaveUndo` used to
      deep-copy every object, event, variable, group and description once per turn and keep a
      hundred such copies -- about 70% of what a turn cost. Each object is now copied *backward*
      into the open undo record the first time it is written to, `SaveUndo` is bookkeeping, and
      `RestoreUndo` writes the record back through the pointers that already exist, so the Game
      instance is no longer replaced (which removed a pile of "`this` may be gone" scaffolding from
      parser.cpp).
      * What makes it safe rather than a discipline problem: reading game state hands back a
        `const T *`, and the only way to get a writable pointer is `Game`'s `Mutable*` accessors,
        which is where the recording happens. The compiler points at every write. `-Wcast-qual` is
        on for `starlane-core` so a C-style cast cannot quietly get round it.
      * A record stays open across an UNDO rather than being discarded, so writes made between an
        UNDO and the next turn (printing "Undone." commits description state, as does the status
        bar) are still covered by the next UNDO, as whole-world snapshots were.
      * `SL_UNDO_AUDIT` (on in Debug) keeps a whole-world copy at every undo point and checks the
        in-place restore against it field by field, via a string-backed `Save::Writer` so the check
        covers exactly what a save file records. All 37 corpus games pass, and deliberately
        breaking a preserve makes it fail.
      * Corpus benchmark 3.40s -> 2.16s, peak memory down 51-68%. Cumulatively over both
        optimization passes: 9.90s -> 2.16s (4.6x) and 487MB -> 89MB on Lost Coastlines.
      * Two transcripts changed, both only in spacing on the internal-error path: the failed
        turn's output and the backstop's message are now separated by the usual two spaces.
- [x] fixed: `Event::GetDuration()` handed out a mutable `Util::Range`, so `%event.Length%` --
      a read -- settled and stored a random roll that is saved state. Replaced with
      `Event::Length()`, which reads the settled value and only takes the writable path to roll.
- [x] fixed: `AloneWithChar` returned whichever character it found first in the player's location.
      ADRIFT's `clsCharacter.AloneWithChar` counts them and answers only when there is exactly one.
