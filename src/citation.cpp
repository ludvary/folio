// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "citation.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {

struct Citation {
    QString key = {};     // "12" or "Smi19" for numbered / labelled styles
    bool bracketed = false;
    QString surname = {}; // author-year style
    QString year = {};
};

const QRegularExpression kYear(R"((?<!\d)(1[89]\d\d|20\d\d)[a-z]?(?!\d))");
const QRegularExpression kHeading(
    R"(^\s*(\d+\.?|[IVX]+\.)?\s*(references|bibliography|literature cited|works cited)\s*$)",
    QRegularExpression::CaseInsensitiveOption);

bool isWordChar(QChar c) { return c.isLetter() || c == '-' || c == '\''; }

// The run of characters around `i` for which `pred` holds, as [start, end).
template <typename Pred>
std::pair<int, int> runAround(const QString& s, int i, Pred pred)
{
    int a = i, b = i;
    while (a > 0 && pred(s[a - 1]))
        --a;
    while (b < s.size() && pred(s[b]))
        ++b;
    return {a, b};
}

// Line and character index under `point`: the nearest character in 2D among
// lines whose box contains it. With dehyphenation one "line" can span two rows,
// so its box may overlap the row actually clicked.
std::optional<std::pair<int, int>> hitTest(const QList<TextLine>& lines, QPointF point)
{
    std::optional<std::pair<int, int>> best;
    double bestDist = INFINITY;
    for (int l = 0; l < lines.size(); ++l) {
        if (!lines[l].bbox.adjusted(-1, -2, 1, 2).contains(point))
            continue;
        for (int i = 0; i < lines[l].chars.size(); ++i) {
            const QRectF& r = lines[l].chars[i];
            const double dx = std::max({r.left() - point.x(), 0.0, point.x() - r.right()});
            const double dy = std::max({r.top() - point.y(), 0.0, point.y() - r.bottom()});
            if (dx * dx + dy * dy < bestDist)
                bestDist = dx * dx + dy * dy, best = std::pair{l, i};
        }
    }
    return best;
}

std::optional<Citation> citationAt(const QList<TextLine>& lines, QPointF point)
{
    const auto hit = hitTest(lines, point);
    if (!hit)
        return std::nullopt;
    const QString& text = lines[hit->first].text;
    const int i = hit->second;

    // [ ... ] around the cursor, on this line.
    int open = -1, close = -1;
    for (int k = i; k >= 0 && k > i - 40; --k) {
        if (text[k] == '[') { open = k; break; }
        if (text[k] == ']' && k != i) break;
    }
    for (int k = i; k < text.size() && k < i + 40; ++k) {
        if (text[k] == ']') { close = k; break; }
        if (text[k] == '[' && k != i) break;
    }
    if (open >= 0 && close > open) {
        // The comma/semicolon-separated part under the cursor; for "3–5" the number under it.
        int at = std::clamp(i, open + 1, close - 1);
        if (text[at].isDigit() || (at > open + 1 && text[at - 1].isDigit() && !text[at].isLetter())) {
            if (!text[at].isDigit())
                --at;
            const auto [a, b] = runAround(text, at, [](QChar c) { return c.isDigit(); });
            return Citation{.key = text.mid(a, b - a), .bracketed = true};
        }
        const auto [a, b] = runAround(text, at, [&](QChar c) { return c != ',' && c != ';' && c != '[' && c != ']'; });
        const QString part = text.mid(a, b - a).trimmed();
        if (!part.isEmpty())
            return Citation{.key = part, .bracketed = true};
    }

    static const QStringList kSkip = {"et", "al", "and", "In", "See", "Ref", "Refs", "Fig", "Eq", "The"};

    if (text[i].isDigit()) {
        const auto [a, b] = runAround(text, i, [](QChar c) { return c.isDigit(); });
        const QString digits = text.mid(a, b - a);
        if (const auto year = kYear.match(text, a); !(year.hasMatch() && year.capturedStart() == a)) {
            return Citation{.key = digits}; // a bare number: "Ref. 12" or a superscript
        } else {
            // A year, as in "(Zee, 2010)": take the closest capitalised word before it.
            static const QRegularExpression name(R"(\b(\p{Lu}[\p{L}'-]+))");
            QString surname;
            for (auto it = name.globalMatch(text.mid(std::max(0, a - 60), std::min(a, 60))); it.hasNext();) {
                const QString w = it.next().captured(1);
                if (!kSkip.contains(w))
                    surname = w;
            }
            if (surname.isEmpty())
                return std::nullopt;
            return Citation{.surname = surname, .year = year.captured(0)};
        }
    }

    // Author-year: the clicked word plus the year after it (which may wrap to the next line).
    QString context = text;
    if (hit->first + 1 < lines.size())
        context += ' ' + lines[hit->first + 1].text;
    const auto [wa, wb] = runAround(context, i, isWordChar);
    const QString word = context.mid(wa, wb - wa);
    if (!word.isEmpty() && word[0].isUpper() && !kSkip.contains(word)) {
        const auto m = kYear.match(context, wb);
        if (m.hasMatch() && m.capturedStart() - wb < 60)
            return Citation{.surname = word, .year = m.captured(0)};
    }
    return std::nullopt;
}

struct Region {
    int page;
    int line; // first line to search on `page`
};

// Start of the reference list: the last "References"/"Bibliography" heading.
std::optional<Region> findReferenceSection(const Document& doc)
{
    for (int p = doc.pageCount() - 1; p >= 0; --p) {
        const QList<TextLine> lines = doc.textLines(p);
        for (int l = lines.size() - 1; l >= 0; --l)
            if (kHeading.match(lines[l].text).hasMatch())
                return Region{p, l + 1};
    }
    return std::nullopt;
}

LinkTarget targetFor(int page, const TextLine& line) { return {page, line.bbox.topLeft()}; }

bool startsWithKey(const QString& lineText, const Citation& c)
{
    const QString t = lineText.trimmed();
    if (t.startsWith('[' + c.key + ']'))
        return true;
    if (c.bracketed || !t.startsWith(c.key) || t.size() == c.key.size())
        return false;
    const QChar next = t[c.key.size()];
    return next == '.' || next == ')' || next.isSpace(); // "12. Smith", "12) Smith", "12 Smith"
}

bool containsWord(const QString& text, const QString& word)
{
    const QRegularExpression re("(?<![\\p{L}])" + QRegularExpression::escape(word) + "(?![\\p{L}])",
                                QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

std::optional<LinkTarget> searchSection(const Document& doc, Region from, const Citation& c)
{
    for (int p = from.page; p < doc.pageCount(); ++p) {
        const QList<TextLine> lines = doc.textLines(p);
        for (int l = (p == from.page ? from.line : 0); l < lines.size(); ++l) {
            if (!c.key.isEmpty()) {
                if (startsWithKey(lines[l].text, c))
                    return targetFor(p, lines[l]);
                continue;
            }
            // Author-year: surname near the start of an entry, year within the next few lines.
            if (!containsWord(lines[l].text.left(60), c.surname))
                continue;
            QString entry;
            for (int k = l; k < std::min<qsizetype>(l + 4, lines.size()); ++k)
                entry += lines[k].text + ' ';
            if (entry.contains(c.year))
                return targetFor(p, lines[l]);
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<LinkTarget> findCitationTarget(const Document& doc, int page, QPointF point)
{
    const auto citation = citationAt(doc.textLines(page), point);
    if (!citation)
        return std::nullopt;

    if (const auto section = findReferenceSection(doc))
        return searchSection(doc, *section, *citation);

    // No heading found: "[12]"-style entries are distinctive enough to search for
    // anywhere; take the last one, since the reference list is usually at the end.
    if (!citation->bracketed)
        return std::nullopt;
    for (int p = doc.pageCount() - 1; p >= 0; --p) {
        const QList<TextLine> lines = doc.textLines(p);
        for (int l = lines.size() - 1; l >= 0; --l)
            if (startsWithKey(lines[l].text, *citation))
                return targetFor(p, lines[l]);
    }
    return std::nullopt;
}
