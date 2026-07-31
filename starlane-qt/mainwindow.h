//
// Created by Adrian Welcker on 22.10.22.
//

#ifndef STARLANE_MAINWINDOW_H
#define STARLANE_MAINWINDOW_H

#include <array>
#include <optional>

#include <QtCore/QBuffer>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtGui/QAction>
#include <QtGui/QImage>
#include <QtMultimedia/QAudioOutput>
#include <QtMultimedia/QMediaPlayer>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTextBrowser>

#include "blorbfile.h"
#include "outputformatter.h"

QT_BEGIN_NAMESPACE
class QCloseEvent;
QT_END_NAMESPACE

class MainWindow: public QMainWindow {
	Q_OBJECT
public:
	MainWindow();

	void OutputText(const char *txt);
	// Applies the game's bibliographic/display info (window title, default font/colors) once it is
	// known. Call after CreateGame() and before RunBeginGame().
	void ApplyGameInfo();
	// Start the once-a-second clock that drives the core's real-time events. Call once the game
	// has begun; there is nothing for it to advance before that.
	void StartEventTimer();
	// Wraps Starlane::BeginGame() with the output-batch bookkeeping (see OutputFormatter).
	// Call this instead of Starlane::BeginGame() directly.
	void RunBeginGame();

	// Loads the TAF file at `path` as a new game, replacing whatever game is currently loaded
	// (asking for confirmation first if one is ongoing). Used by the "Open Game" menu action, a
	// command-line argument, and OS "open with"/double-click delivery alike.
	bool LoadGameFile(const QString &path);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

private:
	QTextBrowser *output;
	QLineEdit *input;
	// Real-time events run on wall-clock seconds, so the core wants a tick a second. It ignores
	// ticks arriving before the game starts or part-way through a command, so this can simply run
	// for the life of the window -- which is what ADRIFT does with its own.
	QTimer *eventTimer;
	OutputFormatter *formatter;

	QAction *openGameAction;
	QAction *saveGameAction;
	QAction *restoreGameAction;
	QAction *transcriptAction;
	QAction *replayAction;
	bool transcribing = false;
	// The open transcript file while `transcribing` is true; null otherwise. Fed via
	// OutputFormatter::SetTranscriptSink(), which calls WriteTranscript() with plain (tag-stripped)
	// output text as it's produced -- see ToggleTranscript()/StopTranscript().
	QFile *transcriptFile = nullptr;

	// The three segments of Starlane::StatusBar, left to right: current location (fixed width),
	// an author-defined segment (stretches to fill the remaining space), and -- only shown for
	// games that use scoring -- the current score, in the status bar's permanent (right-aligned)
	// area.
	QLabel *locationLabel;
	QLabel *userStatusLabel;
	QLabel *scoreLabel;

	// Set while ReplayCommandsTriggered() is feeding commands from a file, so the <waitkey>
	// handler knows not to block the replay on player input that isn't coming.
	bool isReplaying = false;

	// Set while a <waitkey> tag is blocking on WaitForKeyOrClick(), so eventFilter() knows to
	// consume the next key/click instead of letting it reach whatever widget it landed on.
	QEventLoop *waitKeyLoop = nullptr;

	// Set once LoadGameFile() has extracted the current game from a Blorb archive; reset (back to
	// std::nullopt) for a game loaded from a bare .taf. LoadImage() consults this to decide how an
	// <img src="..."> path resolves -- through the Blorb resource map, or as a literal filesystem
	// path -- since that choice depends on how the whole game was loaded, not on any one image.
	std::optional<BlorbFile> currentBlorb;

	// Resolves an <img src="..."> value to its decoded image data, for OutputFormatter's benefit
	// (passed to it as a callback in the constructor). Returns a null QImage if the path can't be
	// resolved or read -- OutputFormatter treats that as "skip this image".
	QImage LoadImage(const QString &path) const;

	// One of ADRIFT's 8 sound channels (numbered 1-8; index 0 of `soundChannels` below is unused
	// so a channel number can index it directly). Mirrors starlane-glk/multimedia.cpp's
	// gSoundChannels/gRecentlyPlayedSound, adapted to QtMultimedia: each channel keeps its own
	// QMediaPlayer/QAudioOutput pair alive for the process's lifetime (parented to `this`, so Qt
	// cleans them up on close), plus whichever QBuffer currently backs a Blorb-resource sound
	// (null when playing directly from a file path via QMediaPlayer::setSource() instead).
	struct SoundChannel {
		QMediaPlayer *player = nullptr;
		QAudioOutput *output = nullptr;
		QBuffer *buffer = nullptr;
		// The src most recently (successfully) started on this channel, so a repeated "play" of
		// the exact same src resumes it (if paused) rather than restarting it from the top.
		QString recentlyPlayedSrc;
	};
	std::array<SoundChannel, 9> soundChannels;

	// Creates the 8 QMediaPlayer/QAudioOutput pairs backing soundChannels[1..8]. Called once from
	// the constructor -- unlike currentBlorb-style per-game state, the channels themselves persist
	// across LoadGameFile() calls; only what's currently playing on each does not (see
	// LoadGameFile()'s StopAllSounds() call).
	void InitSoundChannels();
	// Stops whatever is playing/paused on every channel and forgets recentlyPlayedSrc for each --
	// called when a new game is loaded, so the previous game's audio doesn't keep playing over it.
	void StopAllSounds();
	// The three callbacks OutputFormatter's constructor takes for <audio ...>; see its own doc
	// comment for the channel-range contract these can assume.
	void PlaySound(const QString &src, int channel, bool loop);
	void PauseSound(int channel);
	void StopSound(int channel);

	void CreateMenus();
	// Enables/disables the game-dependent menu actions based on Starlane::GameIsOngoing().
	void UpdateActionState();
	// Refreshes the status bar from Starlane::GetStatusBar(). Per that function's own doc comment,
	// call this after every BeginGame()/ProcessInput()/TimeTick() -- i.e. everywhere
	// UpdateActionState() is already called.
	void UpdateStatusBar();

	// Sends `cmd` to the game as if the player had typed it: echoes it to the output, then runs
	// it through Starlane::ProcessInput() with the usual output-batch bookkeeping.
	void SubmitCommand(const QString &cmd);

	void InputReturnPressed();
	void HandleTimeTick();
	void OpenGameTriggered();
	void SaveGameTriggered();
	void RestoreGameTriggered();
	void ToggleTranscript();
	// Writes `text` (plain, already tag-stripped output) to the open transcript file, if any.
	// Installed as OutputFormatter's transcript sink while `transcribing` is true.
	void WriteTranscript(const QString &text);
	// Closes and forgets the open transcript file, if any, and resets the menu label/`transcribing`
	// accordingly. A no-op if no transcript is active -- safe to call unconditionally, e.g. when a
	// new game is loaded or the window is closing.
	void StopTranscript();
	void ReplayCommandsTriggered();
	// Blocks (via a nested event loop) until the next keypress or mouse click anywhere in the
	// app. Used to implement the <waitkey> tag.
	void WaitForKeyOrClick();
};

#endif  // !STARLANE_MAINWINDOW_H
