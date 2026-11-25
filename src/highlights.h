// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include "document.h"

#include <QColor>
#include <QList>
#include <QString>
#include <array>

// The colours `1a` … `5a` use, drawn translucent over the page.
inline const std::array<QColor, 5> kHighlightColors = {
    QColor("#f5d547"), // 1 yellow
    QColor("#7ccf6a"), // 2 green
    QColor("#5aa9f5"), // 3 blue
    QColor("#f0708a"), // 4 red
    QColor("#b48ef0"), // 5 purple
};

// Older versions of folio kept highlights in ~/.local/share/folio/highlights/
// instead of in the PDF. Returns those for `pdfPath` (ids unset), and the file
// they came from so it can be retired once they're written into the PDF.
QList<Highlight> legacyHighlights(const QString& pdfPath, QString* storeFile);
