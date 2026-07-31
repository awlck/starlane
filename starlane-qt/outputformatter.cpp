//
// Created for text-formatting support in the Qt frontend.
//

#include "outputformatter.h"

#include <starlane-core.h>

#include <QtCore/QMap>
#include <QtCore/QUrl>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QFont>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocument>
#include <QtGui/QTextImageFormat>
#include <QtWidgets/QScrollBar>

namespace {

// Unpacks a starlane-core 0xRRGGBB color into a QColor.
QColor FromPackedRgb(uint32_t rgb) {
	return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Parses a tag's attribute text (everything after the tag name) into a
// key->value map. Quote-aware (single or double quotes); bare flags (no
// "=value") are recorded with an empty value so callers can still test for
// their presence.
QMap<QString, QString> ParseAttributes(const QString &text) {
	QMap<QString, QString> attrs;
	const int n = text.length();
	int i = 0;
	while (i < n) {
		while (i < n && text[i].isSpace()) ++i;
		if (i >= n) break;
		const int keyStart = i;
		while (i < n && text[i] != '=' && !text[i].isSpace()) ++i;
		const QString key = text.mid(keyStart, i - keyStart).toLower();
		while (i < n && text[i].isSpace()) ++i;
		QString value;
		if (i < n && text[i] == '=') {
			++i;
			while (i < n && text[i].isSpace()) ++i;
			if (i < n && (text[i] == '"' || text[i] == '\'')) {
				const QChar quote = text[i++];
				const int valStart = i;
				while (i < n && text[i] != quote) ++i;
				value = text.mid(valStart, i - valStart);
				if (i < n) ++i;  // skip closing quote
			} else {
				const int valStart = i;
				while (i < n && !text[i].isSpace()) ++i;
				value = text.mid(valStart, i - valStart);
			}
		}
		if (!key.isEmpty()) attrs.insert(key, value);
	}
	return attrs;
}

}  // namespace

QColor OutputFormatter::CommandColor() {
	// Same colour used to echo the player's own input in MainWindow.
	Starlane::GameInfo info;
	if (Starlane::GetGameInfo(info) && info.hasInputColour)
		return FromPackedRgb(info.inputColour);
	return Qt::red;
}

OutputFormatter::OutputFormatter(QTextBrowser *browser, std::function<void()> waitKeyHandler,
                                  std::function<QImage(const QString &)> imageLoader,
                                  std::function<void(const QString &, int, bool)> playSound,
                                  std::function<void(int)> pauseSound,
                                  std::function<void(int)> stopSound,
                                  std::function<OutputFormatter *(const QString &)> getWindow)
	: browser(browser), waitKeyHandler(std::move(waitKeyHandler)), imageLoader(std::move(imageLoader)),
	  playSound(std::move(playSound)), pauseSound(std::move(pauseSound)), stopSound(std::move(stopSound)),
	  getWindow(std::move(getWindow)) {
	baseCharFormat.setForeground(browser->palette().color(QPalette::Text));
	const QFont font = browser->font();
	baseCharFormat.setFontFamilies({font.family()});
	baseCharFormat.setFontPointSize(font.pointSizeF() > 0 ? font.pointSizeF() : 10.0);
	baseAlignment = Qt::AlignLeft;
}

void OutputFormatter::SetTranscriptSink(std::function<void(const QString &)> sink) {
	transcriptSink = std::move(sink);
}

void OutputFormatter::ApplyGameDefaults() {
	Starlane::GameInfo info;
	if (!Starlane::GetGameInfo(info)) return;
	if (info.hasOutputColour)
		baseCharFormat.setForeground(FromPackedRgb(info.outputColour));
	if (!info.fontName.empty())
		baseCharFormat.setFontFamilies({QString::fromUtf8(info.fontName.c_str())});
}

QTextCharFormat OutputFormatter::CurrentCharFormat() const {
	return charFormatStack.isEmpty() ? baseCharFormat : charFormatStack.last().format;
}

Qt::Alignment OutputFormatter::CurrentAlignment() const {
	return alignmentStack.isEmpty() ? baseAlignment : alignmentStack.last();
}

void OutputFormatter::ResetFormattingState() {
	charFormatStack.clear();
	alignmentStack.clear();
}

void OutputFormatter::BeginBatch() {
	ResetFormattingState();
	batchStartBlockNumber = browser->document()->lastBlock().blockNumber();
	batchStartCharCount = browser->document()->characterCount();
}

void OutputFormatter::EndBatch() {
	CommitTextRun();
	if (redirectTarget) {
		// Implicit close, mirroring ADRIFT's "unclosed tags are auto-closed" behavior (see this
		// file's header comment): a <window> left unclosed at the end of an output batch still
		// delivers whatever it captured instead of losing it.
		OutputFormatter *target = redirectTarget;
		const QString content = redirectBuffer;
		redirectTarget = nullptr;
		redirectDepth = 0;
		redirectBuffer.clear();
		target->AppendText(content);
	}

	QTextDocument *doc = browser->document();
	if (doc->characterCount() == batchStartCharCount) return;  // nothing was actually output

	QTextBlock startBlock = doc->findBlockByNumber(batchStartBlockNumber);
	if (!startBlock.isValid()) startBlock = doc->firstBlock();

	const qreal startY = doc->documentLayout()->blockBoundingRect(startBlock).top();
	const qreal viewportHeight = browser->viewport()->height();
	QScrollBar *vbar = browser->verticalScrollBar();
	if (doc->size().height() - startY <= viewportHeight) {
		// The whole batch fits on one screen: scroll it fully into view.
		vbar->setValue(vbar->maximum());
	} else {
		// The batch is taller than the viewport: keep its start visible
		// rather than scrolling straight to the (now much further away) end.
		vbar->setValue(qRound(startY));
	}
}

void OutputFormatter::ApplyCurrentBlockAlignment(QTextCursor &cursor) {
	QTextBlockFormat fmt;
	fmt.setAlignment(CurrentAlignment());
	cursor.mergeBlockFormat(fmt);
}

void OutputFormatter::FlushTextRun() {
	if (textRun.isEmpty()) return;

	QString decoded = textRun;
	decoded.replace("&lt;", "<")
	        .replace("&gt;", ">")
	        .replace("&quot;", "\"")
	        .replace("&apos;", "'")
	        .replace("&nbsp;", QChar(0x00A0))
	        .replace("&amp;", "&");
	textRun.clear();

	if (transcriptSink) transcriptSink(decoded);

	QTextCursor cursor(browser->document());
	cursor.movePosition(QTextCursor::End);
	ApplyCurrentBlockAlignment(cursor);
	cursor.insertText(decoded, CurrentCharFormat());
}

void OutputFormatter::CommitTextRun() {
	if (redirectTarget) {
		redirectBuffer += textRun;
		textRun.clear();
		return;
	}
	FlushTextRun();
}

void OutputFormatter::InsertLineBreak() {
	if (transcriptSink) transcriptSink(QStringLiteral("\n"));

	QTextCursor cursor(browser->document());
	cursor.movePosition(QTextCursor::End);
	QTextBlockFormat blockFormat;
	blockFormat.setAlignment(CurrentAlignment());
	cursor.insertBlock(blockFormat);
}

void OutputFormatter::PushCharFormat(const QString &tagName, const QTextCharFormat &format) {
	charFormatStack.push_back({tagName, format});
}

void OutputFormatter::PopCharFormat(const QString &tagName) {
	if (!charFormatStack.isEmpty() && charFormatStack.last().tagName == tagName)
		charFormatStack.pop_back();
}

void OutputFormatter::HandleFontTag(const QString &attributes) {
	QTextCharFormat fmt = CurrentCharFormat();
	const auto attrs = ParseAttributes(attributes);

	if (attrs.contains("face") && !attrs["face"].isEmpty())
		fmt.setFontFamilies({attrs["face"]});

	if (attrs.contains("size") && !attrs["size"].isEmpty()) {
		const QString &sizeStr = attrs["size"];
		bool ok = false;
		const double size = sizeStr.toDouble(&ok);
		if (ok) {
			if (sizeStr.startsWith('+') || sizeStr.startsWith('-'))
				fmt.setFontPointSize(qMax(1.0, fmt.fontPointSize() + size));
			else
				fmt.setFontPointSize(size);
		}
	}

	const QString colorStr = attrs.contains("color") ? attrs["color"] : attrs.value("colour");
	if (!colorStr.isEmpty()) {
		const QColor color(colorStr);
		if (color.isValid()) fmt.setForeground(color);
	}

	PushCharFormat("font", fmt);
}

void OutputFormatter::HandleImgTag(const QString &attributes) {
	if (!imageLoader) return;
	const QString src = ParseAttributes(attributes).value("src");
	if (src.isEmpty()) return;

	const QImage image = imageLoader(src);
	if (image.isNull()) return;  // not found/unreadable: skip silently, as a missing <img> should

	// The image's stored resource stays at natural size regardless of how it ends up displayed
	// below -- only the QTextImageFormat's own width/height (computed fresh below, from the pane's
	// *current* width) controls that, so re-showing the same src later at a different pane width
	// sizes independently rather than picking up whatever size an earlier showing left cached.
	const QUrl url(QStringLiteral("starlane-image:") + src);
	browser->document()->addResource(QTextDocument::ImageResource, url, QVariant(image));

	QSize displaySize = image.size();
	const qreal margin = 2 * browser->document()->documentMargin();
	const int maxWidth = qMax(1, (int) (browser->viewport()->width() - margin));
	if (displaySize.width() > maxWidth) {
		displaySize.setHeight(qMax(1, qRound((qreal) displaySize.height() * maxWidth / displaySize.width())));
		displaySize.setWidth(maxWidth);
	}

	QTextImageFormat imgFormat;
	imgFormat.setName(url.toString());
	imgFormat.setWidth(displaySize.width());
	imgFormat.setHeight(displaySize.height());

	QTextCursor cursor(browser->document());
	cursor.movePosition(QTextCursor::End);
	ApplyCurrentBlockAlignment(cursor);
	cursor.insertImage(imgFormat);
}

void OutputFormatter::HandleAudioTag(const QString &rest) {
	// ParseAttributes already treats a bare word (no "=value") as a flag with an empty value, so
	// "play"/"pause"/"stop" -- which AUDREGEX allows in any position, "play" even being optional
	// when src is given directly -- fall out of the same parse as the channel/src/loop attributes,
	// rather than needing their own separate tokenizing pass.
	const auto attrs = ParseAttributes(rest);

	int channel = 1;
	if (attrs.contains("channel")) {
		bool ok = false;
		const int parsed = attrs["channel"].toInt(&ok);
		if (ok) channel = parsed;
	}
	if (channel < 1 || channel > 8) return;  // out of range: ignore, same as the original Runner

	if (attrs.contains("pause")) {
		if (pauseSound) pauseSound(channel);
	} else if (attrs.contains("stop")) {
		if (stopSound) stopSound(channel);
	} else {
		const QString src = attrs.value("src");
		if (src.isEmpty()) return;
		const QString loopVal = attrs.value("loop");
		const bool loop = !loopVal.isEmpty() && loopVal[0].toUpper() == QLatin1Char('Y');
		if (playSound) playSound(src, channel, loop);
	}
}

void OutputFormatter::HandleTag(const QString &tagRaw) {
	const QString tag = tagRaw.trimmed();
	if (tag.isEmpty()) return;

	int spaceIdx = -1;
	for (int i = 0; i < tag.length(); ++i) {
		if (tag[i].isSpace()) {
			spaceIdx = i;
			break;
		}
	}
	const QString name = (spaceIdx < 0 ? tag : tag.left(spaceIdx)).toLower();
	const QString rest = spaceIdx < 0 ? QString() : tag.mid(spaceIdx + 1).trimmed();

	if (name == "b") {
		QTextCharFormat f = CurrentCharFormat();
		f.setFontWeight(QFont::Bold);
		PushCharFormat("b", f);
		return;
	}
	if (name == "/b") { PopCharFormat("b"); return; }
	if (name == "i") {
		QTextCharFormat f = CurrentCharFormat();
		f.setFontItalic(true);
		PushCharFormat("i", f);
		return;
	}
	if (name == "/i") { PopCharFormat("i"); return; }
	if (name == "u") {
		QTextCharFormat f = CurrentCharFormat();
		f.setFontUnderline(true);
		PushCharFormat("u", f);
		return;
	}
	if (name == "/u") { PopCharFormat("u"); return; }
	if (name == "c") {
		QTextCharFormat f = CurrentCharFormat();
		f.setForeground(CommandColor());
		PushCharFormat("c", f);
		return;
	}
	if (name == "/c") { PopCharFormat("c"); return; }
	if (name == "font") { HandleFontTag(rest); return; }
	if (name == "/font") { PopCharFormat("font"); return; }

	if (name == "center" || name == "centre") { alignmentStack.push_back(Qt::AlignHCenter); return; }
	if (name == "left") { alignmentStack.push_back(Qt::AlignLeft); return; }
	if (name == "right") { alignmentStack.push_back(Qt::AlignRight); return; }
	if (name == "/center" || name == "/centre" || name == "/left" || name == "/right") {
		if (!alignmentStack.isEmpty()) alignmentStack.pop_back();
		return;
	}

	if (name == "br") { InsertLineBreak(); return; }

	if (name == "del") {
		QTextCursor cursor(browser->document());
		cursor.movePosition(QTextCursor::End);
		cursor.deletePreviousChar();
		return;
	}

	if (name == "cls") {
		browser->clear();
		ResetFormattingState();
		batchStartBlockNumber = 0;
		return;
	}

	if (name == "waitkey") {
		browser->ensureCursorVisible();
		if (waitKeyHandler) waitKeyHandler();
		return;
	}

	if (name == "img") { HandleImgTag(rest); return; }
	if (name == "audio") { HandleAudioTag(rest); return; }

	if (name == "window") {
		if (!rest.isEmpty() && getWindow) {
			if (OutputFormatter *target = getWindow(rest)) {
				redirectTarget = target;
				redirectDepth = 1;
				redirectBuffer.clear();
			}
		}
		return;
	}

	// <wait n>, <bgcolor>/<bgcolour>: recognized so their markup never leaks into visible output,
	// but intentionally no-op until their own dedicated follow-up work.
}

void OutputFormatter::HandleRedirectedTag(const QString &tagRaw) {
	const QString lower = tagRaw.trimmed().toLower();
	if (lower == QStringLiteral("/window")) {
		if (--redirectDepth == 0) {
			OutputFormatter *target = redirectTarget;
			const QString content = redirectBuffer;
			redirectTarget = nullptr;
			redirectBuffer.clear();
			target->AppendText(content);
			return;
		}
	} else if (lower == QStringLiteral("window") || lower.startsWith(QStringLiteral("window "))) {
		++redirectDepth;
	}
	redirectBuffer += '<';
	redirectBuffer += tagRaw;
	redirectBuffer += '>';
}

void OutputFormatter::AppendText(const QString &chunk) {
	for (const QChar &c : chunk) {
		if (inTag) {
			if (!quoteChar.isNull()) {
				tagBuffer += c;
				if (c == quoteChar) quoteChar = QChar();
				continue;
			}
			if (c == '"' || c == '\'') {
				quoteChar = c;
				tagBuffer += c;
				continue;
			}
			if (c == '<') {
				// The previous '<' never got a matching '>': treat it (and
				// whatever followed) as literal text and start scanning a
				// fresh tag from here.
				textRun += '<';
				textRun += tagBuffer;
				tagBuffer.clear();
				continue;
			}
			if (c == '>') {
				CommitTextRun();
				if (redirectTarget) HandleRedirectedTag(tagBuffer);
				else HandleTag(tagBuffer);
				tagBuffer.clear();
				inTag = false;
				continue;
			}
			tagBuffer += c;
			continue;
		}

		if (c == '<') {
			inTag = true;
			quoteChar = QChar();
			tagBuffer.clear();
			continue;
		}
		if (c == '\n') {
			CommitTextRun();
			if (redirectTarget) redirectBuffer += '\n';
			else InsertLineBreak();
			continue;
		}
		textRun += c;
	}
	CommitTextRun();
}
