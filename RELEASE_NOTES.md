# v0.1.1

Source release of the ScanJet 4200C native C driver and GTK application.
The README now states the project's aim to counter the scanner's planned
obsolescence by keeping usable hardware out of electronic waste.

- Native libusb scanning with preview and adjustable final scan area.
- PNG, JPEG, TIFF, and PDF output from the GTK application.
- Linux and Apple Silicon macOS builds; 25 offline tests on each platform.
- Full-bed 600 dpi scanning verified on Linux and macOS; full-bed 600 × 1200
  dpi scanning verified on Linux.
- Source package excludes HP software, vendor documentation, research
  dumps, historical SANE source, and personal scan images.

The macOS application uses Homebrew libraries installed on the target Mac.
The current binaries are local development builds; this release provides
source archives and build instructions.
