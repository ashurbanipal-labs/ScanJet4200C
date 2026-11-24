# Distribution scope

This repository contains project-authored C source, build configuration,
tests, and documentation. It omits the HP PrecisionScan installer and all
extracted executables, DLLs, drivers, resources, decompiler output, Ghidra
project files, and copied device datasheets. It also omits historical SANE
checkouts and scan images from the development workspace.

The implementation was informed by analysis of the scanner and historical
HP driver behavior. In particular, USB commands, register values, timing,
and calibration behavior are represented in the project-authored source.
The repository is an independent project, not an HP product.

The GNU General Public License applies to the project-authored source. It
does not grant rights to third-party works that are not included here.
