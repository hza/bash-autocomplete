# fastlane.sh: lanes from fastlane/Fastfile (or ./Fastfile): platforms, then lanes as "name<TAB>platform"
f=fastlane/Fastfile
[ -f "$f" ] || f=Fastfile
[ -f "$f" ] || exit 0
awk '
/^[ \t]*platform[ \t]+:/ { p = $2; sub(/^:/, "", p); sub(/[^A-Za-z0-9_].*/, "", p); print p "\tplatform"; next }
/^[ \t]*(private_)?lane[ \t]+:/ { if ($1 == "private_lane") next; n = $2; sub(/^:/, "", n); sub(/[^A-Za-z0-9_].*/, "", n); print n "\t" (p == "" ? "lane" : p " lane") }
/^end/ { p = "" }
' "$f" | sort -u -k1,1
