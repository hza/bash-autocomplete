# Generating specs: man pages, --help, cobra

Three commands write a spec for you into `~/.config/autocomplete/CMD.yaml`:

| Command | Source |
|---|---|
| `autocomplete from man CMD...` | the man page (flags, descriptions, values) |
| `autocomplete from help CMD...` | `CMD --help` output, for tools without a man page |
| `autocomplete from cobra CMD...` | the program's own completion, for Go programs built with cobra (kubectl, helm, gh, hugo, ...) |

All three take `--force` to overwrite an existing file.

## From man pages

Most system commands (`ps`, `ls`, `grep`, `tar`, `ssh`, ...) document their options in a
man page. autocomplete can read the page and turn it into flag completion with
descriptions, so you don't have to write a spec by hand.

```sh
autocomplete from man ps ls grep    # writes ~/.config/autocomplete/ps.yaml, ls.yaml, grep.yaml
autocomplete from man --force ps    # overwrite an existing file
```

Every generated spec starts with a `description:` (shown by `autocomplete list --long`):
the man page's NAME line, the intro paragraph of `--help` output (for cobra programs,
of `CMD --help`), else the `whatis` line (`man -f CMD`). If none is found it is left
empty (`description: ''`) for you to fill in.

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

### What gets extracted

- **Flags**: every option entry in the page, like `-a`, `--text`, or `-A num, --after-context=num`.
- **Descriptions**: the first sentence of each option's text.
- **Values**: a flag takes a value when its entry or the SYNOPSIS shows one (`-O fmt`,
  `[-p pid]`). A value named like a file or path completes files, one named like a
  directory completes directories, and any other value completes nothing.
- **Arguments**: files.

### Limits

- Man pages are free text, so the result is a heuristic. Pages that show example command
  lines at the same indentation as the options (curl, for example) can produce a few extra
  flags.
- Combined short flags like `ls -la` are accepted, but TAB doesn't suggest more letters to
  add to them.
- Values and subcommands are not inferred. Add them by hand to a generated spec.

## From --help

```sh
autocomplete from help rsync node
```

Uses the same option parser as `from man`, on the output of `CMD --help`. On top of that it
reads:

- option lists starting at the left margin (python's `-b     : issue warnings…`);
- the `usage:` line, so a tool whose help is only a synopsis (`[--append] [-e program]`)
  still gets its flags;
- `Commands:` / `Available Commands:` sections, which become subcommands with their
  descriptions. Their own flags are not read: `from help` never runs `CMD SUB --help`,
  since a subcommand that ignores `--help` would do real work.
- without such a section, one usage block per subcommand (`jfr print [--events <filter>]
  <file>` followed by an indented description line). Each block becomes a subcommand with
  the flags from its brackets, file arguments for `<file>`, and the description.

The command is run with `--help` and stdin from `/dev/null`; don't use it on programs that
might act on an unknown flag.

## From cobra

```sh
autocomplete from cobra helm
```

Programs built with Go's cobra library answer `CMD __complete WORDS…` with their own
completions, including live values (helm releases, kube contexts, gh repositories). The
generated spec is just

```yaml
bridge: cobra
```

and every TAB asks the program. File and directory answers, extension filters and
"no space" hints are honoured. `from cobra` refuses programs that don't answer like cobra.
A program with a bundled spec (kubectl, helm, docker) keeps using the spec unless you
generate one: your `~/.config/autocomplete` file wins.
