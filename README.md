# SMB Manager <!-- omit in toc -->

- [Overview](#overview)
- [Original Problem](#original-problem)
- [Constraints](#constraints)
- [Development Notes](#development-notes)
- [Build](#build)
  - [Windows](#windows)
  - [Linux](#linux)
- [Tests](#tests)
- [Releasing](#releasing)

## Overview

SMB Manager allows you to connect to a remote SMB server, view
files/folders details including inode numbers and hard link counts, and then
find possible duplicate files and replace one with a link to the other.

It does this as QUICKLY as possible, and as such it shows potential matches
WITHOUT doing a comparison of the file contents. It relies on the human
operator to know what is actually a match. AS SUCH THIS APP IS VERY DANGEROUS.

I do want to add byte-by-byte comparisons at some point, but for my initial
use case, I didn't need it.

Here's what v0.3.1 looks like in action, with the interface for searching and
viewing results on the left, and detailed directory listings on the right:

![a screenshot of the app running on Windows](./docs/screenshot-v0.3.1.png)

## Original Problem

I've got an SMB share with large files that never change, but there are some
copies of the same file in different folders with different names, and because
the files are large, this wastes a lot of disk space. So I wanted to find these
duplicate files, and use [hard links](https://en.wikipedia.org/wiki/Hard_link)
to force them both to share the same bytes on disk.

There are command line solutions that will do this (e.g.
[jdupes](https://codeberg.org/jbruchon/jdupes)), but I wanted a different
experience, including:

1. do all work remotely via an existing SMB user, with that user's credentials
   and permissions.
2. avoid reading every byte of every file I wanted to compare, and instead
   quickly locate potential matches, then choose the actual matches by hand.
3. browse files/folders in a GUI, seeing inode and hard link info.

## Constraints

- GUI application
- App starts quickly, stays responsive during work (lightweight, batches slow
  work in threads)
- Low resource usage
- Cross platform (Windows & Linux required; macOS is nice-to-have)
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
  configure  - regenerate CMake's build files (run after editing CMakeLists.txt)
  release    - build smbmgr (Release, app only)
  debug      - build smbmgr (Debug, app only)
  test-unit  - build + run the serverless unit suite
  test-all   - build + run every suite (needs Docker)
  clean      - remove the build/ directory
```

| command     | notes                                                                                  |
| ----------- | -------------------------------------------------------------------------------------- |
| `configure` | Run this on a fresh sync or after a `clean`, or whenever `CMakeLists.txt` has changed. |
| `release`   | Generates a release executable. If it fails, try `configure release`.                  |
| `debug`     | Generates a debug executable. If it fails, try `configure debug`.                      |
| `test-unit` | Builds and runs unit tests. Does not require Docker.                                   |
| `test-all`  | Builds and runs unit tests + integration tests. Requires Docker.                       |
| `clean`     | `rm -rf build`. You'll need to re-run `configure` after.                               |

### Windows

- I developed this using [MSBuild 18.8 (Visual Studio 2026)](https://visualstudio.microsoft.com/)
- Qt's MSVC binaries can be installed by selecting "Custom Installation" in the [online installer](https://doc.qt.io/qt-6/qt-online-installation.html).
- To use git and bash scripts, I use [Git for Windows](https://git-scm.com/install/windows)
- To use the Makefile, I installed `make` via [scoop](https://scoop.sh/).
- To run the integration tests, you'll need [Docker](https://docs.docker.com/desktop/setup/install/windows-install/) installed and running.

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

| command          | notes                                                                                                   |
| ---------------- | ------------------------------------------------------------------------------------------------------- |
| `make test-unit` | Builds and runs the unit tests. These don't need a server.                                              |
| `make test-all`  | Builds and runs every test, including the integration and widget tests that need a Samba test server. |

Running anything beyond the unit tests requires **Docker** with Compose v2. The test run starts a Samba container, runs the SMB-backed tests against it, and then tears the container down. If Docker isn't on your PATH, those tests are skipped and only the unit tests run. See [ADR 4](./docs/decisions/0004-automated-test-architecture.md) for background.

## Releasing

`.github/workflows/release.yml` builds Windows and Linux artifacts and attaches them to a Release. This job requires the `VERSION` in `CMakeLists.txt` to match the pushed tag.

So the specific steps are:

1. Bump `VERSION` in the top-level `CMakeLists.txt`'s `project()` call. `VERSION` is used in the Linux `.deb`'s filename via CPack (`CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT"`). Note that the Windows zip's name is unversioned.

2. Create a Release with a tag `vMAJOR.MINOR.PATCH`, matching the `CMakeLists.txt` value exactly. The workflow verifies this in the `check-version` job.

3. The tag creation triggers the release workflow, and when the workflow is complete, it attaches the build artifacts to the GitHub Release automatically.

Note that you can manually run this workflow on any commit without a `v*` tag, and it will safely skip trying to upload the artifacts to a release.
