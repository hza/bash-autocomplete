# dscl record paths for the local node: [dirs|users]
case "$1" in
dirs) dscl . -list / | sed 's|^|/|' ;;
users) dscl . -list /Users | grep -v '^_' | sed 's|^|/Users/|' ;;
*) dscl . -list / | sed 's|^|/|'
   for d in Users Groups; do dscl . -list /$d | sed "s|^|/$d/|"; done ;;
esac
