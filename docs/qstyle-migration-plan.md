# Plan: a custom QStyle for cloudmus-qt's design system

## Status

**Decided, not started.** This document exists so the analysis behind the decision survives
between sessions — implementation is deliberately deferred; do not start it without checking in
first, since it's a substantial, multi-file change to the frontend's entire visual layer.

## Context

cloudmus-qt (`fronts/qt/`) styles itself today entirely through a hand-generated global Qt
Style Sheet (QSS), built by `Theme::buildStyleSheet()` (`fronts/qt/src/Theme/StyleSheet.cpp`,
eight blocks: buttons, text buttons, sliders, scrollbar, toolbar, panels, progress bar, sidebar
tree) and applied via `Theme::applyGlobalStyleSheet()` — called once at startup and re-invoked
on every light/dark theme flip. The design tokens it draws from
(`Theme::Tokens`/`Spacing`/`Radius`/`Metrics`/`Typography`/`Icons`) are solid and out of scope
for this change; this plan is about *how the app paints itself*, not the token values
themselves.

Across several rounds of bug-fixing, three structural QSS limitations became clear and
increasingly painful to keep patching around:

1. **`QTreeView`'s branch/chevron indicator never rendered quite right through QSS**, because
   three independent, uncoordinated things all determine the same visual outcome:
   - `QTreeView::indentation()` (a view property, `Theme::Spacing::space5`, set in
     `MainWindow.cpp`) reserves the branch column's *width* — completely decoupled from the
     glyph's own pixel size.
   - The chevron glyph itself has to be a real file on disk for QSS's
     `QTreeView::branch { image: url(...); }` to reference it — `Theme::Icons::chevronAssetPath()`
     renders a tinted SVG to a PNG and caches it under the app's cache directory purely because
     QSS can't take an in-memory `QPixmap`.
   - `NavItemDelegate`'s own left-text-padding (`NavItemDelegate.cpp`, currently
     `rect.left() + Theme::Spacing::space2`) is a third, independent source contributing to the
     same "gap before the text" the user actually sees.

   Each bug report ("shows a stray blue selection sliver," "chevron erased by the hover fill,"
   "chevron looks blurry at a fractional display scale," "gap between chevron and text is too
   big and the fix didn't change anything") turned out to trace back to a *different* one of
   these three uncoordinated sources — every point fix addressed one symptom without any single
   piece of code actually owning the whole picture. See git history on `NavItemDelegate.cpp` and
   `Theme/Icons.cpp`'s `chevronAssetPath()` for the blow-by-blow.

2. **QSS cannot animate anything** — only instant `:hover`/`:pressed` state swaps. The user
   wants a smooth hover-fade on ordinary buttons, which is architecturally impossible via QSS
   alone; `NowPlayingBar.cpp`'s `IconHoverButton` and `HoverHandleSlider` classes already
   hand-roll *instant* (non-animated) hover behavior in C++ specifically because QSS has no
   transition primitive at all to reach for.

3. **Most non-custom-painted widgets are barely styled** (`QCheckBox`, `QLineEdit`,
   `QDialogButtonBox`, most of `QMenu` beyond background color) — either plain Fusion look or
   one-off QSS rules — and the user wants comprehensive coverage, plus custom-drawn splitter
   handles and macOS-style overlay scrollbars (auto-hiding, fading in/out — not the current
   fixed-width, always-visible thin bar).

**Decision**: write a custom `QStyle` for the specific pain points above, rather than continuing
to add QSS rules. Additionally, the user wants the app's accent color to be picked up from the
desktop's own accent-color preference when the platform theme supplies one, instead of always
using the design system's hardcoded coral (`#C23D16` light / `#FF6A45` dark).

**Related, already-executed decision** (see git history / commit around the same time as this
document): the AppImage build's pinned Qt version was raised from 6.5.3 to **6.9.3**
(`packaging/appimage/Dockerfile`'s `ARG QT_VERSION`) — verified against Qt's own Linux
requirements docs that official prebuilt binaries need glibc ≥2.28 through Qt 6.9 but ≥2.34 from
Qt 6.10 on, and the build's Debian 11 "bullseye" base (glibc 2.31, chosen specifically for an
old, widely-compatible floor) clears 6.9's requirement but not 6.10's. This matters here because
it means **`QPalette::Accent` (Qt 6.6+) is unconditionally available** on both the AppImage
build and any local dev build — the accent-color design below does not need a `#if QT_VERSION`
fallback path the way an earlier draft of this plan assumed.

## Scope: what moves into the QStyle vs. what stays QSS

Keep the QStyle's surface area narrow — only override what QSS is provably unable to do (needs
geometry Qt won't otherwise expose, needs animation, or needs to eliminate a file-on-disk
workaround). Re-implementing something QSS already renders correctly in `drawPrimitive()`/
`drawControl()` is pure risk for zero benefit.

**Moves into `Theme::CloudMusStyle` (a `QProxyStyle` wrapping Fusion):**

| Concern | Method(s) overridden | Why QSS can't do it |
|---|---|---|
| Icon/play button background + hover-fade animation | `drawControl(CE_PushButtonBevel)`, `polish(QWidget*)` + an event filter | Animation is impossible in QSS |
| `QTreeView` branch/chevron indicator | `drawPrimitive(PE_IndicatorBranch)` | Removes the PNG-file-cache detour entirely — paint `Theme::icon()`'s `QIcon` directly, no disk I/O, no oversampling-vs-DPI mismatch |
| Sidebar's native "current item" decoration | `drawPrimitive(PE_FrameFocusRect)` → no-op for the sidebar tree | `NavItemDelegate` already fully paints selection itself; today's QSS `selection-background-color`/`selection-color` just recolors this native decoration to blend in instead of removing the redundant paint |
| Overlay scrollbar geometry/painting | `styleHint(SH_ScrollBar_Transient)`, `pixelMetric(PM_ScrollBarExtent)`, `drawComplexControl(CC_ScrollBar)` | True overlay (zero reserved layout width, floats over content, auto-hide) needs geometry control QSS's box model can't express |

**Stays QSS** (`Theme::StyleSheet.cpp`, kept but smaller): `QMenu` background/border/selected-item
color, `QProgressBar` groove/chunk, `QToolBar` flattening, the one-off objectName rules
(`#sourceAuthCard`, `#toastLabel`, `#transportSeparator`, `#secondaryLabel`), `primary`/
`secondary` text buttons (static fill/border, no animation requested for these), `QSlider`
groove/handle colors, and new coverage for `QCheckBox`/`QLineEdit`/`QDialogButtonBox` (plain
color/border rules — no reason to route these through `drawPrimitive`).

QSS and a custom `QStyle` compose fine — QSS is evaluated by Qt's style-sheet engine, which
itself calls into whatever the *current* `QStyle` is for anything it doesn't override itself.

## Why `QProxyStyle` wrapping Fusion, not a from-scratch `QCommonStyle`

`class Theme::CloudMusStyle : public QProxyStyle`, constructed as
`new CloudMusStyle(QStyleFactory::create("fusion"))`.

The known `QProxyStyle` caveat — "no guarantee `QStyle::proxy()` is called by every internal Qt
code path" — matters most for something *deep and pervasive* that other internal style paths
call as a private helper rather than through the public virtual dispatch table. None of this
app's four target concerns are like that: `PE_IndicatorBranch`, `CE_PushButtonBevel`, and
`CC_ScrollBar`/`PM_ScrollBarExtent` are all leaf-level, polymorphically-dispatched entry points
views/widgets call directly through `style()->...()` — exactly the kind of well-isolated
override point proxy styles are designed for (arguably the textbook example, per Qt's own
"Styles" example and multiple third-party custom-QStyle writeups).

A from-scratch `QCommonStyle` would mean re-implementing (or explicitly delegating, case by
case) *every* other `drawPrimitive`/`drawControl`/`pixelMetric`/`subControlRect` switch arm just
to reach Fusion-equivalent quality for everything currently fine (`QMenu`, `QProgressBar`,
`QLineEdit`, `QCheckBox`, `QDialogButtonBox`, panels...) — strictly more code and strictly more
regression risk for zero requested benefit. `QProxyStyle::method()`'s fallthrough is Fusion's own
real implementation, so "wrap Fusion, override four things" gets Fusion-quality everything-else
for free.

## File layout (`fronts/qt/src/Theme/`, per the repo's "folder = namespace" convention)

```
Style.h / Style.cpp          Theme::CloudMusStyle : public QProxyStyle — the four
                              overrides above, polish()/unpolish() for hover-animation
                              lifecycle
StyleAnimations.h / .cpp     Shared per-widget animation state: QHash<QWidget*, ...>
                              used for BOTH button hover-fade and scrollbar
                              fade/auto-hide — one state-map pattern for the whole
                              style, not two divergent ones
AccentColor.h / .cpp         Theme::AccentColor::resolve() — system accent-color
                              resolution (see below), deliberately separate from
                              Tokens.cpp
Tokens.cpp                   No signature changes; palette()'s internals: accent/
                              accentHover/accentPressed/onAccent computed via
                              AccentColor::resolve() instead of pure hardcoded
                              literals; surface*/ink*/border* stay hardcoded
StyleSheet.cpp/.h            Stays, shrinks (see scope split above)
Icons.cpp/.h                 chevronAssetPath() and its private file cache get
                              deleted once Style.cpp paints branches directly —
                              currently only called from sidebarTreeBlock()
```

`main.cpp`'s `QApplication::setStyle(QStringLiteral("Fusion"))` becomes
`QApplication::setStyle(new Theme::CloudMusStyle(QStyleFactory::create("fusion")))`, with the
accent-color capture (below) happening before that line runs.

Everything inside `Style.cpp` consumes `Theme::palette()`/`Theme::icon()`/`Theme::Spacing`/
`Theme::Radius`/`Theme::Metrics` exactly as `StyleSheet.cpp` does today — none of those need API
changes for this migration.

## Hover-fade animation mechanism

- `StyleAnimations.cpp` owns `QHash<const QWidget*, QVariantAnimation*>`, created lazily on
  first hover, cleaned up in `unpolish()` and via `QObject::destroyed` (defensive — a stale hash
  entry pointing at a freed widget is a real crash risk during an animation tick).
- `polish(QWidget*)` sets `Qt::WA_Hover` on buttons with `variant="icon"`/`variant="play"` and
  installs an event filter (Qt widgets don't route `HoverEnter`/`HoverLeave` through the style
  directly — the style has to observe them via a filter).
- One `QVariantAnimation` per widget, a `qreal` "hover progress" in `[0, 1]`,
  `QEasingCurve::OutCubic`, ~120–150ms. `valueChanged` triggers `widget->update()`. Direction
  reverses in place on repeated enter/leave (via `setDirection()`/`setCurrentTime()`), not a
  restart-from-zero, so rapid mouse in/out doesn't glitch.
- `drawControl(CE_PushButtonBevel)` blends between rest/hover fill colors by that progress value
  for `variant="icon"`/`"play"` buttons; `:pressed` stays an instant, non-animated swap (normal
  platform convention — press feedback should never lag).
- `IconHoverButton` (`NowPlayingBar.cpp`) keeps existing, narrowed to just its glyph-recolor job
  (`applyIcon(hoverColor()/restColor())`) — background hover-fade moves to the style, but
  recoloring an SVG glyph on hover is fundamentally an icon-regeneration concern the style has no
  reasonable hook to own, so this half of the class stays. Update its class doc comment when this
  lands.
- `HoverHandleSlider`'s `handleVisible` property hack is a separate, smaller pre-existing wart —
  not one of the three headline pain points, deliberately deferred past this migration's first
  pass; `subControlRect(SC_SliderHandle)` returning an empty rect when not hovered/dragging would
  be its natural QStyle-native replacement if revisited later.

## System accent-color mechanism

`QApplication::setStyle("Fusion")` does not retroactively rewrite an already-populated
`QPalette` — platform theme integration (KDE/GTK) supplies its own initial palette during
`QGuiApplication` construction, before `setStyle()` (or `main()`'s first line after the
`QApplication` constructor) ever runs. **Capture point**: read
`qApp->palette().color(QPalette::Accent)` immediately after `QApplication app(argc, argv)` is
constructed, before `QApplication::setStyle(...)` is called.

Since the AppImage's Qt floor is now 6.9.3, `QPalette::Accent` (Qt 6.6+) is the **primary**
mechanism, unconditionally — no `#if QT_VERSION` branch needed anywhere in this design.

**Heuristic — "genuinely customized vs. Qt's own stock default"**: compare the captured accent
against a hardcoded reference for Fusion's own known stock default (a fixed `QColor` constant,
verified once against an unmodified Qt install — not derived at runtime). Within a small
tolerance (~RGB delta 8, to allow for rounding), treat a match as "nothing was actually
customized" and fall back to the design system's own coral; treat a mismatch as a genuine
platform accent and use it.

`AccentColor::resolve()` runs once at startup, before `Theme::applyGlobalStyleSheet()`/
constructing `CloudMusStyle`, producing one `QColor`. `Tokens.cpp`'s `palette(Mode)` computes
`accent`/`accentHover`/`accentPressed`/`onAccent` from that resolved color (hover/pressed = a
fixed lighten/darken step per mode; `onAccent` = a computed high-contrast black/white pick via
relative luminance), with the fallback path reproducing today's exact hardcoded coral values
bit-for-bit — zero visual change for anyone on a desktop with no accent customization.

Live updates (desktop accent changed while the app is running, no restart) are out of scope —
no reliable Qt signal exists for "the platform accent changed" independent of
`colorSchemeChanged`. Recompute once at startup only; document as a known limitation.

## `QTreeView` branch/indentation fix

- `drawPrimitive(PE_IndicatorBranch, option, painter, widget)`: when `option->state &
  State_Children`, pick glyph (`chevron_right`/`expand_more` by `State_Open`) and color (`accent`
  if `State_Selected`, else `inkSecondary` — matching today's four QSS rules exactly), call
  `Theme::icon(name, color, 12)`, paint into `option->rect`. Zero files on disk; reuses
  `Theme::Icons::icon()`'s existing in-memory, DPR-aware cache.
- The style does **not** recompute indentation math — `option->rect` is whatever Qt's own branch
  layout already computed for this row; the style only draws *within* it. `QTreeView::indentation()`
  (`MainWindow.cpp`, unchanged) stays the sole reservation-width owner.
- The actual "gap too big" bug fix is **not** a `drawPrimitive` change — it's that
  `NavItemDelegate::paint()`'s `textLeft` computation becomes the *only* remaining place
  reasoning about the gap, once the chevron is reliably exactly 12px centered in whatever column
  Qt reserves (today's bug was the *image file's* footprint disagreeing with the column, not the
  column itself). Re-verify `NavItemDelegate.cpp`'s `rect.left() + Theme::Spacing::space2` still
  gives the intended visual gap once this lands — expected to need at most a one-line tweak,
  since the three-way disagreement that caused the original bug no longer exists by construction.
- Suppress the sidebar's native "current item" decoration outright
  (`drawPrimitive(PE_FrameFocusRect)` → no-op when `widget` is the sidebar `QTreeView`) rather
  than continuing to recolor it to blend in — `NavItemDelegate` already fully owns the selection
  visual, so the native decoration is pure redundancy once suppressed cleanly.
- Cleanup: delete `Theme::Icons::chevronAssetPath()` and its private on-disk cache — confirmed
  (at the time this plan was written) to have exactly one call site, `sidebarTreeBlock()`, which
  disappears in the same change.

## macOS-style overlay scrollbar

`QStyle` methods are stateless per call — they don't have a persistent-storage contract for
"this scrollbar's fade is 40% through, started 200ms ago." Geometry and paint are the style's
job; the **auto-hide timer** is a genuinely different lifecycle concern (needs to fire on a
schedule independent of any paint/hover event) and needs real `QTimer` state living somewhere
with proper start/stop/reset semantics.

- **Geometry/reflow-avoidance**: `styleHint(SH_ScrollBar_Transient)` → `true`. This is a real,
  long-standing (pre-6.5, no version guard needed) `QStyle::StyleHint` — confirmed against Qt's
  own documentation and a real `QProxyStyle` example in Qt's docs showing exactly this override
  pattern. Qt's `QAbstractScrollArea` already implements the "don't reserve layout space, show/
  hide by activity" machinery behind this hint; it's just off by default under Fusion. This is a
  one-line override, not new plumbing to invent.
- **Paint**: `pixelMetric(PM_ScrollBarExtent)` returns a small thin width (8–10px);
  `drawComplexControl(CC_ScrollBar)` paints the thumb with token colors.
- **Fade + auto-hide**: reuse the same `QHash<QWidget*, ...>` pattern from
  `StyleAnimations.cpp` (one shared state-map idiom for the whole style), extended to also watch
  `QEvent::Wheel`/scroll-value-changed on tracked scrollbars, not just hover. No new `QScrollBar`
  subclass needed — `SH_ScrollBar_Transient` operates on the stock `QScrollBar`s `QTreeView`/
  `QListView` already create internally.

## Custom splitter handles

**Not through the `QStyle`** — through `QSplitter::createHandle()` and a small
`Ui::ThemedSplitterHandle : public QSplitterHandle` subclass instead. There are exactly two
splitters, both constructed directly in `MainWindow.cpp` — a two-line subclass swap at both call
sites is fully localized. `CC_Splitter`/`PE_IndicatorDockWidgetResizeHandle` are more
Fusion-internals-entangled than `PE_IndicatorBranch` (splitter rendering interacts with
`QSplitter`'s own resize-cursor/drag-hit-testing logic), a worse match for the `QProxyStyle`
caveat's actual risk zone, and would apply the custom look to every `QSplitter` app-wide
indiscriminately via style dispatch when there are only two known instances — no benefit over a
direct, localized subclass swap. `ThemedSplitterHandle::paintEvent()` reuses `Theme::palette()`
directly, consistent with how the item-view delegates already self-paint using theme tokens.

## Suggested implementation order

Each phase independently buildable and testable — deliberately not one giant rewrite.

1. **Skeleton, zero visual change.** `CloudMusStyle` wrapping Fusion, every method either
   omitted or trivially forwarding. Wire into `main.cpp` + `CMakeLists.txt`. Verify: app looks
   pixel-identical to before (QSS still does 100% of the work; this phase only proves the
   wrap-Fusion approach is safe here).
2. **Accent color.** `AccentColor.h/.cpp`, the `main()` capture, `Tokens.cpp`'s internals switch.
   Verify: unmodified desktop → zero visual change (coral fallback); a desktop with a customized
   accent (e.g. KDE System Settings) → picked up correctly.
3. **`QTreeView` branches + indentation.** `drawPrimitive(PE_IndicatorBranch)`, native-decoration
   suppression, remove the now-dead `chevronAssetPath()` and the corresponding QSS rules. Verify:
   chevrons correct open/closed/selected at both 100% and a fractional display scale; no stray
   selection sliver; gap-to-text matches the design mock.
4. **Button hover-fade animation.** `HoverAnimator`, `polish()`'s hover wiring,
   `drawControl(CE_PushButtonBevel)`. Verify: every icon/play button fades smoothly; press is
   still instant; rapid hover in/out doesn't glitch.
5. **Overlay scrollbars.** `styleHint`/`pixelMetric`/`drawComplexControl` + the shared timer/fade
   state. Verify: no permanent scrollbar chrome at rest; appears on scroll/hover, fades after
   idle; no content reflow when it appears/disappears; drag-to-scroll still works.
6. **Splitter handles.** `ThemedSplitterHandle`, swap both `MainWindow.cpp` call sites. Verify:
   both splitters show the new look; persisted sizes (`settings_.sidebarWidth()`/
   `heroPanelWidth()`) still save/restore correctly.
7. **Remaining QSS-only coverage** (`QCheckBox`/`QLineEdit`/`QDialogButtonBox`) — independent of
   everything above, can happen any time, including in parallel.

## Version-gating checklist

Now that the AppImage floor is Qt 6.9.3, nothing in this plan needs a `#if QT_VERSION` guard:
`QPalette::Accent` (6.6+), `SH_ScrollBar_Transient` (long-standing, pre-6.5), `QVariantAnimation`/
`Qt::WA_Hover`/`QProxyStyle`/`QStyleFactory` (Qt5+) are all unconditionally available. Re-verify
`SH_ScrollBar_Transient`'s exact availability against the Qt 6.9 changelog/headers during
implementation as a sanity check, not because it's expected to be missing.
