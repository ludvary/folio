// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include <QColor>
#include <QList>
#include <QPolygonF>
#include <QString>
#include <array>

struct Highlight {
    int page = 0;
    QList<QPolygonF> quads; // page coordinates
    QString text;
    int color = 0;          // index into kHighlightColors
};

// Drawn translucent over the page.
inline const std::array<QColor, 5> kHighlightColors = {
    QColor("#f5d547"), // 1 yellow
    QColor("#7ccf6a"), // 2 green
    QColor("#5aa9f5"), // 3 blue
    QColor("#f0708a"), // 4 red
    QColor("#b48ef0"), // 5 purple
};

// The highlights of one PDF, stored in ~/.local/share/folio/highlights/ under a
// key derived from the file's contents, so they follow the file if it moves.
class Highlights {
public:
    explicit Highlights(const QString& pdfPath);

    const QList<Highlight>& all() const { return m_list; }
    void add(const Highlight& h);
    void remove(int index);
    void setColor(int index, int color);

private:
    void save() const;

    QString m_file;
    QString m_pdfPath; // informational, stored alongside
    QList<Highlight> m_list;
};
