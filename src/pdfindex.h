// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// Every PDF under a root directory. Starts from an on-disk cache of the
// previous scan (instant), then rescans in the background and emits updated().
class PdfIndex : public QObject {
    Q_OBJECT
public:
    explicit PdfIndex(const QString& root, QObject* parent = nullptr);

    const QStringList& files() const { return m_files; }

signals:
    void updated();

private:
    QString m_cacheFile;
    QStringList m_files;
};
