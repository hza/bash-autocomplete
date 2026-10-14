# How it works

`autocomplete bash` prints one `complete -C` line that registers `autocomplete _complete`
for every command that has a spec; no shell function is defined. `autocomplete bash --man`
also registers every `$PATH` command with a section 1 or 8 man page, skipping commands that
already have a completion (bash-completion, Homebrew scripts loaded earlier) and those with
a spec. For such a command the first TAB parses its man page (`man -P cat`, never running
the command itself) into a spec and caches it; a page without options leaves plain filename
completion. When you press TAB, bash
runs `autocomplete _complete` with the line and cursor position in `COMP_LINE` and
`COMP_POINT`. The binary splits the line into words and walks the spec. It
then prints the matching suggestions, already escaped for the shell and trimmed the way
bash expects.

bash 3.2 lacks `compopt` and `mapfile`, so nothing here relies on them. Commands are
registered with `complete -o nospace`, and the binary adds the trailing space itself to
finished words. `-o bashdefault` lets bash's own `$VAR` completion handle words starting with `$` (the binary prints nothing for them), so unexported shell variables are listed too. Inside braces the engine completes the name itself (`${COL<TAB>` → `${COLUMNS`, without the `}` bash would add). The init line passes the shell's variable names (`compgen -v`, unexported ones included) to the binary on each TAB. TAB on a complete name lists `}` and the operators. Once an operator follows the name inside braces (`${str%`, `${str:`, `${s//`, `${#`, `${!`), the engine lists the matching bash 3.2 parameter-expansion forms with an example and a short description. The list is display-only: TAB never changes the line, and the values are not known (unexported variables never reach the binary). The same applies to `@` directly after non-space text (`user@<TAB>`): bash's own hostname completion lists hosts. A bare `@` at the start of a word is not handed over; it stays with the engine (spec snippets, `curl @<TAB>`).

Descriptions use the way bash handles several matches: it inserts only their common
prefix and lists the rest. When more than one command matches, the binary returns each
one as `name  -- description`, padded so bash lists one per line. The names differ before
the padding starts, so the descriptions are only displayed, never typed into the command
line. A single match is returned as the bare name. bash 3.2 shows escape codes literally
(`^[[2m`), so the list can't use color.
