"""Config layout for this backend, split across three files:

- config.json: {"clientId": ..., "clientSecret": ...} — a Google Cloud OAuth
  client ("TVs and Limited Input devices" type, YouTube Data API v3 enabled)
  that the user must create themselves and place here before auth.start will
  work. Unlike yandex-music, ytmusicapi's OAuth flow has no bundled default
  client as of ytmusicapi>=1.x, and the protocol's auth.start takes no params
  to carry credentials through from the front — so this one-time setup step
  necessarily happens outside the RPC protocol, by editing this file by hand.
- oauth_token.json: the actual refreshable OAuth token, in the exact shape
  ytmusicapi.auth.oauth.token.Token.as_dict() produces. auth.py hands this
  path to a RefreshingToken as its local_cache, and client.py hands the same
  path to YTMusic(auth=...) — ytmusicapi reads/writes it directly, so this
  module never needs to parse or persist token contents itself.
- browser_headers.json: the currently-active auth mechanism (see auth.py's
  BrowserAuthSession — device-code OAuth above is dormant, its every data
  call currently 400s due to a confirmed upstream YouTube-side break,
  https://github.com/sigma67/ytmusicapi/issues/676). Written directly by
  ytmusicapi.auth.browser.setup_browser() from pasted browser request
  headers; this module never parses its contents either, same reasoning as
  oauth_token.json above.
"""
import json
from pathlib import Path

CONFIG_DIR = Path.home() / ".config" / "cloudmus" / "backends" / "youtube-music"
CLIENT_FILE = CONFIG_DIR / "config.json"
TOKEN_FILE = CONFIG_DIR / "oauth_token.json"
BROWSER_HEADERS_FILE = CONFIG_DIR / "browser_headers.json"


def get_client_credentials() -> tuple[str, str] | None:
    if not CLIENT_FILE.exists():
        return None
    data = json.loads(CLIENT_FILE.read_text())
    client_id, client_secret = data.get("clientId"), data.get("clientSecret")
    if not client_id or not client_secret:
        return None
    return client_id, client_secret


def token_path() -> Path:
    """Ensures the config directory exists and returns oauth_token.json's path."""
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    return TOKEN_FILE


def has_token() -> bool:
    return TOKEN_FILE.exists()


def clear_token() -> None:
    TOKEN_FILE.unlink(missing_ok=True)


def browser_headers_path() -> Path:
    """Ensures the config directory exists and returns browser_headers.json's path."""
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    return BROWSER_HEADERS_FILE


def has_browser_auth() -> bool:
    return BROWSER_HEADERS_FILE.exists()


def clear_browser_auth() -> None:
    BROWSER_HEADERS_FILE.unlink(missing_ok=True)
