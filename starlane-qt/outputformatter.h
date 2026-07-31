//
// Hand-rolled parser for ADRIFT's output-text markup (a small, non-HTML tag
// vocabulary -- <b>, <i>, <c>, <font>, <center>, <cls>, <waitkey>, ...).
// Applies formatting directly via QTextCursor/QTextCharFormat/QTextBlockFormat
// instead of going through QTextBrowser's HTML engine, since that markup isn't
// valid HTML and Qt has no equivalent for ADRIFT's "unclosed tags are implicitly
// closed at the end of an output batch" behavior.
//

#ifndef STARLANE_OUTPUTFORMATTER_H
#define STARLANE_OUTPUTFORMATTER_H

#include <functional>

#include <QtCore/QVector>
#include <QtGui/QImage>
#include <QtGui/QTextCharFormat>
#include <QtGui/QTextCursor>
#include <QtWidgets/QTextBrowser>

class OutputFormatter {
public:
	// The command colour used for both echoed player input and <c> tags: the current game's own
	// InputColour, if it specifies one, or a default red otherwise.
	static QColor CommandColor();

	// `imageLoader` resolves an <img src="..."> value to its decoded image data -- however the
	// frontend wants to interpret that path (a Blorb resource lookup, a plain file on disk, ...).
	// A null QImage means "couldn't be loaded", which HandleImgTag treats as "skip silently".
	// `playSound`/`pauseSound`/`stopSound` back the three forms of <audio ...> (see
	// HandleAudioTag): `channel` is always in [1, 8] (ADRIFT's 8 sound channels) by the time any
	// of these run -- this is the boundary where that gets checked, so the frontend's own
	// channel-array lookups never need to.
	// `getWindow` resolves a <window NAME> tag's target: the OutputFormatter that the text up to
	// the matching </window> should be redirected into, instead of this instance's own browser
	// (MainWindow::GetOrCreateSecondaryWindow() creates a dockable/floating pane for NAME the
	// first time it's seen, and reuses it afterward). Passed identically to every OutputFormatter
	// instance -- the main window's and every secondary window's alike -- so a <window> tag nested
	// inside another window's own redirected content keeps working, recursively.
	OutputFormatter(QTextBrowser *browser, std::function<void()> waitKeyHandler,
	                 std::function<QImage(const QString &)> imageLoader,
	                 std::function<void(const QString &src, int channel, bool loop)> playSound,
	                 std::function<void(int channel)> pauseSound,
	                 std::function<void(int channel)> stopSound,
	                 std::function<OutputFormatter *(const QString &name)> getWindow);

	// Re-derives the base text color/font from the current game's FontName/OutputColour, if it
	// specifies any. Call once after CreateGame() (and before the first output) -- none of that is
	// known yet when the formatter is constructed alongside the rest of the window.
	void ApplyGameDefaults();

	// Installs (or, given an empty std::function, removes) a callback invoked with plain text as
	// it's produced -- once per flushed text run, decoded of entities and with none of ADRIFT's own
	// tag markup, plus once with "\n" for every line break. Used by MainWindow to mirror all output
	// (and, since the echoed player command is fed back through this same tag parser, echoed
	// commands too) to a transcript file while one is active.
	void SetTranscriptSink(std::function<void(const QString &)> sink);

	// Called before the first OutputText call of a new "batch" (BeginGame(),
	// ProcessInput(), or TimeTick()): resets formatting to defaults (ADRIFT's
	// implicit tag auto-close) and records where this batch's content starts,
	// for EndBatch()'s scroll adjustment.
	void BeginBatch();
	// Feed a chunk of raw output text (as received from Starlane::TextOutputter)
	// through the tokenizer, applying formatting/tags as they're encountered.
	void AppendText(const QString &chunk);
	// Called once the batch's core call has returned: adjusts scrolling so
	// that newly-added text is visible, without scrolling the start of a
	// larger-than-one-screen batch out of view. A no-op if the batch didn't
	// actually produce any output -- otherwise a real-time tick that had
	// nothing to report would still yank the scroll position back down on
	// every single one, fighting a player who had scrolled up to reread.
	void EndBatch();

private:
	struct CharFormatFrame {
		QString tagName;  // "b", "i", "u", "c", "font" -- used to match up closing tags
		QTextCharFormat format;
	};

	QTextBrowser *browser;
	std::function<void()> waitKeyHandler;
	std::function<QImage(const QString &)> imageLoader;
	std::function<void(const QString &src, int channel, bool loop)> playSound;
	std::function<void(int channel)> pauseSound;
	std::function<void(int channel)> stopSound;
	std::function<OutputFormatter *(const QString &name)> getWindow;
	std::function<void(const QString &)> transcriptSink;

	QTextCharFormat baseCharFormat;
	Qt::Alignment baseAlignment;
	QVector<CharFormatFrame> charFormatStack;
	QVector<Qt::Alignment> alignmentStack;

	int batchStartBlockNumber = 0;
	// Document character count when this batch began -- if EndBatch() finds it unchanged, the
	// batch produced no visible output at all, so the scroll position is left alone.
	int batchStartCharCount = 0;

	// Tokenizer state, carried across AppendText() calls in case a tag is
	// ever split across two OutputText invocations.
	bool inTag = false;
	QChar quoteChar;
	QString tagBuffer;
	QString textRun;

	// Window-redirection state, carried across AppendText() calls the same way the tag tokenizer
	// state above is -- a <window NAME>...</window> block can itself be split across separate
	// OutputText() invocations (e.g. one per Print action in a task). Non-null `redirectTarget`
	// means text/tags are currently being captured (verbatim, uninterpreted) into `redirectBuffer`
	// instead of being applied to this instance's own browser; `redirectDepth` counts further
	// nested <window ...> opens seen since, so the matching </window> can be found even when the
	// captured content itself opens further windows (those are only interpreted later, when the
	// finished buffer is replayed through redirectTarget->AppendText() -- see HandleRedirectedTag).
	OutputFormatter *redirectTarget = nullptr;
	int redirectDepth = 0;
	QString redirectBuffer;

	void ResetFormattingState();
	void FlushTextRun();
	// Routes the pending text run to redirectBuffer (verbatim) if a <window> redirect is active,
	// or otherwise to FlushTextRun() as usual. The single commit point AppendText() uses for
	// "the plain text accumulated so far is complete" -- at a tag boundary, a line break, or the
	// end of a batch -- regardless of which of the two destinations it's currently headed to.
	void CommitTextRun();
	void InsertLineBreak();
	void HandleTag(const QString &tag);
	// Handles one complete "<...>" tag encountered while capturing a <window NAME>...</window>
	// block (see AppendText()/CommitTextRun()): tracks nesting depth so a further <window> tag
	// inside the captured content doesn't end the capture at its own </window>, and otherwise just
	// re-emits the tag's exact source text into redirectBuffer -- it's interpreted later, when the
	// finished buffer is fed through redirectTarget->AppendText().
	void HandleRedirectedTag(const QString &tag);
	void HandleFontTag(const QString &attributes);
	// Inserts the image an <img src="..."> tag refers to in-line at the current cursor position,
	// scaled down (never up) to fit the output pane's current width if it doesn't already. Any
	// other attribute (width, height, ...) is ignored -- matching the original ADRIFT Runner,
	// which ignores them too (confirmed against its Global.vb).
	void HandleImgTag(const QString &attributes);
	// Handles all three forms of <audio ...>: play (`play src="..."` or a bare `src="..."`,
	// optionally with `channel="N"` and/or `loop="Y"`), `pause`, and `stop` (each optionally with
	// `channel="N"` too) -- matching ADRIFT's own AUDREGEX (FileIO.vb/Generator.vb) rather than
	// requiring the tag's attributes/action word in any particular order. `channel` defaults to 1
	// and is dropped silently if out of [1, 8], same as the original Runner (Global.vb).
	void HandleAudioTag(const QString &rest);
	void PushCharFormat(const QString &tagName, const QTextCharFormat &format);
	void PopCharFormat(const QString &tagName);
	void ApplyCurrentBlockAlignment(QTextCursor &cursor);
	QTextCharFormat CurrentCharFormat() const;
	Qt::Alignment CurrentAlignment() const;
};

#endif  // !STARLANE_OUTPUTFORMATTER_H
