// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include <QPointer>
#include <QWidget>

// Overlay listing every keybinding: a card over a dimmed backdrop covering the
// window. Toggle with Ctrl-h; Esc, q or a click on the backdrop close it. Keep the table in cheatsheet.cpp in sync with the
// bindings in pageview.cpp, mainwindow.cpp and filetree.cpp.
class Cheatsheet : public QWidget {
    Q_OBJECT
public:
    explicit Cheatsheet(QWidget* parent);

    void toggle();

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QWidget* m_card;

    QPointer<QWidget> m_previousFocus;
};
