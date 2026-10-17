# Unofficial AmneziaVPN Google Ping & Rank

Modified distribution by [dmitiilubchenko-source](https://github.com/dmitiilubchenko-source), dated **2026-10-18**, based on [amnezia-vpn/amnezia-client](https://github.com/amnezia-vpn/amnezia-client), tag **5.0.3.0**, commit **de93650**.

This is an independent, experimental fork, not an official Amnezia release. It is not affiliated with, endorsed by, or signed by the Amnezia developers. The original Amnezia project and its contributors retain their authorship and copyright. Amnezia names and artwork identify the upstream project; no trademark ownership or endorsement is claimed.

The client and these modifications are distributed under the **GNU GPL version 3**, with the full text in [LICENSE](LICENSE). Third-party components retain their respective licenses; see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md), [dependency source links](DEPENDENCY-SOURCES.md), and the license files included in the Windows archive. The software is provided without warranty, as described in the license. No additional restriction on modification or redistribution is imposed by this fork.

## Changes in this distribution

- Windows / Premium location benchmark: sequential connections, five ICMP requests to the same Google IPv4 address, average RTT, loss, and sorted results.
- Checks that the test uses the VPN route; stops when the active subscription or relevant settings change.
- Cancellation and attempted restoration of the original location, protocol, and connection state.
- A measurement of the current connection without switching countries.
- Russian interface strings and a visible Google Ping & Rank v3 heading.
- An explicit message when another client instance prevents this build from starting.
- Automated tests, dependency lockfile, build instructions and scripts. Earlier diagnostic ICMP/HTTPS modules remain in the source.

Premium authentication and subscription requirements are unchanged. Users supply their own subscription. This distribution includes no user's VPN keys, account settings or credentials.

Source for the modified application is in this repository and in the source asset attached to the same release as the Windows binary. The source asset includes initialized third-party submodules, unlike GitHub's automatic source ZIP. Build scripts and configuration are in [BUILD-GOOGLE-PING.md](BUILD-GOOGLE-PING.md) and `tools/`; dependency recipes and source URLs are included in `recipes/`.

The initial Windows artifact was compiled from the upstream base plus the changes in this release before the publication commit was created. Its embedded upstream commit identifier remains de93650. The source release contains those changes; byte-for-byte reproduction also depends on timestamps, toolchains and downloaded packages.
