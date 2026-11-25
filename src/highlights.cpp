// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "highlights.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>

namespace {
// How the old store named its files: size + first MiB of the PDF.
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

QList<Highlight> legacyHighlights(const QString& pdfPath, QString* storeFile)
{
    *storeFile = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/highlights/" +
                 contentKey(pdfPath) + ".json";
    QList<Highlight> result;
    QFile f(*storeFile);
    if (!f.open(QIODevice::ReadOnly))
        return result;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object()["highlights"].toArray()) {
        const QJsonObject o = v.toObject();
        Highlight h;
        h.page = o["page"].toInt();
        h.color = kHighlightColors[std::clamp(o["color"].toInt(), 0, int(kHighlightColors.size()) - 1)];
        for (const QJsonValue& q : o["quads"].toArray()) {
            const QJsonArray n = q.toArray();
            if (n.size() == 8)
                h.quads.append(QPolygonF({{n[0].toDouble(), n[1].toDouble()}, {n[2].toDouble(), n[3].toDouble()},
                                          {n[4].toDouble(), n[5].toDouble()}, {n[6].toDouble(), n[7].toDouble()}}));
        }
        if (!h.quads.isEmpty())
            result.append(h);
    }
    return result;
}
