# How it works

`autocomplete bash` prints a small shell function and registers it for every command that
has a spec. When you press TAB, the function passes the line up to the cursor to
`autocomplete _complete`. The binary splits the line into words and walks the spec. It
then prints the matching suggestions, already escaped for the shell and trimmed the way
bash expects.

bash 3.2 lacks `compopt` and `mapfile`, which carapace's script relies on. Instead, the
function reads suggestions with a plain `while read` loop. Commands are registered with
`complete -o nospace`, and the binary adds the trailing space itself to finished words.

Descriptions use the way bash handles several matches: it inserts only their common
prefix and lists the rest. When more than one command matches, the binary returns each
one as `name  -- description`, padded so bash lists one per line. The names differ before
the padding starts, so the descriptions are only displayed, never typed into the command
line. A single match is returned as the bare name. bash 3.2 shows escape codes literally
(`^[[2m`), so the list can't use color.
