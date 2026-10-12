# autocomplete: fast tab completion for macOS bash 3.2

**Fast tab completion for the stock macOS bash 3.2 (`/bin/bash`), written in C.**

macOS still ships bash 3.2 from 2007, and most modern completion tools have dropped support
for it. This project was built specifically for that bash. All completion logic runs in a
single compiled C binary, so pressing TAB doesn't run any shell code. Completion is
instant and new terminals open as fast as before.

```console
$ docker run --network <TAB>
bridge   host   none   my_app_default
$ git checkout fe<TAB>
$ git checkout feature/login
$ make in<TAB>
$ make install
```

It is a lightweight alternative to bash-completion that completes docker, git, kubectl, make,
mvn, npm and more. You don't need Homebrew bash, the `bash-completion` package or zsh, and
there are no shell scripts. It shines on parameter monsters like curl and wget. Hundreds of
flags are grouped to fit your screen, and TAB on an empty argument shows ready-made usage
examples.

## Why another bash completion tool?

If you use bash on a Mac, you've probably found that completion tools don't support it:

- **bash-completion 2.x** requires bash 4.2 or newer.
- **bash-completion 1.3**, the last version that runs on bash 3.2, is unmaintained. It
  loads every completion script at shell startup, which makes each new terminal slow to open.
- The usual workaround is to `brew install bash` and change your login shell.

autocomplete works with the bash already on your Mac:

- **Built for macOS bash 3.2.** Tested against Apple's `/bin/bash` and Apple clang.
- **Fast because it's all C.** Bash registers the binary with `complete -C`, so every TAB
  calls the binary directly. No bash functions, no `sed`/`awk` pipelines, no subshells.
- **Fast shell startup.** The init line registers commands and loads nothing else. Specs
  are read only when you press TAB.
- **No dependencies.** One binary, plain C, builds with the Xcode Command Line Tools.

## Features

- Bundled completions for **docker**, **git**, **kubectl**, **make**, **mvn**, **java**, **jfr**,
  **python**, **node**, **npx**, **bun**, **bunx**, **npm**, **yarn**, **pnpm**, **pip**, **go**,
  **cargo**, **gradle**, **brew**, **gh**, **ssh**, **scp**, **terraform**, **helm**, **aws**,
  **tmux**, **ps**, **kill**, **pkill**, **killall**, **lsof**, **find**, **grep**, **help** (bash builtins), **man** (page names from `manpath`), **jq**, **curl**, **wget**, **code** (VS Code),
  macOS **open**, **launchctl**, **defaults**, **diskutil**, **say**, **caffeinate**, **pbcopy**,
  **pbpaste**, **sips**, **screencapture**, **duti**, **dutix**, **mdls**, and `autocomplete` itself.
- Wrapper commands **sudo**, **env**, **time**, **xargs** complete the command after them with
  that command's own completion (`sudo git chec<TAB>` → `checkout`).
- Live values from your environment:
  - docker: running containers, images, networks, volumes
  - git: branches, tags, remotes, stashes, changed files
  - kubectl: namespaces, contexts, pods, nodes, resource types
  - npm: `package.json` scripts and dependencies
  - mvn: profiles and modules from `pom.xml`, plugin goals for plugins the project uses
  - make: Makefile targets and variables, with descriptions from `## text` or the comment
    above each target (honors `make -C dir` and `make -f file`)
  - killall, pkill: your running process names; kill, `caffeinate -w`: PIDs with process names
  - say: voices with language and sample text (`say -v Ed<TAB>`), audio devices
  - code: installed extensions for `--uninstall-extension`/`--disable-extension`
  - sips: property keys with types for `-g`/`-s`; screencapture: app bundle ids for `-B`
  - duti: extensions with their UTI and current app for `-x`; app bundle ids for `-s` (also by app name: `duti -s Preview<TAB>` → `com.apple.Preview`), then every UTI known to the system as `UTI -- .ext → current app` (typing an extension finds its UTIs: `duti -s com.apple.Preview md<TAB>`; when several match they are listed as `ts1: …`, `ts2: …` and `ts2<TAB>` picks the second; the same works for `-d`/`-l`: `duti -d md<TAB>` → `duti -d net.daringfireball.markdown`) (★ = declared by the app, ✔ = currently opens with it), read live from LaunchServices via `osascript`
    (`duti -s com.apple.Preview com.ad<TAB>` → `com.adobe.pdf`), then the role
  - mdls: Spotlight attribute names with descriptions for `-name`
  - ssh, scp: hosts from `~/.ssh/config` and `known_hosts` (`scp gr<TAB>` → `grafana:`)
  - brew: installed formulae and services
  - gh: pull requests, issues, workflow runs, gists, labels, releases, repos
  - yarn, pnpm: `package.json` scripts, dependencies and workspace packages
  - pip: installed packages; go: packages and test names; cargo: bins, examples, tests
  - terraform: resources from state, `*.tfvars`; helm: releases, repo charts, namespaces
  - node: `package.json` scripts for `--run`, test reporters; npx, bunx: binaries in `node_modules/.bin`
  - bun: scripts, dependencies (`remove`, `why`, `update`), `node_modules/.bin` for `bun x`
  - python: module names for `-m` (`python3 -m http<TAB>` → `http.server`)
  - java: source files to run (`java <TAB>`: `*.java` files under the cwd declaring `main`), module names (`-m`, `--add-modules`), jars and class paths
  - aws: profiles, regions, S3 buckets; tmux: sessions and buffers
  - open: app names (`-a`) and bundle ids (`-b`); launchctl: services and LaunchAgent plists
- Descriptions next to subcommands and flags when several match
  (`git stash <TAB>` → `pop  -- Apply a stash and remove it`).
- Usage examples for commands with hundreds of flags: `curl <TAB>` and `wget <TAB>` list
  common recipes (`07  @7  -- curl -X DELETE https://httpbin.org/delete   📚 any other
  method: PUT, PATCH, DELETE`) instead of nothing, and `curl @7<TAB>` inserts it; `ps @<TAB>` and `jfr @<TAB>` list recipes too (top CPU/memory, hot methods, GC pauses); `for`, `while`, `until`, `case` and `if <TAB>` show practical one-liners; `if git <TAB>` completes git, and so do `then`/`do`/`else`/`elif git <TAB>`.
- Handles `--flag=value`, `image:tag`, quoted words and file names with spaces.
- Add a command by writing a small YAML file, with no rebuild
  ([how](docs/adding-commands.md)).
- Generate flag completion for system commands like `ps`, `ls` or `tar` from their man
  pages, for other tools from their `--help` output, and delegate Go/cobra programs
  (helm, gh, hugo) to their own live completion ([details](docs/generated-specs.md)).
- Built-in completion for SSH hosts, users, PIDs, process names and signals; `~user/` paths.
- Complete shell aliases like the command they wrap (`autocomplete alias kubectl k`).
- Very large specs (over 100 KB) are compiled to a cache, so TAB doesn't re-parse YAML.

## Requirements

- To build from source: the Xcode Command Line Tools (`xcode-select --install`) on macOS, or gcc/clang and make on Linux.
- bash 3.2 (the macOS `/bin/bash`) or any newer bash.

## Installation

Prebuilt binaries are published for macOS (Apple silicon and Intel) and Linux (x86_64, arm64).

**Homebrew** (macOS, Linux):

```sh
brew install hza/tap/autocomplete
echo 'eval "$(autocomplete bash)"' >> ~/.bashrc
```

**Install script** (no sudo; installs to `~/.local` and adds the `eval` line to `~/.bashrc`):

```sh
curl -fsSL https://github.com/hza/bash-autocomplete/releases/latest/download/install.sh | sh
# or
wget -qO- https://github.com/hza/bash-autocomplete/releases/latest/download/install.sh | sh
```

Set `PREFIX=/usr/local`, `PROFILE=~/.bash_profile` or `VERSION=v0.1.0` for `sh` to change the
defaults (e.g. `... | PREFIX=$HOME/bin sh`). The script verifies the download's SHA-256
checksum. Uninstall with `... | sh -s -- --uninstall` (same `PREFIX`/`PROFILE`).

### From source

```sh
git clone https://github.com/hza/bash-autocomplete.git autocomplete
cd autocomplete
make install
```

`make install`:

- installs the binary to `/usr/local/bin/autocomplete`,
- installs the bundled specs to `/usr/local/share/autocomplete/`,
- precompiles any spec over 100 KB into the cache,
- adds `eval "$(autocomplete bash)"` to `~/.bashrc`, unless it's already there.

Open a new terminal (or run `source ~/.bashrc`) and press TAB after `docker`, `git` or `make`.

> macOS Terminal starts login shells, which read `~/.bash_profile`, not `~/.bashrc`.
> `make install` warns if your `~/.bash_profile` doesn't load `~/.bashrc`; add
> `[ -f ~/.bashrc ] && . ~/.bashrc` to it.

Options:

```sh
make install PREFIX=$HOME/.local          # install without sudo (put ~/.local/bin on PATH first)
make install PROFILE=$HOME/.bash_profile  # use a different startup file
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
| `autocomplete list` | List commands that have a spec, one per line with its description. |
| `autocomplete list --paths` | Same, with the spec file each command uses (handy to see which copy wins, or where a bundled spec lives). |
| `autocomplete list --user` | Only your own specs in `~/.config/autocomplete` (generated with `from`, aliases, hand-written); combines with `--paths`. |
| `autocomplete check FILE` | Validate a spec and print how it was understood (as JSON). |
| `autocomplete cache [NAME...]` | Precompile specs over 100 KB into the cache (all if no names). |
| `autocomplete from man [--force] CMD...` | Write `~/.config/autocomplete/CMD.yaml` from `man CMD`. |
| `autocomplete from help [--force] CMD...` | Same, from `CMD --help` (flags and subcommands). |
| `autocomplete from cobra [--force] CMD...` | Same, as a bridge to the program's own cobra completion (`bridge: cobra`). |
| `autocomplete alias [--force] CMD NAME...` | Complete `NAME` (e.g. a shell alias) like `CMD` by symlinking `~/.config/autocomplete/NAME.yaml` to its spec. |
| `autocomplete alias --rm NAME...` | Remove aliases made by `autocomplete alias` (only symlinks are removed). |
| `autocomplete alias --list` | List aliases as `NAME -> CMD`. |

## FAQ

### How do I get tab completion for git and docker in macOS bash?

Install autocomplete (`make install`) and open a new terminal. You don't need Homebrew
bash, `bash-completion` or zsh.

### Do I need to upgrade bash or switch to zsh?

No. autocomplete is designed for the bash 3.2 that macOS ships as `/bin/bash`.

### Why is it faster than bash-completion?

bash-completion is written in bash and sources its scripts into every shell.
autocomplete keeps all logic in a compiled C binary that runs only when you press TAB.
Shell startup does nothing beyond one `complete` registration.

### Can I add completion for my own command?

Yes. Write `~/.config/autocomplete/<command>.yaml` describing subcommands, flags and
values. See [Adding your own commands](docs/adding-commands.md).

## Documentation

- [Adding your own commands](docs/adding-commands.md): a quick start for new commands
- [Writing specs](docs/writing-specs.md): spec format, value types, supported YAML,
  where specs are looked up
- [Generating specs](docs/generated-specs.md): `autocomplete from man|help|cobra`
- [Cache](docs/cache.md): how parsed specs are cached and how to reset it
- [Troubleshooting](docs/troubleshooting.md): common problems and fixes
- [How it works](docs/how-it-works.md): what happens when you press TAB

## Building from source

```sh
make            # builds ./autocomplete
make clean
```

The code is plain C with no dependencies, in `src/`: `main.c` (subcommands), `complete.c`
(spec walk), `output.c` (candidate list and display), `builtins.c` (files, make, `$PATH`,
`exec` completers), `spec.c` (spec lookup and cache), `yaml.c` (YAML reader and writer),
`manparse.c` (man page reader) and `util.c`. Bundled specs live in `specs/`, grouped into topic subfolders (`vcs/`, `cloud/`, `lang/`, …); specs are found at any depth.

## License

MIT. See [LICENSE](LICENSE).
