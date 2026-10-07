# autocomplete — agent notes

Carapace-like bash completion engine in C. Target: macOS stock `/bin/bash` 3.2 + Apple clang. Not a git repo.

## Docs policy
- This file is for agents only: constraints, verification steps, gotchas. Keep it terse.
- Never add human-facing content here (usage, install steps, tutorials, examples for users). That goes in `README.md` (overview, install, quick start) or `docs/` (`writing-specs.md` spec format, `cache.md`, `troubleshooting.md`, `how-it-works.md`).
- When a change affects users (new spec, new command, install behaviour, schema change), update the matching README/docs page; update this file only if agent-relevant facts changed.

## Files
- `autocomplete.c` — engine: spec dir lookup, line tokenizer (`split`), spec walk (`do_complete`), candidate output (`emit`), built-in completers (`complete_files`, `complete_make_targets`), `bash`/`list`/`check`/`_complete` subcommands.
- `yaml.c`/`yaml.h` — own YAML-subset parser → `ynode` tree (`Y_NULL/BOOL/STR/ARR/OBJ`). No numbers: non-null/bool scalars are strings. No anchors, tags, `|`/`>` scalars. Tabs in indent = error.
- `manparse.c`/`.h` — `man_spec(cmd)`: `MANWIDTH=1000 man -P cat | col -bx` → spec tree. Option entries = lines at the most common indent of entry-shaped lines; value from entry arg or SYNOPSIS `[-x arg]`. Heuristic; curl examples leak a few bogus flags.
- `autocomplete gen [-f] CMD` writes `~/.config/autocomplete/CMD.yaml` via `yaml_write` (single-quotes anything ambiguous; `-1`-style keys must be quoted or PyYAML reads ints). `man.list` (any spec dir, one name per line) → live `man_spec` fallback when no YAML, cached under src `man:<cmd>`, never auto-invalidated; also registered by `bash`/`list`. `autocomplete register CMD...` appends to `~/.config/autocomplete/man.list` (skips dupes/invalid names).
- `specs/*.yaml` — bundled specs (docker, make, git, autocomplete). Filename = command name.
- `Makefile` — `make`, `make install` (also appends `eval "$(autocomplete bash)"` to `$(PROFILE)` once, adding a newline first if missing), `make uninstall` (removes only bundled specs + exact init line), `clean`. `PREFIX`, `PROFILE` overridable.
- `carapace` — upstream reference snippet only; not used.

## Compatibility
- Pre-release, no users yet: do NOT keep backward compatibility. Change the spec format, CLI, cache format etc. to the cleanest design and migrate the bundled specs and docs in the same change. No legacy fallbacks, aliases or dual syntaxes.

## Hard constraints
- bash 3.2: no `compopt`, `mapfile`, `${arr[-1]}`, assoc arrays; `source <(...)` silently does nothing (use `eval "$(...)"`). Init script must stay 3.2-compatible.
- Function registered with `complete -o nospace`; the engine appends a trailing space to "finished" candidates. Dirs and `nospace: true` exec values get no space. Keep this convention in any new completer (`emit(cand, space)`).
- `emit`/`emit_desc` shell-escape, filter by prefix of `g_cur`, de-duplicate, trim to bash's current word using `COMP_WORDBREAKS` (needed for `image:tag`, `--flag=value`), and queue into `g_cands`; `flush()` (called once in `main` after `do_complete`) prints. Never `printf` candidates directly.
- Descriptions (`description:` on a command or flag spec): `flush` prints `name  -- desc` lines padded to > half terminal width (one per line) only when ≥2 candidates and any has a desc; a single candidate is always bare. Correctness relies on bash inserting only the common prefix of ambiguous matches; never let two candidates share text past the name.
- No colors: bash 3.2 readline prints ESC as literal `^[` in the match list (verified).
- C only, no external deps, `-Wall -Wextra` must stay warning-free.
- Spec format is YAML only (JSON support was removed deliberately; don't re-add).

## Spec schema
`{description?, flags: {name: {value?, description?}}, args: value, commands: {name: spec}}`. A flag without `value` takes no value (`find_flag` returns the inner `value`). Value: null/empty (nothing), `any`, `file`, `dir`, `{exec, suffix?, nospace?}` (popen, one candidate per line, `<none>` lines skipped, stderr dropped), `"@subcommands"` (parent's commands), `"@make-targets"` (reads `-f/--file/--makefile`, `-C/--directory` from flag values seen on the line; target description = `## text` after the deps, else the contiguous `#` comment block directly above), or a list (union). Root flags are matched at every depth. Lookup dirs: `$AUTOCOMPLETE_SPECS` (colon list) → `~/.config/autocomplete` → `<bin dir>/specs` → `<bin dir>/../share/autocomplete`; first `<cmd>.yaml|.yml` wins.

## Verify after changes
- `make` — zero warnings.
- Engine directly: `./autocomplete _complete "docker run --network " "$COMP_WORDBREAKS_VALUE"` (default wordbreaks `"'@><=;|&(:` if 2nd arg omitted). One candidate per line.
- Through bash 3.2: `/bin/bash --norc -c 'eval "$(./autocomplete bash)"; COMP_LINE="git sw"; COMP_POINT=6; _autocomplete; printf "<%s>\n" "${COMPREPLY[@]}"'`.
- Parser: `./autocomplete check FILE` prints parsed tree as JSON; compare with PyYAML (available): `json.loads(check_output(['./autocomplete','check',f])) == yaml.safe_load(open(f))`. Do this for every spec and any parser change.
- Before refactors, snapshot `_complete` outputs for a case list and diff after. git cases need a throwaway repo (put it in the scratchpad).
- Test install/uninstall only with `PREFIX=<scratch> PROFILE=<scratch file>`; never touch the real `~/.bash_profile` or `/usr/local`.
- Docker values need a running daemon; empty output without one is expected, not a bug.

- Interactive display (what the user actually sees on TAB/listing) can only be checked in a pty: fork `/bin/bash --rcfile <rc> -i` via Python `pty`, set a window size with `TIOCSWINSZ` (size 0x0 suppresses listings), send text + `\t`, then `\x1b?` (possible-completions) to get the list. A second `\t` in the harness didn't trigger the list.

## Gotchas
- The harness safety check rejects `bash -c '...'` scripts containing ` rm` (e.g. a test line `git remote rm`); run such cases outside `bash -c`.
- `print_bash` embeds the absolute path of the binary when invoked with a path, bare name otherwise (resolved via PATH at TAB time).
- `exec` commands run in the shell's cwd, not in `git -C`/`make -C` dirs (only `@make-targets` honours `-C`).
