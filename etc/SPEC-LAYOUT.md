# Spec layout and startup cost (review, 2026-10-14)

Question: should we shrink the binary, add an install-time `index.yaml`, or
switch to `a/`…`z/` folders to make spec lookup faster? No to all three.

## Measurements (Apple silicon, 300 runs each)

| case | ms/run |
|---|---|
| `/usr/bin/true` (bare process spawn) | 3.8 |
| `_complete`, `xattr ` (spec in `macos/`) | 5.6 |
| `_complete`, `zzznospec ` (miss, full recursive scan) | 5.9–6.1 |

- A hit and a full miss scan differ by run-to-run noise only: lookup is well
  under 0.1 ms (~30–40 dentry-cached syscalls over 150 specs in 10 folders).
- About 4 ms of every TAB is fork/exec + dyld, which no layout change avoids.
- Binary size (~90 KB, only `libSystem`) doesn't matter: pages are mapped on
  demand and stay in the page cache. Size starts to matter in the MB range.

## Why not an index or a–z folders

- **Index**: goes stale (manual copies, `make` from the repo, folder changes);
  checking whether it's fresh costs about as much as the scan; writing it at
  TAB time fails on read-only prefixes (Homebrew, `/usr/local`).
- **a–z in the source tree**: loses the topic grouping (`vcs/`, `cloud/`,
  `macos/`…) and separates helper scripts from related specs.
- **a–z only at install** (source keeps topics): saves ~0.05 ms, still breaks
  cross-folder helper paths (`$AUTOCOMPLETE_SPEC_DIR/../lang/java.sh`), and
  the flat install below gives the same one-lookup result more simply.

## If lookup ever shows up in a profile

Keep the topic folders in the source tree and **flatten them on install**:
`make install` / `install.sh` copy every spec into `share/autocomplete/`.
Names are already unique across folders, so lookup becomes one `access()`
call per lookup dir. Only the helper-script paths need fixing.

## Where time actually goes

1. `exec` values: each runs `popen` → `sh` → the command (5–50+ ms).
2. Specs under `CACHE_MIN` (100 KB): parsed from YAML on every TAB.
3. `$PATH` / man directory scans (`@command`, `bash --man` init).
