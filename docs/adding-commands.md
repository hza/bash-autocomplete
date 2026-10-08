# Adding your own commands

Drop a YAML file into `~/.config/autocomplete/`, named after the command:

```yaml
# ~/.config/autocomplete/kubectl.yaml
flags:
  --context: {value: any, description: Name of the kubeconfig context}
commands:
  logs:
    description: Print the logs of a pod    # shown when several commands match
    flags:
      -f: {description: Stream the logs}    # no "value": the flag takes none
    args:
      exec: kubectl get pods -o name | cut -d/ -f2
  apply:
    flags:
      -f: {value: file, description: File to apply}
```

Then run `eval "$(autocomplete bash)"` again (or open a new terminal).

Check the file for mistakes with:

```sh
autocomplete check ~/.config/autocomplete/kubectl.yaml
```

Later edits to the file take effect on the next TAB; only adding a *new* command needs the
`eval` step.

## Shell aliases

If you have `alias dk=docker`, give `dk` the docker completions with:

```sh
autocomplete alias docker dk   # ~/.config/autocomplete/dk.yaml -> docker's spec
```

Then re-run the `eval` step. `-f` replaces an existing `dk` spec. Since it's a symlink, edits
to the docker spec apply to `dk` too.

See [Writing specs](writing-specs.md) for the full format: all value types, supported YAML
and where specs are looked up.
