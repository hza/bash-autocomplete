# Completions from man pages

Most system commands (`ps`, `ls`, `grep`, `tar`, `ssh`, ...) document their options in a
man page. autocomplete can read the page and turn it into flag completion with
descriptions, so you don't have to write a spec by hand.

There are two ways to use it.

## Generate a spec (recommended)

```sh
autocomplete gen ps ls grep     # writes ~/.config/autocomplete/ps.yaml, ls.yaml, grep.yaml
autocomplete gen -f ps          # overwrite an existing file
```

The result is an ordinary spec. Open it and fix anything the parser got wrong, or add
live values (see [Writing specs](writing-specs.md)). For example, change `-p`'s value in
`ps.yaml` to list running PIDs:

```yaml
  -p:
    value: {exec: "ps -axo pid= | tr -d ' '"}
```

Open a new terminal (or run `eval "$(autocomplete bash)"` again) so the new command is
registered.

## Live, without a spec file

```sh
autocomplete man ps tar
eval "$(autocomplete bash)"     # or open a new terminal
```

`man` adds the names to `~/.config/autocomplete/man.list` (one per line, `#` comments
allowed), which you can also edit by hand. Remove a line from that file to stop using the man page.

Any listed command that has no YAML spec gets completion straight from its man page. The
page is parsed on the first TAB, which takes about 150 ms, and the result is cached. After
a system update changes a man page, run `autocomplete cache` to re-read it. If a command
has both a YAML spec and a `man.list` entry, the YAML spec wins.

## What gets extracted

- **Flags**: every option entry in the page, like `-a`, `--text`, or `-A num, --after-context=num`.
- **Descriptions**: the first sentence of each option's text.
- **Values**: a flag takes a value when its entry or the SYNOPSIS shows one (`-O fmt`,
  `[-p pid]`). A value named like a file or path completes files, one named like a
  directory completes directories, and any other value completes nothing.
- **Arguments**: files.

## Limits

- Man pages are free text, so the result is a heuristic. Pages that show example command
  lines at the same indentation as the options (curl, for example) can produce a few extra
  flags.
- Combined short flags like `ls -la` are accepted, but TAB doesn't suggest more letters to
  add to them.
- Values and subcommands are not inferred. Add them by hand to a generated spec.
