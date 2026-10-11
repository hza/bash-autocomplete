#!/bin/sh
# find value suggestions: sh find.sh min|time|size|perm|depth|date|name|path|regex|fstype|links|xattr|group
# Prints "value<TAB>description" lines.
row() { printf '%s\t%s\n' "$1" "$2"; }
case $1 in
min)
    row -5 "within the last 5 minutes"
    row -30 "within the last 30 minutes"
    row -60 "within the last hour"
    row -1440 "within the last day"
    row +60 "more than an hour ago"
    row 10 "exactly 10 minutes ago (rounded up)" ;;
time)
    row -1 "within the last 24 hours"
    row -7 "within the last week"
    row +30 "more than 30 days ago"
    row +365 "more than a year ago"
    row -2h "within the last 2 hours (units s m h d w)"
    row +1w "more than a week ago"
    row 0 "today (less than 24 hours)" ;;
size)
    row +100M "larger than 100 MB"
    row +1G "larger than 1 GB"
    row -10k "smaller than 10 KB"
    row 0 "zero length"
    row +0 "not empty"
    row +1M "larger than 1 MB" ;;
perm)
    row 644 "exactly rw-r--r--"
    row 755 "exactly rwxr-xr-x"
    row +111 "executable by anyone"
    row -002 "world-writable"
    row -4000 "setuid"
    row 600 "exactly rw------- (private)" ;;
depth)
    row 1 "this directory only"
    row 2 "one level of subdirectories"
    row 3 "two levels of subdirectories" ;;
date)
    row "$(date +%Y-%m-%d)" "today"
    row "$(date +%Y-%m-01)" "start of this month"
    row "$(date +%Y-01-01)" "start of this year"
    row "$(date '+%Y-%m-%d %H:00')" "start of this hour" ;;
name)
    row '*.log' "log files"
    row '*.txt' "text files"
    row '*.java' "Java sources"
    row '*~' "editor backups"
    row '.*' "hidden files"
    row '*.orig' "merge leftovers"
    row .DS_Store "Finder metadata"
    row node_modules "npm dependency dirs"
    row target "Maven build dirs"
    row build "build output dirs" ;;
path)
    row './.git' "the git dir (with -prune)"
    row '*/node_modules/*' "anything inside node_modules"
    row '*/target/*' "anything inside Maven build dirs"
    row '*/src/test/*' "test sources"
    row '*/.*' "hidden paths" ;;
regex)
    row '.*\.(jpg|png|gif)' "images (with -E)"
    row '.*/[0-9]+\.txt' "numbered text files (with -E)"
    row '.*\.(c|h)' "C sources (with -E)" ;;
fstype)
    row local "local file systems"
    row apfs "APFS volumes"
    row nfs "NFS mounts"
    row smbfs "SMB shares"
    row rdonly "read-only file systems" ;;
links)
    row +1 "files with extra hard links"
    row 1 "exactly one link"
    row 2 "empty directories have 2" ;;
xattr)
    row com.apple.quarantine "downloaded files"
    row com.apple.metadata:kMDItemWhereFroms "download source URL"
    row com.apple.FinderInfo "Finder flags"
    row com.apple.lastuseddate#PS "last opened date" ;;
group)
    id -Gn 2>/dev/null | tr ' ' '\n' | sort -u ;;
esac
