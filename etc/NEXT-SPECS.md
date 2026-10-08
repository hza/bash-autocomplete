# Next specs

Candidates for new bundled specs, best first. A spec is most useful when it has a **live value
source**: values that come from the system at TAB time and that nobody remembers. Each entry
lists those sources and the commands that produce them.

Done already: brew, defaults, launchctl, diskutil, open, kill/pkill/killall, caffeinate, say,
pbcopy, pbpaste, sips, screencapture, jq, curl, wget, code.

## macOS built-ins

### xcrun / simctl
`xcrun simctl boot|shutdown|erase|install|launch|terminate|openurl|io <device> ...`.
Used a lot by iOS developers, and device UDIDs are impossible to type by hand.
- Devices: `xcrun simctl list devices -j` → `UDID<TAB>name (state, runtime)`; also accept
  `booted`.
- Runtimes: `xcrun simctl list runtimes`; device types: `xcrun simctl list devicetypes`.
- `xcrun` itself: `--sdk` values from `xcodebuild -showsdks`, `--find <tool>`.
- Needs Xcode (not just the Command Line Tools) to test.

### networksetup
Long `-get…`/`-set…` flag list (dozens), each taking a network service.
- Services: `networksetup -listallnetworkservices | tail -n +2` (names have spaces, e.g.
  `Wi-Fi`, `USB 10/100/1000 LAN`; `emit` escapes them).
- Hardware ports/devices: `networksetup -listallhardwareports` (`en0` for `-setairportpower`).
- Wi-Fi networks: `networksetup -listpreferredwirelessnetworks en0`.
- Flags can come from `networksetup -help` / `-printcommands`.

### pmset
- `-g` subcommands: `live`, `custom`, `cap`, `sched`, `ups`, `ps`, `batt`, `pslog`, `rawlog`,
  `therm`, `thermlog`, `assertions`, `assertionslog`, `sysload`, `log`, `uuid`, `history`…
- Settings keys: `displaysleep`, `disksleep`, `sleep`, `womp`, `powernap`, `hibernatemode`,
  `standby`, `autopoweroff`, `lowpowermode`, `tcpkeepalive`… (from `pmset -g` output).
- Scopes `-a`, `-b`, `-c`, `-u`; `schedule`/`repeat` event types (`sleep`, `wake`, `poweron`,
  `shutdown`, `wakeorpoweron`).

### security
Keychain tool with ~60 subcommands; perfect for the `commands:` tree.
- Subcommands and descriptions: `security help`.
- Keychains: `security list-keychains` (paths, strip quotes).
- Common: `find-generic-password -s/-a/-w`, `add-generic-password`,
  `delete-generic-password`, `find-certificate -c/-a/-p`, `find-identity -v -p codesigning`,
  `unlock-keychain`, `dump-keychain`.
- Identities for `codesign -s`: `security find-identity -v -p codesigning` (reuse there).

### plutil
`-lint`, `-p`, `-convert xml1|binary1|json|swift|objc`, `-extract <keypath> <fmt>`,
`-insert`, `-replace`, `-remove`, `-type`, `-o file`, `-r`, `-s`. Args: files.
- Keypaths for `-extract`/`-replace`/`-remove`: would need the file name from the line;
  skip, or a future `@plist-keys` builtin.

### hdiutil
Verbs as `commands:`: `attach`, `detach`, `create`, `convert`, `mount`, `unmount`, `info`,
`verify`, `resize`, `compact`, `imageinfo`, `makehybrid`.
- `detach`/`unmount`: mounted volumes from `ls /Volumes` and `/dev/disk*` from
  `hdiutil info`.
- `-format` values: `UDZO UDBZ ULFO ULMO UDRW UDRO UDSP UDTO`; `-fs`: `APFS HFS+ HFS+J exFAT
  MS-DOS`.
- Args: `.dmg`/`.sparsebundle` files.

### codesign / spctl / xattr
The "app is damaged / can't be opened" fixes.
- codesign: `-s` identities from `security find-identity -v -p codesigning` (plus `-` for
  ad hoc), `--options runtime`, `--entitlements file`, `-d`, `-v`, `--deep`, `--force`.
- spctl: `--assess`, `--status`, `--add`, `--master-enable/disable`, `-t execute|install|open`.
- xattr: `-l`, `-p`, `-w`, `-d`, `-c`, `-r`; attribute names for `-d`/`-p` from
  `xattr <file>` (needs the file argument; otherwise a fixed list: `com.apple.quarantine`,
  `com.apple.metadata:kMDItemWhereFroms`, `com.apple.provenance`).

### softwareupdate
`--list`, `--install <label>`, `--all`, `--recommended`, `--restart`, `--download`,
`--fetch-full-installer --full-installer-version <ver>`, `--list-full-installers`,
`--install-rosetta --agree-to-license`, `--schedule on|off`, `--history`.
- Labels: from `softwareupdate --list` (`* Label: …` lines). Slow (network): consider not
  live, or accept the delay.
- Installer versions: `softwareupdate --list-full-installers`.

### tccutil
`tccutil reset <service> [bundle-id]`.
- Services (fixed list): `All`, `Accessibility`, `AddressBook`, `AppleEvents`, `Calendar`,
  `Camera`, `ListenEvent`, `Microphone`, `Photos`, `PostEvent`, `Reminders`, `ScreenCapture`,
  `SystemPolicyAllFiles`, `SystemPolicyDesktopFolder`, `SystemPolicyDocumentsFolder`,
  `SystemPolicyDownloadsFolder`, …
- Bundle ids: reuse the app-bundle exec from `open -b`.

### scutil
`--get` / `--set` with `ComputerName`, `LocalHostName`, `HostName`; also `--dns`, `--proxy`,
`--nwi`, `-r host`, `--nc list|status|start|stop <service>`.
- VPN services for `--nc`: `scutil --nc list` (quoted names).

### mdfind / mdutil / mdls
- mdfind: `-onlyin dir`, `-name`, `-live`, `-count`, `-0`, `-attr <attr>`, `-literal`,
  `-interpret`.
- mdls: `-name <attr>`, `-raw`, `-nullMarker`.
- Attribute names (`kMDItemContentType`, `kMDItemWhereFroms`, …): `mdimport -A` lists all,
  with descriptions.
- mdutil: `-s`, `-i on|off`, `-E`, `-a`, `-d`; args: volumes from `/Volumes` and `/`.

### Smaller ones
- **pbcopy/pbpaste**: done.
- **open -a**: done.
- **osascript**: `-e`, `-l AppleScript|JavaScript`, `-i`, `-s`; args: `.scpt`/`.applescript`
  files.
- **shortcuts**: `run <name>`, `list`, `view`, `sign`; shortcut names from `shortcuts list`
  (fun and easy, like `say -v`).
- **dscl**: `.` + `-read|-list|-create /Users/<name>`; users from `dscl . -list /Users`.
- **log**: `show|stream|collect|config`, `--predicate`, `--style`, `--level`,
  `--process <name>` (reuse process list from killall).
- **system_profiler**: data types from `system_profiler -listDataTypes`
  (`SPHardwareDataType`…), `-detailLevel mini|basic|full`, `-json`.
- **ditto**, **textutil** (`-convert txt|html|rtf|docx…`), **afplay**, **qlmanage**,
  **profiles**, **sw_vers**, **nettop**, **fs_usage**.

## Dev tools common on Macs

### rsync
Big flag list; generate from `rsync --help` like curl/wget. Host:path completion like scp
(reuse the ssh hosts exec). Note: macOS ships openrsync/rsync 2.6.9, Homebrew rsync 3.x has
more flags; generate from the one installed and say which in the spec description.

### ffmpeg
Huge, but the valuable parts are lists: `-c:v`/`-c:a` codecs from `ffmpeg -codecs`/
`-encoders`, `-f` formats from `ffmpeg -formats`, `-pix_fmt` from `ffmpeg -pix_fmts`,
`-preset` (`ultrafast` … `veryslow`). Args: files.

### swift
`swift build|test|run|package|repl`; `swift package` subcommands (`init --type`,
`resolve`, `update`, `show-dependencies`, `add-dependency`…); `run <target>`
from `swift package describe --type json` (executable products); `test --filter`.

### pod (CocoaPods), fastlane, mas
- pod: `install`, `update <pod>` (pods from `Podfile.lock`), `repo`, `spec`, `init`, `outdated`.
- fastlane: lanes from `fastlane lanes --json` or parse `fastlane/Fastfile` (`lane :name`).
- mas (Mac App Store CLI): `install|uninstall|upgrade|info|open <app-id>`; installed apps from
  `mas list` → `id<TAB>name`.

### colima / orbctl
Docker on Mac. colima: `start|stop|status|delete|ssh|list`, `--profile` from `colima list`,
`--cpu/--memory/--disk`, `--runtime docker|containerd`, `--vm-type qemu|vz`.
orbctl: `start|stop|list|create|delete|shell`, machines from `orbctl list`.

### Version managers: nvm, pyenv, rbenv, asdf
- pyenv/rbenv: `install` (available versions from `pyenv install --list`), `local`/`global`/
  `shell`/`uninstall` (installed from `pyenv versions --bare`).
- nvm: a shell function, not an executable; `complete -C` still works by name, but `nvm`
  must exist when the engine registers names. Versions from `ls ~/.nvm/versions/node`.
- asdf: plugins from `asdf plugin list`, versions from `asdf list <plugin>`.

## Engine ideas these would use
- **Value from another word on the line** (xattr names from the file argument, plutil
  keypaths): a generic `{exec, ...}` placeholder such as `$1`/`$FILE` for earlier positional
  words.
- **Shared exec snippets**: the app-bundle-id exec is now copied in open.yaml and
  screencapture.yaml (and would be in tccutil). Anchors aren't supported by the YAML parser;
  a builtin `@app-bundle-ids` (and `@processes`, `@pids`) would remove the copies.
- **Slow sources** (softwareupdate, brew search): a per-exec cache TTL.
