// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "document.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

// Structured text (characters + positions) per page, built on first use.
struct Document::TextCache {
    QHash<int, fz_stext_page*> pages;
};

namespace {
fz_point toFz(QPointF p) { return fz_make_point(float(p.x()), float(p.y())); }

QColor annotColor(fz_context* ctx, pdf_annot* annot)
{
    int n = 0;
    float c[4] = {};
    pdf_annot_color(ctx, annot, &n, c);
    if (n == 1)
        return QColor::fromRgbF(c[0], c[0], c[0]);
    if (n == 3)
        return QColor::fromRgbF(c[0], c[1], c[2]);
    if (n == 4)
        return QColor::fromCmykF(c[0], c[1], c[2], c[3]);
    return QColor("#f5d547"); // no colour set: the usual highlighter yellow
}

// The highlight annotations of one page.
void readHighlights(fz_context* ctx, fz_page* page, int pageNumber, QList<Highlight>& out)
{
    pdf_page* pdfPage = pdf_page_from_fz_page(ctx, page);
    if (!pdfPage)
        return;
    for (pdf_annot* a = pdf_first_annot(ctx, pdfPage); a; a = pdf_next_annot(ctx, a)) {
        if (pdf_annot_type(ctx, a) != PDF_ANNOT_HIGHLIGHT)
            continue;
        Highlight h;
        h.page = pageNumber;
        h.color = annotColor(ctx, a);
        h.id = pdf_to_num(ctx, pdf_annot_obj(ctx, a));
        for (int i = 0; i < pdf_annot_quad_point_count(ctx, a); ++i) {
            const fz_quad q = pdf_annot_quad_point(ctx, a, i);
            h.quads.append(QPolygonF({{q.ul.x, q.ul.y}, {q.ur.x, q.ur.y}, {q.lr.x, q.lr.y}, {q.ll.x, q.ll.y}}));
        }
        if (!h.quads.isEmpty())
            out.append(h);
    }
}
} // namespace

Document::Document(const QString& path) : m_text(std::make_unique<TextCache>()), m_path(path)
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
            readHighlights(m_ctx, page, i, m_highlights);
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
    rememberFileState();
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
    fz_page* p = nullptr;
    fz_pixmap* pix = nullptr;
    fz_device* dev = nullptr;
    fz_var(p);
    fz_var(pix);
    fz_var(dev);

    fz_try(m_ctx) {
        p = fz_load_page(m_ctx, m_doc, page);
        const fz_matrix ctm = fz_scale(scale, scale);
        const fz_irect box = fz_round_rect(fz_transform_rect(fz_bound_page(m_ctx, p), ctm));
        pix = fz_new_pixmap_with_bbox(m_ctx, fz_device_rgb(m_ctx), box, nullptr, 0);
        fz_clear_pixmap_with_value(m_ctx, pix, 0xff);
        dev = fz_new_draw_device(m_ctx, fz_identity, pix);

        // Everything except highlight annotations, which the view draws itself.
        fz_run_page_contents(m_ctx, p, dev, ctm, nullptr);
        if (pdf_page* pdfPage = pdf_page_from_fz_page(m_ctx, p)) {
            for (pdf_annot* a = pdf_first_annot(m_ctx, pdfPage); a; a = pdf_next_annot(m_ctx, a))
                if (pdf_annot_type(m_ctx, a) != PDF_ANNOT_HIGHLIGHT)
                    pdf_run_annot(m_ctx, a, dev, ctm, nullptr);
        } else {
            fz_run_page_annots(m_ctx, p, dev, ctm, nullptr);
        }
        fz_run_page_widgets(m_ctx, p, dev, ctm, nullptr);
        fz_close_device(m_ctx, dev);

        // Deep copy: the pixmap's memory is freed below.
        image = QImage(fz_pixmap_samples(m_ctx, pix), fz_pixmap_width(m_ctx, pix),
                       fz_pixmap_height(m_ctx, pix), fz_pixmap_stride(m_ctx, pix),
                       QImage::Format_RGB888).copy();
    }
    fz_always(m_ctx) {
        fz_drop_device(m_ctx, dev);
        fz_drop_pixmap(m_ctx, pix);
        fz_drop_page(m_ctx, p);
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

// --- highlights (PDF annotations) ---------------------------------------------

bool Document::canAnnotate() const
{
    return pdf_specifics(m_ctx, m_doc) != nullptr;
}

QString Document::highlightText(const Highlight& h) const
{
    // Select from the middle of the first quad's left edge to the middle of the
    // last one's right edge; MuPDF's selection handles ligatures and line breaks.
    const QPolygonF& first = h.quads.first();
    const QPolygonF& last = h.quads.last();
    const QPointF a = (first[0] + first[3]) / 2; // ul, ll
    const QPointF b = (last[1] + last[2]) / 2;   // ur, lr
    return selectionText(h.page, a, b, SelectMode::Chars).simplified();
}

void Document::rememberFileState()
{
    const QFileInfo info(m_path);
    m_fileTime = info.lastModified();
    m_fileSize = info.size();
}

QString Document::checkUnchanged() const
{
    const QFileInfo info(m_path);
    if (info.lastModified() != m_fileTime || info.size() != m_fileSize)
        return QString("%1 changed on disk (synced from elsewhere?). Reopen it with o, then try again.")
            .arg(info.fileName());
    return {};
}

QString Document::save()
{
    pdf_document* pdf = pdf_specifics(m_ctx, m_doc);
    // Incremental saves append to the file and never touch the existing bytes.
    // Files that can't take one (e.g. repaired ones) get a full copy written
    // next to them and renamed over the original, so it's never half-written.
    const bool incremental = pdf_can_be_saved_incrementally(m_ctx, pdf);
    const QString target = incremental ? m_path : m_path + ".folio-tmp";
    const QByteArray file = QFile::encodeName(target);
    pdf_write_options options = pdf_default_write_options;
    options.do_incremental = incremental;

    std::string error;
    fz_try(m_ctx) {
        pdf_save_document(m_ctx, pdf, file.constData(), &options);
    }
    fz_catch(m_ctx) {
        error = fz_convert_error(m_ctx, nullptr);
    }
    if (!error.empty()) {
        if (!incremental)
            QFile::remove(target);
        return QString("Couldn't save highlights: %1").arg(QString::fromStdString(error));
    }
    if (!incremental && std::rename(file.constData(), QFile::encodeName(m_path).constData()) != 0) {
        QFile::remove(target);
        return QString("Couldn't save highlights: %1").arg(std::strerror(errno));
    }
    // The in-memory document still describes the file as it was when opened.
    // Saving again from it would write an update that points into the wrong
    // places (MuPDF's own viewer reloads after saving for the same reason).
    return reload();
}

QString Document::reload()
{
    const QByteArray file = QFile::encodeName(m_path);
    fz_document* fresh = nullptr;
    QList<Highlight> highlights;
    std::string error;
    fz_var(fresh);
    fz_try(m_ctx) {
        fresh = fz_open_document(m_ctx, file.constData());
        for (int i = 0; i < pageCount(); ++i) {
            fz_page* page = fz_load_page(m_ctx, fresh, i);
            fz_try(m_ctx) {
                readHighlights(m_ctx, page, i, highlights);
            }
            fz_always(m_ctx) {
                fz_drop_page(m_ctx, page);
            }
            fz_catch(m_ctx) {
                fz_rethrow(m_ctx);
            }
        }
    }
    fz_catch(m_ctx) {
        fz_drop_document(m_ctx, fresh);
        error = fz_convert_error(m_ctx, nullptr);
    }
    if (!error.empty())
        return QString("Saved, but couldn't reload %1: %2. Reopen it before highlighting again.")
            .arg(QFileInfo(m_path).fileName(), QString::fromStdString(error));
    fz_drop_document(m_ctx, m_doc);
    m_doc = fresh;
    m_highlights = highlights;
    rememberFileState();
    return {};
}

// Runs fn(pdf_page*, pdf_annot*) on the annotation behind `h`.
template <typename Fn>
QString Document::editAnnotation(const Highlight& h, Fn fn)
{
    std::string error;
    fz_page* page = nullptr;
    fz_var(page);
    fz_try(m_ctx) {
        page = fz_load_page(m_ctx, m_doc, h.page);
        pdf_page* pdfPage = pdf_page_from_fz_page(m_ctx, page);
        for (pdf_annot* a = pdfPage ? pdf_first_annot(m_ctx, pdfPage) : nullptr; a; a = pdf_next_annot(m_ctx, a)) {
            if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, a)) == h.id) {
                fn(pdfPage, a);
                break;
            }
        }
    }
    fz_always(m_ctx) {
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx) {
        error = fz_convert_error(m_ctx, nullptr);
    }
    return error.empty() ? QString() : QString::fromStdString(error);
}

QString Document::addHighlights(const QList<Highlight>& highlights)
{
    if (!canAnnotate())
        return "Highlights can only be saved in PDF files.";
    if (const QString changed = checkUnchanged(); !changed.isEmpty())
        return changed;

    for (Highlight h : highlights) {
        std::string error;
        fz_page* page = nullptr;
        pdf_annot* annot = nullptr;
        fz_var(page);
        fz_var(annot);
        fz_try(m_ctx) {
            page = fz_load_page(m_ctx, m_doc, h.page);
            annot = pdf_create_annot(m_ctx, pdf_page_from_fz_page(m_ctx, page), PDF_ANNOT_HIGHLIGHT);
            std::vector<fz_quad> quads;
            for (const QPolygonF& q : h.quads)
                quads.push_back({toFz(q[0]), toFz(q[1]), toFz(q[3]), toFz(q[2])}); // ul ur ll lr
            pdf_set_annot_quad_points(m_ctx, annot, int(quads.size()), quads.data());
            const float rgb[3] = {float(h.color.redF()), float(h.color.greenF()), float(h.color.blueF())};
            pdf_set_annot_color(m_ctx, annot, 3, rgb);
            pdf_update_annot(m_ctx, annot); // builds the appearance other viewers draw
            h.id = pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot));
        }
        fz_always(m_ctx) {
            pdf_drop_annot(m_ctx, annot);
            fz_drop_page(m_ctx, page);
        }
        fz_catch(m_ctx) {
            error = fz_convert_error(m_ctx, nullptr);
        }
        if (!error.empty())
            return QString("Couldn't add highlight: %1").arg(QString::fromStdString(error));
        m_highlights.append(h);
    }
    return save();
}

QString Document::removeHighlight(int index)
{
    if (index < 0 || index >= m_highlights.size())
        return {};
    if (const QString changed = checkUnchanged(); !changed.isEmpty())
        return changed;
    const QString error = editAnnotation(m_highlights[index], [this](pdf_page* page, pdf_annot* a) {
        pdf_delete_annot(m_ctx, page, a);
    });
    if (!error.isEmpty())
        return QString("Couldn't delete highlight: %1").arg(error);
    m_highlights.removeAt(index);
    return save();
}

QString Document::setHighlightColor(int index, const QColor& color)
{
    if (index < 0 || index >= m_highlights.size())
        return {};
    if (const QString changed = checkUnchanged(); !changed.isEmpty())
        return changed;
    const QString error = editAnnotation(m_highlights[index], [this, &color](pdf_page*, pdf_annot* a) {
        const float rgb[3] = {float(color.redF()), float(color.greenF()), float(color.blueF())};
        pdf_set_annot_color(m_ctx, a, 3, rgb);
        pdf_update_annot(m_ctx, a);
    });
    if (!error.isEmpty())
        return QString("Couldn't recolor highlight: %1").arg(error);
    m_highlights[index].color = color;
    return save();
}
