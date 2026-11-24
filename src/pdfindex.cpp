// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "pdfindex.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

namespace {
// All PDFs below `root`, skipping hidden directories.
QStringList scan(const QString& root)
{
    QStringList pdfs;
    QDirIterator it(root, {"*.pdf"}, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        pdfs.append(it.next());
    return pdfs;
}
} // namespace

PdfIndex::PdfIndex(const QString& root, QObject* parent) : QObject(parent)
{
    // One cache file per root directory.
    const QByteArray key = QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_cacheFile = dir + "/index-" + key + ".txt";

    if (QFile f(m_cacheFile); f.open(QIODevice::ReadOnly | QIODevice::Text))
        m_files = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);

    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, dir] {
        m_files = watcher->result();
        watcher->deleteLater();
        QDir().mkpath(dir);
        if (QFile f(m_cacheFile); f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            f.write(m_files.join('\n').toUtf8());
        emit updated();
    });
    watcher->setFuture(QtConcurrent::run(scan, root));
}
