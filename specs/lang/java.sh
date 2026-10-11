#!/bin/sh
# java source-file launch: *.java files declaring main(), as cwd-relative paths.
find . \( -name target -o -name build -o -name out -o -name node_modules -o -name '.*' ! -name . \) -prune \
    -o -name '*.java' -print 2>/dev/null |
    xargs grep -lE '(^|[[:space:]])void[[:space:]]+main[[:space:]]*\(' 2>/dev/null |
    sed 's|^\./||' | sort
