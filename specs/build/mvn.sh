#!/bin/sh
# mvn -Dtest= / -Dit.test= values: sh mvn.sh unit|it
# CLASS, CLASS#method; comma lists complete the last item.
# Cached per project dir in ~/.cache/autocomplete/mvn-tests: the test file list and each listing.
# A cache file is reused while nothing under the */src/test dirs is newer than it (≤ 1 h).
cur=$AUTOCOMPLETE_CUR
case $cur in *,*) pre=${cur%,*},; w=${cur##*,} ;; *) pre=; w=$cur ;; esac
ann='^[[:space:]]*@(Test|ParameterizedTest|RepeatedTest|TestFactory|TestTemplate)([[:space:](]|$)'
dir=${XDG_CACHE_HOME:-$HOME/.cache}/autocomplete/mvn-tests
b=$dir/$(pwd | cksum | cut -d' ' -f1)
fresh() {
    [ -n "$(find "$1" -mmin -60 2>/dev/null)" ] || return 1
    sed 's|/src/test/.*|/src/test|' "$b.files" | sort -u | while read -r r; do
        find "$r" -newer "$1" -print 2>/dev/null | head -1
    done | grep -q . && return 1
    return 0
}
save() { mkdir -p "$dir" && cat > "$1.$$" && mv "$1.$$" "$1"; }
if ! fresh "$b.files"; then
    find . \( -name target -o -name node_modules -o -name .git \) -prune -o -path '*/src/test/*' -name '*.java' -print 2>/dev/null | save "$b.files"
fi
list() {
    case $w in
    *#*)
        cls=${w%%#*}
        grep "/$cls\.java\$" "$b.files" | tr '\n' '\0' | xargs -0 awk -v a="$ann" -v p="$cls#" '
            $0 ~ a { t = 1; next }
            t && /^[ \t]*@/ { next }
            t && match($0, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/) { s = substr($0, RSTART, RLENGTH); sub(/[ \t]*\($/, "", s); print p s; t = 0 }' | sort -u ;;
    *)
        # classes with test methods; failsafe names (IT*, *IT, *ITCase) are "it", the rest "unit"
        tr '\n' '\0' < "$b.files" | xargs -0 grep -lE "$ann" 2>/dev/null | while read -r f; do
            n=${f##*/}; n=${n%.java}
            case $n in IT*|*IT|*ITCase) k=it ;; *) k=unit ;; esac
            [ "$k" = "$1" ] && echo "$n"
        done | sort -u ;;
    esac
}
case $w in *#*) c=$b-$1-${w%%#*} ;; *) c=$b-$1 ;; esac
fresh "$c" || list "$1" | save "$c"
sed "s|^|$pre|" "$c"
