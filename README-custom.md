# dolphin-custom

A checkout of KDE's [Dolphin](https://invent.kde.org/system/dolphin) to modify,
with scripts that build it and put it in place of the distribution's Dolphin.

* branch `custom`, based on **v26.08.0** — the version Arch ships (`dolphin 26.08.0-4`)
* branch `release/26.08` — untouched upstream, for rebasing onto later fixes

## Use it

```sh
./install-custom.sh              # build and install over the packaged Dolphin
./install-custom.sh --set-default   # ...and make it the file manager for folders
./uninstall-custom.sh            # put the distribution's Dolphin back
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
./install-custom.sh
```

Rebuilds are incremental, so after the first build a change takes seconds rather
than minutes.

### Moving to a newer Dolphin

```sh
git fetch origin
git rebase v26.08.1 custom     # or the next release tag
./install-custom.sh
```

If upstream has moved to a branch you do not have yet:

```sh
git remote set-branches --add origin release/26.12
git fetch origin
```

## What lands where

`install-custom.sh` runs `cmake --install` with the prefix `/usr`, so the build
overwrites the packaged files in place: `/usr/bin/dolphin`, the Dolphin
libraries and KIO/KPart plugins, the `.desktop` files, icons and translations.

Before the first install it tars up every file the `dolphin` package owns into
`/var/lib/dolphin-custom/package-files.tar`, and it keeps CMake's
`install_manifest.txt` next to it so the uninstaller knows what this build added.

## Caveats

* Upgrading the `dolphin` package overwrites your build. The pacman hook
  installed here prints a reminder; re-run `install-custom.sh` afterwards.
* `pacman -Qkk dolphin` will report the files as modified. That is expected.
* `uninstall-custom.sh` restores by reinstalling the package, which is the
  cleanest way back; the tarball is the fallback if the package is gone.
