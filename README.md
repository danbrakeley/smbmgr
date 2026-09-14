# SMB Manager <!-- omit in toc -->

- [Overview](#overview)
- [Original Problem](#original-problem)
- [Dangers and Alternatives](#dangers-and-alternatives)
- [Design Constraints](#design-constraints)
- [Development Notes](#development-notes)
- [Build](#build)
  - [Windows](#windows)
  - [Linux](#linux)
- [Tests](#tests)
- [Releasing](#releasing)

## Overview

SMB Manager allows you to manage the files on a remote SMB server.

Features include:

- view remote inode information
- manage hard links
- search for possible duplicate files
- search for all groups of files that share hard links

This project was previously called Hard Link Manager (hardlinkmgr), but has
been renamed.

## Original Problem

Create an interactive GUI tool for remotely managing space on my NAS. I want
this tool to help me identify where space is being used, allow me to do basic
file management on files/folders, and to view and manage hard links.

## Dangers and Alternatives

If you aren't aware of hard links and inodes, make sure you understand what
they are and the dangers of using hard links before you use this app:

- [hard link (Wikipedia)](https://en.wikipedia.org/wiki/Hard_link)
- [inode (Wikipedia)](https://en.wikipedia.org/wiki/Inode)

If your use case involves duplicate files that you want to edit independently,
then hard links are not for you. I'd check out filesystems with
[COW](https://en.wikipedia.org/wiki/Copy-on-write) support. For example,
I know a Synology NAS that uses BTRFS can enable [Fast file clone](https://kb.synology.com/en-my/DSM/help/DSM/AdminCenter/file_service_advanced_introduction?version=7)
to get COW support on copies made through SMB. Something like that may be a
better solution for your use case.

Also, if you don't care about working via SMB, and you just want to find
duplicate files and replace them with hard links, then tools such as
[jdupes](https://codeberg.org/jbruchon/jdupes)) exist and are probably a
better match for what you want.

## Design Constraints

- GUI application
- App starts quickly (no leading screen/loading bar)
- App stays responsive during work (smart use of threads)
- Low resource usage
- Cross platform (Windows & Linux required; macOS is a future goal)
- Looks and feels like a native app on each platform.

## Development Notes

- [roadmap.md](./docs/roadmap.md) - Where this app is heading
- [ADRs](./docs/decisions/) - Architectural Decision Records

## Build

The included [`Makefile`](./Makefile) handles most common operations in a
cross-platform way.

```text
$ make help
Targets:
  configure        - regenerate CMake's build files (run after editing CMakeLists.txt)
  release          - build smbmgr (Release, app only)
  debug            - build smbmgr (Debug, app only)
  test-unit        - build + run the unit tests (pure logic, fast)
  test-integration - build + run unit + serverless integration tests (no Docker)
  test-docker      - build + run the Samba-backed suites (needs Docker)
  test-all         - build + run every suite (needs Docker)
  clean            - remove the build/ directory
```

| command            | notes                                                                                  |
| ------------------ | -------------------------------------------------------------------------------------- |
| `configure`        | Run this on a fresh sync or after a `clean`, or whenever `CMakeLists.txt` has changed. |
| `release`          | Generates a release executable. If it fails, try `configure release`.                  |
| `debug`            | Generates a debug executable. If it fails, try `configure debug`.                      |
| `test-unit`        | Builds and runs unit tests. Fast; does not require Docker.                             |
| `test-integration` | Builds and runs unit tests + serverless integration tests. Does not require Docker.    |
| `test-docker`      | Builds and runs the Samba-backed tests only. Requires Docker.                          |
| `test-all`         | Builds and runs every test. Requires Docker.                                           |
| `clean`            | `rm -rf build`. You'll need to re-run `configure` after.                               |

### Windows

- Developed using [MSBuild 18.8 (Visual Studio 2026)](https://visualstudio.microsoft.com/)
- Qt's MSVC binaries can be installed by selecting "Custom Installation" in the [online installer](https://doc.qt.io/qt-6/qt-online-installation.html).
- For git and bash, use [Git for Windows](https://git-scm.com/install/windows)
- To use the Makefile, install `make` (e.g. via [scoop](https://scoop.sh/)).
- To run the full test suite, install [Docker](https://docs.docker.com/desktop/setup/install/windows-install/).

Builds end up in `build\windows\bin\{Release|Debug}\smbmgr.exe`. Required Qt .dlls are copied into the same folder.

### Linux

For Ubuntu 26.04, here's the `apt install` line I used:

```bash
sudo apt install git curl build-essential cmake ninja-build qt6-base-dev qt6-svg-dev qt6-wayland libgl1-mesa-dev
```

- `cmake` + `ninja-build` — the `linux-*` presets use the Ninja generator.
- `qt6-base-dev` — Qt Widgets/Network/Test development files (Qt 6.10 on 26.04); the Test module's CMake config ships in this package too, so no separate package is needed to build the `tests/` suites.
- `qt6-svg-dev` — Qt6::Svg development files (headers + CMake config), needed for the toolbar/action icons. `qt6-base-dev` only pulls in the runtime library (`libqt6svg6`), not this, so it must be listed explicitly.
- `qt6-wayland` — Qt's Wayland platform plugin, so the app runs natively on Ubuntu's default Wayland session.
- `libgl1-mesa-dev` — OpenGL headers, required when linking against Qt6::Gui.

Additionally, you'll need **Docker** with Compose v2 to run all the tests.

## Tests

| command                 | notes                                                                                                           |
| ----------------------- | --------------------------------------------------------------------------------------------------------------- |
| `make test-unit`        | Builds and runs the unit tests: pure logic, no server or network, fast.                                         |
| `make test-integration` | Builds and runs the unit tests plus integration tests that need no server but touch the OS or network (slower). |
| `make test-docker`      | Builds and runs only the integration and widget tests that need a Samba test server.                           |
| `make test-all`         | Builds and runs every test.                                                                                     |

`make test-docker` and `make test-all` require **Docker** with Compose v2. The test run starts a Samba container, runs the SMB-backed tests against it, and then tears the container down. If Docker isn't on your PATH, those tests are skipped and only the unit tests run. See [ADR 4](./docs/decisions/0004-automated-test-architecture.md) for background.

## Releasing

`.github/workflows/release.yml` builds Windows and Linux artifacts and attaches them to a Release. This job requires the `VERSION` in `CMakeLists.txt` to match the pushed tag.

So the specific steps are:

1. Bump `VERSION` in the top-level `CMakeLists.txt`'s `project()` call. `VERSION` is used in the Linux `.deb`'s filename via CPack (`CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT"`). Note that the Windows zip's name is unversioned.

2. Create a Release with a tag `vMAJOR.MINOR.PATCH`, matching the `CMakeLists.txt` value exactly. The workflow verifies this in the `check-version` job.

3. The tag creation triggers the release workflow, and when the workflow is complete, it attaches the build artifacts to the GitHub Release automatically.

Note that you can manually run this workflow on any commit without a `v*` tag, and it will safely skip trying to upload the artifacts to a release.
