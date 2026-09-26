"""以简体中文为源验证 UI 语言包。 / Validate UI packs against Simplified Chinese."""
import argparse
import json
from pathlib import Path
import re
import sys

LOCALE = re.compile(r"[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*")
TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def placeholders(text):
    result = set()
    offset = 0
    while offset < len(text):
        if text[offset] == "}":
            raise ValueError("Unmatched closing brace")
        if text[offset] != "{":
            offset += 1
            continue
        end = text.find("}", offset)
        name = text[offset + 1:end] if end >= 0 else ""
        if not TOKEN.fullmatch(name):
            raise ValueError("Invalid placeholder or unmatched opening brace")
        result.add(name)
        offset = end + 1
    return result


def load_pack(path):
    data = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    if not isinstance(data, dict) or not isinstance(data.get("_meta"), dict):
        raise ValueError("Expected object and _meta object")
    meta = data["_meta"]
    for key in ("locale", "name", "nameEnglish"):
        if not isinstance(meta.get(key), str) or not meta[key]:
            raise ValueError(f"Invalid _meta.{key}")
        meta[key].encode("utf-8", errors="strict")
    if not LOCALE.fullmatch(meta["locale"]):
        raise ValueError("Invalid locale identifier")
    for key, value in data.items():
        if key == "_meta":
            continue
        if not key or not isinstance(value, str) or not value:
            raise ValueError(f"Expected nonempty string: {key}")
        value.encode("utf-8", errors="strict")  # 拒绝孤立代理项。 / Reject lone surrogates.
        placeholders(value)
    return meta, {key: value for key, value in data.items() if key != "_meta"}


def validate(directory):
    errors, messages, packs, seen = [], [], {}, set()
    for path in sorted(directory.glob("*.json")):
        try:
            meta, strings = load_pack(path)
            locale = meta["locale"]
            if locale in seen:
                errors.append(f"ERROR {path.name}: duplicate locale {locale}")
            seen.add(locale)
            if path.stem != locale:
                errors.append(f"ERROR {path.name}: filename must match _meta.locale")
            packs[path.name] = strings
        except (OSError, ValueError, UnicodeError) as exc:
            errors.append(f"ERROR {path.name}: {exc}")
    source = packs.get("zh-CN.json")
    if source is None or not source:
        errors.append("ERROR zh-CN.json: canonical source missing, empty or invalid")
        return errors, messages
    for filename, strings in packs.items():
        missing, unknown = source.keys() - strings.keys(), strings.keys() - source.keys()
        if missing:
            errors.append(f"ERROR {filename}\nMissing keys:\n  " + "\n  ".join(sorted(missing)))
        if unknown:
            errors.append(f"ERROR {filename}\nUnknown keys:\n  " + "\n  ".join(sorted(unknown)))
        for key in source.keys() & strings.keys():
            expected, actual = placeholders(source[key]), placeholders(strings[key])
            if expected != actual:
                errors.append(f"ERROR {filename}\nPlaceholder mismatch: {key}\n"
                              f"  source: {sorted(expected)}\n  translation: {sorted(actual)}")
        messages.append(f"{filename}: {len(source.keys() & strings.keys())} / {len(source)} keys")
    return errors, messages


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[2] / "assets" / "i18n")
    args = parser.parse_args()
    errors, messages = validate(args.directory)
    print("\n".join(messages + errors))
    if not errors:
        print("PASS: all locales and placeholders valid (source: zh-CN)")
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())
