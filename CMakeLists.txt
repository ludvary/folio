# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)
project(folio VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)

add_executable(folio
  src/main.cpp
  src/document.cpp
  src/pageview.cpp
  src/mainwindow.cpp
  src/filetree.cpp
  src/fuzzyfinder.cpp
  src/recents.cpp
  src/theme.cpp
  src/pdfindex.cpp
  src/citation.cpp
  src/highlights.cpp
  src/statusbar.cpp
  src/cheatsheet.cpp
)
target_link_libraries(folio PRIVATE Qt6::Widgets Qt6::Concurrent PkgConfig::MUPDF)

# cmake --install build                      -> /usr/local (needs sudo)
# cmake --install build --prefix ~/.local    -> just for you
include(GNUInstallDirs)
install(TARGETS folio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/folio.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/folio.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
