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

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_BUILD_TYPE=Release -DSANITY_WITH_GUI=ON
cmake --build build --config Release --parallel
cd build; cpack -G WIX -C Release
# -> podcast-sanity-<version>-win64.msi
```

Notes:

* `CPACK_WIX_UPGRADE_GUID` is fixed, so a newer MSI **upgrades** the existing
  install instead of stacking beside it. Never change it after the first public
  release.
* The WiX generator needs the WiX Toolset on `PATH` and, for a self-contained
  installer, the Qt runtime DLLs. `packaging/wix_patch.xml` is the hook for that
  (see below); it is optional and the MSI builds without it.
* Branding images (`data/podcast-sanity.ico`, `data/wix-banner.bmp`,
  `data/wix-dialog.bmp`) are optional and referenced only when present.

### Bundling the Qt runtime

Either deploy the DLLs next to the executable with `windeployqt` before running
`cpack`:

```powershell
windeployqt --release --multimedia build\Release\podcast-sanity.exe
```

or list them in `packaging/wix_patch.xml` so WiX carries them into the MSI.

## Codec availability

`src/encode.cpp` compiles the encoders in only when their library is found:
MP3 via LAME, FLAC and Ogg via libsndfile, WAV always. The export page hides any
format whose encoder is missing, so a package that ships without LAME simply
offers WAV and FLAC instead of failing at the last click.
