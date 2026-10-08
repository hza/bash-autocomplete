# autocomplete — agent notes

Bash completion engine in C. Target: macOS stock `/bin/bash` 3.2 + Apple clang. Not a git repo.

## Docs policy
- This file is for agents only: constraints, verification steps, gotchas. Keep it terse.
- Never add human-facing content here (usage, install steps, tutorials, examples for users). That goes in `README.md` (overview, install, quick start) or `docs/` (`writing-specs.md` spec format, `cache.md`, `troubleshooting.md`, `how-it-works.md`).
- When a change affects users (new spec, new command, install behaviour, schema change), update the matching README/docs page; update this file only if agent-relevant facts changed.

## Files
- `autocomplete.c` — engine: spec dir lookup, line tokenizer (`split`), spec walk (`do_complete`), candidate output (`emit`), built-in completers (`complete_files`, `complete_make_targets`), `bash`/`list`/`check`/`_complete` subcommands.
- `yaml.c`/`yaml.h` — own YAML-subset parser → `ynode` tree (`Y_NULL/BOOL/STR/ARR/OBJ`). No numbers: non-null/bool scalars are strings. No anchors, tags, `|`/`>` scalars. Tabs in indent = error.
- `manparse.c`/`.h` — `man_spec(cmd)`: `MANWIDTH=1000 man -P cat | col -bx` → spec tree. Option entries = lines at the most common indent of entry-shaped lines; value from entry arg or SYNOPSIS `[-x arg]`. Heuristic; curl examples leak a few bogus flags.
- `autocomplete man [--force] CMD` writes `~/.config/autocomplete/CMD.yaml` from `man_spec` via `yaml_write` (single-quotes anything ambiguous; `-1`-style keys must be quoted or PyYAML reads ints). No live man fallback: completion always comes from YAML.
- `autocomplete alias [--force] CMD NAME...` symlinks `~/.config/autocomplete/NAME.yaml` to the realpath of CMD's spec (no alias concept in the engine; NAME is just another spec).; `alias --rm NAME...` unlinks only symlinks there; `alias --list` prints `NAME -> CMD` (target basename), also feeds `--rm` completion.
- Cache: only specs > `CACHE_MIN` (100 KB) get a `.cache` (`load_spec`, `autocomplete cache`); smaller ones are parsed from YAML every TAB.
- `specs/*.yaml` — bundled specs (~30: dev tools, macOS tools, wrappers sudo/env/time/xargs, autocomplete). Filename = command name; `pip3.yaml` is a symlink to `pip.yaml`, `python.yaml` to `python3.yaml` (its `-m` exec always runs `python3`). Untested live here (not installed): gh, tmux, yarn, cargo, gradle.
- `Makefile` — `make`, `make install` (also appends `eval "$(autocomplete bash)"` to `$(PROFILE)` once, adding a newline first if missing), warns (no edit) when PROFILE is `~/.bashrc` and the login file (first of `.bash_profile`/`.bash_login`/`.profile`) has no `.bashrc` mention, `make uninstall` (removes only bundled specs + exact init line), `clean`. `PREFIX`, `PROFILE` overridable.
- `llms.txt` (hand-written index, absolute GitHub URLs) and `llms-full.txt` (generated: `make llms-full.txt` concatenates README + docs). Rerun after any README/docs change; update `llms.txt` when docs pages are added/renamed.
- `tools/gen_npm.py` — regenerates `specs/npm.yaml` from the installed npm (`npm <cmd> --help` + npm's `docs/content/using-npm/config.md`): `python3 tools/gen_npm.py specs/npm.yaml`. Merges into the existing spec: hand-written root flags, command descriptions/`args`, flag values/descriptions win. Skips npm's typo aliases (`TYPOS`). Don't hand-edit generated flag lists in npm.yaml; change the generator or the hand-written parts and rerun (must be idempotent: rerun → no diff).

## Compatibility
- Pre-release, no users yet: do NOT keep backward compatibility. Change the spec format, CLI, cache format etc. to the cleanest design and migrate the bundled specs and docs in the same change. No legacy fallbacks, aliases or dual syntaxes.

## Hard constraints
- bash 3.2: no `compopt`, `mapfile`, `${arr[-1]}`, assoc arrays; `source <(...)` silently does nothing (use `eval "$(...)"`). Init script must stay 3.2-compatible.
- Function registered with `complete -o nospace`; the engine appends a trailing space to "finished" candidates. Dirs and `nospace: true` exec values get no space. Keep this convention in any new completer (`emit(cand, space)`).
- `emit`/`emit_desc` shell-escape, filter by prefix of `g_cur`, de-duplicate, trim to bash's current word using `COMP_WORDBREAKS` (needed for `image:tag`, `--flag=value`), and queue into `g_cands`; `flush()` (called once in `main` after `do_complete`) prints. Never `printf` candidates directly.
- Descriptions (`description:` on a command or flag spec): `flush` prints `name  -- desc` lines padded to > half terminal width (one per line) only when ≥2 candidates and any has a desc; a single candidate is always bare. Correctness relies on bash inserting only the common prefix of ambiguous matches; never let two candidates share text past the name. Each candidate has a `kind` (`g_kind` at emit time: command/file/dir/target/var, NULL for exec); if kinds differ, `flush` prefixes descriptions with a `[kind]` tag (stand-in for sections: bash 3.2 sorts the list, no `nosort`). Set `g_kind` in any new builtin completer. `collapse_vars` (start of `flush`): on empty `g_cur`, `var` candidates are replaced by one display-only `🔧 vars` candidate (the gap is U+00A0: `emit_desc` escapes plain spaces) (kind target, desc = var names) unless there are no other candidates; it can't be inserted because ≥1 other candidate shares no prefix with it. Sorts last (emoji after ASCII). Name column is padded by display width (`dwidth`: 4-byte UTF-8 = 2 cols), not bytes.
- No colors: bash 3.2 readline prints ESC as literal `^[` in the match list (verified).
- C only, no external deps, `-Wall -Wextra` must stay warning-free.
- Spec format is YAML only (JSON support was removed deliberately; don't re-add).

## Spec schema
`{description?, aliases?: [name], when?: {file, contains?}, flags: {name: {value?, description?}}, args: value, commands: {name: spec}}`. `aliases` (subcommands only) are matched like the key (`find_command`) but not listed: `emit_commands` appends `(a, b)` to the command's description and emits an alias only when `g_cur` equals it exactly. A flag without `value` takes no value (`find_flag` returns the inner `value`). Value: null/empty (nothing), `any`, `file`, `dir`, `{exec, suffix?, nospace?}` (popen, one candidate per line, `name<TAB>desc` lines give a description (copied: `emit_desc` strdups desc), `<none>` lines skipped, stderr dropped), `"@subcommands"` (parent's commands), `"@command"` (as `args`: cursor word lists `$PATH` executables (kind command; a word with `/` → files); once a non-`=` positional word is typed, `do_complete` recurses on the line from that word, so the rest uses that command's spec; no spec → files), `"@make-variables"` (`NAME=`, names `[A-Z0-9_]` only (`upper_name`), for `=`/`:=`/`::=`/`?=`/`+=`/`!=` assignments, optional `export`/`override`, no space; same desc rules and makefile lookup as targets), `"@make-targets"` (reads `-f/--file/--makefile`, `-C/--directory` from flag values seen on the line; target description = `## text` after the deps, else the contiguous `#` comment block directly above), or a list (union). `when` (subcommands only): `emit_commands` skips the command unless `file` (cwd-relative) exists and contains `contains` (plain `strstr`, no popen); `find_command` ignores it. mvn plugin goals use it on `<artifactId>X</artifactId>` in pom.xml. Root flags are matched and listed at every depth (listing: own flags then root's). A flag key ending in `=` (mvn `-Dtest=`) is emitted without a space. Lookup dirs: `$AUTOCOMPLETE_SPECS` (colon list) → `~/.config/autocomplete` → `<bin dir>/specs` → `<bin dir>/../share/autocomplete`; first `<cmd>.yaml|.yml` wins.

## Verify after changes
- `make` — zero warnings.
- Engine directly: `COMP_LINE="docker run --network " ./autocomplete _complete` (`COMP_POINT` optional, in characters; wordbreaks from env `COMP_WORDBREAKS` if exported, else default `"'@><=;|&(:`). One candidate per line.
- Through bash 3.2: registration is `complete -o nospace -C '<bin> _complete' names...` (no shell function; bash passes `COMP_LINE`/`COMP_POINT` via env, not `COMP_WORDBREAKS`), so it can only be exercised in a pty (see below).
- Parser: `./autocomplete check FILE` prints parsed tree as JSON; compare with PyYAML (available): `json.loads(check_output(['./autocomplete','check',f])) == yaml.safe_load(open(f))`. Do this for every spec and any parser change.
- Before refactors, snapshot `_complete` outputs for a case list and diff after. git cases need a throwaway repo (put it in the scratchpad).
- Test install/uninstall only with `PREFIX=<scratch> PROFILE=<scratch file>` (or `HOME=<scratch>`: the login-file check reads `$HOME`); never touch the real `~/.bash_profile` or `/usr/local`.
- Docker values need a running daemon; empty output without one is expected, not a bug.

- Interactive display (what the user actually sees on TAB/listing) can only be checked in a pty: fork `/bin/bash --rcfile <rc> -i` via Python `pty`, set a window size with `TIOCSWINSZ` (size 0x0 suppresses listings), send text + `\t`, then `\x1b?` (possible-completions) to get the list. A second `\t` in the harness didn't trigger the list.

## Gotchas
- The harness safety check rejects `bash -c '...'` scripts containing ` rm` (e.g. a test line `git remote rm`); run such cases outside `bash -c`.
- `print_bash` embeds the absolute path of the binary when invoked with a path, bare name otherwise (resolved via PATH at TAB time).
- `exec` commands run in the shell's cwd, not in `git -C`/`make -C` dirs (only `@make-targets`/`@make-variables` honour `-C`).
