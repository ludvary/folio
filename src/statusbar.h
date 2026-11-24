// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QTimer;

// Bottom bar: file name on the left, search count and "page / pages" on the
// right. Doubles as vim's command line for / and ? searches. When not pinned
// it stays hidden, appearing only while searching or showing a message.
class StatusBar : public QWidget {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);

    void setPinned(bool pinned);
    bool pinned() const { return m_pinned; }
    void setFile(const QString& name);
    void setPage(int page, int pageCount);
    void setSearchInfo(const QString& info);
    void showMessage(const QString& message); // replaces the file name for a few seconds
    void startSearch(bool forward);

signals:
    void searchSubmitted(const QString& query, bool forward);
    void searchFinished(); // submitted or cancelled: focus can go back to the page

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void endSearch();
    void updateVisibility();

    QLabel* m_name;
    QLabel* m_prompt;
    QLineEdit* m_input;
    QLabel* m_search;
    QLabel* m_page;
    QTimer* m_messageTimer;
    QString m_file;
    bool m_pinned = true;
    bool m_searchForward = true;
    bool m_searching = false; // not m_input->isVisible(): that is false while the bar itself is hidden
};
