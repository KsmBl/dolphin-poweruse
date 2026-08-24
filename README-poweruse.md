# dolphin-poweruse

A checkout of KDE's [Dolphin](https://invent.kde.org/system/dolphin) to modify,
with scripts that build it and put it in place of the distribution's Dolphin.

* branch `custom`, based on **v26.08.0** — the version Arch ships (`dolphin 26.08.0-4`)
* branch `release/26.08` — untouched upstream, for rebasing onto later fixes

## What this fork changes

### Custom context menu entries, split by kind

**Settings → Configure Dolphin… → Custom Actions** holds two separate lists:

* **When directories are selected** — entries offered for a selection of folders
* **When files are selected** — entries offered for a selection of files

Each entry is a name, an icon and a command. They appear in the context menu
below the built-in entries, in their own group.

A selection that mixes files and folders gets neither list, since an entry
written for one kind rarely fits the other.

The command understands the usual placeholders:

| | |
|---|---|
| `%f` | path of the item |
| `%F` | paths of every selected item |
| `%u`, `%U` | the same as URLs |
| `%d` | the folder the item is in |

A command with no placeholder gets the paths appended, so `ark --add` works as
written. Everything is quoted, so spaces in names are safe.

Entries live in `~/.config/dolphinrc`:

```ini
[CustomActions][Directories][0]
Name=Open Terminal Here
Icon=utilities-terminal
Command=konsole --workdir %f
```

Touched upstream files: `src/dolphincontextmenu.{h,cpp}` (the entries),
`src/settings/dolphinsettingsdialog.cpp` (the page), `src/main.cpp` (the
`--drives` option) and `src/CMakeLists.txt`.
Everything else lives in `src/customactions.{h,cpp}` and
`src/settings/customactions/`, which keeps rebasing onto a new Dolphin cheap.

### A tab listing the connected drives

`drives:/` is a folder holding one entry per drive, partition, removable disk
and container Solid knows about — including LUKS and LVM volumes. Clicking an
entry jumps to that device's mount point; a device that is not mounted yet gets
mounted first, and an encrypted container asks for its passphrase at that
moment. Entries that are not mounted say so in their name.

The list is drawn as wide rows rather than icons: a same-sized icon on the
left, the device node above its mount point next to it, and a bar underneath
showing how full that partition or container is, with the used and total size
spelled out. Devices that are not mounted say so and have no bar.

Because it is an ordinary URL, the list is an ordinary tab: it can be split,
bookmarked, opened in a new window, or typed into the location bar.

```sh
dolphin --drives      # opens the list in a tab
dolphin drives:/      # the same thing
```

The listing itself is a KIO worker in `src/kioworkers/drives/`, installed
alongside Dolphin as `kf6/kio/kio_drives.so`, so the protocol is available to
every KDE application. The row drawing lives in `src/drives/`, which the view
container swaps in while a `drives:/` URL is shown.

## Use it

```sh
./install-poweruse.sh              # build and install over the packaged Dolphin
./install-poweruse.sh --set-default   # ...and make it the file manager for folders
./uninstall-poweruse.sh            # put the distribution's Dolphin back
```

The first run asks for your sudo password, installs the build dependencies, and
— since Dolphin was not installed on this machine — installs the `dolphin`
package first. That is deliberate: the package pulls in every library this build
needs at runtime, and gives the uninstaller something to restore.

Options:

```
-j <n>          parallel build jobs
--set-default   register Dolphin as the handler for folders
--no-build      install what is already in build/
--no-hook       skip the pacman upgrade-warning hook
```

## Making changes

```sh
git switch custom
# edit, then:
git commit -am "..."
./install-poweruse.sh
```

Rebuilds are incremental, so after the first build a change takes seconds rather
than minutes.

### Moving to a newer Dolphin

```sh
git fetch origin
git rebase v26.08.1 custom     # or the next release tag
./install-poweruse.sh
```

If upstream has moved to a branch you do not have yet:

```sh
git remote set-branches --add origin release/26.12
git fetch origin
```

## What lands where

`install-poweruse.sh` runs `cmake --install` with the prefix `/usr`, so the build
overwrites the packaged files in place: `/usr/bin/dolphin`, the Dolphin
libraries and KIO/KPart plugins, the `.desktop` files, icons and translations.

Before the first install it tars up every file the `dolphin` package owns into
`/var/lib/dolphin-poweruse/package-files.tar`, and it keeps CMake's
`install_manifest.txt` next to it so the uninstaller knows what this build added.

## Caveats

* Upgrading the `dolphin` package overwrites your build. The pacman hook
  installed here prints a reminder; re-run `install-poweruse.sh` afterwards.
* `pacman -Qkk dolphin` will report the files as modified. That is expected.
* `uninstall-poweruse.sh` restores by reinstalling the package, which is the
  cleanest way back; the tarball is the fallback if the package is gone.
