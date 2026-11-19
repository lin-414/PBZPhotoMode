# -*- coding: utf-8 -*-
"""Validate dist/PBZPhotoModeCpp/PBZPhotoModeCpp.modsmenu.json.

Checks the Mods Menu 149 descriptor contract (types, keys, defaults) and compares
defaults against config.ini.example. Run from the PBZPhotoModeCpp directory:
    Python\\bin\\python.exe tools\\check_modsmenu_descriptor.py
"""
import configparser
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DESCRIPTOR = os.path.join(ROOT, "assets", "PBZPhotoModeCpp.modsmenu.json")
CONFIG = os.path.join(ROOT, "config.ini.example")
SCHEMA_CPP = os.path.join(ROOT, "src", "pbzfm_schema_map.cpp")

TYPE_BY_FIELD_KIND = {"Bool": "toggle", "Float": "number", "Enum": "choice", "Key": "choice"}


def parse_schema_keys():
    text = open(SCHEMA_CPP, encoding="utf-8").read()
    result = {}
    for kind, key in re.findall(r"\b(Bool|Float|Enum|Key)\([^,\n]+,\s*\"([A-Za-z0-9_]+)\"", text):
        result[key] = TYPE_BY_FIELD_KIND.get(kind, "key")
    return result


def text_ok(value, limit):
    if isinstance(value, dict):
        if not isinstance(value.get("en"), str) or not value["en"].strip():
            return False
        return all(isinstance(v, str) and len(v) <= limit for v in value.values())
    return isinstance(value, str) and 0 < len(value) <= limit


def main():
    errors = []
    if not os.path.exists(DESCRIPTOR):
        print("missing", DESCRIPTOR)
        return 1
    data = json.load(open(DESCRIPTOR, encoding="utf-8"))
    schema = parse_schema_keys()

    if not text_ok(data.get("title"), 80):
        errors.append("title")
    if not text_ok(data.get("description"), 300):
        errors.append("description")
    if not isinstance(data.get("version"), str) or len(data["version"]) > 20:
        errors.append("version")

    settings = data.get("settings")
    if not isinstance(settings, list) or not (1 <= len(settings) <= 100):
        errors.append("settings array")
        settings = []

    cp = configparser.ConfigParser(interpolation=None)
    cp.read(CONFIG, encoding="utf-8")

    def ini_value(key):
        for section in cp.sections():
            if cp.has_option(section, key):
                return cp.get(section, key)
        return None

    seen = set()
    count = 0
    for index, item in enumerate(settings):
        where = "settings[%d]" % index
        kind = item.get("type")
        if kind not in {"toggle", "choice", "number", "key", "action", "heading"}:
            errors.append("%s: type %r" % (where, kind))
            continue
        if not text_ok(item.get("label"), 80):
            errors.append("%s: label" % where)
        if kind == "heading":
            continue
        key = item.get("key", "")
        if not re.fullmatch(r"[A-Za-z0-9_]{1,64}", key):
            errors.append("%s: key %r" % (where, key))
            continue
        if key in seen:
            errors.append("%s: duplicate key %s" % (where, key))
        seen.add(key)
        count += 1
        expected_type = schema.get(key)
        if expected_type is None:
            errors.append("%s: %s not in pbzfm_schema_map.cpp" % (where, key))
        elif expected_type != kind:
            errors.append("%s: %s is %s in schema, descriptor says %s" % (where, key, expected_type, kind))
        if kind == "toggle":
            if not isinstance(item.get("default"), bool):
                errors.append("%s: toggle default" % where)
            raw = ini_value(key)
            if raw is None:
                errors.append("%s: %s missing in config.ini.example" % (where, key))
            elif bool(item["default"]) != (raw.strip().lower() in {"true", "1", "on", "yes"}):
                errors.append("%s: %s default %r != ini %r" % (where, key, item["default"], raw))
        elif kind == "number":
            for field in ("min", "max", "step"):
                if not isinstance(item.get(field), (int, float)):
                    errors.append("%s: %s %s" % (where, key, field))
            if not errors and not (item["step"] > 0 and item["min"] <= item["max"]):
                errors.append("%s: %s range" % (where, key))
            default = item.get("default")
            if not isinstance(default, (int, float)):
                errors.append("%s: %s default" % (where, key))
            elif item["min"] <= default <= item["max"]:
                raw = ini_value(key)
                if raw is None:
                    errors.append("%s: %s missing in config.ini.example" % (where, key))
                elif abs(float(raw) - float(default)) > 1e-9:
                    errors.append("%s: %s default %r != ini %r" % (where, key, default, raw))
            else:
                errors.append("%s: %s default out of range" % (where, key))
        elif kind == "choice":
            options = item.get("options")
            if not isinstance(options, list) or not options or len(options) > 64:
                errors.append("%s: %s options" % (where, key))
                continue
            values = [option.get("value") for option in options]
            if item.get("default") not in values:
                errors.append("%s: %s default not an option" % (where, key))
            raw = ini_value(key)
            if raw is not None:
                raw_text = raw.strip()
                raw = raw_text.lower()
                if key.startswith("key_"):
                    # 键位 choice 存的是键名（例如 F2 / MOUSE_LEFT / OEM_4），大小写按配置为准。
                    if str(item.get("default", "")).strip().lower() != raw:
                        errors.append("%s: %s default %r != ini %r" % (where, key, item.get("default"), raw_text))
                else:
                    if key == "freeze_mode":
                        expected = "pause" if raw == "pause" else "dilation"
                    elif key == "mouse_vertical_mode":
                        expected = "look" if raw == "look" else "translate"
                    else:
                        expected = item["default"]
                    if item.get("default") != expected:
                        errors.append("%s: %s default %r != ini %r" % (where, key, item.get("default"), raw))

    print("descriptor:", DESCRIPTOR)
    print("settings:", count, "headings:", len(settings) - count, "errors:", len(errors))
    for error in errors:
        print(" -", error)
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
