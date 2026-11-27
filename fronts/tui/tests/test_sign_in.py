import pytest
from textual.app import App
from textual.widgets import Input, TextArea

from cloudmus_tui.sign_in import SignInScreen, plain_text

METHOD = {
    "kind": "form",
    "message": 'Open <a href="https://example.com">the site</a> [signed in] &amp; paste.',
    "fields": [
        {"name": "user", "secret": False},
        {"name": "headers", "secret": False, "multiline": True},
    ],
}


def test_plain_text_keeps_a_links_address():
    assert plain_text(METHOD["message"]) == "Open the site (https://example.com) [signed in] & paste."


@pytest.mark.asyncio
async def test_submitting_gives_the_values_by_field_name():
    results = []

    class Host(App):
        def on_mount(self):
            self.push_screen(SignInScreen("Source", METHOD), results.append)

    app = Host()
    async with app.run_test(size=(100, 40)) as pilot:
        app.screen.query_one(Input).value = "me"
        app.screen.query_one(TextArea).text = "cookie: a=b\nx: y"
        await pilot.click("#submit")
        await pilot.pause()

    assert results == [{"user": "me", "headers": "cookie: a=b\nx: y"}]
