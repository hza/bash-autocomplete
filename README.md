# autocomplete

Fast, dependency-free command-line completion for the **bash 3.2 that ships with macOS**,
inspired by [carapace](https://github.com/carapace-sh/carapace).

Completions are described in small YAML files, so adding a new command doesn't need a
rebuild. The engine is a single C binary with no runtime dependencies.

```console
$ docker run --network <TAB>
bridge   host   none   my_app_default
$ git checkout fe<TAB>
$ git checkout feature/login
$ make in<TAB>
$ make install
```

## Why

macOS still ships bash 3.2 as `/bin/bash`, and the usual completion tools don't fit it well:

- **carapace** generates a bash script that relies on bash 4 features (`mapfile`,
  `compopt`, negative array indexes). On bash 3.2 it doesn't work.
- **bash-completion 2.x** requires bash 4.2+.
- **bash-completion 1.3**, the last version for bash 3.2, is old and unmaintained. It loads
  every completion script at shell startup, which makes each new terminal noticeably slower
  to open.

autocomplete works with the bash you already have. Shell startup only defines one small
function, and nothing is loaded until you press TAB.

## Features

- Works with stock macOS `/bin/bash` 3.2, with no Homebrew bash or `bash-completion` needed.
- Bundled completions for **docker**, **git** and **make**.
- Live values: running containers, images, networks and volumes for docker; branches,
  tags, remotes, stashes and changed files for git; Makefile targets for make
  (honours `make -C dir` and `make -f file`).
- Shows a short description next to each subcommand when several match
  (`git stash <TAB>` → `pop  -- Apply a stash and remove it`).
- Handles `--flag=value`, `image:tag`, quoted words and file names with spaces.
- New commands are plain YAML files, so there's nothing to compile
  ([how to add one](docs/adding-commands.md)).
- Parsed specs are cached, so a TAB doesn't re-read the YAML each time.

## Requirements

- macOS with the Xcode Command Line Tools (`xcode-select --install`) for `cc` and `make`.
- bash 3.2 or newer.

## Installation

```sh
git clone <repo-url> autocomplete
cd autocomplete
make install
```

`make install`:

- installs the binary to `/usr/local/bin/autocomplete`,
- installs the bundled specs to `/usr/local/share/autocomplete/`,
- precompiles them into the cache,
- adds `eval "$(autocomplete bash)"` to `~/.bash_profile`, unless it's already there.

Open a new terminal (or run `source ~/.bash_profile`) and press TAB after `docker`, `git`
or `make`.

Options:

```sh
make install PREFIX=$HOME/.local          # install without sudo (put ~/.local/bin on PATH first)
make install PROFILE=$HOME/.bashrc        # use a different startup file
```

If `/usr/local` isn't writable, use `sudo make install`. In that case the cache isn't built
during install; it's built on your first completion instead.

### Using it without installing

```sh
make
eval "$(./autocomplete bash)"                     # recommended
source /dev/stdin <<<"$(./autocomplete bash)"     # if you prefer `source`
```

> `source <(autocomplete bash)` does **not** work in bash 3.2. A long-standing bash bug
> makes it silently read nothing. Use one of the forms above.

### Uninstalling

```sh
make uninstall          # use the same PREFIX / PROFILE you installed with
```

This removes the binary, the bundled specs, their cache files and the exact `eval` line
from your profile. Your own specs in `~/.config/autocomplete` are left untouched.

## Commands

| Command | Description |
|---|---|
| `autocomplete bash` | Print the bash init script (use with `eval`). |
| `autocomplete list` | List commands that have a spec. |
| `autocomplete check FILE` | Validate a spec and print how it was understood (as JSON). |
| `autocomplete cache [NAME...]` | Precompile specs into the cache (all if no names). |

## Documentation

- [Adding your own commands](docs/adding-commands.md): a quick start for new commands
- [Writing specs](docs/writing-specs.md): spec format, value types, supported YAML,
  where specs are looked up
- [Cache](docs/cache.md): how parsed specs are cached and how to reset it
- [Troubleshooting](docs/troubleshooting.md): common problems and fixes
- [How it works](docs/how-it-works.md): what happens when you press TAB

## Building from source

```sh
make            # builds ./autocomplete
make clean
```

The code is plain C with no dependencies: `autocomplete.c` (engine) and `yaml.c` (YAML
reader). Bundled specs live in `specs/`.
