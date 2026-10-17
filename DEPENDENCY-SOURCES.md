# Dependency sources and licenses

The modified client is GPLv3; original notices are preserved in [LICENSE](LICENSE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). This fork does not modify the third-party libraries listed below. Binary releases contain additional component license texts in `licenses/`. Dynamic Qt DLLs can be replaced with ABI-compatible builds. The application source and build scripts allow rebuilding/relinking the client and statically linked libraries.

Source downloads below are maintained by the respective upstream projects. Version pins, archive checksums where provided, patches and build options for Conan dependencies are in `conan.lock`, `conanfile.py` and `recipes/`. The complete application source asset contains the initialized git submodules and their license notices.

| Component | Version/source for this Windows build | Source location |
| --- | --- | --- |
| Amnezia client | 5.0.3.0, upstream de93650 plus fork changes | This repository and complete source asset on the same release |
| Qt | 6.10.0, upstream MSVC 2022 x64 kit | [Complete source archive](https://download.qt.io/archive/qt/6.10/6.10.0/single/qt-everywhere-src-6.10.0.tar.xz), [module repositories and license files](https://code.qt.io/cgit/qt/qt5.git/tree/?h=v6.10.0) |
| OpenSSL | 3.6.2 | [Source tag](https://github.com/openssl/openssl/tree/openssl-3.6.2), `recipes/openssl` |
| OpenVPN | 2.7.0, upstream plus committed build patches | [Source tag](https://github.com/OpenVPN/openvpn/tree/v2.7.0), `recipes/openvpn` |
| libssh | 0.11.3, static library | [Source archive](https://www.libssh.org/files/0.11/libssh-0.11.3.tar.xz), `recipes/libssh` |
| zlib | 1.3.2 | [Source tag](https://github.com/madler/zlib/tree/v1.3.2) |
| LZ4 | 1.10.0 | [Source tag](https://github.com/lz4/lz4/tree/v1.10.0) |
| LZO | 2.10 | [Source archive](https://www.oberhumer.com/opensource/lzo/download/lzo-2.10.tar.gz) |
| AmneziaWG Windows | 3.1.20260814 | [Source tag](https://github.com/amnezia-vpn/amneziawg-windows/tree/v3.1.20260814), `recipes/awg-windows` |
| Wintun | 0.14.1 | [Source repository](https://git.zx2c4.com/wintun/), [source tag](https://git.zx2c4.com/wintun/tag/?h=0.14.1), `recipes/wintun` |
| TAP Windows | 9.27.0 | [Source tag](https://github.com/OpenVPN/tap-windows6/tree/9.27.0), `recipes/tap-windows6` |
| Mullvad split-tunnel driver | 1.2.5.0, source 198ecde6e8cf547a01810b9ae87cefba562f7f99 | [Exact driver source](https://github.com/mullvad/win-split-tunnel/tree/198ecde6e8cf547a01810b9ae87cefba562f7f99), [binary snapshot pinned by upstream](https://github.com/mullvad/mullvadvpn-app-binaries/tree/ff0e3746c89a04314377cffeb52faaa976413a69), `recipes/win-split-tunnel` |
| Amnezia Xray bindings | 1.4.0, including its pinned Go modules | [Source tag](https://github.com/amnezia-vpn/amnezia-xray-bindings/tree/v1.4.0), `recipes/amnezia-xray-bindings` |
| tun2socks | 2.6.0, including its pinned Go modules | [Source tag](https://github.com/xjasonlyu/tun2socks/tree/v2.6.0), `recipes/tun2socks` |
| Routing data | v2ray-rules-dat 202603162227 | [Upstream data release](https://github.com/Loyalsoldier/v2ray-rules-dat/releases/tag/202603162227), `recipes/v2ray-rules-dat` |
| QtKeychain | 7460df6a978669290de5b56c2d98b199b61c3f88 | [Source commit](https://github.com/frankosterfeld/qtkeychain/tree/7460df6a978669290de5b56c2d98b199b61c3f88), `client/3rd/qtkeychain` |
| SortFilterProxyModel | f2881493e42bd7b7d5b7abe804dad084dd610b71 | [Source commit](https://github.com/mitchcurtis/SortFilterProxyModel/tree/f2881493e42bd7b7d5b7abe804dad084dd610b71), `client/3rd/SortFilterProxyModel` |
| Other bundled client sources/assets | Versions embedded in the repository | `client/3rd/`, `client/images/flagKit/`, and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) |

Microsoft runtime files and driver signing certificates are supplied under their upstream redistribution terms; Visual Studio and Windows SDK themselves are separate build prerequisites. No Microsoft toolchain is included in the release source archive. Qt-generated module SBOMs from the original kit are included in the Windows release for component attribution.
