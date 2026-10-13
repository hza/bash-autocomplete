"""Regenerate specs/lang/npm.yaml from the installed npm.

Usage: python3 tools/gen_npm.py specs/lang/npm.yaml

Commands, aliases, subcommands and per-command flags come from `npm <cmd> --help`;
flag descriptions from npm's docs/content/using-npm/config.md. Hand-written parts of
the existing spec win: root flags, command descriptions/args, flag values/descriptions.
"""
import re, subprocess, yaml, os, sys
spec_path = sys.argv[1]
old = yaml.safe_load(open(spec_path))
npm_root = os.path.dirname(os.path.dirname(os.path.realpath(subprocess.check_output(["which", "npm"], text=True).strip())))
cfg = open(os.path.join(npm_root, "docs/content/using-npm/config.md")).read()

def cfg_desc(name):
    m = re.search(r"^#### `%s`\n(.*?)(?=^#### |\Z)" % re.escape(name), cfg, re.M | re.S)
    if not m: return None
    paras = [p.strip() for p in m.group(1).split("\n\n") if p.strip() and not p.strip().startswith("*")]
    if not paras: return None
    t = re.sub(r"\s+", " ", paras[0]).replace("`", "")
    t = re.split(r"(?<=[a-z0-9)])\. ", t)[0].rstrip(".")
    return t if len(t) <= 80 else t[:77].rsplit(" ", 1)[0] + "..."

def run(*a):
    return subprocess.run(["npm", *a], capture_output=True, text=True).stdout

allc = run("help").split("All commands:")[1].split("\n\n")[1]
cmds = [c.strip() for c in allc.replace("\n", " ").split(",") if c.strip()]

TYPOS = {"isnt", "isnta", "isntal", "isntall", "isntall-clean", "innit", "udpate", "tst"}
WS = old["flags"]["-w"]["value"]
DEPS = old["commands"]["uninstall"]["args"]
pr = lambda *v: {"exec": "printf '%s\\n' " + " ".join(v)}

def value_for(name, arg):
    if "|" in arg: return pr(*arg.split("|"))
    if arg == "workspace-name": return WS
    if arg in ("file", "script-shell", "editor", "browser", "userconfig", "globalconfig", "cafile"): return "file"
    if arg in ("pack-destination", "prefix", "cache"): return "dir"
    return "any"

def parse_flags(opts):
    flags = {}
    # token scan: flag optionally followed by <arg> or a bare word (only --preid)
    toks = re.findall(r"(--?[A-Za-z][\w-]*)(?: <([^>]+)>| (prerelease-id))?", opts)
    for f, arg, bare in toks:
        if f in flags: continue
        name = f.lstrip("-")
        d = cfg_desc(name)
        if d is None and name.startswith("no-") and cfg_desc(name[3:]):
            d = "Disable: " + cfg_desc(name[3:])[0].lower() + cfg_desc(name[3:])[1:]
            d = d if len(d) <= 80 else d[:77].rsplit(" ", 1)[0] + "..."
        e = {}
        a = arg or bare
        if a: e["value"] = value_for(name, a)
        if d: e["description"] = d
        flags[f] = e
    # short aliases: -S|--save -> copy long's desc
    for s, l in re.findall(r"(?<![\w-])(-[A-Za-z])\|(--[\w-]+)", opts):
        flags[s] = dict(flags.get(l, {}))
    return flags

out_cmds = {}
for c in cmds:
    h = run(c, "--help")
    if not h.strip(): continue
    lines = h.splitlines()
    desc = lines[0].strip()
    m = re.search(r"^alias(?:es)?: (.*)$", h, re.M)
    aliases = [a.strip() for a in m.group(1).split(",")] if m else []
    aliases = [a for a in aliases if a not in TYPOS and (len(a) <= 2 or not c.startswith(a)) and a != c]
    usage = h.split("Usage:")[1].split("Options:")[0] if "Usage:" in h else ""
    opts = h.split("Options:")[1].split("\n\n")[0] if "Options:" in h else ""
    o = old["commands"].get(c, {})
    e = {"description": o.get("description", desc)}
    if aliases: e["aliases"] = aliases
    fl = parse_flags(opts)
    for k, v in (o.get("flags") or {}).items():  # hand-written values/descriptions win
        fl[k] = {**fl.get(k, {}), **v}
    for k in list(fl):  # global flags are matched at every depth already
        if k in old["flags"] and k not in (o.get("flags") or {}): del fl[k]
    if fl: e["flags"] = fl
    subs = []
    for u in usage.splitlines():
        mm = re.match(r"npm %s ([a-z][a-z0-9-]*)\b" % re.escape(c), u.strip())
        if mm and mm.group(1) not in subs: subs.append(mm.group(1))
    if subs and c not in ("help", "run", "exec", "init", "version", "view", "search", "install", "uninstall"):
        oc = o.get("commands", {})
        e["commands"] = {s: oc.get(s, {}) for s in subs}
        for s, v in oc.items(): e["commands"].setdefault(s, v)
    if "args" in o: e["args"] = o["args"]
    elif re.search(r"<(package-spec|pkg|package)", usage): e["args"] = "any"
    out_cmds[c] = e

# drop commands that are now aliases of another
alias_of = {a for e in out_cmds.values() for a in e.get("aliases", [])}
for a in alias_of: out_cmds.pop(a, None)
new = {"version": old["version"], "description": old["description"], "flags": old["flags"], "commands": out_cmds}
if "snippets" in old: new["snippets"] = old["snippets"]

class D(yaml.SafeDumper):
    def ignore_aliases(self, x): return True
D.add_representer(list, lambda r, x: r.represent_sequence("tag:yaml.org,2002:seq", x, flow_style=all(isinstance(i, str) for i in x)))
open(spec_path, "w").write(yaml.dump(new, Dumper=D, sort_keys=False, width=1000))
