// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include "document.h"
#include "highlights.h"
#include "theme.h"

#include <QAbstractScrollArea>
#include <QHash>
#include <QImage>
#include <QTimer>
#include <memory>
#include <optional>
#include <vector>

// Continuous-scroll page canvas with vim-style navigation, text selection,
// highlights and an optional two-page layout.
class PageView : public QAbstractScrollArea {
    Q_OBJECT
public:
    PageView(std::shared_ptr<Document> doc, int startPage, QWidget* parent = nullptr);

    const Document& document() const { return *m_doc; }

    int currentPage() const; // page under the viewport's center
    void setTheme(const Theme& theme);
    void goTo(const LinkTarget& target);

    // Open at a fixed zoom with `target.point` near the top-left (used by link previews).
    void showTarget(const LinkTarget& target, double zoom);
    double zoom() const { return m_zoom; }

    int pageCount() const { return m_doc->pageCount(); }

    // Vim-style search: jumps to the first match after (or before) the top of
    // the view. False if there are no matches. n / N then move between them.
    bool search(const QString& query, bool forward);
    QString searchStatus() const; // "[3/17]", or empty without an active search

    bool dualPage() const { return m_dual; }
    bool firstPageOnRight() const { return m_firstOnRight; }
    void setDualPage(bool dual, bool firstOnRight);

signals:
    void statusChanged();             // page or search position changed
    void message(const QString& text); // e.g. "search hit BOTTOM, continuing at TOP"

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct PagePoint {
        int page;
        QPointF point; // page coordinates (points)
    };

    int columns() const { return m_dual ? 2 : 1; }
    void relayout();
    void setZoom(double zoom);
    double fitWidthZoom() const;
    void scrollVertical(int dy);
    void scrollHorizontal(int dx);
    void goToPage(int page);
    void scrollToTarget(const LinkTarget& target);
    int pageAt(int y) const;      // first page of the row at layout y
    int lastInRow(int page) const;
    QRectF pageRect(int page) const; // in viewport coordinates
    std::optional<PagePoint> pagePointAt(QPointF viewportPos) const;
    QPointF toViewport(int page, QPointF point) const;
    const QImage& pageImage(int page, qreal dpr);
    void drawQuads(QPainter& p, int page, const QList<QPolygonF>& quads) const;

    bool hasSelection() const { return m_selStartPage >= 0; }
    void updateSelection(QPointF viewportPos);
    void clearSelection();
    std::optional<std::pair<QPointF, QPointF>> selectionOn(int page) const;
    QList<QPolygonF> selectionQuadsOn(int page) const;
    QString selectedText() const;
    void copySelection();
    void highlightSelection(int color);
    void autoScroll();

    void searchNext(bool forward);
    void clearSearch();
    void scrollToHit();
    double layoutY(int page, double pageY) const; // page coordinate -> layout pixels
    int highlightAt(int page, QPointF point) const;
    void showLinkPreview(const LinkTarget& target, QPoint globalPos);

    std::shared_ptr<Document> m_doc;
    bool m_inPreview = false; // q / Esc close the preview instead of the window
    std::optional<LinkTarget> m_pendingTarget; // applied once the first layout exists
    double m_zoom = 1.0;      // logical pixels per point
    bool m_fitWidth = true;   // keep fitting width on resize until the user zooms
    bool m_dual = false;
    bool m_firstOnRight = false;
    std::vector<QRectF> m_pageRects; // layout coordinates, before scrolling
    std::vector<int> m_pageTop;      // = m_pageRects[i].top(), shared within a row
    int m_contentWidth = 0;
    int m_contentHeight = 0;
    QHash<int, QImage> m_cache; // rendered and recolored
    Theme m_theme;

    // selection, possibly spanning pages; start is where the drag began
    int m_selStartPage = -1, m_selEndPage = -1;
    QPointF m_selStart, m_selEnd;
    SelectMode m_selMode = SelectMode::Chars;
    mutable QHash<int, QList<QPolygonF>> m_selQuads; // per page, computed when drawn
    bool m_selecting = false;
    QPointF m_lastMousePos;
    QTimer m_autoScroll; // while dragging near the top/bottom edge

    // search
    QString m_searchQuery;
    bool m_searchForward = true;
    std::vector<QList<QList<QRectF>>> m_searchHits; // per page, per match: one rect per line
    int m_searchTotal = 0;
    int m_hitPage = -1, m_hitIndex = -1; // current match

    int m_selectedHighlight = -1; // clicked highlight: x deletes, a recolors, y copies
    int m_lastColor = 0;

    // vim input state
    int m_count = 0;
    bool m_pendingG = false;
};
