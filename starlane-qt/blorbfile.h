//
// Created by Adrian Welcker on 30.07.26.
//

#ifndef STARLANE_BLORBFILE_H
#define STARLANE_BLORBFILE_H

#include <optional>

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QVector>

// Parses the Blorb resource-archive format (reference/ifarchive-if-specs/Blorb-Spec.md), which
// ADRIFT 5 uses to bundle a game file together with the images/sounds it references. ADRIFT's own
// Blorb writer leaves the top-level FORM chunk's declared length wrong (see the spec's "ADRIFT 5
// Compatibility Issues" section, and starlane-glk/starlane-glk.cpp's locate_gamefile(), which works
// around the same bug for the Glk frontend by patching that field in-place) -- rather than patch
// it, this parser simply never reads it: sub-chunks are scanned until the buffer runs out instead
// of trusting the declared FORM size.
class BlorbFile {
public:
	// A resource's raw chunk contents, alongside its four-character chunk type (e.g. "ADRI",
	// "JPEG", "WAV ") identifying its format.
	struct Resource {
		QByteArray type;
		QByteArray data;
	};

	// True if `data` starts with a Blorb container's "FORM"/"IFRS" signature. Doesn't validate
	// the rest of the structure -- call Parse() for that.
	static bool IsBlorbData(const QByteArray &data);

	// Parses `data` as a Blorb file. Returns std::nullopt if it isn't a well-formed Blorb
	// container (bad signature, a chunk whose declared length runs past the end of `data`, or no
	// resource index chunk).
	static std::optional<BlorbFile> Parse(const QByteArray &data);

	// The game file bundled as the Exec resource -- ADRIFT packs its raw, still-encoded .taf
	// bytes in here as a chunk of type "ADRI", exactly as they'd be read from a standalone .taf
	// file, so this can be passed straight to Starlane::CreateGame(). Empty if the file has no
	// Exec resource (a Blorb containing only resources for a separately-distributed .taf file).
	QByteArray GetExecResource() const;

	// Looks up a resource (e.g. an image or sound) by its Blorb usage (kUsagePict/kUsageSnd/
	// kUsageData) and resource number -- the same number Starlane::GetBlorbResourceForPath()
	// returns for a path referenced from an <img>/<audio> tag. std::nullopt if there is no such
	// resource.
	std::optional<Resource> GetResource(quint32 usage, quint32 number) const;

	// Blorb resource usage codes (Blorb-Spec.md's "Contents of the Resource Index Chunk"), as
	// big-endian four-character codes.
	static constexpr quint32 kUsagePict = 0x50696374;  // 'Pict'
	static constexpr quint32 kUsageSnd = 0x536e6420;   // 'Snd '
	static constexpr quint32 kUsageData = 0x44617461;  // 'Data'
	static constexpr quint32 kUsageExec = 0x45786563;  // 'Exec'

private:
	struct Chunk {
		QByteArray type;
		qint64 dataStart;
		qint64 dataLength;
	};

	static quint64 ResourceKey(quint32 usage, quint32 number) {
		return (quint64(usage) << 32) | number;
	}

	// Kept around (cheaply -- QByteArray is implicitly shared) so GetResource() can slice
	// resource bytes out of it on demand; `chunks` only records offsets/lengths into this buffer.
	QByteArray rawData;
	QVector<Chunk> chunks;
	QHash<quint64, int> resourcesByUsageNumber;  // (usage, number) -> index into `chunks`
};

#endif  // !STARLANE_BLORBFILE_H
