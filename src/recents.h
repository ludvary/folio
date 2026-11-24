// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include <QList>
#include <QString>

// Most-recently-opened PDFs with the page you were on, persisted as JSON.
class Recents {
public:
    struct Entry {
        QString path;
        int page = 0;
        bool dualPage = false;
        bool firstPageOnRight = false;
    };

    Recents(); // loads from disk

    const QList<Entry>& entries() const { return m_entries; }
    Entry entryFor(const QString& path) const; // defaults if unknown
    void touch(const Entry& entry);            // move to front, save

private:
    void save() const;

    QString m_file;
    QList<Entry> m_entries;
};
