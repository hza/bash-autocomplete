# Adding your own commands

Drop a YAML file into `~/.config/autocomplete/`, named after the command:

```yaml
# ~/.config/autocomplete/kubectl.yaml
flags:
  --context: any
commands:
  logs:
    description: Print the logs of a pod    # shown when several commands match
    flags:
      -f:                 # flag without a value
    args:
      exec: kubectl get pods -o name | cut -d/ -f2
  apply:
    flags:
      -f: file
```

Then run `eval "$(autocomplete bash)"` again (or open a new terminal).

Check the file for mistakes with:

```sh
autocomplete check ~/.config/autocomplete/kubectl.yaml
```

Later edits to the file take effect on the next TAB; only adding a *new* command needs the
`eval` step.

See [Writing specs](writing-specs.md) for the full format: all value types, supported YAML
and where specs are looked up.
