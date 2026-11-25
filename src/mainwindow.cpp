// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "mainwindow.h"

#include "cheatsheet.h"
#include "document.h"
#include "filetree.h"
#include "pageview.h"
#include "pdfindex.h"
#include "statusbar.h"

#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <memory>
#include <stdexcept>

namespace {
QSettings config()
{
    return QSettings(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/config.ini",
                     QSettings::IniFormat);
}

QString prettyPath(const QString& path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home + '/') ? '~' + path.mid(home.size()) : path;
}

QString buildStyleSheet(const Theme& t)
{
    return QString(R"(
        QSplitter::handle { background: %4; width: 1px; }
        QTreeView { background: %3; border: none; padding: 4px; outline: 0; }
        QTreeView::item { padding: 3px 2px; border-radius: 4px; }
        QTreeView::item:selected { background: %4; color: %1; }
        QFrame#finder { background: %3; border: 1px solid %4; border-radius: 10px; }
        QFrame#finder QLineEdit { background: %5; border: 1px solid %4; border-radius: 6px;
                                  padding: 6px 8px; color: %1; selection-background-color: %6; }
        QFrame#finder QLineEdit:focus { border-color: %6; }
        QFrame#finder QListWidget { background: transparent; border: none; outline: 0; }
        QFrame#finder QListWidget::item { padding: 5px 8px; border-radius: 6px; color: %2; }
        QFrame#finder QListWidget::item:selected { background: %4; color: %1; }
        QFrame#linkPreview { background: %5; border: 2px solid %6; }
        QDialog { background: %3; }
        QWidget#statusBar { background: %3; border-top: 1px solid %4; }
        QWidget#statusBar QLabel { color: %2; }
        QWidget#statusBar QLabel#statusName, QWidget#statusBar QLabel#statusPrompt { color: %1; }
        QWidget#statusBar QLabel#statusPrompt { color: %6; font-weight: bold; }
        QFrame#cheatsheet { background: %3; border: 1px solid %4; border-radius: 12px; }
        QFrame#cheatsheet QLabel#cheatTitle { color: %1; font-size: 15pt; font-weight: 600; }
        QFrame#cheatsheet QLabel#cheatSection { color: %6; font-size: 8pt; font-weight: 700; padding-top: 2px; }
        QFrame#cheatsheet QLabel#cheatDesc { color: %2; }
        QFrame#cheatsheet QLabel#cheatSep { color: %2; }
        QFrame#cheatsheet QLabel#kbd { color: %1; background: %5; border: 1px solid %4; border-radius: 5px;
                                       padding: 1px 6px; font-family: "JetBrains Mono", "Fira Code", monospace; }
        QWidget#statusBar QLineEdit { background: transparent; border: none; color: %1; padding: 0; }
        QScrollBar:vertical { background: transparent; width: 8px; }
        QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 24px; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }
    )")
        .arg(t.fg.name(), t.muted.name(), t.surface.name(), t.border.name(), t.canvas.name(),
             t.accent.name());
}
} // namespace

MainWindow::MainWindow(const QString& root, QWidget* parent)
    : QMainWindow(parent),
      m_splitter(new QSplitter(this)),
      m_tree(new FileTree(root, m_splitter)),
      m_status(new StatusBar(this)),
      m_finder(new FuzzyFinder(this)),
      m_index(new PdfIndex(root, this)),
      m_themes(loadThemes())
{
    auto* placeholder = new QLabel("o  open a PDF\nt  toggle file tree\nCtrl-h  all keybindings", m_splitter);
    placeholder->setAlignment(Qt::AlignCenter);
    m_splitter->addWidget(m_tree);
    m_splitter->addWidget(placeholder);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 900});
    m_splitter->setHandleWidth(1);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_splitter, 1);
    layout->addWidget(m_status);
    setCentralWidget(central);
    m_status->setFile("folio");
    m_status->setPage(0, 0);
    m_status->setSearchInfo({});
    m_status->setPinned(config().value("statusbar", true).toBool());
    setWindowTitle("folio");

    QApplication::setStyle(QStyleFactory::create("Fusion")); // same look on every desktop
    applyTheme(config().value("theme", "ember").toString());

    connect(m_tree, &FileTree::fileChosen, this, &MainWindow::openFile);
    connect(m_tree, &FileTree::escapePressed, this, [this] { if (m_view) m_view->setFocus(); });
    connect(m_index, &PdfIndex::updated, this, [this] {
        if (m_finderShowsFiles)
            m_finder->refresh(fileItems());
    });

    connect(new QShortcut(QKeySequence("o"), this), &QShortcut::activated, this, &MainWindow::showFileFinder);
    connect(new QShortcut(QKeySequence("t"), this), &QShortcut::activated, this, &MainWindow::toggleTree);
    connect(new QShortcut(QKeySequence("Shift+T"), this), &QShortcut::activated, this, &MainWindow::showThemePicker);
    connect(new QShortcut(QKeySequence("Shift+C"), this), &QShortcut::activated, this, &MainWindow::editColors);
    connect(new QShortcut(QKeySequence("Ctrl+R"), this), &QShortcut::activated, this, &MainWindow::toggleOriginalColors);
    connect(new QShortcut(QKeySequence("Shift+H"), this), &QShortcut::activated, this, &MainWindow::showHighlightList);
    auto* cheatsheet = new Cheatsheet(this);
    connect(new QShortcut(QKeySequence("Ctrl+H"), this), &QShortcut::activated, cheatsheet, &Cheatsheet::toggle);
    connect(new QShortcut(QKeySequence("Ctrl+Space"), this), &QShortcut::activated, this, [this] {
        m_status->setPinned(!m_status->pinned());
        config().setValue("statusbar", m_status->pinned());
    });

    // Search, typed into the status bar like vim's command line.
    connect(new QShortcut(QKeySequence("/"), this), &QShortcut::activated, this, [this] {
        if (m_view)
            m_status->startSearch(true);
    });
    connect(new QShortcut(QKeySequence("?"), this), &QShortcut::activated, this, [this] {
        if (m_view)
            m_status->startSearch(false);
    });
    connect(m_status, &StatusBar::searchFinished, this, [this] {
        if (m_view)
            m_view->setFocus();
    });
    connect(m_status, &StatusBar::searchSubmitted, this, [this](const QString& query, bool forward) {
        if (m_view && !m_view->search(query, forward))
            m_status->showMessage("Pattern not found: " + query);
    });
}

QList<FuzzyFinder::Item> MainWindow::fileItems() const
{
    QList<FuzzyFinder::Item> items;
    QSet<QString> seen;
    for (const Recents::Entry& e : m_recents.entries()) {
        items.append({prettyPath(e.path), e.path});
        seen.insert(e.path);
    }
    for (const QString& path : m_index->files())
        if (!seen.contains(path))
            items.append({prettyPath(path), path});
    return items;
}

void MainWindow::showFileFinder()
{
    m_finderShowsFiles = true;
    m_finder->popup(fileItems(), "Open PDF…", [this](const QString& path) { openFile(path); });
}

void MainWindow::showThemePicker()
{
    m_finderShowsFiles = false;
    QList<FuzzyFinder::Item> items;
    for (const Theme& t : m_themes)
        items.append({t.name == m_theme.name ? t.name + "  (current)" : t.name, t.name});
    m_finder->popup(items, "Theme…", [this](const QString& name) {
        applyTheme(name);
        config().setValue("theme", name);
    });
}

void MainWindow::applyTheme(const QString& name)
{
    m_theme = m_themes.first();
    for (const Theme& t : m_themes)
        if (t.name == name)
            m_theme = t;

    // Paper/ink chosen in the colors dialog, per theme.
    QSettings cfg = config();
    cfg.beginGroup("colors-" + m_theme.name);
    if (const QColor c(cfg.value("paper").toString()); c.isValid())
        m_theme.paper = c;
    if (const QColor c(cfg.value("ink").toString()); c.isValid())
        m_theme.ink = c;

    QPalette pal;
    pal.setColor(QPalette::Window, m_theme.canvas);
    pal.setColor(QPalette::WindowText, m_theme.fg);
    pal.setColor(QPalette::Base, m_theme.surface);
    pal.setColor(QPalette::AlternateBase, m_theme.surface);
    pal.setColor(QPalette::Text, m_theme.fg);
    pal.setColor(QPalette::PlaceholderText, m_theme.muted);
    pal.setColor(QPalette::Button, m_theme.surface);
    pal.setColor(QPalette::ButtonText, m_theme.fg);
    pal.setColor(QPalette::Highlight, m_theme.accent);
    pal.setColor(QPalette::HighlightedText, m_theme.canvas);
    pal.setColor(QPalette::ToolTipBase, m_theme.surface);
    pal.setColor(QPalette::ToolTipText, m_theme.fg);
    QApplication::setPalette(pal);
    qApp->setStyleSheet(buildStyleSheet(m_theme));

    if (m_view)
        m_view->setTheme(pageTheme());
}

void MainWindow::editColors()
{
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Page colors — " + m_theme.name);
    auto* form = new QFormLayout(dialog);

    const std::pair<const char*, QColor Theme::*> fields[] = {{"paper", &Theme::paper}, {"ink", &Theme::ink}};
    QList<QPushButton*> swatches;
    auto paintSwatch = [this](QPushButton* button, QColor Theme::*field) {
        const QColor c = m_theme.*field;
        button->setText(c.name());
        button->setStyleSheet(QString("background: %1; color: %2; padding: 6px 18px; border-radius: 6px;")
                                  .arg(c.name(), c.lightness() > 128 ? "#000000" : "#ffffff"));
    };

    for (const auto& [key, field] : fields) {
        auto* button = new QPushButton(dialog);
        paintSwatch(button, field);
        swatches.append(button);
        form->addRow(key == QString("paper") ? "Paper (background)" : "Ink (text)", button);

        connect(button, &QPushButton::clicked, dialog, [this, button, key, field, paintSwatch] {
            const QColor original = m_theme.*field;
            QColorDialog picker(original, this);
            picker.setOption(QColorDialog::DontUseNativeDialog); // the native one has no live preview
            connect(&picker, &QColorDialog::currentColorChanged, this, [this, field](const QColor& c) {
                m_theme.*field = c;
                m_originalColors = false; // show what's being picked
                if (m_view)
                    m_view->setTheme(pageTheme());
            });
            if (picker.exec() == QDialog::Accepted) {
                m_theme.*field = picker.selectedColor();
                QSettings cfg = config();
                cfg.setValue("colors-" + m_theme.name + "/" + key, (m_theme.*field).name());
            } else {
                m_theme.*field = original;
            }
            if (m_view)
                m_view->setTheme(pageTheme());
            paintSwatch(button, field);
        });
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Reset | QDialogButtonBox::Close, dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, dialog, [=, this] {
        QSettings cfg = config();
        cfg.remove("colors-" + m_theme.name);
        cfg.sync();
        applyTheme(m_theme.name);
        for (int i = 0; i < swatches.size(); ++i)
            paintSwatch(swatches[i], fields[i].second);
    });
    dialog->show();
}

bool MainWindow::openFile(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    std::shared_ptr<Document> doc;
    try {
        doc = std::make_shared<Document>(absolute);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "folio", QString("Could not open %1:\n%2").arg(absolute, e.what()));
        return false;
    }

    rememberPosition(); // of the document we're leaving
    const Recents::Entry saved = m_recents.entryFor(absolute);

    // One-time move of highlights from folio's old store into the PDF itself.
    QString legacyStore;
    QString importError;
    if (const QList<Highlight> legacy = legacyHighlights(absolute, &legacyStore); !legacy.isEmpty()) {
        importError = doc->addHighlights(legacy);
        if (importError.isEmpty())
            QFile::rename(legacyStore, legacyStore + ".imported");
    }

    auto* view = new PageView(std::move(doc), saved.page); // shared with link previews
    view->setTheme(pageTheme());
    view->setDualPage(saved.dualPage, saved.firstPageOnRight);
    delete m_splitter->replaceWidget(1, view);
    m_view = view;
    m_currentPath = absolute;
    rememberPosition();
    connect(view, &PageView::statusChanged, this, &MainWindow::updateStatus);
    connect(view, &PageView::message, m_status, &StatusBar::showMessage);
    m_status->setFile(QFileInfo(absolute).fileName());
    updateStatus();
    if (!importError.isEmpty())
        m_status->showMessage(importError);
    setWindowTitle(QFileInfo(absolute).fileName() + " — folio");
    m_view->setFocus();
    return true;
}

void MainWindow::rememberPosition()
{
    if (m_view)
        m_recents.touch({m_currentPath, m_view->currentPage(), m_view->dualPage(), m_view->firstPageOnRight()});
}

void MainWindow::updateStatus()
{
    if (!m_view)
        return;
    m_status->setPage(m_view->currentPage() + 1, m_view->pageCount());
    m_status->setSearchInfo(m_view->searchStatus());
}

Theme MainWindow::pageTheme() const
{
    Theme t = m_theme;
    if (m_originalColors) {
        t.paper = Qt::white;
        t.ink = Qt::black;
    }
    return t;
}

void MainWindow::toggleOriginalColors()
{
    m_originalColors = !m_originalColors;
    if (m_view)
        m_view->setTheme(pageTheme());
}

void MainWindow::showHighlightList()
{
    if (!m_view)
        return;
    m_finderShowsFiles = false;
    const Document& doc = m_view->document();
    QList<Highlight> sorted = doc.highlights();
    std::stable_sort(sorted.begin(), sorted.end(), [](const Highlight& a, const Highlight& b) {
        return a.page < b.page;
    });
    QList<FuzzyFinder::Item> items;
    for (const Highlight& h : sorted) {
        const QPointF at = h.quads.first().boundingRect().topLeft();
        items.append({QString("p.%1   %2").arg(h.page + 1).arg(doc.highlightText(h)),
                      QString("%1 %2 %3").arg(h.page).arg(at.x()).arg(at.y())});
    }
    m_finder->popup(items, "Highlights…", [this](const QString& value) {
        const QStringList v = value.split(' ');
        if (m_view && v.size() == 3)
            m_view->goTo({v[0].toInt(), QPointF(v[1].toDouble(), v[2].toDouble())});
    });
}

void MainWindow::setTreeVisible(bool visible)
{
    m_tree->setVisible(visible);
    if (visible)
        m_tree->setFocus();
    else if (m_view)
        m_view->setFocus();
}

void MainWindow::toggleTree()
{
    // If the tree is open but unfocused, first press just focuses it.
    if (m_tree->isVisible() && !m_tree->hasFocus())
        m_tree->setFocus();
    else
        setTreeVisible(!m_tree->isVisible());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    rememberPosition();
    event->accept();
}
