#!/usr/bin/env bash
#
# dolphin-poweruse - put the distribution's Dolphin back
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BACKUP_DIR="/var/lib/dolphin-poweruse"
HOOK_FILE="/etc/pacman.d/hooks/95-dolphin-poweruse.hook"

keep_extras=0

msg()  { printf '\033[1;34m::\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m::\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m::\033[0m %s\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --keep-extras) keep_extras=1 ;;
        -h|--help)
            printf 'Usage: %s [--keep-extras]\n\n  --keep-extras  leave files this build added that the package does not have\n' "${0##*/}"
            exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done

[[ $EUID -ne 0 ]] || die "run this as your normal user - it calls sudo where it needs to."

# Files our build installed that the package does not own would survive a
# reinstall and shadow nothing - remove them unless asked otherwise.
[[ -d $BACKUP_DIR || ! -d /var/lib/dolphin-custom ]] || BACKUP_DIR="/var/lib/dolphin-custom"

if (( ! keep_extras )) && [[ -f $BACKUP_DIR/install_manifest.txt ]]; then
    msg "Removing files this build added that the package does not have ..."
    owned="$(mktemp)"
    pacman -Qlq dolphin 2>/dev/null | sed 's|/$||' | sort > "$owned"
    while read -r path; do
        [[ -n $path ]] || continue
        grep -qxF "$path" "$owned" || { [[ -f $path ]] && sudo rm -f "$path"; }
    done < "$BACKUP_DIR/install_manifest.txt"
    rm -f "$owned"
fi

if pacman -Qq dolphin >/dev/null 2>&1; then
    msg "Reinstalling the dolphin package to restore its files ..."
    sudo pacman -S --noconfirm dolphin
elif [[ -f $BACKUP_DIR/package-files.tar ]]; then
    msg "Package is gone; restoring the backed-up files instead ..."
    sudo tar -C / -xf "$BACKUP_DIR/package-files.tar"
else
    warn "Nothing to restore from: no dolphin package and no backup."
fi

msg "Removing our leftovers ..."
# The old name, for an install made before the project was renamed.
sudo rm -f "$HOOK_FILE" "/etc/pacman.d/hooks/95-dolphin-custom.hook"
sudo rm -rf "$BACKUP_DIR" "/var/lib/dolphin-custom"

sudo update-desktop-database /usr/share/applications 2>/dev/null || true

msg "Done - the distribution's Dolphin is back. ($PROJECT_DIR is untouched.)"
