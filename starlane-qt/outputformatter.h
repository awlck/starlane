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
#include <QtGui/QTextCharFormat>
#include <QtGui/QTextCursor>
#include <QtWidgets/QTextBrowser>

class OutputFormatter {
public:
	// The command colour used for both echoed player input and <c> tags: the current game's own
	// InputColour, if it specifies one, or a default red otherwise.
	static QColor CommandColor();

	OutputFormatter(QTextBrowser *browser, std::function<void()> waitKeyHandler);

	// Re-derives the base text color/font from the current game's FontName/OutputColour, if it
	// specifies any. Call once after CreateGame() (and before the first output) -- none of that is
	// known yet when the formatter is constructed alongside the rest of the window.
	void ApplyGameDefaults();

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
	// larger-than-one-screen batch out of view.
	void EndBatch();

private:
	struct CharFormatFrame {
		QString tagName;  // "b", "i", "u", "c", "font" -- used to match up closing tags
		QTextCharFormat format;
	};

	QTextBrowser *browser;
	std::function<void()> waitKeyHandler;

	QTextCharFormat baseCharFormat;
	Qt::Alignment baseAlignment;
	QVector<CharFormatFrame> charFormatStack;
	QVector<Qt::Alignment> alignmentStack;

	int batchStartBlockNumber = 0;

	// Tokenizer state, carried across AppendText() calls in case a tag is
	// ever split across two OutputText invocations.
	bool inTag = false;
	QChar quoteChar;
	QString tagBuffer;
	QString textRun;

	void ResetFormattingState();
	void FlushTextRun();
	void InsertLineBreak();
	void HandleTag(const QString &tag);
	void HandleFontTag(const QString &attributes);
	void PushCharFormat(const QString &tagName, const QTextCharFormat &format);
	void PopCharFormat(const QString &tagName);
	void ApplyCurrentBlockAlignment(QTextCursor &cursor);
	QTextCharFormat CurrentCharFormat() const;
	Qt::Alignment CurrentAlignment() const;
};

#endif  // !STARLANE_OUTPUTFORMATTER_H
