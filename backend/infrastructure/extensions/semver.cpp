#include "infrastructure/extensions/semver.h"
#include <QRegularExpression>
#include <QStringList>

namespace nekotune::semver {
namespace {
const QRegularExpression &versionPattern() {
    static const QRegularExpression pattern(
        R"(^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-((?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$)");
    return pattern;
}
struct Comparator {
    QString op; // "", "<", "<=", ">", ">="
    Version version;
};
using Set = QList<Comparator>;

/// A partial version such as "1", "1.2", "1.x" or "*". Missing parts are wildcards.
struct Partial {
    int parts = 0;
    quint64 major = 0, minor = 0, patch = 0;
    QStringList prerelease;
};
std::optional<Partial> partial(QString text) {
    if (text.startsWith('v') || text.startsWith('='))
        text.remove(0, 1);
    if (text.isEmpty() || text == "*" || text.compare("x", Qt::CaseInsensitive) == 0)
        return Partial{};
    if (const auto full = parse(text))
        return Partial{3, full->major, full->minor, full->patch, full->prerelease};
    Partial result;
    const auto pieces = text.split('.');
    if (pieces.size() > 3)
        return std::nullopt;
    for (const auto &piece : pieces) {
        if (piece == "*" || piece.compare("x", Qt::CaseInsensitive) == 0)
            break;
        bool ok = false;
        const auto value = piece.toULongLong(&ok);
        if (!ok || (piece.size() > 1 && piece.startsWith('0')))
            return std::nullopt;
        (result.parts == 0 ? result.major : result.parts == 1 ? result.minor : result.patch) = value;
        ++result.parts;
    }
    return result;
}
Version lower(const Partial &p) { return {p.major, p.parts > 1 ? p.minor : 0, p.parts > 2 ? p.patch : 0, p.prerelease}; }
/// Exclusive upper bound of a wildcard partial; nullopt when unbounded.
std::optional<Version> upperWildcard(const Partial &p) {
    if (p.parts == 0)
        return std::nullopt;
    if (p.parts == 1)
        return Version{p.major + 1, 0, 0, {"0"}};
    return Version{p.major, p.minor + 1, 0, {"0"}};
}
bool expand(const QString &token, Set &set) {
    static const QRegularExpression op(R"(^(<=|>=|<|>|=|\^|~>?)?\s*(.*)$)");
    const auto match = op.match(token);
    const auto prefix = match.captured(1);
    const auto parsed = partial(match.captured(2));
    if (!parsed)
        return false;
    const auto &p = *parsed;
    const auto from = lower(p);
    if (prefix == "^") {
        Version until = p.parts == 0 ? Version{} : p.major > 0 || p.parts == 1 ? Version{p.major + 1, 0, 0, {"0"}}
                                         : p.minor > 0 || p.parts == 2   ? Version{0, p.minor + 1, 0, {"0"}}
                                                                         : Version{0, 0, p.patch + 1, {"0"}};
        set.append({">=", from});
        if (p.parts > 0)
            set.append({"<", until});
        return true;
    }
    if (prefix.startsWith('~')) {
        set.append({">=", from});
        if (p.parts > 0)
            set.append({"<", p.parts == 1 ? Version{p.major + 1, 0, 0, {"0"}} : Version{p.major, p.minor + 1, 0, {"0"}}});
        return true;
    }
    if (prefix.isEmpty() || prefix == "=") {
        if (p.parts == 3) {
            set.append({"", from});
            return true;
        }
        if (p.parts > 0) {
            set.append({">=", from});
            set.append({"<", *upperWildcard(p)});
        }
        return true;
    }
    if (p.parts == 3 || p.parts == 0) {
        if (p.parts == 0) {
            if (prefix == "<" || prefix == ">")
                set.append({"<", Version{0, 0, 0, {"0"}}}); // matches nothing
            return true;
        }
        set.append({prefix, from});
        return true;
    }
    // Partial comparators: >1.2 means >=1.3.0, <=1.2 means <1.3.0.
    if (prefix == ">")
        set.append({">=", *upperWildcard(p)});
    else if (prefix == ">=")
        set.append({">=", from});
    else if (prefix == "<")
        set.append({"<", Version{from.major, from.minor, from.patch, {"0"}}});
    else
        set.append({"<", *upperWildcard(p)});
    return true;
}
std::optional<QList<Set>> parseRange(const QString &range) {
    QList<Set> result;
    for (auto alternative : range.split("||")) {
        alternative = alternative.trimmed();
        Set set;
        static const QRegularExpression hyphen(R"(^(\S+)\s+-\s+(\S+)$)");
        if (const auto match = hyphen.match(alternative); match.hasMatch()) {
            const auto from = partial(match.captured(1)), to = partial(match.captured(2));
            if (!from || !to)
                return std::nullopt;
            if (from->parts > 0)
                set.append({">=", lower(*from)});
            if (to->parts == 3)
                set.append({"<=", lower(*to)});
            else if (to->parts > 0)
                set.append({"<", *upperWildcard(*to)});
        } else {
            // Allow "> 1.2" by joining operators with the version that follows.
            static const QRegularExpression spacing(R"((<=|>=|<|>|=|\^|~>?)\s+)");
            const auto tokens = QString(alternative).replace(spacing, "\\1").split(' ', Qt::SkipEmptyParts);
            for (const auto &token : tokens)
                if (!expand(token, set))
                    return std::nullopt;
        }
        result.append(set);
    }
    return result;
}
bool test(const Comparator &c, const Version &v) {
    const int order = compare(v, c.version);
    if (c.op.isEmpty())
        return order == 0;
    if (c.op == "<")
        return order < 0;
    if (c.op == "<=")
        return order <= 0;
    if (c.op == ">")
        return order > 0;
    return order >= 0;
}
} // namespace

std::optional<Version> parse(const QString &text) {
    const auto match = versionPattern().match(text);
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = true;
    Version result{match.captured(1).toULongLong(&ok), match.captured(2).toULongLong(&ok),
                   match.captured(3).toULongLong(&ok)};
    if (!ok)
        return std::nullopt;
    if (match.hasCaptured(4))
        result.prerelease = match.captured(4).split('.');
    return result;
}
int compare(const Version &left, const Version &right) {
    for (const auto [a, b] : {std::pair{left.major, right.major}, {left.minor, right.minor}, {left.patch, right.patch}})
        if (a != b)
            return a < b ? -1 : 1;
    if (left.prerelease.isEmpty() || right.prerelease.isEmpty())
        return left.prerelease.isEmpty() == right.prerelease.isEmpty() ? 0 : left.prerelease.isEmpty() ? 1 : -1;
    for (qsizetype i = 0; i < qMin(left.prerelease.size(), right.prerelease.size()); ++i) {
        bool leftNumeric = false, rightNumeric = false;
        const auto a = left.prerelease[i].toULongLong(&leftNumeric), b = right.prerelease[i].toULongLong(&rightNumeric);
        if (leftNumeric && rightNumeric) {
            if (a != b)
                return a < b ? -1 : 1;
        } else if (leftNumeric != rightNumeric)
            return leftNumeric ? -1 : 1;
        else if (const int order = left.prerelease[i].compare(right.prerelease[i]); order != 0)
            return order < 0 ? -1 : 1;
    }
    return left.prerelease.size() == right.prerelease.size() ? 0 : left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}
bool validRange(const QString &range) { return parseRange(range).has_value(); }
bool satisfies(const QString &text, const QString &range) {
    const auto version = parse(text);
    const auto sets = parseRange(range);
    if (!version || !sets)
        return false;
    for (const auto &set : *sets) {
        bool all = true, prereleaseAllowed = version->prerelease.isEmpty();
        for (const auto &comparator : set) {
            all = all && test(comparator, *version);
            // Our synthetic "-0" upper bounds do not name a prerelease the user asked for.
            const auto &bound = comparator.version;
            if (!bound.prerelease.isEmpty() && bound.prerelease != QStringList{"0"} && bound.major == version->major &&
                bound.minor == version->minor && bound.patch == version->patch)
                prereleaseAllowed = true;
        }
        if (all && prereleaseAllowed)
            return true;
    }
    return false;
}
} // namespace nekotune::semver
