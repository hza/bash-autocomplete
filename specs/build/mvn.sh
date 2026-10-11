#!/bin/sh
# mvn -Dtest= / -Dit.test= values: sh mvn.sh unit|it
# CLASS, CLASS#method; comma lists complete the last item.
cur=$AUTOCOMPLETE_CUR
case $cur in *,*) pre=${cur%,*},; w=${cur##*,} ;; *) pre=; w=$cur ;; esac
ann='^[[:space:]]*@(Test|ParameterizedTest|RepeatedTest|TestFactory|TestTemplate)([[:space:](]|$)'
srcs() { find . \( -name target -o -name node_modules -o -name .git \) -prune -o -path '*/src/test/*' -name "$1.java" -print 2>/dev/null; }
case $w in
*#*)
    cls=${w%%#*}
    srcs "$cls" | xargs awk -v a="$ann" -v p="$pre$cls#" '
        $0 ~ a { t = 1; next }
        t && /^[ \t]*@/ { next }
        t && match($0, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/) { s = substr($0, RSTART, RLENGTH); sub(/[ \t]*\($/, "", s); print p s; t = 0 }' | sort -u ;;
*)
    # classes with test methods; failsafe names (IT*, *IT, *ITCase) are "it", the rest "unit"
    srcs '*' | xargs grep -lE "$ann" 2>/dev/null | while read -r f; do
        n=${f##*/}; n=${n%.java}
        case $n in IT*|*IT|*ITCase) k=it ;; *) k=unit ;; esac
        [ "$k" = "$1" ] && echo "$pre$n"
    done | sort -u ;;
esac
