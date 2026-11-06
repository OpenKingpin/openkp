# OpenKP — agent notes

Local Q2PRO fork for an unofficial Kingpin multiplayer client. `upstream` is fetch-only. Do not push, open a pull request, add `origin`, or commit unless the owner asks in this conversation.

## Product

- First release is a 64-bit client that joins existing kpded2 servers.
- Base engine is Q2PRO. Do not switch the base to yquake2, KMKingpin, R1Q2, or KingpinQ3.
- Players bring their own `main/` data. Do not add paks or other game assets.
- Single-player and co-op stay out until the multiplayer client connects and plays.

## Do not vendor

- QuakeTools/Kingpin-SDK-v1.21, including `gamesrc/`, `kpdata.exe`, example maps, and `NavLib.lib`
- MonkeyHarris/monkey-mod, except a later port of `unlagged.c` that keeps Neil Toronto's GPL notice and does not bring `g_local.h` with it
- Prebuilt `navlib` objects. Pathfinding source, when a game module exists, is drFredz/Kingpin_NavLib (MIT, keep the notice)
- kingpin-linux payloads: 1999 `kingpin.x86`, `ref_glx.so`, and the SDK-built `gamei386.so`
- `msvcrt.lib` and `msvcrt_winxp.obj`
- yquake2remaster, q2repro, q2pro-ng
- KingpinQ3 as a tree. A single file can be proposed later; see docs/open-questions.md

## Engine facts

- Kingpin on the wire is protocol 32, 1400-byte datagrams, game port 31510, master `kingpin.info` UDP 27900.
- Q2PRO defaults are protocol 34, `MAX_MSGLEN` 32 KiB, port 27910, directory `baseq2`.
- Protocol files are shared with Q2PRO's server sources. Change them for the client. Do not build or ship `q2proded`.
- The client does not load the server's game library. Compile the `q2pro` target only.

## Writing

Match these docs. State the license of a file from its header, not from the GitHub sidebar. Do not invent cvars, opcodes, or ports.
