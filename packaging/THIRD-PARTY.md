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
