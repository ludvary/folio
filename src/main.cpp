// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the folio contributors

#include "mainwindow.h"

#include <QApplication>
#include <QDir>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("folio");
    app.setDesktopFileName("folio"); // matches folio.desktop, so docks and taskbars show the right icon

    // Root the file tree where we were launched; launchers that start us in / get home.
    QString root = QDir::currentPath();
    if (root == "/")
        root = QDir::homePath();

    MainWindow window(root);
    window.resize(1200, 1100);

    const QStringList args = app.arguments();
    if (args.size() >= 2) {
        if (!window.openFile(args[1]))
            return 1;
        window.setTreeVisible(false);
    } else {
        window.setTreeVisible(true);
    }

    window.show();
    return app.exec();
}
