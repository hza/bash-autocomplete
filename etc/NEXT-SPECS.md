# Next specs

Open work on bundled specs. Every spec candidate from the earlier list now exists.

## Snippets still missing
Root `snippets:` for gradle, make, mvn, cargo, go, java, git, gh, docker (docker has
snippets only under the prune/compose subcommands). Check that `X <TAB>` still lists
subcommands afterwards (git: in a throwaway repo, `alias_exec`).

Deliberately without snippets: sudo, env, time, xargs, caffeinate, do, then, else, elif,
help. Their args are `@command`, and snippets would hide the command list on an empty word.

## Verify on a machine that has the tool
Written from knowledge, never run live here:
- **ffmpeg:** awk parsing of `-encoders`/`-filters`/`-pix_fmts` in `tools/ffmpeg.sh`.
- **mas:** subcommand set varies by version (`get`, `lucky`, `config`); `mas list` sed.
- **fastlane:** built-in commands and flags; `--env` values untested.
- **colima:** profile lookup assumes `colima list --json` prints one `"name":"…"` per line.
- **orbctl**, **pyenv**, **rbenv**, **asdf** (0.16+ syntax), **nvm** (a shell function: must
  exist when `autocomplete bash` registers names).
- **shortcuts run:** printed only flags here (probably no shortcuts on this Mac).

## Fixes found along the way
- **Snippets starting with `sudo`** are listed but never inserted: `@key` needs the command to
  start with the spec name. This affects pmset, security, softwareupdate, scutil, mdutil, spctl,
  mas and others. Either drop `sudo` (and say "(sudo)" in the description, as fs_usage does)
  or teach `add_snippets` to accept a `sudo ` prefix.
- **Unquoted `cache: N`** in about 19 older specs (xcrun, gcloud, psql, mysql, …) fails the
  PyYAML comparison: write `cache: '300'`.
- **`tools/gen_npm.py`** is no longer idempotent: on the committed npm.yaml it drops hand-set
  `show: always` lines and reorders some flags (npm 11.19.1 here). It now keeps `snippets`.
- **pkill `-l`:** the spec says "List signal names"; on macOS it may mean long output. Check
  `man pkill`.

## Engine ideas
- **Value from another word on the line:** xattr names from the file argument, plutil `-extract`
  keypaths. This needs a generic `{exec, ...}` placeholder such as `$1`/`$FILE` for earlier
  positional words. `specs/lang/asdf.sh` gets by today by parsing `$COMP_LINE` itself.
- **Shared exec snippets:** the app-bundle-id exec is copied in open, screencapture and tccutil.
  Anchors aren't supported; a builtin `@app-bundle-ids` would remove the copies.
- **Per-line cache key:** `cache:` keys on exec, cwd and cur, not the line, so `asdf install
  <plugin>` can't cache versions per plugin.
