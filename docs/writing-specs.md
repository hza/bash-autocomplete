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
| `positional` | List of values by position: the 1st positional argument completes to the 1st entry, the 2nd to the 2nd, and so on; past the end of the list, `args` applies. E.g. `git worktree add` uses `[dir, <branches>]` |
| `commands` | Map of subcommand name → spec |
| `description` | One-line summary shown next to this command when several commands match |
| `aliases` | List of other names for this subcommand, e.g. `aliases: [i, add]` under `install`; each is matched like the main name and shown in its description (`install  -- Install a package (🔥 i, add)`); an alias is offered as a candidate only once typed in full |
| `alias_exec` | Shell command printing user-defined aliases of this spec's subcommands, one `name expansion` line each (space or tab between), e.g. git's `git config --get-regexp '^alias\.' \| sed 's/^alias\.//'`. A plain rename (`re = rebase`) is shown in the target's description like a static alias (`rebase -- … (🔥 re)`); others are listed with `🔥 alias: <expansion>` as description; a name defined twice (git: global, then local) takes the last definition; after an alias the line completes as the subcommand it expands to (first non-flag word, alias chains followed). Shell aliases (`!...`) resolve only with a `: git <sub>` hint inside, e.g. `!f() { : git switch ; ... }; f` |
| `bridge` | Top level only: `cobra` hands every completion to the program itself (`CMD __complete …`, Go programs built with cobra); the rest of the spec is ignored. Written by `autocomplete from cobra CMD` |
| `when` | Only list this subcommand if a file in the current directory exists and (optionally) contains a string, e.g. `when: {file: pom.xml, contains: "<artifactId>spotless-maven-plugin</artifactId>"}`. Used for plugin goals that exist only in some projects. A typed command is still recognised either way |
| `examples` | List of `{command, description?}` (any level), shown on TAB after the command word (on an empty word only when nothing else would be listed, always after `@`) as `1️⃣ @1  -- curl -sS https://api.github.com/zen   📚 quiet GET, errors still shown` (1️⃣ `@1` … 9️⃣ `@9`, then 📌 `@a` … `@z`; at most 35 are shown). Typing `@1`, `@a` … and TAB replaces it with the example's command after the command name (`curl @1` → `curl -sS https://api.github.com/zen`); examples that don't start with the command name (pipelines like `git rev-parse HEAD \| pbcopy`) are only listed. Write them yourself (don't copy text from other projects' docs: their licenses may require attribution); meant for commands whose TAB lists nothing else. |
| `groups` | Map of short-flag letters → `{description?}`, grouping rarely used short flags by meaning. Each key lists the letters of `-x` flags of this spec (or the top-level one) that are hidden from the list and shown together in one entry: `"Up": {description: proxy}` gives `-📌 Up  -- proxy: (U) Proxy user and password (p) HTTP proxy tunnel (using CONNECT)`. Top-level groups apply at every depth. `autocomplete check` reports letters with no matching flag |

Each flag is a map with two optional keys, usually written on one line:

| Key | Meaning |
|---|---|
| `value` | What the flag's value completes to. Leave it out if the flag takes no value. |
| `description` | One-line summary shown next to the flag when several flags match |
| `repeat` | `true`: the flag may be given several times (`ssh -v -v`). A flag without a `value` that is already on the line isn't offered again unless it has `repeat: true`; flags with a value are always offered |
| `show` | `always`: a frequently used flag, never folded into a group or into `-📌` (see below). `collapsed` (short flags only): a rarely used flag, left out of the list and only named in a `📌` entry; still listed when nothing else matches. `default` (or leaving it out): the normal behaviour |

When a short and a long flag with the same description are both listed, only the long one is shown, with the short one as a hint: `--data  -- HTTP POST data (🔥 -d)`. Give a short/long pair the same description to get this.

Flags on the top-level spec are offered after any subcommand too. A flag name ending in `=` (e.g. mvn's `'-Dtest=': {}`) is completed without a trailing space, so you can type the value straight after it; give it a `value` to complete that value too (mvn's `-Dspring-boot.run.profiles=` lists `application-<profile>.*` files). Inside an open quote (`-Dx='-Xmx1g -X`) the last space-separated word is completed, so a quoted value can hold several. Quote such keys in YAML.

On a bare `-<TAB>`, if the command has short flags and 15 or more long ones, only the short ones are listed and the long ones are folded into one last entry, `-📌 --  -- 256 long options: type -- to list`. Type `--<TAB>` to list them.

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
| `file` / `dir` | paths / directories only; a word like `~ro` lists `~root/`-style home directories |
| `file: "*.json *.yaml"` | files matching one of the space-separated patterns, plus directories to descend into |
| `"@hosts"` | host names from `~/.ssh/config` (`Host` lines without wildcards) and `~/.ssh/known_hosts` (not hashed entries or IP addresses) |
| `"@users"` | login users (not the `_` system accounts), with their full names |
| `"@pids"` | process IDs of your processes, with the program name |
| `"@processes"` | names of your running programs |
| `"@signals"` | signal names: `HUP`, `INT`, `KILL`, … |
| `exec: <shell command>` | one suggestion per output line; `name<TAB>description` shows the description next to it (e.g. `printf '%s\t%s\n' "$pid" "$name"`) |
| &nbsp;&nbsp;`suffix: ":"` | appended to each suggestion |
| &nbsp;&nbsp;`nospace: true` | don't add a space after accepting it |
| &nbsp;&nbsp;`tree: true` | show `a/b` names as `a/` until you type past the `/`, like paths (e.g. `origin/` for remote branches) |
| &nbsp;&nbsp;`tree: "."` | the same, folding at another separator character (e.g. `com.` for UTIs in duti) |
| &nbsp;&nbsp;`cache: 300` | reuse the output for that many seconds (per command, current directory and, if the command reads `$AUTOCOMPLETE_CUR`, typed word), for slow commands; kept in `~/.cache/autocomplete/exec/` |
| &nbsp;&nbsp;`filter: false` | show every line the command prints, even ones not starting with the typed word (the command filters itself, e.g. duti turns `md` into `net.daringfireball.markdown`) |
| &nbsp;&nbsp;`examples: true` | list the `value<TAB>description` lines as numbered `1️⃣ @1`, `2️⃣ @2` … entries; typing `@2` and TAB inserts that value single-quoted (good for templates such as docker `--format`) |
| `"@subcommands"` | the parent command's subcommands (e.g. `git help <TAB>`) |
| `"@command"` | a command from `$PATH`; the words after it are completed with that command's spec (e.g. `sudo`, `xargs`). Words with `=` before the command are skipped (`env FOO=1 git …`), and so is `!` (`if ! git …`). Use it as `args`. If the spec has `examples`, an empty word shows them instead of every executable (`if <TAB>`) |
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
  On a bare `make <TAB>` the variables are folded into one display-only `📌 vars`
  line listing their names, so targets come first. It is never inserted; type a letter
  (`make C<TAB>`) to list matching variables with their values:
  ```
  $ make <TAB>
  all      -- Build everything
  build
  📌 vars  -- CC PREFIX X
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
- For anything longer than a one-liner, put a script next to the spec and run it via
  `$AUTOCOMPLETE_SPEC_DIR`, the directory of the spec being used (symlinks resolved), e.g.
  `exec: 'osascript -l JavaScript "$AUTOCOMPLETE_SPEC_DIR/duti.js" apps'`. If you copy such a spec
  into `~/.config/autocomplete`, copy its script too.
- `exec` commands also get `$AUTOCOMPLETE_CUR`: the word being completed, with shell escapes
  removed (`Visual\ Stu` → `Visual Stu`). Use it with `filter: false` to match by your own rules.
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

Files must be named `<command>.yaml` or `<command>.yml`. Inside each directory,
specs may be organised into subfolders (up to 4 levels deep): a file directly in
the directory wins over one in a subfolder, and subfolders are searched in
alphabetical order. Helper scripts used by `exec` (see `$AUTOCOMPLETE_SPEC_DIR`)
belong in the same folder as their spec.

The bundled [`specs/`](../specs) are grouped by topic (`vcs/`, `cloud/`, `lang/`,
`build/`, `macos/`, `net/`, `shell/`, `tools/`); e.g. `vcs/git.yaml`,
`cloud/docker.yaml`, `lang/npm.yaml` are good real-world examples.
