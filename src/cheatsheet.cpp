// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "cheatsheet.h"

#include "highlights.h"

#include <QFrame>

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace {
struct Binding {
    const char* keys; // space-separated chips; "/" and "…" are drawn as plain separators
    const char* description;
};

struct Section {
    const char* title;
    std::initializer_list<Binding> bindings;
};

// Laid out in three columns, top to bottom.
const std::initializer_list<std::initializer_list<Section>> kColumns = {
    {
        {"Navigation", {
            {"j / k", "scroll down / up"},
            {"h / l", "scroll left / right"},
            {"Ctrl-d / Ctrl-u", "half page down / up"},
            {"Ctrl-f / Ctrl-b", "full page down / up"},
            {"J / K", "next / previous page"},
            {"gg / G", "first / last page"},
            {"42G", "go to page 42"},
            {"5j 3J …", "counts repeat any motion"},
        }},
        {"View", {
            {"+ / -", "zoom in / out  (Ctrl + scroll)"},
            {"s", "fit width"},
            {"d", "double-page mode"},
            {"D", "first page on the right"},
            {"Ctrl-r", "black on white ↔ theme"},
            {"Ctrl-Space", "status bar"},
        }},
    },
    {
        {"Text & highlights", {
            {"drag", "select text, across pages too"},
            {"double-click", "select a word"},
            {"y / Ctrl-c", "copy"},
            {"a", "highlight selection"},
            {"1a … 5a", "highlight in color"}, // swatches appended below
            {"click", "select a highlight"},
            {"x", "delete selected highlight"},
            {"H", "list highlights"},
            {"Esc", "clear selection"},
        }},
        {"Search", {
            {"/ / ?", "search forward / backward"},
            {"n / N", "next / previous match"},
            {"Esc", "clear matches"},
        }},
    },
    {
        {"Citations", {
            {"right-click", "preview citation or link"},
            {"q / Esc", "close preview"},
        }},
        {"Files & look", {
            {"o", "open: recents, then fuzzy search"},
            {"t", "file tree  (j k h l, Enter)"},
            {"T", "theme"},
            {"C", "page colors"},
            {"Ctrl-h", "this cheatsheet"},
            {"q", "quit"},
        }},
    },
};

QWidget* keyChips(const QString& keys, QWidget* parent)
{
    auto* box = new QWidget(parent);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    const QStringList tokens = keys.split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < tokens.size(); ++i) {
        // A lone "/" between two keys is a separator; "/" as the first key is the search key.
        const bool separator = (tokens[i] == "/" && i > 0) || tokens[i] == "…";
        auto* label = new QLabel(tokens[i], box);
        label->setObjectName(separator ? "cheatSep" : "kbd");
        row->addWidget(label);
    }
    row->addStretch();
    return box;
}
} // namespace

Cheatsheet::Cheatsheet(QWidget* parent) : QWidget(parent), m_card(new QFrame(this))
{
    m_card->setObjectName("cheatsheet"); // for the stylesheet
    setFocusPolicy(Qt::StrongFocus);
    hide();

    auto* center = new QGridLayout(this); // keeps the card centered over the backdrop
    center->setContentsMargins(16, 16, 16, 16);
    center->addWidget(m_card, 0, 0, Qt::AlignCenter);

    QWidget* card = m_card;
    auto* outer = new QVBoxLayout(card);
    outer->setContentsMargins(28, 22, 28, 24);
    outer->setSpacing(18);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel("Keybindings", card);
    title->setObjectName("cheatTitle");
    auto* hint = new QLabel("Esc to close", card);
    hint->setObjectName("cheatDesc");
    header->addWidget(title);
    header->addStretch();
    header->addWidget(hint);
    outer->addLayout(header);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(36);
    for (const auto& column : kColumns) {
        auto* col = new QVBoxLayout;
        col->setSpacing(6);
        for (const Section& section : column) {
            auto* heading = new QLabel(QString(section.title).toUpper(), card);
            heading->setObjectName("cheatSection");
            col->addWidget(heading);
            auto* grid = new QGridLayout;
            grid->setHorizontalSpacing(14);
            grid->setVerticalSpacing(5);
            int row = 0;
            for (const Binding& b : section.bindings) {
                grid->addWidget(keyChips(b.keys, card), row, 0);
                QString description = b.description;
                if (QString(b.keys) == "1a … 5a") { // show the five colors
                    description += "  ";
                    for (const QColor& c : kHighlightColors)
                        description += QString("<span style='color:%1'>●</span>").arg(c.name());
                }
                auto* desc = new QLabel(description, card);
                desc->setObjectName("cheatDesc");
                grid->addWidget(desc, row++, 1);
            }
            grid->setColumnStretch(1, 1);
            col->addLayout(grid);
            col->addSpacing(14);
        }
        col->addStretch();
        columns->addLayout(col);
    }
    outer->addLayout(columns);

    parent->installEventFilter(this); // to keep covering the window on resize
}

void Cheatsheet::toggle()
{
    if (isVisible()) {
        hide();
        return;
    }
    m_previousFocus = QApplication::focusWidget();
    setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus();
}

bool Cheatsheet::event(QEvent* event)
{
    // While open, every key comes here instead of triggering window shortcuts (o, t, …).
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

bool Cheatsheet::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return false;
}

void Cheatsheet::paintEvent(QPaintEvent*)
{
    QPainter(this).fillRect(rect(), QColor(0, 0, 0, 120)); // dim what's behind
}

void Cheatsheet::mousePressEvent(QMouseEvent* event)
{
    if (!m_card->geometry().contains(event->position().toPoint()))
        hide(); // clicked the backdrop
}

void Cheatsheet::keyPressEvent(QKeyEvent* event)
{
    const bool ctrlH = event->key() == Qt::Key_H && (event->modifiers() & Qt::ControlModifier);
    if (event->key() == Qt::Key_Escape || event->text() == "q" || ctrlH)
        hide();
}

void Cheatsheet::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (m_previousFocus)
        m_previousFocus->setFocus();
}
