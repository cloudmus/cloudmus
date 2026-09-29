# Developing for Windows

Everything Windows-specific is built and tried out from Linux: a Docker image
cross-builds with the official Qt MinGW toolchain (Windows CMake under Wine),
and the host's Wine runs the result. Neither replaces a real Windows machine
for how things look (see [What Wine can't tell](#what-wine-cant-tell)).

Windows-only code sits behind `Q_OS_WIN`, or in `*Win.cpp` files listed under
`if(WIN32)` in `fronts/qt/CMakeLists.txt`. A Linux build doesn't compile any
of it, so passing Linux tests says nothing about it: build for Windows too.

## Building

The full installer, once and for packaging changes (slow: it also bundles
Python and the backends):

```bash
./build-windows.sh     # -> dist/CloudMus-x86_64-Setup.exe, dist/windows-stage/
```

After that, to check that a change compiles and links for Windows, rebuild
just the exe, incrementally, in the same image:

```bash
docker run --rm -v "$PWD":/workspace:z -w /workspace \
    cloudmus-windows-builder bash packaging/windows/build-exe-in-docker.sh
# -> build-windows/bin/cloudmus-qt.exe
```

- `build-windows/` and `dist/windows-stage/` belong to root (made inside the
  container). Don't write into them from the host; put your own scripts and
  scratch files elsewhere.
- Only one build at a time: both commands share `build-windows/`. Don't start
  one while someone else's `./build-windows.sh` is running.
- Don't edit sources while a build runs: Ninja can pick up a half-applied
  change, and the link fails with confusing undefined references.

## Running under Wine

```bash
packaging/windows/run-in-wine.sh                  # a window on your display
packaging/windows/run-in-wine.sh --shot out.png   # headless: Xvfb, one screenshot
```

It runs a copy of `dist/windows-stage/`, with `build-windows/bin/cloudmus-qt.exe`
laid over it when that one is newer. The Wine prefix is its own
(`$CLOUDMUS_WINE_DIR`, default `/tmp/cloudmus-wine`), so `~/.wine` isn't
touched. The debug log is on. Headless mode waits `SHOT_DELAY` seconds (default
25) before the screenshot, and then kills Wine and Xvfb.

Under Wine the app really works: the backends start from the bundled Python,
the local-folder backend sees `~/Music`, and the sign-in flows start. Keep
that in mind: a run can start OAuth device flows against real services.

Windows settings the app reads come from the prefix's registry, set with
`WINEPREFIX=… wine reg add …` before a run (or by editing
`$CLOUDMUS_WINE_DIR/prefix/user.reg`). For example, ClearType, which the app
follows for text smoothing:

```bash
for kv in "FontSmoothing REG_SZ 2" "FontSmoothingType REG_DWORD 2" \
          "FontSmoothingOrientation REG_DWORD 1"; do
    set -- $kv
    WINEPREFIX=/tmp/cloudmus-wine/prefix wine reg add 'HKCU\Control Panel\Desktop' \
        /v "$1" /t "$2" /d "$3" /f
done
```

With it on, zoomed-in glyph edges in a screenshot show color fringes; with
`FontSmoothingType` 1 (grayscale) they don't.

## What Wine can't tell

Wine checks that the Windows build starts, runs and lays out correctly. It
doesn't reproduce Windows' own rendering and shell, so check these on a real
Windows 10/11 and say so when handing a change over:

- how fonts look: DirectWrite and FreeType look the same under Wine
  (whether ClearType is on does show, see above);
- DWM: glass (acrylic, blur), window cloaking, the dark title bar;
- tray icons, tray balloons and their sound, the taskbar's color scheme;
- fractional display scaling (125%, 150%).
