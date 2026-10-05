#!/bin/bash
# Installs Batida (the Audio Unit) for this Mac user, from the latest release:
#
#   curl -fsSL https://raw.githubusercontent.com/ikeaang-hue/Oddfield-Batida/main/install.sh | bash
#
# The same line updates it; add "-s -- --uninstall" after bash to remove it.
#
# Batida isn't signed with an Apple Developer ID yet. macOS blocks unsigned
# plugins only when they carry the "downloaded from the internet" mark
# (com.apple.quarantine), which browsers add and curl doesn't, so this
# fetches the release with curl. Nothing needs a password: the plugin goes in
# ~/Library/Audio/Plug-Ins/Components, like a build from source.
#
# While the repository is private, the download goes through the GitHub CLI
# (gh), signed in to an account that can see it. BATIDA_ZIP picks another zip
# (a URL, or file:///path for a local one).

set -euo pipefail

repo="ikeaang-hue/Oddfield-Batida"
zip_name="Batida-macOS.zip"
components="$HOME/Library/Audio/Plug-Ins/Components"
component="$components/Batida.component"
library="$HOME/Music/Oddfield/Batida"

fail() { printf 'batida: %s\n' "$1" >&2; exit 1; }

# The latest release or, before there is one, the newest pre-release (GitHub's
# "latest" link skips pre-releases).
download() {
    if [ -n "${BATIDA_ZIP:-}" ]; then
        curl -fsSL -o "$1" "$BATIDA_ZIP"
        return
    fi
    curl -fsSL -o "$1" "https://github.com/$repo/releases/latest/download/$zip_name" 2> /dev/null && return
    local url tag
    url=$(curl -fsSL "https://api.github.com/repos/$repo/releases" 2> /dev/null |
          grep -o "\"browser_download_url\": *\"[^\"]*/$zip_name\"" | head -1 | sed 's/.*"\(http[^"]*\)"$/\1/') || true
    [ -n "$url" ] && curl -fsSL -o "$1" "$url" && return
    # While the repository is private: through the GitHub CLI.
    command -v gh > /dev/null || return 1
    tag=$(gh release list --repo "$repo" --exclude-drafts --limit 1 --json tagName --jq '.[0].tagName' 2> /dev/null) || return 1
    [ -n "$tag" ] && gh release download "$tag" --repo "$repo" --pattern "$zip_name" --output "$1" --clobber 2> /dev/null
}

# Hosts cache the list of Audio Units; this makes them look again.
refresh_hosts() { killall -9 AudioComponentRegistrar 2>/dev/null || true; }

[ "$(uname -s)" = "Darwin" ] || fail "Batida is a macOS Audio Unit."
macos_major=$(sw_vers -productVersion | cut -d. -f1)
[ "$macos_major" -ge 12 ] || fail "Batida needs macOS 12 or later (this Mac has $(sw_vers -productVersion))."

if [ "${1:-}" = "--uninstall" ]; then
    rm -rf "$component"
    refresh_hosts
    echo "Batida removed. Your library is still in $library."
    exit 0
fi
[ $# -eq 0 ] || fail "unknown option $1 (the only one is --uninstall)."

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "Downloading Batida…"
download "$tmp/Batida.zip" || fail "couldn't download Batida from github.com/$repo/releases."
ditto -x -k "$tmp/Batida.zip" "$tmp/unzipped" || fail "the download isn't a valid zip."
[ -d "$tmp/unzipped/Batida.component" ] || fail "the download has no Batida.component in it."

mkdir -p "$components"
rm -rf "$component"
mv "$tmp/unzipped/Batida.component" "$component"
# In case the zip came by another route (a browser, AirDrop) and carries the mark.
xattr -dr com.apple.quarantine "$component" 2>/dev/null || true
refresh_hosts

version=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$component/Contents/Info.plist" 2>/dev/null || echo "")
echo "Batida $version installed in $components."
if [ -d "/Library/Audio/Plug-Ins/Components/Batida.component" ]; then
    echo "There's an older Batida in /Library/Audio/Plug-Ins/Components too; remove it so hosts load this one:"
    echo "  sudo rm -rf /Library/Audio/Plug-Ins/Components/Batida.component"
fi
echo "Quit and reopen your host, then add Batida as an instrument (AU Instruments → Oddfield → Batida)."
