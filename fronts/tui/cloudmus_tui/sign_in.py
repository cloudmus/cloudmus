"""A source's sign-in form (auth/prompt's `form` method, docs/protocol.md
§10.3)."""
from __future__ import annotations

import html
import re
from typing import Any

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import Button, Input, Label, Static, TextArea


def plain_text(message: str) -> str:
    """A source's message without its minimal HTML (docs/protocol.md §10); a
    link's address is kept beside its text."""
    message = re.sub(r"<br\s*/?>", "\n", message)
    message = re.sub(
        r'<a href="([^"]*)">(.*?)</a>',
        lambda m: m[2] if m[2] in m[1] else f"{m[2]} ({m[1]})",
        message,
    )
    return html.unescape(re.sub(r"<[^>]+>", "", message))


class SignInScreen(ModalScreen[Any]):
    """Dismissed with the values by field name, or None if cancelled."""

    CSS = """
    SignInScreen { align: center middle; }
    #sign-in { width: 80%; height: auto; max-height: 90%; border: round $accent;
               background: $surface; padding: 1 2; }
    #sign-in TextArea { height: 10; }
    #sign-in Horizontal { height: auto; }
    #sign-in Button { margin: 1 2 0 0; }
    """

    BINDINGS = [Binding("escape", "cancel", "cancel")]

    def __init__(self, source_name: str, method: dict[str, Any]):
        super().__init__()
        self._source_name = source_name
        self._method = method

    def compose(self) -> ComposeResult:
        with VerticalScroll(id="sign-in"):
            yield Label(f"{self._source_name} — sign in")
            if self._method.get("message"):
                # markup=False: the source's text may well contain brackets.
                yield Static(plain_text(self._method["message"]), markup=False)
            for index, field in enumerate(self._method["fields"]):
                caption = field.get("label") or field["name"]
                yield Label(caption, markup=False)
                if field.get("multiline"):
                    yield TextArea(id=f"field-{index}")
                else:
                    yield Input(password=field["secret"], id=f"field-{index}")
            with Horizontal():
                yield Button("Submit", variant="primary", id="submit")
                yield Button("Cancel", id="cancel")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id != "submit":
            self.dismiss(None)
            return
        values = {}
        for index, field in enumerate(self._method["fields"]):
            widget = self.query_one(f"#field-{index}")
            values[field["name"]] = widget.text if isinstance(widget, TextArea) else widget.value
        self.dismiss(values)

    def action_cancel(self) -> None:
        self.dismiss(None)
