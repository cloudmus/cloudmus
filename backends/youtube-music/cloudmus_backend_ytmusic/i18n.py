"""This backend's translator; the server switches its language."""
from rpc_common.i18n import Translator

from .locales import CATALOGS

translator = Translator(CATALOGS)
tr = translator.tr
