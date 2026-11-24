// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "pageview.h"

#include "citation.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kGap = 8;          // space around and between pages
constexpr int kScrollStep = 60;  // j / k
constexpr int kTargetMargin = 24; // space left of / above a link target
constexpr double kZoomStep = 1.15;
constexpr int kHighlightAlpha = 110;
constexpr int kAutoScrollZone = 40; // px from the top/bottom edge where dragging scrolls
} // namespace

PageView::PageView(std::shared_ptr<Document> doc, std::shared_ptr<Highlights> highlights, int startPage,
                   QWidget* parent)
    : QAbstractScrollArea(parent),
      m_doc(std::move(doc)),
      m_highlights(std::move(highlights)),
      m_pendingTarget(LinkTarget{startPage, {NAN, NAN}})
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setCursor(Qt::IBeamCursor);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &PageView::statusChanged);
    m_autoScroll.setInterval(16);
    connect(&m_autoScroll, &QTimer::timeout, this, &PageView::autoScroll);
}

void PageView::setTheme(const Theme& theme)
{
    m_theme = theme;
    m_cache.clear();
    viewport()->update();
}

void PageView::showTarget(const LinkTarget& target, double zoom)
{
    m_fitWidth = false;
    m_zoom = zoom;
    m_pendingTarget = target;
}

void PageView::goTo(const LinkTarget& target)
{
    if (m_pageTop.empty())
        m_pendingTarget = target;
    else
        scrollToTarget(target);
}

void PageView::setDualPage(bool dual, bool firstOnRight)
{
    if (dual == m_dual && firstOnRight == m_firstOnRight)
        return;
    const int page = currentPage();
    m_dual = dual;
    m_firstOnRight = firstOnRight;
    if (m_pageTop.empty())
        return; // applied at the first layout
    if (m_fitWidth) {
        m_zoom = std::clamp(fitWidthZoom(), 0.1, 10.0);
        m_cache.clear();
    }
    relayout();
    goToPage(page);
}

// --- layout ----------------------------------------------------------------

void PageView::relayout()
{
    const int n = m_doc->pageCount();
    const int cols = columns();
    const double columnWidth = m_doc->maxPageWidth() * m_zoom;
    const int offset = m_dual && m_firstOnRight ? 1 : 0; // a blank slot before page 1

    m_pageRects.resize(n);
    m_pageTop.resize(n);
    double y = kGap;
    for (int i = 0; i < n;) {
        // One row: the pages from i up to the end of its row.
        const int rowEnd = std::min(n, i + cols - (i + offset) % cols);
        double rowHeight = 0;
        for (int j = i; j < rowEnd; ++j) {
            const int col = (j + offset) % cols;
            const QSizeF size = m_doc->pageSize(j) * m_zoom;
            // Single: centered. Double: the two pages meet in the middle, like a book.
            const double inColumn = cols == 1 ? (columnWidth - size.width()) / 2
                                  : col == 0  ? columnWidth - size.width()
                                              : 0;
            const double x = kGap + col * (columnWidth + kGap) + std::floor(inColumn);
            m_pageRects[j] = QRectF(x, y, size.width(), size.height());
            m_pageTop[j] = int(y);
            rowHeight = std::max(rowHeight, size.height());
        }
        y += std::ceil(rowHeight) + kGap;
        i = rowEnd;
    }
    m_contentHeight = int(y);
    m_contentWidth = int(std::ceil(cols * columnWidth + (cols - 1) * kGap)) + 2 * kGap;

    const QSize vp = viewport()->size();
    verticalScrollBar()->setRange(0, std::max(0, m_contentHeight - vp.height()));
    verticalScrollBar()->setPageStep(vp.height());
    horizontalScrollBar()->setRange(0, std::max(0, m_contentWidth - vp.width()));
    horizontalScrollBar()->setPageStep(vp.width());
    viewport()->update();
}

double PageView::fitWidthZoom() const
{
    const int cols = columns();
    return (viewport()->width() - (cols + 1.0) * kGap) / (cols * m_doc->maxPageWidth());
}

void PageView::setZoom(double zoom)
{
    zoom = std::clamp(zoom, 0.1, 10.0);
    if (m_pageTop.empty()) { // first layout
        m_zoom = zoom;
        relayout();
        return;
    }

    // Keep the point at the vertical center of the viewport fixed while zooming.
    const int centerY = verticalScrollBar()->value() + viewport()->height() / 2;
    const int page = pageAt(centerY);
    const double offsetPts = (centerY - m_pageTop[page]) / m_zoom;

    if (zoom != m_zoom)
        m_cache.clear();
    m_zoom = zoom;
    relayout();
    verticalScrollBar()->setValue(int(m_pageTop[page] + offsetPts * m_zoom) - viewport()->height() / 2);
}

int PageView::currentPage() const
{
    if (m_pageTop.empty())
        return m_pendingTarget ? m_pendingTarget->page : 0;
    return pageAt(verticalScrollBar()->value() + viewport()->height() / 2);
}

int PageView::pageAt(int y) const
{
    auto it = std::upper_bound(m_pageTop.begin(), m_pageTop.end(), y);
    int page = std::max(0, int(it - m_pageTop.begin()) - 1);
    while (page > 0 && m_pageTop[page - 1] == m_pageTop[page])
        --page; // first page of the row
    return page;
}

int PageView::lastInRow(int page) const
{
    while (page + 1 < int(m_pageTop.size()) && m_pageTop[page + 1] == m_pageTop[page])
        ++page;
    return page;
}

QRectF PageView::pageRect(int page) const
{
    const int contentX = m_contentWidth < viewport()->width()
                             ? (viewport()->width() - m_contentWidth) / 2
                             : -horizontalScrollBar()->value();
    return m_pageRects[page].translated(contentX, -verticalScrollBar()->value());
}

std::optional<PageView::PagePoint> PageView::pagePointAt(QPointF pos) const
{
    if (m_pageTop.empty())
        return std::nullopt;
    // The page in this row under (or nearest to) the cursor horizontally.
    const int first = pageAt(int(pos.y()) + verticalScrollBar()->value());
    int page = first;
    for (int i = first; i <= lastInRow(first); ++i)
        if (pos.x() >= pageRect(i).left())
            page = i;
    const QRectF rect = pageRect(page);
    const QPointF origin = m_doc->pageBounds(page).topLeft();
    return PagePoint{page, origin + (pos - rect.topLeft()) / m_zoom};
}

QPointF PageView::toViewport(int page, QPointF point) const
{
    return pageRect(page).topLeft() + (point - m_doc->pageBounds(page).topLeft()) * m_zoom;
}

void PageView::scrollVertical(int dy)
{
    verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
}

void PageView::scrollHorizontal(int dx)
{
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
}

void PageView::goToPage(int page)
{
    page = std::clamp(page, 0, m_doc->pageCount() - 1);
    verticalScrollBar()->setValue(m_pageTop[page] - kGap);
}

void PageView::scrollToTarget(const LinkTarget& target)
{
    goToPage(target.page);
    const QPointF origin = m_doc->pageBounds(target.page).topLeft();
    if (!std::isnan(target.point.y()))
        scrollVertical(int((target.point.y() - origin.y()) * m_zoom) - kTargetMargin + kGap);
    if (!std::isnan(target.point.x()))
        horizontalScrollBar()->setValue(
            int(m_pageRects[target.page].x() + (target.point.x() - origin.x()) * m_zoom) - kTargetMargin);
}

// --- painting --------------------------------------------------------------

const QImage& PageView::pageImage(int page, qreal dpr)
{
    QImage& img = m_cache[page];
    if (img.isNull() || img.devicePixelRatio() != dpr) {
        img = m_doc->render(page, m_zoom * dpr);
        recolor(img, m_theme);
        img.setDevicePixelRatio(dpr);
    }
    return img;
}

void PageView::drawQuads(QPainter& p, int page, const QList<QPolygonF>& quads) const
{
    for (const QPolygonF& quad : quads) {
        QPolygonF mapped;
        for (const QPointF& pt : quad)
            mapped << toViewport(page, pt);
        p.drawPolygon(mapped);
    }
}

void PageView::paintEvent(QPaintEvent*)
{
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), m_theme.canvas);

    const int top = verticalScrollBar()->value();
    const int bottom = top + viewport()->height();
    const qreal dpr = devicePixelRatioF();

    const int first = pageAt(top);
    int last = first;
    for (int i = first; i < m_doc->pageCount() && m_pageTop[i] < bottom; ++i) {
        last = i;
        const QRectF rect = pageRect(i);
        const QImage& img = pageImage(i, dpr);
        if (img.isNull())
            p.fillRect(rect, m_theme.paper);
        else
            p.drawImage(rect.topLeft(), img);
    }

    const QList<Highlight>& highlights = m_highlights->all();
    for (int h = 0; h < highlights.size(); ++h) {
        if (highlights[h].page < first || highlights[h].page > last)
            continue;
        QColor fill = kHighlightColors[highlights[h].color];
        fill.setAlpha(kHighlightAlpha);
        p.setBrush(fill);
        p.setPen(h == m_selectedHighlight ? QPen(kHighlightColors[highlights[h].color], 2) : QPen(Qt::NoPen));
        drawQuads(p, highlights[h].page, highlights[h].quads);
    }

    if (!m_searchQuery.isEmpty()) {
        QColor other = m_theme.accent, current = m_theme.accent;
        other.setAlpha(70);
        current.setAlpha(170);
        p.setPen(Qt::NoPen);
        for (int page = first; page <= last; ++page) {
            const QList<QList<QRectF>>& hits = m_searchHits[page];
            for (int h = 0; h < hits.size(); ++h) {
                p.setBrush(page == m_hitPage && h == m_hitIndex ? current : other);
                for (const QRectF& r : hits[h])
                    p.drawRect(QRectF(toViewport(page, r.topLeft()), toViewport(page, r.bottomRight())));
            }
        }
    }

    if (hasSelection()) {
        QColor fill = m_theme.accent;
        fill.setAlpha(90);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        for (int page = std::max(first, std::min(m_selStartPage, m_selEndPage));
             page <= std::min(last, std::max(m_selStartPage, m_selEndPage)); ++page) {
            if (!m_selQuads.contains(page))
                m_selQuads[page] = selectionQuadsOn(page);
            drawQuads(p, page, m_selQuads[page]);
        }
    }

    // Drop renders that are well out of view.
    const int keep = 2 * columns();
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it.key() < first - keep || it.key() > last + keep)
            it = m_cache.erase(it);
        else
            ++it;
    }
}

void PageView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    const bool firstLayout = m_pageTop.empty();
    if (m_fitWidth)
        setZoom(fitWidthZoom());
    else
        relayout();
    if (firstLayout && m_pendingTarget) {
        scrollToTarget(*m_pendingTarget);
        m_pendingTarget.reset();
    }
}

void PageView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        m_fitWidth = false;
        setZoom(m_zoom * std::pow(kZoomStep, event->angleDelta().y() / 120.0));
        event->accept();
        return;
    }
    // Trackpads report exact pixels; mouse wheels report 120 per notch.
    QPoint delta = event->pixelDelta();
    if (delta.isNull())
        delta = event->angleDelta() * kScrollStep / 120;
    scrollHorizontal(-delta.x());
    scrollVertical(-delta.y());
    if (m_selecting)
        updateSelection(m_lastMousePos);
    event->accept();
}

// --- selection and highlights ------------------------------------------------

void PageView::clearSelection()
{
    m_selStartPage = m_selEndPage = -1;
    m_selQuads.clear();
    viewport()->update();
}

void PageView::updateSelection(QPointF pos)
{
    if (!hasSelection())
        return;
    if (const auto hit = pagePointAt(pos)) {
        m_selEndPage = hit->page;
        m_selEnd = hit->point;
        m_selQuads.clear();
        viewport()->update();
    }
}

// The part of the selection on `page`, as two points: from the start point to
// the end of the first page, whole pages in between, from the top of the last.
std::optional<std::pair<QPointF, QPointF>> PageView::selectionOn(int page) const
{
    if (!hasSelection())
        return std::nullopt;
    const bool reversed = m_selEndPage < m_selStartPage;
    const int firstPage = reversed ? m_selEndPage : m_selStartPage;
    const int lastPage = reversed ? m_selStartPage : m_selEndPage;
    if (page < firstPage || page > lastPage)
        return std::nullopt;
    const QRectF bounds = m_doc->pageBounds(page);
    const QPointF a = page == firstPage ? (reversed ? m_selEnd : m_selStart) : bounds.topLeft();
    const QPointF b = page == lastPage ? (reversed ? m_selStart : m_selEnd) : bounds.bottomRight();
    return std::pair{a, b};
}

QList<QPolygonF> PageView::selectionQuadsOn(int page) const
{
    const auto range = selectionOn(page);
    return range ? m_doc->selectionQuads(page, range->first, range->second, m_selMode) : QList<QPolygonF>();
}

QString PageView::selectedText() const
{
    QStringList parts;
    for (int page = std::min(m_selStartPage, m_selEndPage); page <= std::max(m_selStartPage, m_selEndPage); ++page)
        if (const auto range = selectionOn(page))
            parts << m_doc->selectionText(page, range->first, range->second, m_selMode).trimmed();
    parts.removeAll(QString());
    return parts.join('\n');
}

void PageView::copySelection()
{
    QString text;
    if (hasSelection())
        text = selectedText();
    else if (m_selectedHighlight >= 0 && m_selectedHighlight < m_highlights->all().size())
        text = m_highlights->all()[m_selectedHighlight].text;
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
    if (QApplication::clipboard()->supportsSelection())
        QApplication::clipboard()->setText(text, QClipboard::Selection); // middle-click paste
}

void PageView::highlightSelection(int color)
{
    // One highlight per page the selection covers.
    for (int page = std::min(m_selStartPage, m_selEndPage); page <= std::max(m_selStartPage, m_selEndPage); ++page) {
        const auto range = selectionOn(page);
        if (!range)
            continue;
        Highlight h;
        h.page = page;
        h.quads = m_doc->selectionQuads(page, range->first, range->second, m_selMode);
        h.text = m_doc->selectionText(page, range->first, range->second, m_selMode).trimmed();
        h.color = color;
        if (!h.quads.isEmpty())
            m_highlights->add(h);
    }
    clearSelection();
}

void PageView::autoScroll()
{
    const double y = m_lastMousePos.y();
    const double h = viewport()->height();
    double dy = 0;
    if (y < kAutoScrollZone)
        dy = -(kAutoScrollZone - y);
    else if (y > h - kAutoScrollZone)
        dy = y - (h - kAutoScrollZone);
    if (!m_selecting || dy == 0) {
        m_autoScroll.stop();
        return;
    }
    scrollVertical(int(std::clamp(dy, -80.0, 80.0) / 2)); // faster the further out
    updateSelection(m_lastMousePos);
}

int PageView::highlightAt(int page, QPointF point) const
{
    const QList<Highlight>& all = m_highlights->all();
    for (int h = all.size() - 1; h >= 0; --h) // topmost first
        if (all[h].page == page)
            for (const QPolygonF& quad : all[h].quads)
                if (quad.containsPoint(point, Qt::OddEvenFill))
                    return h;
    return -1;
}

void PageView::mousePressEvent(QMouseEvent* event)
{
    const auto hit = pagePointAt(event->position());
    if (!hit)
        return;

    if (event->button() == Qt::RightButton) {
        auto target = m_doc->linkAt(hit->page, hit->point);
        if (!target)
            target = findCitationTarget(*m_doc, hit->page, hit->point); // unlinked citation
        if (target)
            showLinkPreview(*target, event->globalPosition().toPoint());
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    m_selectedHighlight = -1;
    m_selStartPage = m_selEndPage = hit->page;
    m_selStart = m_selEnd = hit->point;
    m_selMode = SelectMode::Chars;
    m_selQuads.clear();
    m_selecting = true;
    m_lastMousePos = event->position();
    viewport()->update();
}

void PageView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_selecting)
        return;
    m_lastMousePos = event->position();
    updateSelection(m_lastMousePos);
    const double y = m_lastMousePos.y();
    if ((y < kAutoScrollZone || y > viewport()->height() - kAutoScrollZone) && !m_autoScroll.isActive())
        m_autoScroll.start();
}

void PageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_selecting)
        return;
    m_selecting = false;
    m_autoScroll.stop();
    updateSelection(event->position());
    if (m_selStartPage == m_selEndPage && m_selStart == m_selEnd && m_selMode == SelectMode::Chars) {
        // A plain click: select the highlight under it, if any.
        m_selectedHighlight = highlightAt(m_selStartPage, m_selStart);
        clearSelection();
    } else if (QApplication::clipboard()->supportsSelection()) {
        QApplication::clipboard()->setText(selectedText(), QClipboard::Selection);
    }
}

void PageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (const auto hit = pagePointAt(event->position())) {
        m_selectedHighlight = -1;
        m_selStartPage = m_selEndPage = hit->page;
        m_selStart = m_selEnd = hit->point;
        m_selMode = SelectMode::Words; // drag after a double-click extends by words
        m_selQuads.clear();
        m_selecting = true;
        m_lastMousePos = event->position();
        viewport()->update();
    }
}

// --- link preview ----------------------------------------------------------

namespace {
// An overlay inside the main window (not a separate popup window: on X11 a
// popup grabs the keyboard, which blocks window-manager keybindings).
// Closes on a click anywhere outside it or any other preview.
class LinkPreview : public QFrame {
public:
    explicit LinkPreview(QWidget* window) : QFrame(window)
    {
        setObjectName("linkPreview"); // for the stylesheet
        setAttribute(Qt::WA_DeleteOnClose);
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
            for (auto* w = static_cast<QWidget*>(watched); w; w = w->parentWidget())
                if (w->objectName() == "linkPreview")
                    return false; // inside this or a nested preview
            close();
        }
        return false;
    }
};
} // namespace

void PageView::showLinkPreview(const LinkTarget& target, QPoint globalPos)
{
    QWidget* host = window();
    auto* preview = new LinkPreview(host);
    auto* layout = new QVBoxLayout(preview);
    layout->setContentsMargins(2, 2, 2, 2); // inside the 2px border

    // A second view of the same document, so text in it can be selected,
    // copied and scrolled like anywhere else.
    auto* view = new PageView(m_doc, m_highlights, target.page, preview);
    view->m_inPreview = true;
    view->setTheme(m_theme);
    view->showTarget(target, m_zoom);
    layout->addWidget(view);

    // As wide as the page at the current zoom (both columns), within the window.
    const int width = std::min(int(m_doc->pageSize(target.page).width() * m_zoom) + 2 * kGap + 4,
                               host->width() - 16);
    const int height = std::min(360, host->height() / 2);
    const QPoint click = host->mapFromGlobal(globalPos);
    QPoint pos(click.x() - width / 2, click.y() + 16);
    if (pos.y() + height > host->height() - 8)
        pos.setY(click.y() - height - 16); // flip above the cursor
    pos.setX(std::clamp(pos.x(), 8, std::max(8, host->width() - width - 8)));
    pos.setY(std::clamp(pos.y(), 8, std::max(8, host->height() - height - 8)));

    preview->setGeometry(QRect(pos, QSize(width, height)));
    preview->show();
    preview->raise();
    view->setFocus();

    QPointer<PageView> origin(this);
    connect(preview, &QObject::destroyed, this, [origin] {
        if (origin)
            origin->setFocus();
    });
}

// --- search ----------------------------------------------------------------

double PageView::layoutY(int page, double pageY) const
{
    return m_pageRects[page].top() + (pageY - m_doc->pageBounds(page).top()) * m_zoom;
}

bool PageView::search(const QString& query, bool forward)
{
    // Smartcase: an uppercase letter makes the search case-sensitive.
    const bool hasUpper = std::any_of(query.begin(), query.end(), [](QChar c) { return c.isUpper(); });
    const Qt::CaseSensitivity cs = hasUpper ? Qt::CaseSensitive : Qt::CaseInsensitive;

    m_searchQuery = query;
    m_searchForward = forward;
    m_searchHits.assign(m_doc->pageCount(), {});
    m_searchTotal = 0;
    for (int page = 0; page < m_doc->pageCount(); ++page) {
        m_searchHits[page] = m_doc->search(page, query, cs);
        m_searchTotal += m_searchHits[page].size();
    }
    if (m_searchTotal == 0) {
        clearSearch();
        return false;
    }

    // First match below (or above) the top of the view.
    const double top = verticalScrollBar()->value();
    m_hitPage = m_hitIndex = -1;
    for (int i = 0; i < m_doc->pageCount() && m_hitPage < 0; ++i) {
        const int page = forward ? i : m_doc->pageCount() - 1 - i;
        const auto& hits = m_searchHits[page];
        for (int j = 0; j < hits.size(); ++j) {
            const int h = forward ? j : hits.size() - 1 - j;
            const double y = layoutY(page, hits[h].first().top());
            if (forward ? y >= top : y < top) {
                m_hitPage = page, m_hitIndex = h;
                break;
            }
        }
    }
    if (m_hitPage < 0) { // none in that direction: wrap
        m_hitPage = -1;
        searchNext(forward);
        return true;
    }
    scrollToHit();
    return true;
}

void PageView::searchNext(bool forward)
{
    if (m_searchTotal == 0)
        return;
    const int n = m_doc->pageCount();
    int page = m_hitPage, index = m_hitIndex;
    if (page < 0) { // start from the very beginning / end
        page = forward ? n - 1 : 0;
        index = forward ? int(m_searchHits[page].size()) : -1;
    }
    index += forward ? 1 : -1;
    bool wrapped = false;
    while (index < 0 || index >= m_searchHits[page].size()) {
        page += forward ? 1 : -1;
        if (page < 0 || page >= n) {
            page = forward ? 0 : n - 1;
            wrapped = true;
        }
        index = forward ? 0 : int(m_searchHits[page].size()) - 1;
    }
    m_hitPage = page;
    m_hitIndex = index;
    if (wrapped)
        emit message(forward ? "search hit BOTTOM, continuing at TOP" : "search hit TOP, continuing at BOTTOM");
    scrollToHit();
}

void PageView::scrollToHit()
{
    const QRectF r = m_searchHits[m_hitPage][m_hitIndex].first();
    const double y = layoutY(m_hitPage, r.top());
    const double top = verticalScrollBar()->value();
    if (y < top || y + r.height() * m_zoom > top + viewport()->height())
        verticalScrollBar()->setValue(int(y - viewport()->height() / 3.0)); // a third of the way down
    const QPointF left = toViewport(m_hitPage, r.topLeft());
    if (left.x() < 0 || left.x() > viewport()->width())
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + int(left.x() - viewport()->width() / 3.0));
    viewport()->update();
    emit statusChanged();
}

void PageView::clearSearch()
{
    m_searchQuery.clear();
    m_searchHits.clear();
    m_searchTotal = 0;
    m_hitPage = m_hitIndex = -1;
    viewport()->update();
    emit statusChanged();
}

QString PageView::searchStatus() const
{
    if (m_searchQuery.isEmpty() || m_hitPage < 0)
        return {};
    int position = m_hitIndex + 1;
    for (int page = 0; page < m_hitPage; ++page)
        position += m_searchHits[page].size();
    return QString("[%1/%2]").arg(position).arg(m_searchTotal);
}

// --- keys ------------------------------------------------------------------

void PageView::keyPressEvent(QKeyEvent* event)
{
    const QString text = event->text();
    const QChar c = text.isEmpty() ? QChar() : text[0];
    const int count = m_count ? m_count : 1;
    const int halfPage = viewport()->height() / 2;

    // Count prefix, e.g. 5j or 42G. A leading 0 is not a count.
    if (c.isDigit() && !(c == '0' && m_count == 0) && !(event->modifiers() & Qt::ControlModifier)) {
        m_count = std::min(m_count * 10 + c.digitValue(), 1000000);
        return;
    }

    if (c == 'g' && !m_pendingG) {
        m_pendingG = true;
        return;
    }
    const bool wasPendingG = m_pendingG;
    m_pendingG = false;

    if (event->modifiers() & Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_D: scrollVertical(count * halfPage); break;
        case Qt::Key_U: scrollVertical(-count * halfPage); break;
        case Qt::Key_F: scrollVertical(count * viewport()->height()); break;
        case Qt::Key_B: scrollVertical(-count * viewport()->height()); break;
        case Qt::Key_C: copySelection(); break;
        default: QAbstractScrollArea::keyPressEvent(event); break;
        }
    } else if (wasPendingG && c == 'g') {
        goToPage(m_count ? m_count - 1 : 0);
    } else if (c == 'G') {
        if (m_count)
            goToPage(m_count - 1);
        else
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    } else if (c == 'j') {
        scrollVertical(count * kScrollStep);
    } else if (c == 'k') {
        scrollVertical(-count * kScrollStep);
    } else if (c == 'h') {
        scrollHorizontal(-count * kScrollStep);
    } else if (c == 'l') {
        scrollHorizontal(count * kScrollStep);
    } else if (c == 'J' || c == 'K') { // next / previous page (or spread)
        int page = pageAt(verticalScrollBar()->value() + kGap);
        for (int i = 0; i < count; ++i)
            page = c == 'J' ? lastInRow(page) + 1 : pageAt(m_pageTop[std::max(0, page - 1)]);
        goToPage(page);
    } else if (c == '+' || c == '=') {
        m_fitWidth = false;
        setZoom(m_zoom * std::pow(kZoomStep, count));
    } else if (c == '-') {
        m_fitWidth = false;
        setZoom(m_zoom / std::pow(kZoomStep, count));
    } else if (c == 's') {
        m_fitWidth = true;
        setZoom(fitWidthZoom());
    } else if (c == 'd') {
        setDualPage(!m_dual, m_firstOnRight);
    } else if (c == 'D') {
        setDualPage(true, !m_firstOnRight);
    } else if (c == 'a') { // highlight the selection, or recolor the clicked highlight; 2a = color 2
        if (m_count)
            m_lastColor = std::clamp(m_count - 1, 0, int(kHighlightColors.size()) - 1);
        if (hasSelection())
            highlightSelection(m_lastColor);
        else if (m_selectedHighlight >= 0)
            m_highlights->setColor(m_selectedHighlight, m_lastColor);
        viewport()->update();
    } else if (c == 'x' && m_selectedHighlight >= 0) {
        m_highlights->remove(m_selectedHighlight);
        m_selectedHighlight = -1;
        viewport()->update();
    } else if (c == 'y') {
        copySelection();
        clearSelection();
    } else if ((c == 'n' || c == 'N') && !m_searchQuery.isEmpty()) {
        for (int i = 0; i < count; ++i)
            searchNext(c == 'n' ? m_searchForward : !m_searchForward); // N reverses, as in vim
    } else if (event->key() == Qt::Key_Escape && (hasSelection() || m_selectedHighlight >= 0)) {
        m_selectedHighlight = -1;
        clearSelection();
    } else if (event->key() == Qt::Key_Escape && !m_searchQuery.isEmpty()) {
        clearSearch(); // like :noh
    } else if (m_inPreview && (c == 'q' || event->key() == Qt::Key_Escape)) {
        parentWidget()->close();
    } else if (c == 'q') {
        window()->close();
    } else {
        QAbstractScrollArea::keyPressEvent(event);
    }
    m_count = 0;
}
