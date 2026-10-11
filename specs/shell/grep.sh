#!/bin/sh
# grep value suggestions: sh grep.sh pattern|lines|count|glob|dir|color|binary
# Prints "value<TAB>description" lines.
row() { printf '%s\t%s\n' "$1" "$2"; }
case $1 in
pattern)
    row TODO "TODO markers"
    row FIXME "FIXME markers"
    row ERROR "error log lines"
    row '^$' "empty lines (-v to drop them)"
    row '^#' "comment lines (-v to drop them)"
    row 'error|warn' "either word (with -E)" ;;
lines)
    row 1 "one line"
    row 3 "three lines"
    row 5 "five lines"
    row 10 "ten lines" ;;
count)
    row 1 "first match only"
    row 5 "first five"
    row 10 "first ten" ;;
glob)
    row '*.c' "C sources"
    row '*.h' "C headers"
    row '*.java' "Java sources"
    row '*.py' "Python sources"
    row '*.js' "JavaScript"
    row '*.ts' "TypeScript"
    row '*.go' "Go sources"
    row '*.md' "Markdown"
    row '*.yaml' "YAML"
    row '*.json' "JSON"
    row '*.xml' "XML"
    row '*.sh' "shell scripts"
    row '*.log' "log files"
    row '*.min.js' "minified JavaScript" ;;
dir)
    row .git "git metadata"
    row node_modules "npm dependencies"
    row target "Maven build output"
    row build "build output"
    row dist "dist output"
    row .idea "IntelliJ settings"
    row vendor "vendored dependencies"
    row '.*' "hidden directories" ;;
color)
    row auto "only on a terminal"
    row always "even in pipes (less -R)"
    row never "no highlighting" ;;
binary)
    row binary "report 'Binary file matches' (default)"
    row without-match "skip binary files (-I)"
    row text "treat as text (-a)" ;;
esac
