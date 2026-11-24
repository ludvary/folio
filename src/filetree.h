// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include <QTreeView>

class QFileSystemModel;

// Folders + PDFs under a root directory, navigable with vim keys.
class FileTree : public QTreeView {
    Q_OBJECT
public:
    FileTree(const QString& root, QWidget* parent = nullptr);

signals:
    void fileChosen(const QString& path);
    void escapePressed();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void activate(const QModelIndex& index);

    QFileSystemModel* m_model;
};
