#!/usr/bin/env python3
"""Fill the mobile .ts files from the desktop's, so a text that is the same
on both reads the same on both.

For each unfinished message in fronts/mobile/translations/cloudmus_<code>.ts,
takes the translation of the same source text from
fronts/desktop/translations/cloudmus_<code>.ts (any context; the desktop's own
context wins), then from tools/mobile-only-translations.json for what the
desktop has no text for. Run after `cmake --build build --target
cloudmus-mobile-update-translations`; messages already translated are kept.
"""
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parents[2]
mobile_dir = root / "mobile" / "translations"
desktop_dir = root / "qt" / "translations"
extra_file = Path(__file__).with_name("mobile-only-translations.json")
extra = json.loads(extra_file.read_text(encoding="utf-8")) if extra_file.exists() else {}

missing = {}
for ts in sorted(mobile_dir.glob("cloudmus_*.ts")):
    code = ts.stem.split("_", 1)[1]
    desktop = desktop_dir / ts.name
    by_context, by_source = {}, {}
    if desktop.exists():
        for ctx in ET.parse(desktop).getroot().iter("context"):
            name = ctx.findtext("name")
            for msg in ctx.iter("message"):
                tr = msg.find("translation")
                if tr is None or tr.get("type") == "unfinished" or not (tr.text or "").strip():
                    continue
                source = msg.findtext("source")
                by_context[(name, source)] = tr.text
                by_source.setdefault(source, tr.text)
    tree = ET.parse(ts)
    for ctx in tree.getroot().iter("context"):
        name = ctx.findtext("name")
        for msg in ctx.iter("message"):
            tr = msg.find("translation")
            if tr is None or tr.get("type") != "unfinished":
                continue
            source = msg.findtext("source")
            text = by_context.get((name, source)) or by_source.get(source) or extra.get(source, {}).get(code)
            if text:
                tr.text = text
                tr.attrib.pop("type", None)
            else:
                missing.setdefault(source, []).append(code)
    ET.indent(tree)
    tree.write(ts, encoding="utf-8", xml_declaration=True)
    # Qt's own header, which ElementTree would not write.
    body = ts.read_text(encoding="utf-8").replace(
        "<?xml version='1.0' encoding='utf-8'?>",
        '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>', 1)
    ts.write_text(body, encoding="utf-8")

json.dump(missing, sys.stdout, ensure_ascii=False, indent=1)
print()
