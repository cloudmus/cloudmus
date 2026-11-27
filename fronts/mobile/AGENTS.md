# Agent instructions — mobile front (`fronts/mobile/`)

On top of the root `AGENTS.md`; before touching `core/`, read `core/AGENTS.md`.
Building, testing and running on a device: `docs/android.md`.

## Pictures

- A picture drawn by an image provider is loaded with `asynchronous: true`:
  drawn on the UI thread it holds every frame meanwhile.
- `sourceSize` is in the screen's logical pixels, Qt multiplies it by the
  density itself; and keep it off the layout's changing sizes, or the picture is
  drawn anew at each of them.

## Animation

Nothing on a page jumps: a panel — a card, a settings cell, a block of
content — that appears, disappears or changes size does it with an animation,
and the rest of the page moves with it.

- Make a block that comes, goes or grows a `Panel` (`qml/Panel.qml`;
  `color: "transparent"` and `padding: 0` for one that isn't a card). The
  setting cells, rows and groups are panels already.
- Show and hide it with `shown`, never `visible`. It drives `visible`,
  `opacity`, `implicitHeight`, `clip` and `Layout.bottomMargin` itself, so
  don't set them; nor `anchors.fill` its content.
- A panel of panels sets `smoothResize: false`: two animations of one height
  lag behind each other.
- `animated: false` only while the page changes as a whole with a transition of
  its own (the Library switching sources). First layout and screen rotation
  are already left unanimated.
- An item that can't be a `Panel` uses `qml/PanelMotion.qml`.
- What's laid over a page (sheet, popup, bar, full screen) animates both in
  and out.
- A test that expects a panel gone waits for it (`QTRY_VERIFY`).
