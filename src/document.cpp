// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "document.h"

#include <mupdf/fitz.h>

#include <QFile>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

// Structured text (characters + positions) per page, built on first use.
struct Document::TextCache {
    QHash<int, fz_stext_page*> pages;
};

namespace {
fz_point toFz(QPointF p) { return fz_make_point(float(p.x()), float(p.y())); }
}

Document::Document(const QString& path) : m_text(std::make_unique<TextCache>())
{
    m_ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!m_ctx)
        throw std::runtime_error("could not create MuPDF context");

    const QByteArray file = QFile::encodeName(path);
    std::string error;

    fz_try(m_ctx) {
        fz_register_document_handlers(m_ctx);
        m_doc = fz_open_document(m_ctx, file.constData());
        const int n = fz_count_pages(m_ctx, m_doc);
        m_pageBounds.reserve(n);
        for (int i = 0; i < n; ++i) {
            fz_page* page = fz_load_page(m_ctx, m_doc, i);
            const fz_rect r = fz_bound_page(m_ctx, page);
            fz_drop_page(m_ctx, page);
            m_pageBounds.emplace_back(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1));
            m_maxPageWidth = std::max(m_maxPageWidth, double(r.x1 - r.x0));
        }
    }
    fz_catch(m_ctx) {
        error = fz_convert_error(m_ctx, nullptr);
    }

    if (!error.empty() || m_pageBounds.empty()) {
        fz_drop_document(m_ctx, m_doc);
        fz_drop_context(m_ctx);
        throw std::runtime_error(error.empty() ? "document has no pages" : error);
    }
}

Document::~Document()
{
    for (fz_stext_page* text : m_text->pages)
        fz_drop_stext_page(m_ctx, text);
    fz_drop_document(m_ctx, m_doc);
    fz_drop_context(m_ctx);
}

QImage Document::render(int page, double scale) const
{
    QImage image;
    fz_pixmap* pix = nullptr;
    fz_var(pix);

    fz_try(m_ctx) {
        pix = fz_new_pixmap_from_page_number(m_ctx, m_doc, page, fz_scale(scale, scale),
                                             fz_device_rgb(m_ctx), 0);
        // Deep copy: the pixmap's memory is freed below.
        image = QImage(fz_pixmap_samples(m_ctx, pix), fz_pixmap_width(m_ctx, pix),
                       fz_pixmap_height(m_ctx, pix), fz_pixmap_stride(m_ctx, pix),
                       QImage::Format_RGB888).copy();
    }
    fz_always(m_ctx) {
        fz_drop_pixmap(m_ctx, pix);
    }
    fz_catch(m_ctx) {
        std::fprintf(stderr, "folio: failed to render page %d: %s\n", page + 1,
                     fz_convert_error(m_ctx, nullptr));
        return QImage();
    }
    return image;
}

// Returns nullptr if the page's text can't be extracted.
static fz_stext_page* loadTextPage(fz_context* ctx, fz_document* doc, int page)
{
    fz_stext_page* text = nullptr;
    fz_var(text);
    fz_stext_options options{};
    options.flags = FZ_STEXT_DEHYPHENATE; // "elec-\ntron" copies as "electron"
    fz_try(ctx) {
        text = fz_new_stext_page_from_page_number(ctx, doc, page, &options);
    }
    fz_catch(ctx) {
        std::fprintf(stderr, "folio: no text on page %d: %s\n", page + 1, fz_convert_error(ctx, nullptr));
    }
    return text;
}

// Cached structured text for a page; nullptr if it can't be extracted.
static fz_stext_page* cachedTextPage(QHash<int, fz_stext_page*>& cache, fz_context* ctx, fz_document* doc, int page)
{
    fz_stext_page*& text = cache[page];
    if (!text)
        text = loadTextPage(ctx, doc, page);
    return text;
}

QList<QPolygonF> Document::selectionQuads(int page, QPointF a, QPointF b, SelectMode mode) const
{
    fz_stext_page* text = cachedTextPage(m_text->pages, m_ctx, m_doc, page);
    if (!text)
        return {};

    fz_point pa = toFz(a), pb = toFz(b);
    if (mode == SelectMode::Words)
        fz_snap_selection(m_ctx, text, &pa, &pb, FZ_SELECT_WORDS);

    fz_quad quads[1024];
    const int n = fz_highlight_selection(m_ctx, text, pa, pb, quads, 1024);
    QList<QPolygonF> result;
    for (int i = 0; i < n; ++i) {
        const fz_quad& q = quads[i];
        result.append(QPolygonF({QPointF(q.ul.x, q.ul.y), QPointF(q.ur.x, q.ur.y),
                                 QPointF(q.lr.x, q.lr.y), QPointF(q.ll.x, q.ll.y)}));
    }
    return result;
}

QString Document::selectionText(int page, QPointF a, QPointF b, SelectMode mode) const
{
    fz_stext_page* text = cachedTextPage(m_text->pages, m_ctx, m_doc, page);
    if (!text)
        return {};

    fz_point pa = toFz(a), pb = toFz(b);
    if (mode == SelectMode::Words)
        fz_snap_selection(m_ctx, text, &pa, &pb, FZ_SELECT_WORDS);

    QString result;
    char* utf8 = nullptr;
    fz_try(m_ctx) {
        utf8 = fz_copy_selection(m_ctx, text, pa, pb, 0);
        result = QString::fromUtf8(utf8);
    }
    fz_always(m_ctx) {
        fz_free(m_ctx, utf8);
    }
    fz_catch(m_ctx) {
        std::fprintf(stderr, "folio: copy failed: %s\n", fz_convert_error(m_ctx, nullptr));
    }
    return result;
}

static QList<TextLine> linesOf(fz_stext_page* text)
{
    QList<TextLine> lines;
    auto toQt = [](fz_rect r) { return QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)); };
    for (fz_stext_block* block = text->first_block; block; block = block->next) {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;
        for (fz_stext_line* l = block->u.t.first_line; l; l = l->next) {
            TextLine line;
            line.bbox = toQt(l->bbox);
            for (fz_stext_char* ch = l->first_char; ch; ch = ch->next) {
                // One QChar per character keeps text[i] aligned with chars[i].
                line.text.append(ch->c >= 0 && ch->c <= 0xFFFF ? QChar(char16_t(ch->c)) : QChar(0xFFFD));
                line.chars.append(toQt(fz_rect_from_quad(ch->quad)));
            }
            lines.append(line);
        }
    }
    return lines;
}

QList<TextLine> Document::textLines(int page) const
{
    fz_stext_page* text = cachedTextPage(m_text->pages, m_ctx, m_doc, page);
    return text ? linesOf(text) : QList<TextLine>();
}

QList<QList<QRectF>> Document::search(int page, const QString& query, Qt::CaseSensitivity cs) const
{
    // Use the cached text if there is one; otherwise extract it just for this
    // search, so searching a long book doesn't keep every page's text in memory.
    fz_stext_page* cached = m_text->pages.value(page);
    fz_stext_page* text = cached ? cached : loadTextPage(m_ctx, m_doc, page);
    if (!text || query.isEmpty())
        return {};
    const QList<TextLine> lines = linesOf(text);
    if (!cached)
        fz_drop_stext_page(m_ctx, text);

    QString all;
    QList<std::pair<int, int>> where; // per character of `all`: (line, char), line -1 for joins
    for (int l = 0; l < lines.size(); ++l) {
        for (int c = 0; c < lines[l].text.size(); ++c)
            where.append({l, c});
        all += lines[l].text + ' ';
        where.append({-1, -1});
    }

    QList<QList<QRectF>> hits;
    for (int at = all.indexOf(query, 0, cs); at >= 0; at = all.indexOf(query, at + query.size(), cs)) {
        QList<QRectF> rects; // one per line the match covers
        int currentLine = -1;
        for (int k = at; k < at + query.size(); ++k) {
            const auto [l, c] = where[k];
            if (l < 0)
                continue;
            if (l != currentLine) {
                rects.append(lines[l].chars[c]);
                currentLine = l;
            } else {
                rects.last() |= lines[l].chars[c];
            }
        }
        if (!rects.isEmpty())
            hits.append(rects);
    }
    return hits;
}

std::optional<LinkTarget> Document::linkAt(int page, QPointF point) const
{
    std::optional<LinkTarget> target;
    const fz_point pt = toFz(point);
    fz_page* p = nullptr;
    fz_link* links = nullptr;
    fz_var(p);
    fz_var(links);

    fz_try(m_ctx) {
        p = fz_load_page(m_ctx, m_doc, page);
        links = fz_load_links(m_ctx, p);
        for (fz_link* link = links; link; link = link->next) {
            if (!fz_is_point_inside_rect(pt, link->rect) || fz_is_external_link(m_ctx, link->uri))
                continue;
            const fz_link_dest dest = fz_resolve_link_dest(m_ctx, m_doc, link->uri);
            const int targetPage = fz_page_number_from_location(m_ctx, m_doc, dest.loc);
            if (targetPage < 0 || targetPage >= pageCount())
                continue;
            // Only XYZ destinations carry a position, and either coordinate may be unset (NaN).
            const bool xyz = dest.type == FZ_LINK_DEST_XYZ;
            target = LinkTarget{targetPage, xyz ? QPointF(dest.x, dest.y) : QPointF(NAN, NAN)};
            break;
        }
    }
    fz_always(m_ctx) {
        fz_drop_link(m_ctx, links);
        fz_drop_page(m_ctx, p);
    }
    fz_catch(m_ctx) {
        std::fprintf(stderr, "folio: could not read links on page %d: %s\n", page + 1,
                     fz_convert_error(m_ctx, nullptr));
    }
    return target;
}
