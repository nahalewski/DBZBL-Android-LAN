# Android and Mac LAN fighting

These ports emulate the game's Xbox networking/session calls over direct UDP on your local network. They do not connect to Xbox LIVE or provide public internet matchmaking. Each device needs its own game installation.

## Prepare both devices

- Install/build compatible versions from the **same source commit and tag state**. Discovery compares the compatibility version exactly. A newer source build may not find an older installed APK even if both boot.
- Connect both devices to the same local IPv4 network. Mac Ethernet and Android Wi-Fi work when the router bridges them; using the same ordinary Wi-Fi is simplest.
- Avoid guest Wi-Fi, AP/client isolation, or a VPN that blocks local traffic. Wireless ADB connection is independent of the game.
- Allow local network access for the Mac app if macOS asks, and allow the app through the firewall.
- Use matching gameplay settings/mods. Start with default 30 FPS, normal rendering and no mods. For initial testing, a single 60-second round with Drama off is the previously tested setup.

The direct paths use UDP **59395** for gameplay, **36001** for discovery and **3074** for supporting metadata. Permit traffic between local peers. Router internet port-forwarding is unnecessary for LAN.

## Enable discovery

**Android:** LAN is enabled by default. Open **Controls → LAN play**, select automatic discovery if you previously chose Offline, save, then fully close the game from Recents and reopen. An optional manual host IPv4 address is available; normally leave it blank.

**Mac:** the native app uses direct LAN/discovery defaults. For a source checkout, run `./macos/host-lan.command` to detect the local address and select the tested network/renderer settings. The script can be used for either hosting or searching; it does not create a room itself. A previously saved Offline/incompatible setting can still require correction.

## Host a fight

1. Open **Versus → Online Battle → Player Match → Create Match**.
2. Set **Private Session off**. Choose the round/time/Drama options.
3. Create the room and remain in its session lobby.

A host only advertises after a real public session exists. Merely launching the app or selecting Host is not enough. Discovery omits full, private, deleted or incompatible rooms and the searcher's own room.

## Join the fight

1. On the guest, open **Versus → Online Battle → Player Match → Custom Match**.
2. Use broad/Random search filters matching the host's rules.
3. Select the host's **gamertag**, then confirm with **A** (Mac keyboard Space).
4. Both players select **Ready**.
5. Choose characters, confirm abilities/options and stage as the game requests.

Searches are bounded and can be repeated if the host was still creating a room. Compatible public rooms include the host name and match properties; blank numbered rows are not the expected discovery behavior. Leave and reopen search if the host changes its name or session.

## Gamertags

Android: **Controls → Gamertag**, enter 1–15 printable characters and apply. The name takes effect without restarting and is saved. Existing joined lobbies may cache the previous name; leave and rejoin to refresh it.

Mac source launcher: save a name in `out/mac-user-data/gamertag.txt`, or pass `--online_gamertag='YourName'`. Finder launches use `online_gamertag` in their Application Support `burstlimit.toml`. These changes apply at the next Mac launch. See the [Mac guide](../macos/README.md#controls-and-gamertag).

## Optional manual addressing

Android: use **Controls → LAN play** to supply the host's IPv4, then fully close and reopen.

Mac source checkout:

```sh
env -u REX_XNET_BIND_IP -u REX_LAN_DISABLED \
  REX_XNET_IP=MAC_LOCAL_IPV4 REX_XNET_SEARCH_IP=HOST_IPV4 \
  ./macos/run.sh --mnk_mode=true --vulkan_dynamic_rendering=false \
  --online_mode=direct --online_input_delay=off \
  --online_tick_sleep=0 --online_fast_tick=true
```

Replace the two address placeholders. Use `REX_XNET_IP` to advertise the local address. **Leave `REX_XNET_BIND_IP` unset** for one game instance per device: the game sends its match-start notification to itself at `127.0.0.1`, so a socket bound exclusively to the Wi-Fi address can stall the start.

`host-lan.command` clears old manual search/bind overrides and uses discovery. Do not use its automatic interface detection unchanged on a host whose VPN/default route selects the wrong interface; explicitly use the correct local IPv4.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| No host listed | Host has created a public room, Private Session off, room not full, compatible builds, broad filters, discovery enabled on both devices |
| Discovery works but join fails | Gameplay/metadata UDP allowed, same local network, host still in its room, no stale manual address |
| Fight stalls immediately after Ready | No Wi-Fi-only bind override; correct local advertised IPv4; matching settings/builds; normal direct timing |
| Old or blank name | Both builds include discovery metadata; repeat Custom Match search; leave/rejoin an existing lobby after renaming |
| Host not found after app update | Update both ends from the same commit/tag state; a historical installed version uses a different compatibility identifier |
| Black fight while controls work | Renderer/device coverage issue; see compatibility, keep tested defaults. Working UDP/menu input does not prove working 3D gameplay. |

Two completed rounds were verified between an M1 Pro Mac and first-generation Pixel Fold, with input observed in both directions. This is bounded LAN coverage, not a guarantee of extended stability or internet play. Current Samsung gameplay/LAN and Razer Edge remain unverified.
