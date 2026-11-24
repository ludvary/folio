// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "highlights.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>

namespace {
// Size + first MiB: fast even for large books, and stable across moves/renames.
QString contentKey(const QString& path)
{
    QFile f(path);
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::number(QFileInfo(path).size()));
    if (f.open(QIODevice::ReadOnly))
        hash.addData(f.read(1 << 20));
    return hash.result().toHex().left(20);
}
} // namespace

Highlights::Highlights(const QString& pdfPath)
    : m_file(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/highlights/" +
             contentKey(pdfPath) + ".json"),
      m_pdfPath(pdfPath)
{
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object()["highlights"].toArray()) {
        const QJsonObject o = v.toObject();
        Highlight h;
        h.page = o["page"].toInt();
        h.text = o["text"].toString();
        h.color = std::clamp(o["color"].toInt(), 0, int(kHighlightColors.size()) - 1);
        for (const QJsonValue& q : o["quads"].toArray()) {
            const QJsonArray n = q.toArray();
            if (n.size() != 8)
                continue;
            h.quads.append(QPolygonF({{n[0].toDouble(), n[1].toDouble()}, {n[2].toDouble(), n[3].toDouble()},
                                      {n[4].toDouble(), n[5].toDouble()}, {n[6].toDouble(), n[7].toDouble()}}));
        }
        if (!h.quads.isEmpty())
            m_list.append(h);
    }
}

void Highlights::add(const Highlight& h)
{
    m_list.append(h);
    save();
}

void Highlights::remove(int index)
{
    if (index >= 0 && index < m_list.size()) {
        m_list.removeAt(index);
        save();
    }
}

void Highlights::setColor(int index, int color)
{
    if (index >= 0 && index < m_list.size()) {
        m_list[index].color = color;
        save();
    }
}

void Highlights::save() const
{
    QJsonArray list;
    for (const Highlight& h : m_list) {
        QJsonArray quads;
        for (const QPolygonF& q : h.quads) {
            QJsonArray n;
            for (const QPointF& p : q)
                n << p.x() << p.y();
            quads.append(n);
        }
        list.append(QJsonObject{{"page", h.page}, {"text", h.text}, {"color", h.color}, {"quads", quads}});
    }
    QDir().mkpath(QFileInfo(m_file).path());
    QFile f(m_file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject{{"path", m_pdfPath}, {"highlights", list}}).toJson());
}
