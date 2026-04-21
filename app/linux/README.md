# st80-2026 — Linux (SDL2)

SDL2 desktop frontend. Tested on Ubuntu 25.10. Other Debian /
Fedora derivatives should work; the `.deb` and `.rpm` packages
target them directly.

## Prerequisites

Build tools and SDL2 dev headers:

    sudo apt-get install build-essential cmake ninja-build \
                         pkg-config libsdl2-dev

For producing installable packages (optional):

    sudo apt-get install dpkg-dev rpm

## Clone, build, test

    git clone https://github.com/avwohl/st80-2026.git
    cd st80-2026
    cmake -S . -B build-linux -G Ninja
    cmake --build build-linux
    (cd build-linux && ctest --output-on-failure)

## Fetch the Xerox image

    mkdir -p reference/xerox-image
    curl -sSLo reference/xerox-image/VirtualImage \
        https://github.com/avwohl/st80-images/releases/download/xerox-v2/VirtualImage

## Run

    ./build-linux/app/linux/st80-linux reference/xerox-image/VirtualImage

`--no-window` runs a headless smoke pass — used by CI and the
`.deb` post-install sanity check.

## Mouse mapping

Three-button mouse mapping:

  * plain click         — red (select)
  * right-click / Alt+Left  — yellow (text menu)
  * middle-click / Ctrl+Left — blue (window menu)

## Build installable packages

    cd build-linux
    cpack -G DEB    # → st80_0.1.0_amd64.deb
    cpack -G RPM    # → st80-0.1.0-1.x86_64.rpm

Install:

    sudo dpkg -i build-linux/st80_*.deb       # Debian / Ubuntu
    sudo rpm -i  build-linux/st80-*.rpm       # Fedora / RHEL / SUSE

The package installs `/usr/bin/st80-linux` and a desktop menu
entry. It does not bundle the Xerox image — that has its own
licence terms; see `/usr/share/doc/st80/README.md` for the fetcher
command.
