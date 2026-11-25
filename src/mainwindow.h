// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#pragma once

#include "fuzzyfinder.h"
#include "highlights.h"
#include "recents.h"
#include "theme.h"

#include <QList>
#include <QMainWindow>
#include <memory>

class FileTree;
class PageView;
class PdfIndex;
class QSplitter;
class StatusBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& root, QWidget* parent = nullptr);

    bool openFile(const QString& path);
    void setTreeVisible(bool visible);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void rememberPosition();
    void toggleTree();
    void showFileFinder();
    void showThemePicker();
    void applyTheme(const QString& name);
    void editColors();
    void toggleOriginalColors();
    void showHighlightList();
    Theme pageTheme() const; // m_theme, or black on white while toggled
    void updateStatus();
    QList<FuzzyFinder::Item> fileItems() const;

    QSplitter* m_splitter;
    FileTree* m_tree;
    StatusBar* m_status;
    FuzzyFinder* m_finder;
    PdfIndex* m_index;
    PageView* m_view = nullptr;
    QString m_currentPath;
    Recents m_recents;
    QList<Theme> m_themes;
    Theme m_theme;
    bool m_originalColors = false; // Ctrl-r
    bool m_finderShowsFiles = false;
};
