# Troubleshooting

| Problem | What to check |
|---|---|
| Nothing happens on TAB | `complete -p docker` should print `complete -o nospace -o bashdefault -C '/path/to/autocomplete _complete' docker`. If not, the `eval` line didn't run; open a new terminal or check your profile. |
| Works in some terminals (e.g. inside `mc`) but not in new Terminal.app windows or tabs | Terminal.app starts login shells, which read `~/.bash_profile` and not `~/.bashrc` (where `make install` puts the `eval` line). Add `[ -f ~/.bashrc ] && . ~/.bashrc` to `~/.bash_profile` instead of copying the `eval` line there. |
| A new spec isn't picked up | Re-run `eval "$(autocomplete bash)"`; bash only registers commands at startup. |
| A spec seems to be ignored | `autocomplete check path/to/spec.yaml` shows errors with line numbers. `AUTOCOMPLETE_DEBUG=1` prints errors while completing. |
| A bundled spec behaves like an old version after upgrading | Old installs put specs directly in `share/autocomplete`; those files take priority over the topic folders. Delete the top-level files (`find /usr/local/share/autocomplete -maxdepth 1 -type f -delete`) and run `make install` again. |
| `NAME: no such spec (broken symlink?)` from `autocomplete cache` / `make install` | An alias points to a spec file that moved. Re-create it: `autocomplete alias --force CMD NAME`. |
| No containers / images suggested | The Docker daemon must be running. |
| List order differs between terminals (📌 snippets not first, `📌 vars` / `-📌` / `🧩` entries mixed in instead of last) | bash sorts the list using your locale. With `LANG=en_US.UTF-8` (VS Code's terminal sets it) macOS ignores emoji when sorting, so entries sort by their text; without `LANG` (C locale) they sort by bytes and emoji come last, 📌 before 🔥. Only the order changes; completion works the same. For the byte order: `export LC_COLLATE=C`. |
| `source <(autocomplete bash)` does nothing | bash 3.2 bug; use `eval "$(autocomplete bash)"`. |

See also [Cache](cache.md): `AUTOCOMPLETE_NOCACHE=1` rules out a stale cache.
