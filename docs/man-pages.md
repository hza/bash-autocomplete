# Completions from man pages

Most system commands (`ps`, `ls`, `grep`, `tar`, `ssh`, ...) document their options in a
man page. autocomplete can read the page and turn it into flag completion with
descriptions, so you don't have to write a spec by hand.

```sh
autocomplete from man ps ls grep    # writes ~/.config/autocomplete/ps.yaml, ls.yaml, grep.yaml
autocomplete from man --force ps    # overwrite an existing file
```

The result is an ordinary spec. Open it and fix anything the parser got wrong, or add
live values (see [Writing specs](writing-specs.md)). For example, change `-p`'s value in
`ps.yaml` to list running PIDs:

```yaml
  -p:
    value: {exec: "ps -axo pid= | tr -d ' '"}
```

Open a new terminal (or run `eval "$(autocomplete bash)"` again) so the new command is
registered. After a system update changes a man page, run `autocomplete from man --force CMD` to
regenerate the spec (this overwrites your edits).

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
