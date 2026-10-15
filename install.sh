#!/bin/sh
# Installs a prebuilt autocomplete release (macOS arm64/x86_64, Linux x86_64/arm64).
#   curl -fsSL https://github.com/hza/bash-autocomplete/releases/latest/download/install.sh | sh
#   wget -qO-  https://github.com/hza/bash-autocomplete/releases/latest/download/install.sh | sh
# Env: PREFIX (default ~/.local), PROFILE (default ~/.bashrc), VERSION (tag, default latest).
# Uninstall: ... | sh -s -- --uninstall
set -eu

REPO=hza/bash-autocomplete
PREFIX=${PREFIX:-$HOME/.local}
PROFILE=${PROFILE:-$HOME/.bashrc}
VERSION=${VERSION:-latest}
INIT='eval "$(autocomplete bash)"'

die() { echo "autocomplete: $*" >&2; exit 1; }

# Profile line: bare name when PREFIX/bin is on PATH, absolute path otherwise
init_line() {
    case ":$PATH:" in
        *":$PREFIX/bin:"*) echo "$INIT" ;;
        *) echo "eval \"\$($PREFIX/bin/autocomplete bash)\"" ;;
    esac
}

if [ "${1:-}" = --uninstall ]; then
    rm -rf "$PREFIX/bin/autocomplete" "$PREFIX/share/autocomplete"
    if [ -f "$PROFILE" ]; then
        line=$(init_line)
        grep -vxF -e "$INIT" -e "$line" "$PROFILE" > "$PROFILE.tmp" || true
        cat "$PROFILE.tmp" > "$PROFILE" && rm -f "$PROFILE.tmp"
    fi
    echo "autocomplete removed from $PREFIX (your specs in ~/.config/autocomplete are kept)"
    exit 0
fi

arch=$(uname -m)
# a shell running under Rosetta reports x86_64 on Apple silicon
[ "$(sysctl -n hw.optional.arm64 2>/dev/null)" = 1 ] && arch=arm64
case "$(uname -s)-$arch" in
    Darwin-arm64) target=darwin-arm64 ;;
    Darwin-x86_64) target=darwin-x86_64 ;;
    Linux-x86_64 | Linux-amd64) target=linux-x86_64 ;;
    Linux-aarch64 | Linux-arm64) target=linux-arm64 ;;
    *) die "no prebuilt binary for $(uname -s) $(uname -m); build from source (make install)" ;;
esac

if [ "$VERSION" = latest ]; then
    base=https://github.com/$REPO/releases/latest/download
else
    base=https://github.com/$REPO/releases/download/$VERSION
fi

if command -v curl >/dev/null 2>&1; then
    fetch() { curl -fsSL -o "$2" "$1"; }
elif command -v wget >/dev/null 2>&1; then
    fetch() { wget -qO "$2" "$1"; }
else
    die "needs curl or wget"
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tarball=autocomplete-$target.tar.gz
echo "downloading $base/$tarball"
fetch "$base/$tarball" "$tmp/$tarball" || die "download failed"
fetch "$base/SHA256SUMS" "$tmp/SHA256SUMS" || die "download of SHA256SUMS failed"

want=$(grep " $tarball\$" "$tmp/SHA256SUMS" | cut -d' ' -f1)
if command -v sha256sum >/dev/null 2>&1; then
    got=$(sha256sum "$tmp/$tarball" | cut -d' ' -f1)
else
    got=$(shasum -a 256 "$tmp/$tarball" | cut -d' ' -f1)
fi
[ -n "$want" ] && [ "$want" = "$got" ] || die "checksum mismatch for $tarball"

mkdir -p "$tmp/x" && tar -xzf "$tmp/$tarball" -C "$tmp/x"
mkdir -p "$PREFIX/bin" "$PREFIX/share"
rm -rf "$PREFIX/share/autocomplete"
cp "$tmp/x/bin/autocomplete" "$PREFIX/bin/autocomplete"
chmod 755 "$PREFIX/bin/autocomplete"
cp -R "$tmp/x/share/autocomplete" "$PREFIX/share/autocomplete"
echo "installed $PREFIX/bin/autocomplete and $PREFIX/share/autocomplete"

line=$(init_line)
if grep -qF 'autocomplete bash' "$PROFILE" 2>/dev/null; then
    echo "$PROFILE: already set up"
else
    if [ -s "$PROFILE" ] && [ -n "$(tail -c 1 "$PROFILE")" ]; then echo >> "$PROFILE"; fi
    echo "$line" >> "$PROFILE"
    echo "$PROFILE: added $line"
fi

# macOS Terminal starts login shells, which read the first of these and never ~/.bashrc
if [ "$PROFILE" = "$HOME/.bashrc" ] && [ "$(uname -s)" = Darwin ]; then
    login=$HOME/.bash_profile
    for f in "$HOME/.bash_profile" "$HOME/.bash_login" "$HOME/.profile"; do
        [ -e "$f" ] && { login=$f; break; }
    done
    if ! grep -qF '.bashrc' "$login" 2>/dev/null; then
        echo
        echo "Warning: $login does not load ~/.bashrc, so new Terminal.app"
        echo "windows and tabs (login shells) won't get completion. Add this line to it:"
        echo
        echo '    [ -f ~/.bashrc ] && . ~/.bashrc'
    fi
fi

echo
echo "Done. Open a new terminal, or enable it in this one with:"
echo
echo "    $line"
