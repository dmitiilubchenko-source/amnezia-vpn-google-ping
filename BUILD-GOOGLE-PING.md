# Build Google Ping & Rank for Windows x64

This guide covers the unofficial fork. The upstream [README](README.md) retains the original project documentation. The initial binary was built on Windows with MSVC 2022 14.44, Windows SDK 10.0.26100, Qt 6.10.0 msvc2022_64, Conan 2.33.0, CMake 4.4.4 and Ninja. Downloads and compilation can take several gigabytes and significant time.

## Prerequisites

Install Visual Studio 2022 Build Tools with Desktop development with C++ (MSVC v143 and Windows SDK), Python, Git, and the open-source Qt 6.10.0 MSVC 2022 x64 kit. Qt needs Core, Gui, Network, Qml/Quick/Quick Controls, Qt5Compat, RemoteObjects, Svg, ShaderTools and Test. Use the published component sources and licenses linked in [DEPENDENCY-SOURCES.md](DEPENDENCY-SOURCES.md).

Put CMake, Ninja and Conan on PATH, for example in a Python virtual environment:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install conan==2.33.0 cmake==4.4.4 ninja
```

Clone this fork and initialize its submodules:

Use an ASCII-only source/build path such as `C:\src\amnezia` on Windows. Qt 6.10's QML import scanner can fail on Cyrillic source paths. A directory junction with an ASCII path can point to an existing checkout without moving it.

```powershell
git clone --branch google-ping-v5.2-5.0.3.0 --recurse-submodules https://github.com/dmitiilubchenko-source/amnezia-vpn-google-ping.git
cd amnezia-vpn-google-ping
```

Alternatively extract the complete source ZIP attached to the release. GitHub's automatically generated source archives omit submodule contents. The build script requires the qtkeychain and SortFilterProxyModel sources to be present.

## Build and package

```powershell
.\tools\windows-google-ping.ps1 -QtRoot 'C:\Qt\6.10.0\msvc2022_64'
```

The script locates MSVC using vswhere, uses the committed Windows release Conan profile, configures Ninja Release, builds the client, service and three test executables, installs the runtime into `build/google-ping-package`, then runs the Premium latency, benchmark and gateway deadline suites. A failed test stops the script. It never registers a service, installs a driver, launches the VPN or changes a user subscription. Installer-only post-install scripts are removed from the package after a successful run.

For a portable MSVC/Windows SDK toolchain, pass `-MsvcEnvironment 'C:\path\setup_x64.bat'`. An optional `-ConanProfile` can supply a profile that includes `tools.microsoft.msbuild:installation_path=` and `tools.cmake.cmaketoolchain:generator=Ninja` under `[conf]` to keep Conan from searching for a registered Visual Studio installation. The environment script must initialize the x64 compiler, linker, SDK headers and libraries before CMake runs.

Local Conan recipes in the committed lockfile were exported with Windows CRLF line endings. The script temporarily normalizes recipe text to CRLF during configuration/build, then restores original bytes. This prevents different recipe revisions when extracting a source ZIP. Conan's remote services and their retained recipe revisions remain external dependencies. The locked OpenSSL recipe revision differs from the in-tree recipe in Android naming; Windows behavior is the same. Do not update the lockfile silently when reproducing a release.

The committed gateway key is a **public** RSA encryption key, recovered from the upstream Windows 5.0.3.0 application and verified against the public gateway service catalogue. It is not a private signing key or subscription credential. Public production gateway discovery URLs are passed through the existing upstream environment variables. Legacy API endpoints remain placeholders. Auto-update is disabled for this custom build.

## Tests

Add the packaged runtime directory to PATH before running the test executables:

```powershell
$env:PATH = (Resolve-Path build/google-ping-package).Path + ';' + $env:PATH
ctest --test-dir build/google-ping/client/tests -C Release -R '^(PremiumLatencyTest|PremiumBenchmarkTest|GatewayRequestDeadlineTest)$' --output-on-failure
```

The benchmark suite uses fake network/connection backends to test sorting, cancellation, restoration, connection drops, stalled requests and late replies without changing a live VPN. The gateway deadline suite verifies transport abortion before completion and cancellation of fallback replies. The native ICMP module in the original v5 release was separately compared with Windows ping on an active tunnel. That earlier check does not validate later changes to the native module. A full real Premium location sweep was not run during development.

For a small build of the benchmark and gateway deadline tests, without the VPN application, Conan, a subscription or initialized submodules, use a C++17 compiler and Qt Core, Network, Concurrent and Test development modules:

```powershell
cmake -S tools/benchmark-tests -B build/benchmark-tests -G Ninja -DCMAKE_PREFIX_PATH='C:\Qt\6.10.0\msvc2022_64'
cmake --build build/benchmark-tests
$env:PATH = 'C:\Qt\6.10.0\msvc2022_64\bin;' + $env:PATH
ctest --test-dir build/benchmark-tests --output-on-failure
```

Run these commands in an MSVC developer shell on Windows. The same CMake project also supports Linux/WSL with Qt 6 development packages; omit the Windows Qt path there. Linux execution covers the controller and deadline logic, not the Windows-native ICMP implementation.

Configuration requests have a total 60-second deadline, including fallback discovery. If this deadline expires, the check stops and asks the user to select the original location and reconnect manually. It does not issue more configurations after an indeterminate request. An unexpected tunnel drop during a switching test attempts to restore the original country and connection state; explicit user changes to the country or protocol are preserved.

Keep the original copyright notices and license files when redistributing. Publish the corresponding modified sources, scripts, and dependency source information alongside a binary release.
