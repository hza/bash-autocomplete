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

A spec has four optional keys, and `commands` nests the same structure for subcommands:

| Key | Meaning |
|---|---|
| `flags` | Map of flag name → flag (see below) |
| `args` | What positional arguments complete to (a value) |
| `commands` | Map of subcommand name → spec |
| `description` | One-line summary shown next to this command when several commands match |

Each flag is a map with two optional keys, usually written on one line:

| Key | Meaning |
|---|---|
| `value` | What the flag's value completes to. Leave it out if the flag takes no value. |
| `description` | One-line summary shown next to the flag when several flags match |

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
| `exec: <shell command>` | one suggestion per output line |
| &nbsp;&nbsp;`suffix: ":"` | appended to each suggestion |
| &nbsp;&nbsp;`nospace: true` | don't add a space after accepting it |
| `"@subcommands"` | the parent command's subcommands (e.g. `git help <TAB>`) |
| `"@make-targets"` | targets of the Makefile, with descriptions (see below) |
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
- `"@make-targets"` reads descriptions from the Makefile itself: either `## text` after the
  target, or the comment lines directly above it (`##` wins if both are present):
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

The bundled [`specs/`](../specs) (docker, git, make) are good real-world examples.
