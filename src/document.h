// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

struct fz_context;
struct fz_document;

// Where an internal link points. `point` is NaN when the link only names a page.
struct LinkTarget {
    int page = 0;
    QPointF point;
};

enum class SelectMode { Chars, Words };

// One line of text as MuPDF groups it, with a box per character (text[i] <-> chars[i]).
struct TextLine {
    QString text;
    QRectF bbox;
    QList<QRectF> chars;
};

// Thin RAII wrapper around a MuPDF document. Not thread-safe (yet).
// All page coordinates are in points, in the page's own space (see pageBounds).
class Document {
public:
    explicit Document(const QString& path); // throws std::runtime_error
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    int pageCount() const { return int(m_pageBounds.size()); }
    QRectF pageBounds(int page) const { return m_pageBounds[page]; }
    QSizeF pageSize(int page) const { return m_pageBounds[page].size(); }
    double maxPageWidth() const { return m_maxPageWidth; }

    // Renders a page at `scale` device pixels per point. Null image on failure.
    QImage render(int page, double scale) const;

    // Text between two points on a page, in reading order.
    QList<QPolygonF> selectionQuads(int page, QPointF a, QPointF b, SelectMode mode) const;
    QString selectionText(int page, QPointF a, QPointF b, SelectMode mode) const;

    QList<TextLine> textLines(int page) const;

    // Occurrences of `query` on a page; each is one rect per line it covers.
    // Matches may span line breaks (lines are joined with a space).
    QList<QList<QRectF>> search(int page, const QString& query, Qt::CaseSensitivity cs) const;

    // Target of the internal link under `point`, if any.
    std::optional<LinkTarget> linkAt(int page, QPointF point) const;

private:
    struct TextCache;

    fz_context* m_ctx = nullptr;
    fz_document* m_doc = nullptr;
    std::vector<QRectF> m_pageBounds;
    double m_maxPageWidth = 0;
    std::unique_ptr<TextCache> m_text;
};
