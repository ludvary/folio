// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "theme.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <array>
#include <cstdio>

namespace {
Theme make(const char* name, const char* canvas, const char* paper, const char* ink, const char* surface,
           const char* border, const char* fg, const char* muted, const char* accent)
{
    return {name, QColor(canvas), QColor(paper), QColor(ink), QColor(surface),
            QColor(border), QColor(fg), QColor(muted), QColor(accent)};
}

QList<Theme> builtinThemes()
{
    //            name             canvas     paper      ink        surface    border     fg         muted      accent
    return {
        make("ember",         "#151412", "#1c1b19", "#d8d0c0", "#252422", "#3e3c38", "#d8d0c0", "#908a7e", "#e08060"),
        make("ember-soft",    "#1c1b19", "#242320", "#d8d0c0", "#2c2b28", "#444240", "#d8d0c0", "#908a7e", "#e08060"),
        make("ember-light",   "#cec2a8", "#e6dac4", "#282418", "#ddd0b8", "#b8ac96", "#282418", "#787060", "#b84c30"),
        make("gruvbox-dark",  "#1d2021", "#282828", "#ebdbb2", "#32302f", "#504945", "#ebdbb2", "#a89984", "#fe8019"),
        make("gruvbox-light", "#d5c4a1", "#fbf1c7", "#3c3836", "#f2e5bc", "#bdae93", "#3c3836", "#7c6f64", "#af3a03"),
        make("original",      "#1c1b19", "#ffffff", "#000000", "#252422", "#3e3c38", "#d8d0c0", "#908a7e", "#e08060"),
    };
}

// A user theme file: optional `base = <theme>` (default ember), then any keys to override.
Theme loadThemeFile(const QString& path, const QList<Theme>& known)
{
    QSettings ini(path, QSettings::IniFormat);
    const QString baseName = ini.value("base", "ember").toString();
    Theme theme = known.first();
    for (const Theme& t : known)
        if (t.name == baseName)
            theme = t;
    theme.name = QFileInfo(path).completeBaseName();

    const std::pair<const char*, QColor*> keys[] = {
        {"canvas", &theme.canvas}, {"paper", &theme.paper}, {"ink", &theme.ink},
        {"surface", &theme.surface}, {"border", &theme.border}, {"fg", &theme.fg},
        {"muted", &theme.muted}, {"accent", &theme.accent},
    };
    for (const auto& [key, color] : keys) {
        if (!ini.contains(key))
            continue;
        const QColor c(ini.value(key).toString());
        if (c.isValid())
            *color = c;
        else
            std::fprintf(stderr, "folio: %s: invalid color for '%s'\n", qPrintable(path), key);
    }
    return theme;
}
} // namespace

QList<Theme> loadThemes()
{
    QList<Theme> themes = builtinThemes();
    const QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/themes");
    for (const QFileInfo& file : dir.entryInfoList({"*.ini"}, QDir::Files, QDir::Name)) {
        const Theme theme = loadThemeFile(file.filePath(), themes);
        themes.removeIf([&](const Theme& t) { return t.name == theme.name; });
        themes.append(theme);
    }
    return themes;
}

void recolor(QImage& image, const Theme& theme)
{
    if (theme.paper == Qt::white && theme.ink == Qt::black)
        return; // identity

    // For each channel: lightness 0..255 -> blend from ink to paper.
    std::array<std::array<int, 256>, 3> target;
    const int ink[3] = {theme.ink.red(), theme.ink.green(), theme.ink.blue()};
    const int paper[3] = {theme.paper.red(), theme.paper.green(), theme.paper.blue()};
    for (int c = 0; c < 3; ++c)
        for (int l = 0; l < 256; ++l)
            target[c][l] = ink[c] + (paper[c] - ink[c]) * l / 255;

    // Remap each pixel's lightness and add back its color offset (r-l, g-l, b-l),
    // so grays land exactly on the theme and colored plots stay colored.
    for (int y = 0; y < image.height(); ++y) {
        uchar* p = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x, p += 3) {
            if (p[0] == p[1] && p[1] == p[2]) { // gray: most of a page, no color to keep
                const int l = p[0];
                p[0] = uchar(target[0][l]), p[1] = uchar(target[1][l]), p[2] = uchar(target[2][l]);
                continue;
            }
            const int l = (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
            for (int c = 0; c < 3; ++c)
                p[c] = uchar(std::clamp(target[c][l] + p[c] - l, 0, 255));
        }
    }
}
