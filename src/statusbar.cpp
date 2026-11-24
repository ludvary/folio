// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "statusbar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>

StatusBar::StatusBar(QWidget* parent)
    : QWidget(parent),
      m_name(new QLabel(this)),
      m_prompt(new QLabel(this)),
      m_input(new QLineEdit(this)),
      m_search(new QLabel(this)),
      m_page(new QLabel(this)),
      m_messageTimer(new QTimer(this))
{
    setObjectName("statusBar"); // for the stylesheet
    setAttribute(Qt::WA_StyledBackground);
    m_name->setObjectName("statusName");
    m_search->setObjectName("statusMuted");
    m_page->setObjectName("statusPage");
    m_prompt->setObjectName("statusPrompt");
    m_input->setObjectName("statusInput");
    m_name->setTextFormat(Qt::PlainText);
    m_name->setMinimumWidth(0);
    m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // long names get cut, not the bar widened

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 3, 12, 3);
    layout->setSpacing(10);
    layout->addWidget(m_prompt);
    layout->addWidget(m_input, 1);
    layout->addWidget(m_name, 1);
    layout->addWidget(m_search);
    layout->addWidget(m_page);
    m_prompt->hide();
    m_input->hide();

    m_input->installEventFilter(this);
    m_messageTimer->setSingleShot(true);
    connect(m_messageTimer, &QTimer::timeout, this, [this] {
        m_name->setText(m_file);
        m_name->setProperty("message", false);
        updateVisibility();
    });
}

void StatusBar::setPinned(bool pinned)
{
    m_pinned = pinned;
    updateVisibility();
}

void StatusBar::updateVisibility()
{
    setVisible(m_pinned || m_searching || m_messageTimer->isActive());
}

void StatusBar::setFile(const QString& name)
{
    m_file = name;
    if (!m_messageTimer->isActive())
        m_name->setText(name);
}

void StatusBar::setPage(int page, int pageCount)
{
    m_page->setText(QString("%1 / %2").arg(page).arg(pageCount));
}

void StatusBar::setSearchInfo(const QString& info)
{
    m_search->setText(info);
    m_search->setVisible(!info.isEmpty());
}

void StatusBar::showMessage(const QString& message)
{
    m_name->setText(message);
    m_messageTimer->start(3000);
    updateVisibility();
}

void StatusBar::startSearch(bool forward)
{
    m_searchForward = forward;
    m_searching = true;
    m_prompt->setText(forward ? "/" : "?");
    m_name->hide();
    m_prompt->show();
    m_input->clear();
    m_input->show();
    updateVisibility();
    m_input->setFocus();
}

void StatusBar::endSearch()
{
    m_searching = false;
    m_prompt->hide();
    m_input->hide();
    m_name->show();
    updateVisibility();
    emit searchFinished();
}

bool StatusBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_input)
        return false;
    if (event->type() == QEvent::FocusOut && m_searching) {
        endSearch(); // clicking away cancels, like leaving vim's command line
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return false;
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape || (key->key() == Qt::Key_Backspace && m_input->text().isEmpty())) {
        endSearch();
        return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        const QString query = m_input->text();
        endSearch();
        if (!query.isEmpty())
            emit searchSubmitted(query, m_searchForward);
        return true;
    }
    return false;
}
