from yandex_music import Client

from . import config


def login() -> str:
    """Device Auth Flow: shows a code, waits for confirmation on the Yandex page, saves the token."""
    client = Client()

    def on_code(code):
        print("Open this link in any browser (a phone works too) and enter the code:")
        print(f"  {code.verification_url}")
        print(f"  Code: {code.user_code}")
        print("Waiting for confirmation...")

    token = client.device_auth(on_code=on_code)
    config.set_token(token.access_token)
    print("Done, token saved to", config.CONFIG_FILE)
    return token.access_token
