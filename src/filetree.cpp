// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "filetree.h"

#include <QFileSystemModel>
#include <QHeaderView>
#include <QKeyEvent>

FileTree::FileTree(const QString& root, QWidget* parent)
    : QTreeView(parent), m_model(new QFileSystemModel(this))
{
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot); // no hidden entries
    m_model->setNameFilters({"*.pdf"});
    m_model->setNameFilterDisables(false); // hide non-matching files instead of greying them out
    m_model->setRootPath(root);

    setModel(m_model);
    setRootIndex(m_model->index(root));
    for (int column = 1; column < m_model->columnCount(); ++column)
        hideColumn(column); // size / type / date
    setHeaderHidden(true);
    setFrameShape(QFrame::NoFrame);
    setExpandsOnDoubleClick(false); // single click handles it (see activate)

    connect(this, &QTreeView::clicked, this, &FileTree::activate);
}

void FileTree::activate(const QModelIndex& index)
{
    if (!index.isValid())
        return;
    if (m_model->isDir(index))
        setExpanded(index, !isExpanded(index));
    else
        emit fileChosen(m_model->filePath(index));
}

void FileTree::keyPressEvent(QKeyEvent* event)
{
    const QModelIndex current = currentIndex();
    const QString text = event->text();

    if (!current.isValid() && (text == "j" || text == "k")) {
        setCurrentIndex(m_model->index(0, 0, rootIndex()));
    } else if (text == "j") {
        if (const QModelIndex below = indexBelow(current); below.isValid())
            setCurrentIndex(below);
    } else if (text == "k") {
        if (const QModelIndex above = indexAbove(current); above.isValid())
            setCurrentIndex(above);
    } else if (text == "l" || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_model->isDir(current) && text == "l")
            expand(current);
        else
            activate(current);
    } else if (text == "h") {
        if (isExpanded(current))
            collapse(current);
        else if (current.parent().isValid() && current.parent() != rootIndex())
            setCurrentIndex(current.parent());
    } else if (event->key() == Qt::Key_Escape) {
        emit escapePressed();
    } else {
        QTreeView::keyPressEvent(event);
    }
}
