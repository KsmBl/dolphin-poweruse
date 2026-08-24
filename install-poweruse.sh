#!/usr/bin/env bash
#
# dolphin-poweruse - build this checkout and put it in place of the system Dolphin
#
# The distribution's files are backed up first, and uninstall-poweruse.sh puts
# them back by reinstalling the package.
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
BACKUP_DIR="/var/lib/dolphin-poweruse"
HOOK_FILE="/etc/pacman.d/hooks/95-dolphin-poweruse.hook"

jobs_arg=""
install_hook=1
set_default=0
run_build=1

msg()  { printf '\033[1;34m::\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m::\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m::\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: ${0##*/} [options]

  -j <n>          Parallel build jobs (default: all cores)
  --set-default   Also make Dolphin the file manager for folders
  --no-hook       Skip the pacman upgrade-warning hook
  --no-build      Install what is already in build/ without rebuilding
  -h, --help      This text

Installing into /usr needs your sudo password.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j) jobs_arg="-j${2:-}"; shift ;;
        --set-default) set_default=1 ;;
        --no-hook) install_hook=0 ;;
        --no-build) run_build=0 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

[[ $EUID -ne 0 ]] || die "run this as your normal user - it calls sudo where it needs to."
command -v pacman >/dev/null || die "this installer targets Arch Linux (pacman not found)."
[[ -f $PROJECT_DIR/CMakeLists.txt ]] || die "run this from the Dolphin checkout."

# --- the distribution package ------------------------------------------------
# Installing it first is the cheapest way to get every runtime dependency, and
# it gives uninstall-poweruse.sh something to restore.
if ! pacman -Qq dolphin >/dev/null 2>&1; then
    warn "The dolphin package is not installed."
    warn "Installing it now: it pulls in the libraries this build needs at runtime,"
    warn "and it is what uninstall-poweruse.sh restores afterwards."
    sudo pacman -S --needed dolphin
fi

PACKAGE_VERSION="$(pacman -Q dolphin 2>/dev/null | awk '{print $2}')"
CHECKOUT_VERSION="$(git -C "$PROJECT_DIR" describe --tags --abbrev=0 2>/dev/null || echo unknown)"
msg "Package: dolphin $PACKAGE_VERSION, checkout: $CHECKOUT_VERSION"
case "$PACKAGE_VERSION" in
    "${CHECKOUT_VERSION#v}"*) ;;
    *) warn "Checkout and package versions differ - that is fine, but the files this"
       warn "build installs may not line up exactly with the package's." ;;
esac

# --- build dependencies ------------------------------------------------------
missing=()
for pkg in extra-cmake-modules cmake ninja gcc kdoctools packagekit-qt6; do
    pacman -Qq "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
done
if (( ${#missing[@]} )); then
    msg "Installing build dependencies: ${missing[*]}"
    sudo pacman -S --needed --noconfirm "${missing[@]}"
fi

# --- build -------------------------------------------------------------------
if (( run_build )); then
    msg "Building (this takes a few minutes) ..."
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr \
          -DBUILD_TESTING=OFF
    cmake --build "$BUILD_DIR" ${jobs_arg:+$jobs_arg}
fi
[[ -x $BUILD_DIR/bin/dolphin ]] || die "no dolphin binary in $BUILD_DIR/bin."

# --- carry over an install made under the old name ---------------------------
OLD_BACKUP_DIR="/var/lib/dolphin-custom"
OLD_HOOK_FILE="/etc/pacman.d/hooks/95-dolphin-custom.hook"
if [[ -d $OLD_BACKUP_DIR && ! -d $BACKUP_DIR ]]; then
    msg "Moving the backup from $OLD_BACKUP_DIR to $BACKUP_DIR"
    sudo mv "$OLD_BACKUP_DIR" "$BACKUP_DIR"
fi
if [[ -f $OLD_HOOK_FILE ]]; then
    sudo rm -f "$OLD_HOOK_FILE"
fi

# --- back up the packaged files ----------------------------------------------
sudo mkdir -p "$BACKUP_DIR"
if [[ ! -f $BACKUP_DIR/package-files.tar ]]; then
    msg "Backing up the distribution's Dolphin to $BACKUP_DIR/package-files.tar"
    pacman -Qlq dolphin | while read -r path; do [[ -f $path ]] && printf '%s\n' "${path#/}"; done \
        | sudo tar -C / -cf "$BACKUP_DIR/package-files.tar" -T -
    printf '%s\n' "$PACKAGE_VERSION" | sudo tee "$BACKUP_DIR/package-version" >/dev/null
else
    msg "Keeping the existing backup in $BACKUP_DIR."
fi

# --- install -----------------------------------------------------------------
msg "Installing over the system Dolphin ..."
sudo cmake --install "$BUILD_DIR" >/dev/null
sudo cp -f "$BUILD_DIR/install_manifest.txt" "$BACKUP_DIR/install_manifest.txt" 2>/dev/null || true

sudo update-desktop-database /usr/share/applications 2>/dev/null || true
sudo gtk-update-icon-cache -q /usr/share/icons/hicolor 2>/dev/null || true

if (( set_default )); then
    xdg-mime default org.kde.dolphin.desktop inode/directory
    msg "Dolphin is now the file manager for folders."
fi

# --- pacman hook: an upgrade puts the distribution's Dolphin back -------------
if (( install_hook )); then
    sudo install -d /etc/pacman.d/hooks
    sudo tee "$HOOK_FILE" >/dev/null <<EOF
# Installed by dolphin-poweruse
[Trigger]
Operation = Upgrade
Type = Package
Target = dolphin

[Action]
Description = dolphin-poweruse: your build was replaced by the upgrade
When = PostTransaction
Exec = /usr/bin/bash -c 'printf "\n>> dolphin was upgraded, so the distribution build is back.\n>> Rebase your changes and re-run %s\n\n" "$PROJECT_DIR/install-poweruse.sh"'
EOF
    msg "Pacman hook installed at $HOOK_FILE"
fi

cat <<EOF

$(msg "Done.")
$(dolphin --version 2>/dev/null | head -1)
  binary:  $(command -v dolphin)
  sources: $PROJECT_DIR  (branch $(git -C "$PROJECT_DIR" rev-parse --abbrev-ref HEAD))

Make your changes, then re-run this script to build and install them again.
To go back to the distribution's Dolphin: $PROJECT_DIR/uninstall-poweruse.sh
EOF
