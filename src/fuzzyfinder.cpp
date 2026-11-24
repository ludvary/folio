// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "fuzzyfinder.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int kMaxShown = 100;

// -1 if the query's characters don't appear in order in `text`, else a score
// rewarding consecutive runs, word starts and matches in the file name.
int fuzzyScore(const QString& query, const QString& text)
{
    const int nameStart = text.lastIndexOf('/') + 1;
    int score = 0;
    int ti = 0;
    int previous = -2;
    for (QChar qc : query) {
        if (qc.isSpace())
            continue;
        qc = qc.toLower();
        while (ti < text.size() && text[ti].toLower() != qc)
            ++ti;
        if (ti == text.size())
            return -1;
        score += 1;
        if (ti == previous + 1)
            score += 5;
        if (ti == 0 || !text[ti - 1].isLetterOrNumber())
            score += 3;
        if (ti >= nameStart)
            score += 2;
        previous = ti++;
    }
    return score - int(text.size()) / 16; // prefer shorter paths
}

// Matching greedily over the whole path can latch onto a directory name
// ("pes" in "papers/") and miss a far better match in the file name itself.
int bestScore(const QString& query, const QString& text)
{
    const int full = fuzzyScore(query, text);
    const int nameStart = text.lastIndexOf('/') + 1;
    if (full < 0 || nameStart == 0)
        return full;
    const int name = fuzzyScore(query, text.mid(nameStart));
    return name < 0 ? full : std::max(full, name + 100);
}
} // namespace

FuzzyFinder::FuzzyFinder(QWidget* parent)
    : QFrame(parent), m_input(new QLineEdit(this)), m_list(new QListWidget(this))
{
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    m_list->setFocusPolicy(Qt::NoFocus); // keep focus (and typing) in the input
    m_list->setUniformItemSizes(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_input);
    layout->addWidget(m_list);

    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::textChanged, this, &FuzzyFinder::refilter);
    connect(m_list, &QListWidget::itemClicked, this, &FuzzyFinder::accept);
    setObjectName("finder"); // for the stylesheet
    hide();
}

void FuzzyFinder::popup(const QList<Item>& items, const QString& placeholder,
                        std::function<void(const QString&)> onChosen)
{
    m_items = items;
    m_onChosen = std::move(onChosen);
    m_input->setPlaceholderText(placeholder);
    if (!isVisible())
        m_previousFocus = QApplication::focusWidget();
    const QWidget* p = parentWidget();
    const int w = std::min(700, p->width() - 40);
    setGeometry((p->width() - w) / 2, 40, w, std::min(420, p->height() - 80));
    m_input->clear();
    refilter();
    show();
    raise();
    m_input->setFocus();
}

void FuzzyFinder::refresh(const QList<Item>& items)
{
    m_items = items;
    if (isVisible())
        refilter();
}

void FuzzyFinder::refilter()
{
    const QString query = m_input->text();
    m_list->clear();

    QList<std::pair<int, int>> matches; // (score, item index)
    for (int i = 0; i < m_items.size(); ++i) {
        const int score = query.isEmpty() ? 0 : bestScore(query, m_items[i].label);
        if (score >= 0 || query.isEmpty())
            matches.append({score, i});
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

    for (int i = 0; i < std::min<qsizetype>(matches.size(), kMaxShown); ++i) {
        const Item& item = m_items[matches[i].second];
        (new QListWidgetItem(item.label, m_list))->setData(Qt::UserRole, item.value);
    }
    m_list->setCurrentRow(0);
}

void FuzzyFinder::hideEvent(QHideEvent* event)
{
    QFrame::hideEvent(event);
    if (m_previousFocus)
        m_previousFocus->setFocus();
}

void FuzzyFinder::moveSelection(int delta)
{
    if (m_list->count() > 0)
        m_list->setCurrentRow(std::clamp(m_list->currentRow() + delta, 0, m_list->count() - 1));
}

void FuzzyFinder::accept()
{
    const QListWidgetItem* item = m_list->currentItem();
    hide();
    if (item && m_onChosen)
        m_onChosen(item->data(Qt::UserRole).toString());
}

bool FuzzyFinder::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_input)
        return false;
    if (event->type() == QEvent::FocusOut) {
        // Clicking elsewhere closes the popup; switching to another app doesn't.
        if (static_cast<QFocusEvent*>(event)->reason() != Qt::ActiveWindowFocusReason && isVisible())
            hide();
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return false;

    const auto* key = static_cast<QKeyEvent*>(event);
    const bool ctrl = key->modifiers() & Qt::ControlModifier;
    switch (key->key()) {
    case Qt::Key_Escape: hide(); return true;
    case Qt::Key_Return:
    case Qt::Key_Enter: accept(); return true;
    case Qt::Key_Down: moveSelection(1); return true;
    case Qt::Key_Up: moveSelection(-1); return true;
    case Qt::Key_J: if (ctrl) { moveSelection(1); return true; } break;
    case Qt::Key_K: if (ctrl) { moveSelection(-1); return true; } break;
    case Qt::Key_N: if (ctrl) { moveSelection(1); return true; } break;
    case Qt::Key_P: if (ctrl) { moveSelection(-1); return true; } break;
    }
    return false;
}
