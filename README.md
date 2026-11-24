# folio

A keyboard-driven PDF viewer for Linux, mostly for reading papers.

![icon](data/folio.svg)

## Why you might like it

**If you've used zathura** and liked never touching the mouse, you'll feel at
home: `j`/`k`, counts like `5j`, `gg`/`G`, `/` search, `d` for double page,
`Ctrl-r` to recolour. On top of that you get:

- right-click a citation and the reference pops up, without losing your place
- highlights
- a fuzzy finder over every PDF under the folder you started in, recent files
  first, instead of typing out paths
- themes you can switch and tweak (page background, text colour) from inside the
  app, no config file needed

**If you've used sioyek** and liked the reference preview, folio has that too,
plus:

- the preview is a real view of the page, so you can select and copy from it.
  Getting a reference into Google Scholar is select, `y`, paste
- highlighting is just select + `a` (`2a` for colour 2); click one and press `x`
  to delete it
- opening files is a fuzzy finder, not a file dialog
- a cleaner look: themed UI, recoloured pages, a small status bar you can hide

**Compared to both,** it's small: one binary, under 3,000 lines of C++ on Qt and
MuPDF (the same rendering engine sioyek uses). It opens instantly, and you don't
have to configure anything before it's usable; `Ctrl-h` lists every key.

To be fair to them: zathura is far more mature and reads more than PDFs (DjVu,
PostScript, comics) through plugins, and sioyek does a lot more (portals,
bookmarks, SyncTeX, ruler mode and so on). If you rely on those, stay where you
are. folio is for reading PDFs, mostly papers, with the handful of things I
actually use done properly.

> **Heads up:** almost all of the code here was written by Claude (Anthropic's
> AI model) over a long session with me directing it. I use folio every day, but
> I have **not** properly reviewed the code yet. It works on my machine; it might
> misbehave on yours. Treat it accordingly, and issues/PRs are very welcome.

## What it does

- vim motions, with counts: `j` `k` `5J` `gg` `42G` `Ctrl-d` and friends
- **right-click a citation** to get a small preview of the reference, which you
  can select and copy from (handy for pasting into Google Scholar). Works with
  linked citations, and makes a decent guess for unlinked ones like `[12]`,
  `Ref. 12`, superscripts and `Smith et al. (2019)`
- text selection that works across pages, `y` to copy
- highlights in five colours, kept outside the PDF so your files are never touched
- `/` search with `n`/`N`, smartcase, matches across line breaks
- fuzzy finder for PDFs with your recent files on top; reopens where you left off
- a file tree, if you want one
- themes (ember, gruvbox, your own) that recolour the page itself, plus
  `Ctrl-r` to flip back to plain black-on-white when someone looks over your
  shoulder and asks why the paper is brown
- double-page mode, minimal status bar, `Ctrl-h` for a cheatsheet

## Building

You need a C++20 compiler, CMake, Qt 6 and MuPDF (with its pkg-config file).
On Fedora:

```sh
sudo dnf install gcc-c++ cmake qt6-qtbase-devel mupdf-devel
cmake -S . -B build
cmake --build build -j4
./build/folio some-paper.pdf
```

I've only built it on Fedora 44 (Qt 6.11, MuPDF 1.28). Older MuPDF versions may
not compile. If you get it working elsewhere, tell me what you had to change.

## Installing

```sh
sudo cmake --install build                  # into /usr/local
# or, without sudo:
cmake --install build --prefix ~/.local     # make sure ~/.local/bin is on your PATH
```

That installs the binary, a desktop entry and an icon, so it shows up in your
launcher. To make it the default PDF viewer:

```sh
xdg-mime default folio.desktop application/pdf
```

To uninstall, `xargs rm < build/install_manifest.txt` (with `sudo` if you
installed system-wide).

## Using it

`Ctrl-h` shows every keybinding. The ones you'll want first:

| key | |
|---|---|
| `o` | open a PDF (recents first, then fuzzy search everything below the current folder) |
| `t` | file tree |
| right-click | preview a citation |
| `a`, `2a` | highlight the selection (in colour 2) |
| `/` `n` `N` | search |
| `T` / `C` | pick a theme / change page colours |
| `Ctrl-r` | black on white ↔ theme |
| `d` | double-page mode |

Started from a terminal, the file tree and finder are rooted in the current
directory; from a launcher, in your home folder.

## Where things live

- `~/.config/folio/config.ini` – settings (theme, status bar, custom page colours)
- `~/.config/folio/themes/*.ini` – your own themes
- `~/.local/share/folio/` – recent files and highlights
- `~/.cache/folio/` – the list of PDFs the finder searches, safe to delete

A theme is just a few colours on top of an existing one:

```ini
# ~/.config/folio/themes/my-gruvbox.ini
base  = gruvbox-dark
paper = #1d2021   ; page background
ink   = #fbf1c7   ; page text
```

Other keys: `canvas`, `surface`, `border`, `fg`, `muted`, `accent`.

## Known rough edges

- pages render on the main thread, so very heavy scanned pages can stutter
- the first search in a document reads every page (about 1 ms each)
- the unlinked-citation matching is heuristic and will miss odd reference styles
- highlights don't show up in other PDF viewers (no export yet)
- only tested on Linux, X11 (bspwm)

## License

AGPL-3.0-or-later, see [LICENSE](LICENSE). That's not really a choice: folio
links against [MuPDF](https://mupdf.com), which is AGPL, so folio has to be too.

It also uses [Qt](https://www.qt.io) (LGPL-3.0). The built-in colour schemes use
the palettes of [Ember](https://github.com/ember-theme) and
[Gruvbox](https://github.com/morhetz/gruvbox), both MIT-licensed. Thanks to
their authors.
