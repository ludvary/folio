// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include <QColor>
#include <QList>
#include <QString>

class QImage;

struct Theme {
    QString name;
    QColor canvas;  // behind the pages
    QColor paper;   // page background (white is mapped to this)
    QColor ink;     // page text (black is mapped to this)
    QColor surface; // sidebar, popups
    QColor border;
    QColor fg;      // UI text
    QColor muted;   // secondary UI text
    QColor accent;  // selection, focus
};

// Built-in themes plus ~/.config/folio/themes/*.ini (which may override built-ins).
QList<Theme> loadThemes();

// Maps the page's white/black to paper/ink, keeping colors recognisable.
void recolor(QImage& image, const Theme& theme);
