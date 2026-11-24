# ScanJet 4200C

An experimental C driver and GTK 4 scanning application for the HP ScanJet
4200C. The scanner communicates through libusb; no SANE backend or HP Windows
software is needed at runtime. This project is independent and is not
affiliated with or endorsed by HP.

The ScanJet 4200C is another victim of planned obsolescence: its hardware can
still make good scans, but software support for current computers disappeared.
This project aims to keep these scanners useful instead of sending them to
electronic waste.

The driver was developed by studying the scanner and the behavior of the
historical HP Windows driver. The C source implements the USB protocol,
calibration, scan planning, image assembly, and motor control. The repository
does not contain HP executables, DLLs, decompiler output, vendor manuals,
datasheets, or the earlier research workspace. Device register values and
behavior learned during reverse engineering are part of this implementation.

## Status

The native driver and GTK application have completed 300 dpi and 600 dpi
scans on Linux and Apple Silicon macOS. Full-bed 600 dpi scans completed on
both systems. A 600 × 1200 dpi full-bed scan completed on Linux; the GTK
application expands the horizontal samples for correct display proportions.
The Mac build has also completed preview, a manually adjusted 300 dpi PNG
scan, and a full-bed 600 dpi scan. The hardware and output should still be
checked after changes, particularly on other scanner units.

## Build

Requirements: C11 compiler, CMake 3.16 or newer, pkg-config, libusb 1.0,
libpng, and GTK 4. On macOS, Xcode command-line tools and the corresponding
Homebrew packages are needed.

Ubuntu/Debian dependencies:

```sh
sudo apt install build-essential cmake pkg-config libgtk-4-dev libusb-1.0-0-dev libpng-dev
```

macOS dependencies:

```sh
brew install cmake pkgconf gtk4 libusb libpng
```

Build and run the offline tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Run `build/bin/scanjet4200c` on Linux. On macOS, open
`build/bin/scanjet4200c.app`. The macOS bundle contains the `native_scan`
helper, but uses the Homebrew libraries installed on the same Mac. Connect
and power on the scanner before starting a preview.

For a read-only USB status check, run `build/bin/probe`. It does not move the
head. `build/bin/native_scan` is a diagnostic command-line scanner. The GTK
application lets you preview the glass, adjust a crop, select resolution and
format, then scan the selected area.

To build only the portable C driver and command-line tools:

```sh
cmake -S . -B build-driver -DHP4200_BUILD_GUI=OFF
cmake --build build-driver --parallel 2
ctest --test-dir build-driver --output-on-failure
```

## Licensing and provenance

The original C implementation in this repository is licensed under
GPL-3.0-or-later; see [LICENSE](LICENSE). Historical HP software and
third-party datasheets are deliberately absent. The HP name identifies
compatible hardware only. See [NOTICE.md](NOTICE.md) for distribution scope.
