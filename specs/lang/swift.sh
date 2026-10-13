# swift.sh targets|products|executables: names from ./Package.swift (no build, no resolve)
f=Package.swift
[ -f "$f" ] || exit 0
flat=$(tr '\n' ' ' < "$f")
case "$1" in
targets)  pat='\.(target|executableTarget|testTarget|macro|plugin|systemLibrary|binaryTarget)\( *name: *"[^"]+"' ;;
products) pat='\.(executable|library|plugin)\( *name: *"[^"]+"' ;;
executables) pat='\.(executable\( *name|executableTarget\( *name): *"[^"]+"' ;;
*) exit 0 ;;
esac
printf '%s\n' "$flat" | grep -oE "$pat" | sed 's/^\.\([A-Za-z]*\).*"\(.*\)"$/\2	\1/' | sort -u -k1,1
