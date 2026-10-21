#include "infrastructure/extensions/zip_archive.h"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <zlib.h>

namespace nekotune::zip {
namespace {
quint16 u16(const uchar *p) { return quint16(p[0] | (p[1] << 8)); }
quint32 u32(const uchar *p) { return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24); }
void put16(QByteArray &out, quint16 value) {
    out.append(char(value & 0xff));
    out.append(char(value >> 8));
}
void put32(QByteArray &out, quint32 value) {
    put16(out, quint16(value & 0xffff));
    put16(out, quint16(value >> 16));
}
struct Entry {
    QString name;
    quint16 method = 0, flags = 0, madeBy = 0;
    quint32 crc = 0, compressed = 0, size = 0, attributes = 0, offset = 0;
};
AppError invalid(const QString &message = QStringLiteral("Unsafe or oversized extension archive")) {
    return failure(message);
}
bool safeName(const QString &name) {
    if (name.isEmpty() || name.contains('\\') || name.startsWith('/') || name.contains(':') || name.contains(QChar(0)))
        return false;
    for (const auto &part : name.split('/'))
        if (part == "..")
            return false;
    return true;
}
Result<void> inflateEntry(QFile &archive, const Entry &entry, QIODevice &out) {
    quint32 crc = crc32(0, nullptr, 0);
    qint64 remaining = entry.compressed, written = 0;
    QByteArray input(64 * 1024, Qt::Uninitialized), output(64 * 1024, Qt::Uninitialized);
    const auto emitBytes = [&](const char *data, qint64 length) -> bool {
        written += length;
        if (written > entry.size || out.write(data, length) != length)
            return false;
        crc = crc32(crc, reinterpret_cast<const Bytef *>(data), uInt(length));
        return true;
    };
    if (entry.method == 0) {
        while (remaining > 0) {
            const auto chunk = archive.read(input.data(), qMin<qint64>(remaining, input.size()));
            if (chunk <= 0 || !emitBytes(input.constData(), chunk))
                return invalid();
            remaining -= chunk;
        }
    } else if (entry.method == 8) {
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            return invalid();
        int status = Z_OK;
        while (status != Z_STREAM_END) {
            if (stream.avail_in == 0) {
                if (remaining <= 0)
                    break;
                const auto chunk = archive.read(input.data(), qMin<qint64>(remaining, input.size()));
                if (chunk <= 0)
                    break;
                remaining -= chunk;
                stream.next_in = reinterpret_cast<Bytef *>(input.data());
                stream.avail_in = uInt(chunk);
            }
            stream.next_out = reinterpret_cast<Bytef *>(output.data());
            stream.avail_out = uInt(output.size());
            status = inflate(&stream, Z_NO_FLUSH);
            if ((status != Z_OK && status != Z_STREAM_END) ||
                !emitBytes(output.constData(), output.size() - stream.avail_out)) {
                inflateEnd(&stream);
                return invalid();
            }
        }
        inflateEnd(&stream);
        if (status != Z_STREAM_END)
            return invalid("Truncated extension archive");
    } else
        return invalid("Unsupported ZIP compression method");
    if (written != entry.size || crc != entry.crc)
        return invalid("Corrupted extension archive");
    return {};
}
} // namespace

Result<void> extract(const QString &path, const QString &destination, const Limits &limits) {
    QFile archive(path);
    if (!archive.open(QIODevice::ReadOnly))
        return failure("Cannot open archive: " + archive.errorString(), ErrorCode::Io);
    const auto fileSize = archive.size();
    if (limits.archiveBytes && fileSize > limits.archiveBytes)
        return invalid();
    const qint64 tail = qMin<qint64>(fileSize, 0xffff + 22);
    archive.seek(fileSize - tail);
    const auto end = archive.read(tail);
    qsizetype eocd = -1;
    for (qsizetype i = end.size() - 22; i >= 0; --i)
        if (u32(reinterpret_cast<const uchar *>(end.constData()) + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        return invalid("Not a ZIP archive");
    const auto *e = reinterpret_cast<const uchar *>(end.constData()) + eocd;
    const quint16 count = u16(e + 10);
    const quint32 directorySize = u32(e + 12), directoryOffset = u32(e + 16);
    if (u16(e + 4) != 0 || u16(e + 6) != 0 || count != u16(e + 8) || count == 0xffff || directoryOffset == 0xffffffff ||
        qint64(directoryOffset) + directorySize > fileSize)
        return invalid("Unsupported ZIP archive (multi-disk or ZIP64)");
    if (count > limits.files)
        return invalid();
    archive.seek(directoryOffset);
    const auto directory = archive.read(directorySize);
    QList<Entry> entries;
    qint64 total = 0;
    qsizetype at = 0;
    for (int i = 0; i < count; ++i) {
        if (at + 46 > directory.size())
            return invalid();
        const auto *c = reinterpret_cast<const uchar *>(directory.constData()) + at;
        if (u32(c) != 0x02014b50)
            return invalid();
        Entry entry;
        entry.madeBy = u16(c + 4);
        entry.flags = u16(c + 8);
        entry.method = u16(c + 10);
        entry.crc = u32(c + 16);
        entry.compressed = u32(c + 20);
        entry.size = u32(c + 24);
        const quint16 nameLength = u16(c + 28), extraLength = u16(c + 30), commentLength = u16(c + 32);
        entry.attributes = u32(c + 38);
        entry.offset = u32(c + 42);
        if (at + 46 + nameLength > directory.size())
            return invalid();
        const auto raw = directory.mid(at + 46, nameLength);
        entry.name = (entry.flags & 0x800) ? QString::fromUtf8(raw) : QString::fromLatin1(raw);
        at += 46 + nameLength + extraLength + commentLength;
        if (entry.flags & 1)
            return invalid("Encrypted ZIP entries are not supported");
        if (entry.size == 0xffffffff || entry.compressed == 0xffffffff || entry.offset == 0xffffffff)
            return invalid("Unsupported ZIP archive (multi-disk or ZIP64)");
        const quint32 unixType = (entry.attributes >> 16) & 0170000;
        const bool unixHost = (entry.madeBy >> 8) == 3;
        total += entry.size;
        if (!safeName(entry.name) || total > limits.unpackedBytes ||
            (unixHost && unixType && unixType != 0100000 && unixType != 0040000))
            return invalid();
        entries.append(entry);
    }
    if (!QDir().mkpath(destination))
        return failure("Cannot create " + destination, ErrorCode::Io);
    const auto root = QFileInfo(destination).canonicalFilePath();
    for (const auto &entry : std::as_const(entries)) {
        const auto target = QDir::cleanPath(root + "/" + entry.name);
        if (!target.startsWith(root + "/"))
            return invalid("Archive path escapes destination");
        if (entry.name.endsWith('/')) {
            QDir().mkpath(target);
            continue;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        archive.seek(entry.offset);
        const auto header = archive.read(30);
        const auto *h = reinterpret_cast<const uchar *>(header.constData());
        if (header.size() != 30 || u32(h) != 0x04034b50)
            return invalid();
        archive.seek(qint64(entry.offset) + 30 + u16(h + 26) + u16(h + 28));
        QFile out(target);
        if (out.exists() || !out.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return invalid("Duplicate archive entry");
        const bool executable = ((entry.attributes >> 16) & 0111) != 0;
        out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                           (executable ? QFileDevice::ExeOwner : QFileDevice::Permissions{}));
        if (auto inflated = inflateEntry(archive, entry, out); !inflated)
            return inflated;
    }
    return {};
}

void Writer::add(const QString &name, const QByteArray &data) {
    QByteArray compressed;
    z_stream stream{};
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    compressed.resize(qsizetype(deflateBound(&stream, uLong(data.size()))));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(compressed.data());
    stream.avail_out = uInt(compressed.size());
    deflate(&stream, Z_FINISH);
    compressed.resize(qsizetype(stream.total_out));
    deflateEnd(&stream);
    const auto encoded = name.toUtf8();
    const quint32 crc = crc32(crc32(0, nullptr, 0), reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size()));
    const quint32 offset = quint32(m_body.size());
    const auto header = [&](QByteArray &out, bool central) {
        put32(out, central ? 0x02014b50 : 0x04034b50);
        if (central)
            put16(out, (3 << 8) | 20);
        put16(out, 20);
        put16(out, 0x800);
        put16(out, 8);
        put16(out, 0);
        put16(out, 0x21);
        put32(out, crc);
        put32(out, quint32(compressed.size()));
        put32(out, quint32(data.size()));
        put16(out, quint16(encoded.size()));
        put16(out, 0);
        if (central) {
            put16(out, 0);
            put16(out, 0);
            put16(out, 0);
            put32(out, 0100644u << 16);
            put32(out, offset);
        }
        out.append(encoded);
    };
    header(m_body, false);
    m_body.append(compressed);
    header(m_directory, true);
    ++m_count;
}
Result<void> Writer::addFile(const QString &name, const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure("Cannot read " + path + ": " + file.errorString(), ErrorCode::Io);
    add(name, file.readAll());
    return {};
}
Result<void> Writer::save(const QString &path) const {
    QByteArray end;
    put32(end, 0x06054b50);
    put16(end, 0);
    put16(end, 0);
    put16(end, quint16(m_count));
    put16(end, quint16(m_count));
    put32(end, quint32(m_directory.size()));
    put32(end, quint32(m_body.size()));
    put16(end, 0);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return failure(file.errorString(), ErrorCode::Io);
    file.write(m_body);
    file.write(m_directory);
    file.write(end);
    if (!file.commit())
        return failure(file.errorString(), ErrorCode::Io);
    return {};
}
} // namespace nekotune::zip
