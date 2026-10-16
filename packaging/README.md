# Packaging

Both installers come from the same `CMakeLists.txt`; the generator is chosen by
platform.

## Linux — .deb

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && cpack -G DEB
# -> podcast-sanity_<version>_amd64.deb
```

The package contains:

| Path | What |
|------|------|
| `/usr/bin/podcast-sanity` | the Qt6 application |
| `/usr/bin/podcast-sanity-render` | the headless renderer |
| `/usr/share/applications/podcast-sanity.desktop` | launcher entry |
| `/usr/share/icons/hicolor/scalable/apps/podcast-sanity.svg` | icon |
| `/usr/share/metainfo/io.github.voloire.podcast-sanity.metainfo.xml` | AppStream |

Runtime dependencies are resolved by `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` and pinned
explicitly to `libqt6widgets6`, `libqt6multimedia6`, `libmp3lame0` and
`libsndfile1`. `qt6-qpa-plugins`, `qt6-multimedia-plugins` and `ffmpeg` are
recommended: the app runs without them in file/corpus test mode, but live
microphone capture needs the multimedia backend and a platform plugin.

Install and remove:

```sh
sudo apt install ./podcast-sanity_0.1.0_amd64.deb
sudo apt remove podcast-sanity
```

## Windows — MSI

The MSI is built on Windows; the toolchain cannot cross-build it from Linux.
vcpkg supplies LAME and libsndfile, and the CMake deploy step pulls in the Qt
runtime, so the resulting package is self-contained.

```powershell
# once, in the vcpkg checkout
vcpkg install mp3lame libsndfile --triplet x64-windows

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_BUILD_TYPE=Release -DSANITY_WITH_GUI=ON -DSANITY_WITH_EBUR128=OFF `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
      -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cd build; cpack -C Release
# -> podcast-sanity-<version>-win64.msi
# -> podcast-sanity-<version>-win64.zip   (same tree, no installer)
```

Notes:

* `CMAKE_TOOLCHAIN_FILE` must be a `-D` cache variable. Putting it in the
  environment does nothing, and the build then silently loses both codecs.
* `CPACK_WIX_UPGRADE_GUID` is fixed, so a newer MSI **upgrades** the existing
  install instead of stacking beside it. Never change it after the first public
  release.
* The MSI installs to `Program Files\podcast-sanity` and creates a Start Menu
  entry plus an uninstaller. The WiX Toolset must be on `PATH`.
* `data/podcast-sanity.ico` is used for the Add/Remove Programs entry. The
  optional `data/wix-banner.bmp`, `data/wix-dialog.bmp` and
  `packaging/wix_patch.xml` are referenced only when present.
* The Qt runtime is deployed by `qt_generate_deploy_app_script`, so no manual
  `windeployqt` step is needed. The codec DLLs and their transitive
  dependencies are collected from vcpkg's `bin` directory at install time.
* `CPack`'s WiX generator accepts only a `.txt` or `.rtf` licence file, so the
  Windows branch points it at a text copy of `LICENSE` made at configure time.

`.github/workflows/ci.yml` runs exactly this on a `windows-2022` runner and
publishes both artifacts, which is the supported way to produce a release.

## Codec availability

`src/encode.cpp` compiles the encoders in only when their library is found:
MP3 via LAME, FLAC and Ogg via libsndfile, WAV always. The export page hides any
format whose encoder is missing, so a package that ships without LAME simply
offers WAV and FLAC instead of failing at the last click.

Because that degradation is silent, the Windows CI job exports one MP3 and one
FLAC before packaging and fails if either file is missing.
