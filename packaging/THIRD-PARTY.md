# Windows runtime components

Podcast Sanity is GPL-3.0: see LICENSE and
https://github.com/Voloire/podcast-sanity for the application source.

The Windows package dynamically links Qt (Core, GUI, Widgets, Network, SVG,
Multimedia and plugins), available under LGPL-3.0 / GPL-3.0 terms:
https://www.qt.io/licensing/open-source-lgpl-obligations
Matching Qt sources: https://download.qt.io/archive/qt/

LAME, libsndfile and their codec dependencies are dynamically linked. Their
license notices are included in the adjacent package-specific directories.
Sources and build recipes: https://github.com/microsoft/vcpkg/tree/master/ports

Microsoft Visual C++ runtime libraries and Direct3D compiler redistributables
are included under Microsoft's redistribution terms. They remain the property
of their respective owners.

DLLs are installed beside the executables and may be replaced with compatible
versions. No container, Python interpreter or development SDK is required to run.

The Ubuntu font family is embedded under the Ubuntu Font Licence 1.0
(licenses/ubuntu-font/UFL-1.0.txt).
Source: https://assets.ubuntu.com/v1/0cef8205-ubuntu-font-family-0.83.zip
SHA-256: 61a2b342526fd552f19fef438bb9211a8212de19ad96e32a1209c039f1d68ecf
