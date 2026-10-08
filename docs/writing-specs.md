# Writing specs

A *spec* is a YAML file that describes one command. Create
`~/.config/autocomplete/<command>.yaml`, then run `eval "$(autocomplete bash)"` again
(or open a new terminal) so bash picks up the new command. Edits to an existing spec take
effect on the next TAB.

## Example

`~/.config/autocomplete/kubectl.yaml`:

```yaml
flags:
  -n: {value: {exec: "kubectl get ns -o name | cut -d/ -f2"}, description: Namespace}
  --namespace: {value: {exec: "kubectl get ns -o name | cut -d/ -f2"}, description: Namespace}
  --context: {value: any, description: Name of the kubeconfig context}
  -h: {description: Show help}        # no value
commands:
  get:
    commands:           # fixed words are written as subcommands
      pods: {}
      deployments: {}
      services: {}
  logs:
    flags:
      -f: {description: Stream the logs}
      --tail: {value: any, description: Number of lines to show}
    args:
      exec: kubectl get pods -o name | cut -d/ -f2
  apply:
    flags:
      -f: {value: file, description: File to apply}
  help:
    args: "@subcommands"
```

## Spec format

A spec has these optional keys, and `commands` nests the same structure for subcommands:

| Key | Meaning |
|---|---|
| `flags` | Map of flag name → flag (see below) |
| `args` | What positional arguments complete to (a value) |
| `commands` | Map of subcommand name → spec |
| `description` | One-line summary shown next to this command when several commands match |
| `aliases` | List of other names for this subcommand, e.g. `aliases: [i, add]` under `install`; each is matched like the main name and shown in its description (`install  -- Install a package (i, add)`); an alias is offered as a candidate only once typed in full |
| `when` | Only list this subcommand if a file in the current directory exists and (optionally) contains a string, e.g. `when: {file: pom.xml, contains: "<artifactId>spotless-maven-plugin</artifactId>"}`. Used for plugin goals that exist only in some projects. A typed command is still recognised either way |
| `groups` | Map of short-flag letters → `{description?}`, grouping rarely used short flags by meaning. Each key lists the letters of `-x` flags of this spec (or the top-level one) that are hidden from the list and shown together in one entry: `"Up": {description: proxy}` gives `-👀 Up  -- proxy: (U) Proxy user and password (p) HTTP proxy tunnel (using CONNECT)`. Top-level groups apply at every depth. `autocomplete check` reports letters with no matching flag |

Each flag is a map with two optional keys, usually written on one line:

| Key | Meaning |
|---|---|
| `value` | What the flag's value completes to. Leave it out if the flag takes no value. |
| `description` | One-line summary shown next to the flag when several flags match |
| `show` | `always`: a frequently used flag, never folded into a group or into `-👀` (see below); `tools/gen_tldr.py` sets it for bundled specs from [tldr](https://tldr.sh) examples. `collapsed` (short flags only): a rarely used flag, left out of the list and only named in a `👀` entry; still listed when nothing else matches. `default` (or leaving it out): the normal behaviour; write it explicitly to keep `tools/gen_tldr.py` from marking the flag `always` |

When a short and a long flag with the same description are both listed, only the long one is shown, with the short one as a hint: `--data  -- HTTP POST data (-d)`. Give a short/long pair the same description to get this.

Flags on the top-level spec are offered after any subcommand too. A flag name ending in `=` (e.g. mvn's `'-Dtest=': {}`) is completed without a trailing space, so you can type the value straight after it. Quote such keys in YAML.

On a bare `-<TAB>`, if the command has short flags and 15 or more long ones, only the short ones are listed and the long ones are folded into one last entry, `-👀  -- 256 long options: type -- to list`. Type `--<TAB>` to list them.

When the flag list is taller than the terminal, flags are grouped by their next letter: `--content-disposition` and `--content-on-error` fall into one `--c  -- connect-timeout content-disposition content-on-error …` entry. Type `--c<TAB>` (no space is added) to list that group; a big group is split again by the following letter. If all flags in a group share a longer prefix, the entry is that prefix: `--no-  -- no-🧩`. In the name list, names sharing a first word are shown once: `sasl-authzid sasl-ir` → `sasl-🧩`. The smallest groups are then shown in full again while the list still fits, so `node --<TAB>` lists its two `--no-` flags directly.

```yaml
flags:
  --rm: {description: Remove the container when it exits}   # takes no value
  --name: {value: any, description: Assign a name}
  -f: {value: file}
  -q: {}                                                     # no value, no description
```

A value can be:

| Value | Completes to |
|---|---|
| *(empty)*, `~`, `null` | nothing |
| `any` | free text, no suggestions |
| `file` / `dir` | paths / directories only |
| `exec: <shell command>` | one suggestion per output line; `name<TAB>description` shows the description next to it (e.g. `printf '%s\t%s\n' "$pid" "$name"`) |
| &nbsp;&nbsp;`suffix: ":"` | appended to each suggestion |
| &nbsp;&nbsp;`nospace: true` | don't add a space after accepting it |
| `"@subcommands"` | the parent command's subcommands (e.g. `git help <TAB>`) |
| `"@command"` | a command from `$PATH`; the words after it are completed with that command's spec (e.g. `sudo`, `xargs`). Words with `=` before the command are skipped (`env FOO=1 git …`). Use it as `args` |
| `"@make-targets"` | targets of the Makefile, with descriptions (see below) |
| `"@make-variables"` | `NAME=` for each upper-case variable (`A-Z`, `0-9`, `_`) the Makefile assigns, for overrides like `make CC=clang` |
| a list `[a, b]` | everything from each item, combined |

Notes:

- Items in a list are value *types*, not literal words: `[pods, deployments]` looks for
  types named `pods` and `deployments`. To offer fixed words, either list them as
  subcommands (`commands: {pods: {}, deployments: {}}`) or use `exec: printf '%s\n' pods deployments`.
- Run `autocomplete check <file>` to validate a spec; errors include line numbers.
- Flags defined at the top level are recognised after any subcommand.
- Descriptions appear when more than one command or flag matches:
  ```
  $ git stash <TAB>
  apply   -- Apply a stash, keeping it
  drop    -- Remove a stash
  pop     -- Apply a stash and remove it
  ...
  ```
  They are plain text: bash 3.2 can't display colors in the completion list. Long
  descriptions are cut to fit the terminal width.
- When one list mixes kinds of suggestions (subcommands, files, make targets and
  variables), each line is tagged, since bash sorts the list and can't show section headers.
  Make variables also show the value the Makefile assigns:
  ```
  $ make D<TAB>
  Dockerfile  -- [target] Generate the Dockerfile
  DESTDIR=    -- [var]    Staging root · ?= /tmp/stage
  ```
  On a bare `make <TAB>` the variables are folded into one display-only `🔧 vars`
  line listing their names, so targets come first. It is never inserted; type a letter
  (`make C<TAB>`) to list matching variables with their values:
  ```
  $ make <TAB>
  all      -- Build everything
  build
  🔧 vars  -- CC PREFIX X
  $ make C<TAB>
  CC=      -- C compiler · ?= cc
  CFLAGS=  -- ?= -O2
  ```
- `"@make-targets"` and `"@make-variables"` read descriptions from the Makefile itself: either `## text` after the
  target or assignment, or the comment lines directly above it (`##` wins if both are present):
  ```make
  # Build the binary
  app: main.c

  test: app  ## Run the test suite
  ```
- `exec` commands run in the current directory, and their errors are discarded.
- Values starting with `@` must be quoted.

## Supported YAML

The YAML reader is built into the binary and covers what specs need: indented mappings and
lists, inline `{...}` and `[...]`, single- and double-quoted strings, and `#` comments.
Anchors, tags and multi-line strings (`|`, `>`) are not supported. Indent with spaces, not
tabs.

A `#` after a space starts a comment, even inside an unquoted value. If a command contains
` #`, quote the whole value:

```yaml
exec: "echo 'not # a comment'"
```

## Where specs are looked up

The first match wins, so your own files override the bundled ones:

1. directories in `$AUTOCOMPLETE_SPECS` (colon-separated)
2. `~/.config/autocomplete`
3. `specs/` next to the binary
4. `../share/autocomplete` relative to the binary (where `make install` puts them)

Files must be named `<command>.yaml` or `<command>.yml`.

The bundled [`specs/`](../specs) (e.g. docker, git, gh, kubectl, make, npm) are good real-world examples.
