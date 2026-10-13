# asdf.sh installed|all|plugins-all: versions for the plugin named on $COMP_LINE
# (first word after the subcommand that is not a flag). Prints nothing without asdf.
command -v asdf >/dev/null 2>&1 || exit 0
case $1 in
plugins-all) asdf plugin list all 2>/dev/null | awk '{print $1}'; exit 0 ;;
esac
set -- "$1" $COMP_LINE
mode=$1; shift
# skip "asdf", the subcommand (and "list all" / "latest")
shift
[ "$1" = all ] && shift
plugin=$1
case $plugin in ""|-*) exit 0 ;; esac
# plugin must be complete (followed by another word or a space)
case $COMP_LINE in *"$plugin "*) ;; *) exit 0 ;; esac
if [ "$mode" = all ]; then
	echo latest
	asdf list all "$plugin" 2>/dev/null | sort -rV 2>/dev/null || asdf list all "$plugin" 2>/dev/null
else
	asdf list "$plugin" 2>/dev/null | sed 's/^[ *]*//'
	echo system
fi
