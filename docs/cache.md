# Cache

Specs larger than 100 KB are compiled to `~/.cache/autocomplete/<command>.cache`
(`$XDG_CACHE_HOME/autocomplete` if set). Smaller specs are read straight from the YAML on
every TAB; at that size the cache saves nothing (see below). A cache file is used only while
it's newer than its YAML and was built from the same file; otherwise it's rebuilt
automatically on the next TAB.

- `autocomplete cache` (or `make cache`) rebuilds the cache for every big spec now.
- `AUTOCOMPLETE_NOCACHE=1` turns the cache off.
- `rm -rf ~/.cache/autocomplete` resets it.

## When it matters

For specs of normal size the cache makes no noticeable difference. Measured on a MacBook,
a TAB takes about 5 ms with or without the cache for all bundled specs (docker, git, make,
each under 10 KB). That time is mostly starting the program, and an `exec` command such as
`docker ps` usually takes far longer than that.

The cache starts to pay off only for very large specs, hence the 100 KB threshold:

| Spec size | With cache | Without cache |
|---|---|---|
| ~20 KB | ~5 ms | ~5 ms |
| ~200 KB | ~7 ms | ~9 ms |
| ~1 MB | ~17 ms | ~28 ms |

So turning it off with `AUTOCOMPLETE_NOCACHE=1` is safe if it ever gets in the way.
