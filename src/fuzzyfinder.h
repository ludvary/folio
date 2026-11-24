// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include <QFrame>
#include <QList>
#include <QPointer>
#include <QString>
#include <functional>

class QLineEdit;
class QListWidget;

// Popup that fuzzy-matches a query against a list of items (files, themes, ...).
class FuzzyFinder : public QFrame {
    Q_OBJECT
public:
    struct Item {
        QString label; // shown and matched against
        QString value; // passed to the callback
    };

    explicit FuzzyFinder(QWidget* parent);

    // Earlier items win ties, so put the most relevant (e.g. recents) first.
    void popup(const QList<Item>& items, const QString& placeholder,
               std::function<void(const QString&)> onChosen);
    void refresh(const QList<Item>& items); // replace items, keep the query

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refilter();
    void moveSelection(int delta);
    void accept();

    QLineEdit* m_input;
    QListWidget* m_list;
    QList<Item> m_items;
    std::function<void(const QString&)> m_onChosen;
    QPointer<QWidget> m_previousFocus; // restored when the popup closes
};
