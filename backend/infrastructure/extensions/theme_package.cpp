#include "infrastructure/extensions/theme_package.h"
#include "infrastructure/extensions/zip_archive.h"
#include "themes/theme_resolution.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>

namespace nekotune::themes {
namespace {
const QStringList modes{"light", "dark"};
const QStringList pageIds{"home", "library", "queue", "lyrics", "settings", "extensions", "music_sources", "lyrics_debug"};
const QStringList uiSlots{"background", "shell", "bottomPlayer", "wallpaper"};
bool isObject(const QVariant &value) { return value.typeId() == QMetaType::QVariantMap; }
bool isNumber(const QVariant &value) {
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        return std::isfinite(value.toDouble());
    default:
        return false;
    }
}
bool isColor(const QVariant &value) {
    static const QRegularExpression pattern("^#(?:[\\da-fA-F]{6}|[\\da-fA-F]{8})$");
    return value.typeId() == QMetaType::QString && pattern.match(value.toString()).hasMatch();
}
Result<void> validateTokens(const QVariant &value, bool legacy) {
    if (!value.isValid() || value.isNull())
        return {};
    if (!isObject(value))
        return failure("Theme tokens must be an object");
    const auto rules = catalog().value("tokens").toMap();
    const auto tokens = value.toMap();
    for (auto it = tokens.cbegin(); it != tokens.cend(); ++it) {
        if (!rules.contains(it.key())) {
            if (legacy)
                continue;
            return failure("Unknown theme token: " + it.key());
        }
        const auto rule = rules.value(it.key()).toMap();
        const auto type = rule.value("type").toString();
        const auto &token = it.value();
        bool valid;
        if (type == "color")
            valid = isColor(token);
        else if (type == "string")
            valid = token.typeId() == QMetaType::QString && !token.toString().isEmpty() && token.toString().size() <= 200;
        else if (type == "bool")
            valid = token.typeId() == QMetaType::Bool;
        else {
            const double number = token.toDouble();
            valid = isNumber(token) && number >= rule.value("min").toDouble() && number <= rule.value("max").toDouble() &&
                    (type != "int" || number == std::floor(number));
        }
        if (!valid)
            return failure("Invalid theme token: " + it.key());
    }
    return {};
}
Result<QString> themeFile(const QString &directory, const QString &relative) {
    if (relative.isEmpty() || relative.contains('\\') || relative.contains(':') || QDir::isAbsolutePath(relative))
        return failure("Theme resource must be a contained relative path");
    const auto parts = relative.split('/');
    for (const auto &part : parts)
        if (part.isEmpty() || part == "..")
            return failure("Theme resource must be a contained relative path");
    auto target = QFileInfo(directory).canonicalFilePath();
    if (target.isEmpty())
        return failure("Theme directory is missing", ErrorCode::NotFound);
    for (const auto &part : parts) {
        target += "/" + part;
        const QFileInfo info(target);
        if (info.isSymLink())
            return failure("Theme links are not allowed");
        if (!info.exists())
            return failure("Theme resource is missing: " + relative, ErrorCode::NotFound);
    }
    if (!QFileInfo(target).isFile())
        return failure("Theme resource is not a file");
    return target;
}
quint32 be32(const QByteArray &data, qsizetype at) {
    const auto *p = reinterpret_cast<const uchar *>(data.constData()) + at;
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
}
quint16 be16(const QByteArray &data, qsizetype at) {
    const auto *p = reinterpret_cast<const uchar *>(data.constData()) + at;
    return quint16((p[0] << 8) | p[1]);
}
Result<qint64> imagePixels(const QByteArray &data) {
    if (data.size() >= 33 && data.startsWith("\x89PNG\r\n\x1a\n") && be32(data, 8) == 13 && data.mid(12, 4) == "IHDR")
        return qint64(be32(data, 16)) * be32(data, 20);
    if (data.size() > 4 && be16(data, 0) == 0xffd8) {
        qsizetype offset = 2;
        while (offset + 4 <= data.size()) {
            if (uchar(data[offset++]) != 0xff)
                break;
            while (offset < data.size() && uchar(data[offset]) == 0xff)
                ++offset;
            if (offset >= data.size())
                break;
            const uchar marker = uchar(data[offset++]);
            if (marker == 0xd9 || marker == 0xda)
                break;
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
                continue;
            if (offset + 2 > data.size())
                break;
            const auto length = be16(data, offset);
            if (length < 2 || offset + length > data.size())
                break;
            if ((marker == 0xc0 || marker == 0xc1 || marker == 0xc2) && length >= 8)
                return qint64(be16(data, offset + 3)) * be16(data, offset + 5);
            offset += length;
        }
    }
    return failure("Theme image must be a valid PNG or JPEG");
}
Result<qint64> checkImage(const QString &directory, const QString &relative) {
    const auto file = themeFile(directory, relative);
    if (!file)
        return file.error();
    static const QRegularExpression image("\\.(png|jpe?g)$", QRegularExpression::CaseInsensitiveOption);
    if (!image.match(relative).hasMatch() || QFileInfo(file.value()).size() > unpackedLimit)
        return failure("Invalid or oversized theme image");
    QFile input(file.value());
    if (!input.open(QIODevice::ReadOnly))
        return failure(input.errorString(), ErrorCode::Io);
    const auto pixels = imagePixels(input.readAll());
    if (!pixels)
        return pixels;
    if (!pixels.value() || pixels.value() > pixelLimit)
        return failure("Theme image pixel limit exceeded");
    return pixels;
}
} // namespace

const QVariantMap &catalog() {
    static const QVariantMap value = [] {
        QFile file(QStringLiteral(":/nekotune/themes/catalog.json"));
        file.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
    }();
    return value;
}

Result<void> validate(const QVariantMap &theme, const QString &directory, bool strict) {
    const bool modern = theme.contains("schemaVersion");
    const auto variants = theme.value("variants");
    if ((strict || modern) && (theme.value("schemaVersion").toInt() != 1 || !isObject(variants) ||
                               !isObject(variants.toMap().value("light")) || !isObject(variants.toMap().value("dark"))))
        return failure("Theme schemaVersion 1 requires light and dark variants");
    if (auto tokens = validateTokens(theme.value("tokens"), !modern); !tokens)
        return tokens;
    QStringList images;
    for (const auto &mode : modes) {
        const auto variant = variants.toMap().value(mode);
        if (!variant.isValid())
            continue;
        if (auto tokens = validateTokens(variant.toMap().value("tokens"), false); !tokens)
            return tokens;
        const auto background = variant.toMap().value("background");
        if (!background.isValid())
            continue;
        const auto bg = background.toMap();
        if (!isObject(background) || bg.value("source").typeId() != QMetaType::QString)
            return failure("Invalid theme background");
        for (const auto &key : {"opacity", "overlayOpacity", "blur"})
            if (bg.contains(key) && (!isNumber(bg.value(key)) || bg.value(key).toDouble() < 0 || bg.value(key).toDouble() > 1))
                return failure(QStringLiteral("Invalid background %1").arg(key));
        if (bg.contains("overlayColor") && !isColor(bg.value("overlayColor")))
            return failure("Invalid background overlayColor");
        if (!images.contains(bg.value("source").toString()))
            images.append(bg.value("source").toString());
    }
    if (theme.contains("ui")) {
        if (!modern || !isObject(theme.value("ui")))
            return failure("QML themes require schemaVersion 1");
        const auto ui = theme.value("ui").toMap();
        QVariantList sources;
        for (auto it = ui.cbegin(); it != ui.cend(); ++it)
            if (it.key() != "pages" && !uiSlots.contains(it.key()))
                return failure("Unknown theme UI slot: " + it.key());
            else if (it.key() != "pages")
                sources.append(it.value());
        if (ui.contains("pages")) {
            if (!isObject(ui.value("pages")))
                return failure("Invalid theme pages");
            const auto pages = ui.value("pages").toMap();
            for (auto it = pages.cbegin(); it != pages.cend(); ++it) {
                if (!pageIds.contains(it.key()))
                    return failure("Unknown theme page: " + it.key());
                sources.append(it.value());
            }
        }
        for (const auto &source : sources) {
            if (source.typeId() != QMetaType::QString || !source.toString().endsWith(".qml"))
                return failure("Theme UI source must be QML");
            if (auto file = themeFile(directory, source.toString()); !file)
                return file.error();
        }
    }
    qint64 pixels = 0;
    for (const auto &image : images) {
        const auto checked = checkImage(directory, image);
        if (!checked)
            return checked.error();
        pixels += checked.value();
    }
    if (pixels > pixelLimit)
        return failure("Theme image pixel limit exceeded");
    return {};
}

Result<PackageInfo> inspectPackage(const QString &directory, const QVariantMap &manifest) {
    const auto contributes = manifest.value("contributes").toMap();
    if (manifest.contains("main") || manifest.contains("library") || manifest.value("nativeModules").toBool() ||
        !manifest.value("dependencies").toMap().isEmpty() || contributes.value("themes").toList().isEmpty() ||
        contributes.size() != 1)
        return failure("Theme packages may only contribute themes, without a backend or native modules");
    static const QRegularExpression allowed("\\.(qml|js|mjs|json|png|jpe?g)$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression code("\\.(qml|js|mjs)$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression image("\\.(png|jpe?g)$", QRegularExpression::CaseInsensitiveOption);
    PackageInfo info;
    qint64 size = 0, pixels = 0;
    const QDir root(directory);
    QDirIterator items(directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                       QDirIterator::Subdirectories);
    while (items.hasNext()) {
        const QFileInfo item(items.next());
        const auto relative = root.relativeFilePath(item.filePath());
        if (item.isSymLink())
            return failure("Theme links are not allowed");
        if (item.isDir())
            continue;
        if (!item.isFile() || !(relative == "extension.json" || allowed.match(relative).hasMatch() || item.fileName() == "qmldir"))
            return failure("Unsupported theme file: " + relative);
        size += item.size();
        info.files.append(relative);
        if (size > unpackedLimit || info.files.size() > 2048)
            return failure("Theme package size limit exceeded");
        if (code.match(relative).hasMatch() || item.fileName() == "qmldir")
            info.hasCode = true;
        if (image.match(relative).hasMatch()) {
            const auto checked = checkImage(directory, relative);
            if (!checked)
                return checked.error();
            pixels += checked.value();
        }
        if (pixels > pixelLimit)
            return failure("Theme image pixel limit exceeded");
    }
    info.files.sort();
    return info;
}

Result<QVariantMap> exportTheme(const QVariantMap &theme, const QString &directory, const QVariantMap &manifest,
                                const QString &destination) {
    if (!destination.endsWith(".zip"))
        return failure("Theme export requires a ZIP path");
    const bool full = manifest.value("type").toString() == "theme";
    zip::Writer writer;
    if (full) {
        const auto info = inspectPackage(directory, manifest);
        if (!info)
            return info.error();
        for (const auto &relative : info.value().files) {
            const auto file = themeFile(directory, relative);
            if (!file)
                return file.error();
            if (auto added = writer.addFile(relative, file.value()); !added)
                return added.error();
        }
    } else {
        QHash<QString, QString> assets;
        qint64 resources = 0;
        QVariantMap variants;
        for (const auto &mode : modes) {
            QVariantMap variant{{"tokens", resolveThemeTokens(catalog(), theme, mode == QLatin1String("dark"))}};
            auto background = theme.value("variants").toMap().value(mode).toMap().value("background").toMap();
            if (!background.isEmpty() && !directory.isEmpty()) {
                const auto source = background.value("source").toString();
                const auto file = themeFile(directory, source);
                if (!file)
                    return file.error();
                auto exported = assets.value(file.value());
                if (exported.isEmpty()) {
                    exported = QStringLiteral("assets/%1.%2").arg(mode, QFileInfo(source).suffix());
                    resources += QFileInfo(file.value()).size();
                    if (resources > unpackedLimit)
                        return failure("Theme package size limit exceeded");
                    assets.insert(file.value(), exported);
                    if (auto added = writer.addFile(exported, file.value()); !added)
                        return added.error();
                }
                background.insert("source", exported);
                variant.insert("background", background);
            }
            variants.insert(mode, variant);
        }
        QVariantMap exported{{"id", "theme"}, {"title", theme.value("title")}, {"schemaVersion", 1}, {"variants", variants}};
        const auto author = theme.value("author", manifest.value("author"));
        if (author.isValid())
            exported.insert("author", author);
        const auto title = theme.value("title");
        const QString name = title.typeId() == QMetaType::QString ? title.toString()
                                                                 : title.toMap().value("en", "Exported theme").toString();
        const QVariantMap package{{"id", "theme.export-" + QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                  {"name", name},
                                  {"version", "1.0.0"},
                                  // Data-only theme packages stay apiVersion 1 so older hosts can import them.
                                  {"apiVersion", 1},
                                  {"type", "theme"},
                                  {"contributes", QVariantMap{{"themes", QVariantList{exported}}}}};
        const auto bytes = QJsonDocument::fromVariant(package).toJson();
        if (resources + bytes.size() > unpackedLimit)
            return failure("Theme package size limit exceeded");
        writer.add("extension.json", bytes);
    }
    if (writer.size() > archiveLimit)
        return failure("Theme archive exceeds 20 MiB");
    if (auto saved = writer.save(destination); !saved)
        return saved.error();
    return QVariantMap{{"path", destination}, {"includesQml", full && theme.contains("ui")}};
}
} // namespace nekotune::themes
