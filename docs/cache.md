# Cache

Specs are read straight from their YAML on every TAB; even the largest bundled ones (about
80 KB) take about 6 ms per TAB, mostly starting the program. The caches below keep the results
of slow work: generating specs from man pages and running commands.

## Why specs aren't cached or compressed

Earlier versions compiled big specs into a binary cache. Measured on a MacBook it made no
difference: a TAB took about 5 ms with or without it, and the largest bundled specs (gh, npm,
about 80 KB) take about 6 ms, nearly all of it starting the program. So it was removed.

Compressing specs (gzip) wouldn't help either. After the first TAB the file is in the OS file
cache, so reading it costs microseconds; decompressing would only add work and an external
library. Shell startup doesn't read specs at all: `autocomplete bash` only lists their names.

## Man page specs

With `eval "$(autocomplete bash --man)"`, commands without a spec get one generated from
their man page on the first TAB (about 0.15 s), stored in `~/.cache/autocomplete/man/<command>.yaml`.
A page that lists no options leaves an empty file, so it isn't parsed again. It is
regenerated when the command's executable or the `autocomplete` binary (a parser update) is newer. To fix up a generated spec, copy it to
`~/.config/autocomplete/` (or run `autocomplete add --from man <command>`): specs there win.

## exec output

An `exec` value with `cache: SECONDS` keeps its output in `~/.cache/autocomplete/exec/` and
reuses it for that long. The entry is per command and current directory (and per typed word
if the command reads `$AUTOCOMPLETE_CUR`). Delete the directory to refresh right away;
`AUTOCOMPLETE_NOCACHE=1` turns it off.

## mvn test names

`mvn test -Dtest=<TAB>` (and `-Dit.test=`) scans `src/test` for test classes and methods, which
can take seconds in big projects. The results are saved in `~/.cache/autocomplete/mvn-tests/` (per
project directory) and reused until a file under any `src/test` directory changes (added,
edited or deleted), or for 1 hour at most. A test module added since the last scan only shows up
after that hour; delete the directory to rescan right away. `AUTOCOMPLETE_NOCACHE` doesn't affect
this cache.

## java main classes

`java <TAB>` lists the `*.java` files under the current directory that declare `main(`;
`mvn exec:java -Dexec.mainClass=<TAB>` offers the same files under `src/main/java` or `src/test/java`
as class names (`com.example.App`). The list is saved in `~/.cache/autocomplete/java-mains/` (per project directory) and reused until a `.java`
file is edited, added, deleted or renamed, or for 1 hour at most. Delete the directory to rescan
right away. `AUTOCOMPLETE_NOCACHE` doesn't affect this cache.
