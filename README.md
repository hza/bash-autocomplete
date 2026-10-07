# autocomplete

Tiny carapace-like completion engine in C for macOS's stock bash 3.2.

## Build & enable

    make && make install            # PREFIX=/usr/local by default
    echo 'eval "$(autocomplete bash)"' >> ~/.bash_profile

To load it into the current shell:

    eval "$(./autocomplete bash)"                       # recommended
    source /dev/stdin <<<"$(./autocomplete bash)"       # if you want to use source

Note: `source <(autocomplete bash)` silently does nothing in bash 3.2 (known bug).

## How it works

The init script registers `_autocomplete` (with `complete -o nospace`) for every spec.
On TAB it runs `autocomplete _complete "<line up to cursor>" "$COMP_WORDBREAKS"`, which
prints one ready-to-use candidate per line (escaped, trimmed to bash's current word).
bash 3.2 has no `compopt`/`mapfile`, so the engine appends the trailing space itself.

## Specs

Each command is described by `<command>.json`. Specs are looked up in this order
(first match wins):

1. `$AUTOCOMPLETE_SPECS` (colon-separated directories)
2. `~/.config/autocomplete`
3. `specs/` next to the binary
4. `../share/autocomplete` relative to the binary (where `make install` puts them)

Adding a command needs no rebuild: drop `<command>.json` into `~/.config/autocomplete`
and run `eval "$(autocomplete bash)"` again. `autocomplete list` shows the available specs.
Set `AUTOCOMPLETE_DEBUG=1` to see JSON errors on stderr.

```json
{
  "flags":    { "--context": "any", "--debug": null, "--config": "dir" },
  "args":     "file",
  "commands": {
    "stop": {
      "flags": { "-t": "any" },
      "args":  { "exec": "docker ps --format '{{.Names}}'" }
    }
  }
}
```

`flags` maps each flag to its value, and `args` describes positional arguments.
A value is one of:

| Value | Meaning |
|---|---|
| `null` | flag takes no value / nothing to complete |
| `"any"` | free text, no suggestions |
| `"file"`, `"dir"` | paths |
| `{"exec": "cmd", "suffix": ":", "nospace": true}` | one suggestion per output line of `cmd` |
| `"@make-targets"` | targets of the makefile (honours `-f` and `-C`) |
| `"@subcommands"` | the parent command's subcommands (e.g. `docker help`) |
| `[ ... ]` | all of the listed values |

Flags at the top level are recognised at any depth.
