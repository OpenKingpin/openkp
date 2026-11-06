# Contributing to OpenKP

Welcome to the OpenKP project! We are building a modern, 64-bit client for *Kingpin: Life of Crime* (1999) based on the battle-tested Q2PRO engine.

Our first milestone is an unofficial multiplayer client that connects to existing `kpded2` and Monkey Mod servers that the community already runs, with modern display support, raw input, high tickrates, and smooth prediction.

Whether you are a developer, server admin, or player from the Kingpin.info community, your feedback, bug reports, and pull requests are warmly welcomed.

---

## Guiding Principles

1. **Protocol Compatibility**: Speak Kingpin protocol 32 over the wire (1400-byte datagrams, default port 31510, master servers on UDP 27900). Join existing servers without requiring changes to their game libraries.
2. **Asset Isolation**: Players bring their own `main/` directory (from Steam, GOG, or original CD). **Never commit commercial Kingpin assets** (PAKs, maps, audio, textures) to this repository.
3. **Clean Code & Licensing**: The codebase is licensed under **GPLv2+**. Maintain clean C99/C11 code consistent with Q2PRO's architecture. Do not vendor or paste raw Kingpin SDK or conflicting proprietary code.
4. **Multiplayer Focus**: Single-player, co-op, and server binaries (`q2proded`) are intentionally out of scope for the initial release until the multiplayer client connects and plays solidly.

---

## Quick Start (Building from Source)

### Prerequisites

* **Linux (Debian/Ubuntu):**
  ```bash
  sudo apt-get install meson ninja-build gcc \
      libsdl2-dev libopenal-dev libpng-dev libjpeg-dev zlib1g-dev \
      libcurl4-gnutls-dev libx11-dev libxi-dev libwayland-dev libdecor-0-dev
  ```
* **Linux (Arch):**
  ```bash
  sudo pacman -S meson ninja gcc sdl2 openal libpng libjpeg-turbo zlib curl libx11 libxi wayland libdecor
  ```
* **Windows (MinGW-w64 via MSYS2) or MSVC:**
  Install Meson, Ninja, and your chosen toolchain.

### Build the Client

```bash
# Configure the build directory (defaults to Kingpin's 'main' folder)
meson setup builddir -Dbase-game=main -Dsave-games=false

# Compile the openkp client
meson compile -C builddir openkp
```

The resulting binary will be located at:
* Linux: `builddir/openkp`
* Windows: `builddir/openkp.exe` (or `openkp64.exe`)

---

## Running in Development

You do **not** need to copy the compiled binary into your Kingpin directory during development. Simply pass `+set sys_basedir` pointing to your Kingpin install:

```bash
./builddir/openkp +set sys_basedir /path/to/Kingpin/
```

Or copy the compiled `openkp` binary directly into your Kingpin installation directory next to `main/`.

---

## Codebase Map

To help you find your way around the codebase:

| Subsystem | Key Files | What it Handles |
| :--- | :--- | :--- |
| **Protocol 32 & Netcode** | `src/client/parse.c`, `src/common/msg.c`, `inc/common/protocol.h` | Kingpin packet parsing, entity delta flags, extra opcodes (`svc_hud`, `svc_muzzleflash3`). |
| **Model & Mesh Loading** | `src/refresh/models.c`, `inc/format/mdx.h`, `inc/refresh/refresh.h` | Kingpin MDX format loading, multi-part meshes (head/body/legs), vertex animations. |
| **World & BSP** | `src/common/bsp.c`, `src/refresh/surf.c`, `src/refresh/mesh.c` | BSP version 38 loading, surface flags, Kingpin lightmaps, textures, and sky surfaces. |
| **UI & Menus** | `src/client/ui/kpmenu.c`, `src/client/ui/ui.c`, `src/client/screen.c` | In-game Kingpin UI styling, console fonts, scoreboard layout, and Bagman/DM HUD elements. |
| **Server Browser** | `src/client/ui/servers.c` | Querying Kingpin master servers on UDP 27900 (`master.kingpin.info`, `master0-4.kingpin.info`, `master2.shavargo.com`). |
| **Player Movement** | `src/common/pmove/template.c` | Client-side prediction matching Kingpin physics, speeds, and ducking/jumping behavior. |
| **Platform Video** | `src/unix/video/sdl.c`, `src/unix/video/x11.c`, `src/windows/client.c` | Window creation, raw input, display modes, and embedded application icons. |

---

## Priority Areas for Help

We especially welcome contributions in these areas:

1. **HUD & Scoreboard Polish**:
   - Bagman team cash balances, stolen money indicators, and proper positioning of weapon/armor counters.
2. **Player Model Tinting & Skins**:
   - Multi-part Kingpin character models (head, torso, legs) and team skin color tinting.
3. **Master Server & Server Browser**:
   - Querying and merging server lists across all active Kingpin masters on UDP 27900 without duplicate listings.
4. **Movement Prediction Tuning**:
   - Fine-tuning physics differences (`DF_NO_BUNNY`, Kingpin crouch speeds, weapon switch timings) to eliminate client/server misprediction on live servers.
5. **Windows Builds & CI Testing**:
   - Testing MinGW and MSVC builds, packaging release archives, and testing on various hardware.

---

## Pull Request Guidelines

1. **Keep PRs focused**: Smaller, atomic pull requests that tackle one feature or bug fix are easiest to review and merge quickly.
2. **Follow existing style**: 4-space indentation, consistent curly bracket placement, and readable naming conventions matching the Q2PRO foundation.
3. **Test against live servers**: Whenever modifying networking or client movement, test by connecting to a live `kpded2` server to confirm compatibility.
4. **Check your build**: Ensure `meson compile -C builddir openkp` compiles without new warnings or errors before opening a PR.

---

Thank you for helping bring a modern, smooth experience back to Kingpin multiplayer!
