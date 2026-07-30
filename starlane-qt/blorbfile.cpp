//
// Created by Adrian Welcker on 30.07.26.
//

#include "blorbfile.h"

namespace {

quint32 ReadBigEndianU32(const char *p) {
	return (quint32(quint8(p[0])) << 24) | (quint32(quint8(p[1])) << 16)
	     | (quint32(quint8(p[2])) << 8) | quint32(quint8(p[3]));
}

}  // namespace

bool BlorbFile::IsBlorbData(const QByteArray &data) {
	return data.size() >= 12 && data.startsWith("FORM") && data.mid(8, 4) == QByteArray("IFRS");
}

std::optional<BlorbFile> BlorbFile::Parse(const QByteArray &data) {
	if (!IsBlorbData(data)) return std::nullopt;

	BlorbFile result;
	result.rawData = data;

	// Scan sub-chunks until the buffer runs out, deliberately ignoring the FORM chunk's own
	// declared length (see the class comment in blorbfile.h) -- ADRIFT's Blorb writer gets it
	// wrong, and ignoring it entirely is simpler than patching it like starlane-glk does.
	qint64 pos = 12;  // past "FORM" + 4-byte length + "IFRS"
	const qint64 end = data.size();
	while (pos + 8 <= end) {
		QByteArray type = data.mid(pos, 4);
		quint32 declaredLen = ReadBigEndianU32(data.constData() + pos + 4);
		qint64 dataStart = pos + 8;
		if (dataStart + (qint64) declaredLen > end) return std::nullopt;  // truncated/corrupt chunk

		result.chunks.push_back(Chunk{type, dataStart, (qint64) declaredLen});

		pos = dataStart + declaredLen;
		if (pos & 1) pos++;  // chunks are padded to an even length
	}

	int ridxIndex = -1;
	for (int i = 0; i < result.chunks.size(); i++) {
		if (result.chunks[i].type == "RIdx") {
			ridxIndex = i;
			break;
		}
	}
	if (ridxIndex < 0) return std::nullopt;  // no resource index: not a valid Blorb file

	const Chunk &ridx = result.chunks[ridxIndex];
	if (ridx.dataLength < 4) return std::nullopt;
	const char *ridxData = data.constData() + ridx.dataStart;
	quint32 numResources = ReadBigEndianU32(ridxData);
	if (4 + (qint64) numResources * 12 > ridx.dataLength) return std::nullopt;

	// The index's "start" field for each resource is an absolute byte offset from the start of
	// the file to that resource chunk's *header* (Blorb-Spec.md's "Contents of the Resource Index
	// Chunk") -- map those header offsets back to the chunks scanned above (whose dataStart is 8
	// bytes past its own header) so each resource can be resolved to one.
	QHash<qint64, int> chunkByHeaderStart;
	for (int i = 0; i < result.chunks.size(); i++)
		chunkByHeaderStart[result.chunks[i].dataStart - 8] = i;

	for (quint32 i = 0; i < numResources; i++) {
		const char *entry = ridxData + 4 + i * 12;
		quint32 usage = ReadBigEndianU32(entry);
		quint32 number = ReadBigEndianU32(entry + 4);
		quint32 startOffset = ReadBigEndianU32(entry + 8);
		auto found = chunkByHeaderStart.constFind((qint64) startOffset);
		if (found == chunkByHeaderStart.cend()) continue;  // dangling reference; ignore
		result.resourcesByUsageNumber[ResourceKey(usage, number)] = found.value();
	}

	return result;
}

QByteArray BlorbFile::GetExecResource() const {
	auto res = GetResource(kUsageExec, 0);
	return res ? res->data : QByteArray();
}

std::optional<BlorbFile::Resource> BlorbFile::GetResource(quint32 usage, quint32 number) const {
	auto found = resourcesByUsageNumber.constFind(ResourceKey(usage, number));
	if (found == resourcesByUsageNumber.cend()) return std::nullopt;
	const Chunk &chunk = chunks[found.value()];
	return Resource{chunk.type, rawData.mid(chunk.dataStart, chunk.dataLength)};
}
