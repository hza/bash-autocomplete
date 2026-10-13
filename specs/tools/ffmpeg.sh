# ffmpeg.sh vencoders|aencoders|sencoders|decoders|muxers|demuxers|pix_fmts|filters|hwaccels: "name<TAB>desc" lines
command -v ffmpeg >/dev/null 2>&1 || exit 0
ff() { ffmpeg -hide_banner "$@" 2>/dev/null; }
# codec/format tables: after the " ---" / " --" separator, "FLAGS name  description"
table() { awk -v t="$1" 'sep && (t == "" || substr($1, 1, 1) == t) { n = $2; $1 = ""; $2 = ""; sub(/^ +/, ""); print n "\t" $0; next } /^ *-+ *$/ { sep = 1 }'; }
case "$1" in
vencoders) ff -encoders | table V; printf 'copy\tcopy the stream as is\n' ;;
aencoders) ff -encoders | table A; printf 'copy\tcopy the stream as is\n' ;;
sencoders) ff -encoders | table S; printf 'copy\tcopy the stream as is\n' ;;
decoders) ff -decoders | table "" ;;
muxers) ff -muxers | table "" ;;
demuxers) ff -demuxers | table "" ;;
pix_fmts) ff -pix_fmts | awk 'sep { print $2 "\t" $3 " components, " $4 " bits/pixel" } /^-----/ { sep = 1 }' ;;
filters) ff -filters | awk 'sep && NF >= 4 { n = $2; d = ""; for (i = 4; i <= NF; i++) d = d (i > 4 ? " " : "") $i; print n "\t" d } /^ *Filters:/ { h = 1 } h && /^ *[A-Z.]+ +[a-z]/ { sep = 1 }' ;;
hwaccels) ff -hwaccels | sed '1d' ;;
esac
