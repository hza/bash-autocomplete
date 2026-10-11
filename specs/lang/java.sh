#!/bin/sh
# java source-file launch: *.java files declaring main(), as cwd-relative paths.
# `classes`: their class names under src/{main,test}/java (mvn -Dexec.mainClass=).
# Cached per cwd in ~/.cache/autocomplete/java-mains, reused while no *.java file or
# directory in the tree is newer than it (dir mtime catches adds/deletes/renames), ≤ 1 h.
dir=${XDG_CACHE_HOME:-$HOME/.cache}/autocomplete/java-mains
c=$dir/$(pwd | cksum | cut -d' ' -f1)
walk() {
    find . \( -name target -o -name build -o -name out -o -name node_modules -o -name '.*' ! -name . \) -prune \
        -o "$@" 2>/dev/null
}
if [ -z "$(find "$c" -mmin -60 2>/dev/null)" ] ||
   [ -n "$(walk \( -name '*.java' -o -type d \) -newer "$c" -print -quit)" ]; then
    mkdir -p "$dir"
    walk -name '*.java' -print |
        xargs grep -lE '(^|[[:space:]])void[[:space:]]+main[[:space:]]*\(' 2>/dev/null |
        sed 's|^\./||' | sort > "$c.$$" && mv "$c.$$" "$c"
fi
if [ "$1" = classes ]; then
    sed -nE 's#^(.*/)?src/(main|test)/java/(.*)[.]java$#\3#p' "$c" | tr / . | sort -u
else
    cat "$c"
fi
