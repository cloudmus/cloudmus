from .log import debug_requested, setup_debug_logging


def main() -> None:
    if debug_requested():
        setup_debug_logging()

    from .app import run

    run()


if __name__ == "__main__":
    main()
