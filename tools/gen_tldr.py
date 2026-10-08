#!/usr/bin/env python3
"""Mark flags used in tldr examples as `show: always` in bundled specs.

    python3 tools/gen_tldr.py [--zip PAGES.zip] specs/*.yaml

Reads the English tldr pages archive (downloaded once to
~/.cache/autocomplete/tldr-pages.en.zip unless --zip is given). For a spec
CMD.yaml it reads pages CMD.md and CMD-SUB.md (common/ and osx/), takes the
flags from every example command line (`[-L|--location]` and bare `--key`),
descends into subcommands named on the line, and adds `show: always` to the
matching flags. The file is edited in place line by line, so hand-written
formatting is kept. Only adds; rerunning gives no diff.

A spec without a top-level `examples:` also gets one from page CMD.md
(common/ preferred over osx/): each example's command line, with `{{…}}`
placeholders unwrapped (`[-L|--location]` and `GET|PUT|…` keep the first
choice), and its description (first sentence, backticks dropped). Existing
examples are never touched, so they can be edited by hand.
"""
import os
import re
import sys
import urllib.request
import zipfile

URL = "https://github.com/tldr-pages/tldr/releases/latest/download/tldr-pages.en.zip"
CACHE = os.path.expanduser("~/.cache/autocomplete/tldr-pages.en.zip")

import json
import subprocess

ENGINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "autocomplete")


def pages(zpath):
    z = zipfile.ZipFile(zpath)
    out = {}
    for name in z.namelist():
        m = re.match(r"(?:pages[^/]*/)?(common|osx)/([^/]+)\.md$", name)
        if m:
            out.setdefault(m.group(2), []).append(z.read(name).decode("utf-8", "replace"))
    return out


def page_text(zpath, cmd):
    """Text of page CMD.md, common/ before osx/, or None."""
    z = zipfile.ZipFile(zpath)
    for plat in ("common", "osx"):
        for name in z.namelist():
            if re.match(r"(?:pages[^/]*/)?%s/%s\.md$" % (plat, re.escape(cmd)), name):
                return z.read(name).decode("utf-8", "replace")
    return None


def placeholder(m):
    """{{[-L|--location]}} -> -L, {{GET|PUT|...}} -> GET, {{x}} -> x"""
    t = m.group(1)
    if t.startswith("[") and t.endswith("]"):
        t = t[1:-1]
    return t.split("|")[0] if "|" in t else t


def examples(text):
    """[(command, description)] of a tldr page."""
    out = []
    for m in re.finditer(r"^- (.+?)\n\n`([^`]+)`\s*$", text, re.M):
        desc = m.group(1).strip().rstrip(":").replace("`", "")
        desc = re.split(r"(?<=[^.])\.\s", desc)[0].rstrip(".")
        out.append((re.sub(r"\{\{(.*?)\}\}", placeholder, m.group(2)), desc))
    return out


def yq(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def add_examples(text, exs):
    """Append an examples: block unless the spec has one."""
    if not exs or re.search(r"^examples\s*:", text, re.M):
        return text, False
    block = ["examples:"]
    for c, d in exs:
        block += ["  - command: " + yq(c), "    description: " + yq(d)]
    return text.rstrip("\n") + "\n" + "\n".join(block) + "\n", True


def example_lines(text):
    return [m.group(1).replace("{{", "").replace("}}", "") for m in re.finditer(r"^`([^`]+)`\s*$", text, re.M)]


def words(line):
    """Words of an example line; [-a|--bee] counts as each alternative."""
    out = []
    for m in re.finditer(r"\[([^\]]*)\]|(\S+)", line):
        if m.group(1) is not None:
            alts = m.group(1).split("|")
            if all(a.startswith("-") for a in alts):
                out.append(alts)
                continue
            out.append([m.group(0)])
        else:
            out.append([m.group(2)])
    return out


def used_flags(spec, cmd, lines):
    """{path tuple: set(flag)}; path = command names below the root."""
    used = {}
    for line in lines:
        ws = words(line)
        if not ws or ws[0][0] != cmd:
            continue
        node, path = spec, ()
        for alts in ws[1:]:
            w = alts[0]
            if not w.startswith("-"):
                sub = (node.get("commands") or {}).get(w) if isinstance(node, dict) else None
                if isinstance(sub, dict) and not any(isinstance(a, str) and a.startswith("-") for a in alts[1:]):
                    node, path = sub, path + (w,)
                continue
            for a in alts:
                name = a.split("=", 1)[0]
                if re.match(r"^--?[A-Za-z0-9?@#][\w.@#?-]*$", name):
                    used.setdefault(path, set()).add(name)
    return used


def targets(spec, used):
    """[(path to the flags map owner, flag)] that exist in the spec."""
    out = set()
    root_flags = spec.get("flags") or {}
    for path, flags in used.items():
        node = spec
        for p in path:
            node = node["commands"][p]
        own = node.get("flags") or {}
        for f in flags:
            if f in own:
                out.add((path, f))
            elif f in root_flags:
                out.add(((), f))
    return out


KEY = re.compile(r"^(\s*)(?:\"((?:[^\"\\]|\\.)*)\"|'((?:[^']|'')*)'|([^\s:#][^:]*?))\s*:(?:\s+(.*))?$")


def edit(text, want):
    """Add show: always to flags at (path, flag) in want; keep formatting."""
    lines = text.split("\n")
    stack = []  # (indent, key)
    i = 0
    changed = False
    while i < len(lines):
        line = lines[i]
        m = KEY.match(line)
        if not m or line.lstrip().startswith("#") or line.lstrip().startswith("- "):
            i += 1
            continue
        ind = len(m.group(1))
        key = m.group(2) if m.group(2) is not None else m.group(3) if m.group(3) is not None else m.group(4)
        if m.group(3) is not None:
            key = key.replace("''", "'")
        rest = (m.group(5) or "").strip()
        while stack and stack[-1][0] >= ind:
            stack.pop()
        keys = [k for _, k in stack]
        stack.append((ind, key))
        # keys == [..commands, SUB.., "flags"]
        if keys and keys[-1] == "flags" and len(keys) % 2 == 1:
            path = tuple(keys[1:-1:2])
            if all(k == "commands" for k in keys[0:-1:2]) and (path, key) in want:
                if rest.startswith("{") and rest.endswith("}"):
                    if not re.search(r"\bshow\s*:", rest):
                        inner = rest[1:-1].strip()
                        new = "{" + (inner + ", " if inner else "") + "show: always}"
                        lines[i] = line[: line.rindex(rest)] + new + line[line.rindex(rest) + len(rest):]
                        changed = True
                elif rest in ("", "{}"):
                    if rest == "{}":
                        lines[i] = line.replace("{}", "{show: always}", 1)
                        changed = True
                    else:
                        j, child = i + 1, None
                        while j < len(lines):
                            s = lines[j]
                            if s.strip() and not s.lstrip().startswith("#"):
                                ci = len(s) - len(s.lstrip())
                                if ci > ind:
                                    child = ci
                                    if re.match(r"\s*show\s*:", s):
                                        child = -1
                                        break
                                    j += 1
                                    continue
                                break
                            j += 1
                        if child is None:
                            lines[i] = line.rstrip() + " {show: always}"
                            changed = True
                        elif child > 0:
                            lines.insert(i + 1, " " * child + "show: always")
                            changed = True
                else:
                    print(f"  skip {key}: unsupported value {rest!r}", file=sys.stderr)
        i += 1
    return "\n".join(lines), changed


def main(argv):
    zpath = None
    if argv[:1] == ["--zip"]:
        zpath, argv = argv[1], argv[2:]
    if not argv:
        sys.exit(__doc__)
    if not zpath:
        zpath = CACHE
        if not os.path.exists(zpath):
            os.makedirs(os.path.dirname(zpath), exist_ok=True)
            print(f"downloading {URL}", file=sys.stderr)
            urllib.request.urlretrieve(URL, zpath)
    allp = pages(zpath)
    for f in argv:
        if os.path.islink(f):
            continue
        cmd = os.path.basename(f).rsplit(".", 1)[0]
        text = open(f).read()
        spec = json.loads(subprocess.check_output([ENGINE, "check", f])) or {}
        lines = []
        for name, texts in allp.items():
            if name == cmd or name.startswith(cmd + "-"):
                for t in texts:
                    ls = example_lines(t)
                    if name != cmd:  # "git-commit" pages show "git commit ..."
                        ls = [l for l in ls if l.split()[:1] == [cmd]]
                    lines += ls
        want = targets(spec, used_flags(spec, cmd, lines))
        new, changed = edit(text, want)
        page = page_text(zpath, cmd)
        new, added = add_examples(new, examples(page) if page else [])
        changed = changed or added
        if changed:
            open(f, "w").write(new)
        print(f"{f}: {len(want)} common flags{', examples added' if added else ''}{' (updated)' if changed else ''}", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
