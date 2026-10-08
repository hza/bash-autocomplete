# Troubleshooting

| Problem | What to check |
|---|---|
| Nothing happens on TAB | `complete -p docker` should print `complete -o nospace -C '/path/to/autocomplete _complete' docker`. If not, the `eval` line didn't run; open a new terminal or check your profile. |
| A new spec isn't picked up | Re-run `eval "$(autocomplete bash)"`; bash only registers commands at startup. |
| A spec seems to be ignored | `autocomplete check path/to/spec.yaml` shows errors with line numbers. `AUTOCOMPLETE_DEBUG=1` prints errors while completing. |
| No containers / images suggested | The Docker daemon must be running. |
| `source <(autocomplete bash)` does nothing | bash 3.2 bug; use `eval "$(autocomplete bash)"`. |

See also [Cache](cache.md): `AUTOCOMPLETE_NOCACHE=1` rules out a stale cache.
