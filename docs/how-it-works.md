# How it works

`autocomplete bash` prints one `complete -C` line that registers `autocomplete _complete`
for every command that has a spec; no shell function is defined. When you press TAB, bash
runs `autocomplete _complete` with the line and cursor position in `COMP_LINE` and
`COMP_POINT`. The binary splits the line into words and walks the spec. It
then prints the matching suggestions, already escaped for the shell and trimmed the way
bash expects.

bash 3.2 lacks `compopt` and `mapfile`, so nothing here relies on them. Commands are
registered with `complete -o nospace`, and the binary adds the trailing space itself to
finished words. `-o bashdefault` lets bash's own `$VAR` completion handle words starting with `$` (the binary prints nothing for them), so unexported shell variables are listed too.

Descriptions use the way bash handles several matches: it inserts only their common
prefix and lists the rest. When more than one command matches, the binary returns each
one as `name  -- description`, padded so bash lists one per line. The names differ before
the padding starts, so the descriptions are only displayed, never typed into the command
line. A single match is returned as the bare name. bash 3.2 shows escape codes literally
(`^[[2m`), so the list can't use color.
