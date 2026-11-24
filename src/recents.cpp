// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "recents.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
constexpr int kMaxEntries = 30;
}

Recents::Recents()
    : m_file(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recents.json")
{
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array()) {
        const QJsonObject o = v.toObject();
        Entry e{o["path"].toString(), o["page"].toInt(), o["dual"].toBool(), o["firstOnRight"].toBool()};
        if (QFileInfo::exists(e.path)) // drop files that were moved or deleted
            m_entries.append(e);
    }
}

Recents::Entry Recents::entryFor(const QString& path) const
{
    for (const Entry& e : m_entries)
        if (e.path == path)
            return e;
    return Entry{path};
}

void Recents::touch(const Entry& entry)
{
    m_entries.removeIf([&](const Entry& e) { return e.path == entry.path; });
    m_entries.prepend(entry);
    if (m_entries.size() > kMaxEntries)
        m_entries.resize(kMaxEntries);
    save();
}

void Recents::save() const
{
    QDir().mkpath(QFileInfo(m_file).path());
    QJsonArray array;
    for (const Entry& e : m_entries)
        array.append(QJsonObject{{"path", e.path}, {"page", e.page}, {"dual", e.dualPage},
                                 {"firstOnRight", e.firstPageOnRight}});
    QFile f(m_file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(array).toJson());
}
