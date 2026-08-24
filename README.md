# dolphin-poweruse

A small fork of **[Dolphin](https://apps.kde.org/dolphin/)**, KDE's file
manager, with a handful of additions for people who live in their file manager:
context menu entries you define yourself, and a drive list that shows what is
mounted where and how full it is.

![The drives view](screenshots/drives.png)

---

## About the original

Dolphin is written and maintained by the **KDE community**, and everything that
makes it a good file manager is their work. This repository is only a thin layer
on top of it:

* Upstream source: <https://invent.kde.org/system/dolphin>
* Homepage and downloads: <https://apps.kde.org/dolphin/>
* Report Dolphin bugs to KDE, not here: <https://bugs.kde.org/>
* The original readme is kept as [README.upstream.md](README.upstream.md)

This fork is **not affiliated with, nor endorsed by, KDE e.V. or the Dolphin
maintainers**. It is licensed **GPL-2.0-or-later**, the same as upstream, and
tracks a release tag rather than diverging: the changes here are deliberately
small and self-contained so they can be rebased onto each new Dolphin release.

If you want Dolphin itself, install it from your distribution — you do not need
this fork.

Current base: **v26.08.0**.

---

## What this fork adds

### 1. Custom context menu entries, split by kind

**Settings → Configure Dolphin… → Custom Actions** holds two independent lists:

| List | Shown when |
|---|---|
| When directories are selected | the selection is made of folders |
| When files are selected | the selection is made of files |

Each entry is a name, an icon and a command, and they appear in the context menu
in their own group. A selection mixing files and folders gets neither list,
since an entry written for one kind rarely fits the other.

Commands understand the usual placeholders:

| Placeholder | Becomes |
|---|---|
| `%f` | the path of the item |
| `%F` | the paths of every selected item |
| `%u`, `%U` | the same, as URLs |
| `%d` | the folder the item is in |

A command with none of them gets the paths appended, so `ark --add` works as
written. Everything is quoted, so names with spaces arrive as one argument.

The command field has a browse button: pick an executable and its path is filled
in, or pick a `.desktop` file and its `Exec` line, name and icon come along,
leaving only the parameters to type.

Entries live in `~/.config/dolphinrc` under `[CustomActions][Directories]` and
`[CustomActions][Files]`, and are read each time the menu opens.

### 2. A drive list at `drives:/`

![One row of the drive list](screenshots/drive-row.png)

`drives:/` is a folder holding one entry per drive, partition, removable disk
and container Solid knows about — LUKS and LVM volumes included. Each row shows

* the **mount point** first, with `/home/you` written as `~`,
* the **device node** underneath it,
* and a **usage bar** with the used and total size spelled out.

Devices that are not mounted say so and have no bar.

What you can do with a row:

| Action | Result |
|---|---|
| Double-click | opens it; an unmounted device is mounted first |
| Right-click | open, mount/unmount, copy the mount point or device node, properties |
| Properties | the usual dialog on the mount point, so its size is worked out for you |

Mounting goes through Solid, which asks **UDisks2** to mount as the current user
— no root — and an encrypted container asks for its passphrase at that point.

Because it is an ordinary URL, the list behaves like any other tab: split it,
bookmark it, or type it into the location bar.

```sh
dolphin --drives      # open the list in a tab
dolphin drives:/      # the same thing
```

The listing is a KIO worker installed as `kf6/kio/kio_drives.so`, so `drives:/`
also works in other KDE applications.

---

## Install

```sh
./install-poweruse.sh                # build and install over the packaged Dolphin
./install-poweruse.sh --set-default  # ...and make it the handler for folders
./uninstall-poweruse.sh              # put the distribution's Dolphin back
```

The installer targets **Arch Linux**. It asks for your sudo password, installs
the build dependencies, and installs the `dolphin` package first if it is
missing — that package pulls in every library this build needs at runtime, and
gives the uninstaller something to restore. Before the first install it tars up
every file the package owns into `/var/lib/dolphin-poweruse/`.

Options:

```
-j <n>          parallel build jobs
--set-default   register Dolphin as the handler for folders
--no-build      install what is already in build/
--no-hook       skip the pacman upgrade-warning hook
```

On another distribution, build it the usual way and install wherever your
Dolphin lives:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

## Working on it

```sh
git switch custom     # the branch holding these changes
# edit, then:
./install-poweruse.sh # rebuilds incrementally and installs
```

Moving to a newer Dolphin is a rebase onto the new tag:

```sh
git fetch upstream
git rebase v26.08.1 custom
./install-poweruse.sh
```

The changes are kept as a thin patch on purpose. New code lives in
`src/customactions.{h,cpp}`, `src/settings/customactions/`, `src/drives/` and
`src/kioworkers/drives/`; upstream files only gain a call, a settings page
registration, the `--drives` option and their entries in `CMakeLists.txt`.

## Caveats

* Upgrading the `dolphin` package overwrites this build. The pacman hook
  installed here prints a reminder; re-run `install-poweruse.sh` afterwards.
* `pacman -Qkk dolphin` reports the files as modified. That is expected.
* `uninstall-poweruse.sh` restores by reinstalling the package, which is the
  cleanest way back; the tarball in `/var/lib/dolphin-poweruse/` is the fallback
  if the package is gone.

## License

GPL-2.0-or-later, inherited from Dolphin. See [LICENSES/](LICENSES) for the full
texts, and the upstream project for the copyright of everything this fork is
built on.
