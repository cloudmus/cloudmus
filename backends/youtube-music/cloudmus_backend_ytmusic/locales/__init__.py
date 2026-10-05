"""Per-language dictionaries of this backend (see rpc_common.i18n)."""
from .ru import STRINGS as ru
from .fr import STRINGS as fr
from .es import STRINGS as es
from .de import STRINGS as de
from .it import STRINGS as it
from .be import STRINGS as be

CATALOGS = {"ru": ru, "fr": fr, "es": es, "de": de, "it": it, "be": be}
