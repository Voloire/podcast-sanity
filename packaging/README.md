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

Native x64 C++/Qt application. No Docker, WSL, Python or Qt SDK is needed on the
target machine. The MSI installs for the current user into
`%LOCALAPPDATA%\Podcast Sanity`, with Start menu and desktop shortcuts, and an
entry in Windows **Settings → Apps → Installed apps**. No administrator account
is required. A ZIP containing the same runtime is also produced.

Build prerequisites:

* Visual Studio Build Tools 2019 or 2022, C++ workload and Windows SDK;
* CMake **4.3+** (per-user MSI support), Qt 6.5+ Widgets and Multimedia for MSVC x64;
* WiX **3.14** (`candle.exe` and `light.exe`);
* vcpkg packages `mp3lame:x64-windows` and `libsndfile:x64-windows`.

Build, test and package from PowerShell:

```powershell
.\packaging\build-windows.ps1
# Defaults match the Windows development machine: MSVC 2019 and deps under $HOME.
# Override paths / generator on other machines:
.\packaging\build-windows.ps1 -QtRoot C:\Qt\6.8.1\msvc2022_64 `
  -VcpkgRoot C:\vcpkg -WixRoot C:\tools\wix `
  -Generator 'Visual Studio 17 2022'
```

Artifacts: `build-windows\packages\podcast-sanity-0.2.0-win64.msi` and `.zip`.
The release script requires all codecs and stops on a failed build or test.
`windeployqt` runs on both build output and the CPack install tree. The package
includes the Windows platform/audio plugins, codecs, transitive DLLs, Visual C++
runtime, and license notices. Merely deploying DLLs into a build folder is not
sufficient to package them; `deploy-windows.cmake.in` handles the install tree.

Verify the **installed MSI**, with SDK/Qt directories removed from the process
environment (this leaves the application installed):

```powershell
.\packaging\test-windows-msi.ps1 `
  -Msi .\build-windows\packages\podcast-sanity-0.2.0-win64.msi
```

This checks installation, CLI startup, all seven GUI pages, mastering and MP3
export. Logs and screenshots are in `build-windows\installer-test`. Microphone
enumeration can be checked with `podcast-sanity.exe --list-audio-devices`; the
selftest uses synthetic audio and does not record the microphone.

Keep `CPACK_WIX_UPGRADE_GUID` stable for future upgrades. The current build is
unsigned. `data/podcast-sanity.ico` is checked in; `generate-icon.py` is only an
optional artwork regeneration tool.

The Studio-branded installer offers a checked **Launch Podcast Sanity** option
on the finish page. The payload excludes the software OpenGL renderer and TLS,
network-information, generic and QML-tooling plugins. Qt Network remains a
Multimedia dependency; no network service is used by the application.

For upgrade and uninstall verification, preserve the original 0.1.0 installer:

```powershell
.\packaging\test-windows-msi.ps1 `
  -Msi .\build-windows\packages\podcast-sanity-0.2.0-win64.msi `
  -PreviousMsi .\build-windows\packages-0.1.0\podcast-sanity-0.1.0-win64.msi -Uninstall
```

Start without an installed newer version when using `-PreviousMsi`. The test
checks one Apps & features entry at 0.2.0 and one current-user MSI registration,
then checks runtime removal when `-Uninstall` is supplied. Windows may expose
the uninstall entry under HKLM even with the user product registered in HKCU;
the test checks both uninstall roots.

Signing requires an owner-provided certificate in the Windows certificate store:
`build-windows.ps1 -SignThumbprint <certificate-thumbprint>` signs both executables
before packaging and the MSI afterwards. `-TimestampUrl` overrides the default
RFC 3161 timestamp service. Without a thumbprint the build stays unsigned; signing
and timestamping were not exercised in the offline release verification.

Accepted WiX ICE warnings: ICE61 allows same-version reinstalls, ICE69 reflects
CPack's shortcut component layout, and ICE90/ICE91 reflect per-user directories.
Other validation diagnostics remain enabled.

## Codec availability

`src/encode.cpp` compiles the encoders in only when their library is found:
MP3 via LAME, FLAC and Ogg via libsndfile, WAV always. The export page hides any
format whose encoder is missing, so a package that ships without LAME simply
offers WAV and FLAC instead of failing at the last click.
