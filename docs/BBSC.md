# BBSC: Bloodborne seamless co-op in the port

BBSC (seamless co-op for Bloodborne, GPL-2.0-or-later; https://seamlesshunt.com) keeps a co-op
session alive through deaths, lamps, area changes, bosses and the Hunter's Dream, for up to five
hunters. It is written for shadp2p (a shadPS4 fork); the port carries it in its online module.

## Switching it on

Launcher, Online page: "Play online" and "Seamless co-op (BBSC)". Every player needs it on and the
same server: a shadNet server with BBSC's game server next to it. Without the switch nothing of
BBSC is installed and the online module behaves as before.

The launcher's defaults are BBSC's public servers, Seamless Hunt (0.6): shadNet
`play.seamlesshunt.com:31313`, WebAPI `http://play.seamlesshunt.com:31315`, game server
`http://play.seamlesshunt.com` (plain http: BBSC's game server listens on 20443 without TLS). The
account (online id and password) is made on https://seamlesshunt.com; the launcher only takes it.
shadPS4's public shadNet server (`srv.shadps4.net`) accepts only shadPS4's own builds since
2026-10-12: it signs the port out (notification 19) and then refuses its logins for a while.

BBSC's game server on this PC: when the server's own game server does not answer (its
`:20443/bb-eu/ss.info` timed out for players while the rest of the public server worked,
2026-10-14), `run.sh` starts BBSC's `bbsc_gameserver.py` (`scripts/bbsc_gameserver`, from
BBSC-Server-Linux) on 127.0.0.1:20443 and points the game's server there; it sends the summon
calls to the shadNet server's WebAPI, accounts and bells go to the shadNet server as before.
Messages and bloodstains are then this PC's only. It stops with the game; its logs and data are in
`<user>/bbsc/`. `BB_BBSC_LOCAL_SERVER=1` always, `=0` never, `auto` (default) when the server's
does not answer within 5 s. (BBSC's own `BBSC_RELAY` is for the TCP relay in its Windows DLL; the
port's relay is the UDP one on port 3478 of the shadNet server.)

Environment: `BB_ONLINE=1 BB_BBSC=1` (what the launcher sets). BBSC's own switches
(`SHADPS4_BBSC_*`, `SHADPS4_BLOODBORNE_*`, see `gpu/bbnet/shadps4/core/bbsc_persistence.h`) work as
in BBSC: most features are on by default, `=0` turns one off.

## Where it lives

| Part | Files |
|---|---|
| Game code (hooks, byte patches, session policy, world sync) | `gpu/bbnet/shadps4/core/bloodborne_re.cpp`, `bbsc_*.{h,cpp}`, `bbsc_messages.inc` (BBSC's, unchanged) |
| Network (signaling packets, relay fallback, room replay, placement header) | BBSC's changes merged into `gpu/bbnet/shadps4/core/libraries/network` and `np/` (three-way merge against shadp2p 6e97b40, the base of both); the relay as in BBSC's BBport build of 2026-10-14 (below) |
| NP lookup (`sceNpLookupNpId`, a third and later player) | `gpu/bbnet/shadps4/core/libraries/np/np_utility.{h,cpp}` (BBSC's), registered in `bbnet_register` |
| Hooks in the game's code (any game) | `gpu/shim/bbport_guest_code_hooks.{h,cpp}`: shadPS4's `Core::InstallGuestCodeHook` interface, the port's trampolines |
| What BBSC asks of shadPS4 | `gpu/bbnet/include/` (`core/cpu_patches.h`, `core/memory.h`, `core/file_format/psf.h`, `common/memory_patcher.h`), `gpu/bbnet/bbsc_glue.cpp` |
| Local game server | `scripts/bbsc_gameserver/` (BBSC's, unchanged; NoDreamForHunter format, MIT), chosen by `scripts/online.py bbsc-local`, started by `run.sh` |
| Start | `bbnet_image_ready` (`gpu/bbnet/bbnet_glue.cpp`), called by the runtime once the image is mapped and patched (`src/probe.c` -> `runtime_net_image_ready`) |

Differences from BBSC's own builds:
- The game's code stays writable while BBSC is on (`Core::KeepGuestCodeWritable`): BBSC writes
  byte patches into it and switches some while the game runs; the port's own code patches leave it
  so too.
- Any retail Bloodborne 1.09 (every region runs the same executable) counts as CUSA03173; every
  BBSC site is still byte-checked.
- Saves: `BBSC_SAVE_DIR` points at `<user>/savedata/<id>/CUSA00207/SPRJ0005` (the port's layout).
- Its loading screen is a line in the port's overlay (`bbgpu_overlay_banner`); the cinematic
  camera (F1, keyboard and mouse) gets the window's events through `bbgpu_add_event_filter`.
- Not ported: the animated title card (off by default in BBSC too), Discord status, its launcher's
  account creation and server cards (the port's launcher takes the server address and account).
- The "bell check" trace hook does not install (BBSC's own byte patch is at the same address); it
  only logs.

## Updates taken

- 2026-10-14, BBSC 0.0.1 "bug fixes" (its BBport build, `bbport-1.7.2-bbsc.patch`): the relay goes
  through the server for every known peer from the first frame (`SHADPS4_P2P_RELAY=auto` or
  `direct` = the old "direct first, relay after 2 s", `=0` off), peers are kept by online id with
  every endpoint as an alias (the one-way relay behind CGNAT that died after 30 s); the NP lookup
  above. Its signaling and matching files were already the same as here. The fixes inside
  `bbsc.dll` of that release (death and respawn, sign targeting, relay TCP framing) have no
  published source yet; they come in when BBSC publishes it.

## Checks

`out/dev/verify/verify.sh NAME BB_ONLINE=1 BB_BBSC=1`: loads the save offline-capable, 14 self-tests
pass, no faults (2026-10-14). Real co-op needs two players on a BBSC server.
