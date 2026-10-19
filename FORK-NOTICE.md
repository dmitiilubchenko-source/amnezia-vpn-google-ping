# Unofficial AmneziaVPN Google Ping & Rank

Modified distribution by [dmitiilubchenko-source](https://github.com/dmitiilubchenko-source), dated **2026-10-19**, based on [amnezia-vpn/amnezia-client](https://github.com/amnezia-vpn/amnezia-client), tag **5.0.3.0**, commit **de93650**.

This is an independent, experimental fork, not an official Amnezia release. It is not affiliated with, endorsed by, or signed by the Amnezia developers. The original Amnezia project and its contributors retain their authorship and copyright. Amnezia names and artwork identify the upstream project; no trademark ownership or endorsement is claimed.

The client and these modifications are distributed under the **GNU GPL version 3**, with the full text in [LICENSE](LICENSE). Third-party components retain their respective licenses; see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md), [dependency source links](DEPENDENCY-SOURCES.md), and the license files included in the Windows archive. The software is provided without warranty, as described in the license. No additional restriction on modification or redistribution is imposed by this fork.

## Changes in this distribution

- Windows / Premium two-stage benchmark: five ICMP measurements per country, then thirty measurements at one-second intervals for up to five candidates, all using one Google IPv4 address. A warm-up request is excluded.
- Median, nearest-rank P95, observed loss, and mean absolute changes between adjacent successful RTTs. Detailed results and quick estimates are explicitly separated; sorting prioritizes loss, P95, median and RTT variation within each group.
- Checks that the test uses the VPN route; stops when the active subscription or relevant settings change.
- Cancellation and attempted restoration of the original location, protocol, and connection state.
- A thirty-request measurement of the current connection without switching countries.
- Favorites, a favorites-only view and thirty-request tests of selected countries.
- Encrypted local per-subscription persistence of the latest country results and favorites, dated history labels, a 24-hour stale marker and a leader from the latest detailed check. No automatic connection changes based on that recommendation.
- Overall progress and an approximate remaining time; separate test runs are visibly distinguished.
- Russian interface strings and a visible v5 marker on the Google Ping & Rank page.
- An explicit message when another client instance prevents this build from starting.
- Automated tests, dependency lockfile, build instructions and scripts. Earlier diagnostic ICMP/HTTPS modules remain in the source.
- v5.1 fixes: attempted restoration after unexpected tunnel drops, bounded configuration requests, separate local ICMP failures, periodic stale-result refresh, and build-script test execution. These changes are included in the local v5.1 build; the previously published v5 EXE does not include them.
- Post-v5 addition: one speedometer button pings known cached server endpoints with three ICMP requests and up to four concurrent probes. It does not switch the VPN or request new configurations. Unknown/expired endpoints are shown as a dash; connected checks use the current route. Per-country five-request buttons were removed.

Premium authentication and subscription requirements are unchanged. Users supply their own subscription. This distribution includes no user's VPN keys, account settings or credentials.

Source for the modified application is in this repository and in the source asset attached to the same release as the Windows binary. The source asset includes initialized third-party submodules, unlike GitHub's automatic source ZIP. Build scripts and configuration are in [BUILD-GOOGLE-PING.md](BUILD-GOOGLE-PING.md) and `tools/`; dependency recipes and source URLs are included in `recipes/`.

The v3 Windows artifact was compiled from upstream de93650 plus its changes; v4 was compiled from fork commit 2def035 plus the v4 changes; v5 was compiled from fork commit 000728d plus the v5 changes, before their respective publication commits were created. The source asset on each release contains that release's changes. Byte-for-byte reproduction also depends on timestamps, toolchains and downloaded packages. Previous releases and tags are retained.

The local v5.1 build was compiled on 2026-10-19 from fork commit ef4516b plus the changes described in FIXES-VALIDATION.md. Its modified Windows source archive includes the initialized Windows submodules. The package includes all dependency notices retained from v5, the same dependency versions, new Windows test logs, and a UI preview explicitly marked as test data.

The local v5.2 update fixes a queued-connect benchmark race, waits up to 90 seconds for the tunnel, accepts invalid informational service dates without dropping initialization, and distinguishes missing server ICMP replies from Windows API errors. The quick-ping summary reports known/unknown targets and outcomes; diagnostics omit endpoint addresses and credentials.
