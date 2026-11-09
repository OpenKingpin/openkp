/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

/*
========================================================================

.MDX triangle model file format (Kingpin)

Kingpin: Life of Crime introduced the .MDX model format, derived from Quake II's
.MD2 format but expanded for multi-part modular meshes and advanced visual effects.

Key differences from Quake II MD2:
1. Magic identifier is 'IDPX' (MDX_IDENT) with version 4 (MDX_VERSION).
2. OpenGL command stream (`ofs_glcmds`):
   - In MD2, each GL command is: [count, (s, t, vert_index)...].
   - In MDX, each command has an embedded sub-object ID header:
     [count, sub_object_id, (s_float, t_float, vert_index)...].
   - Texture coordinates are normalized floating point values [0.0..1.0]
     rather than 16-bit integer texcoords.
3. Sub-objects allow individual mesh parts (e.g., body segments, headwear,
   weapon attachments) to be toggled visible/invisible or textured separately
   via bitmasks (`hidden_parts`).
4. Frames retain the MD2 vertex compression (`dmd2trivertx_t`), where positions
   are packed into 8-bit unsigned integers relative to per-frame `scale` and
   `translate` vectors, with normals encoded as discrete lookup indices into
   the 162-entry direction table.
5. Additional SFX tables (`num_sfxdef`, `num_sfxent`, `num_sfxobj`) provide
   offsets for metadata used by Kingpin's special-effects subsystem.

========================================================================
*/

/** Magic four-character code: 'IDPX' in little-endian order. */
#define MDX_IDENT       MakeLittleLong('I','D','P','X')

/** Kingpin MDX format version number. */
#define MDX_VERSION     4

/** Maximum number of discrete triangles per MDX sub-mesh. */
#define MDX_MAX_TRIANGLES   6144

/** Maximum number of compressed vertices per frame. */
#define MDX_MAX_VERTS       4096

/** Maximum number of keyframe animations supported in a single MDX model. */
#define MDX_MAX_FRAMES      1024

/** Maximum number of skin textures referenced by an MDX file header. */
#define MDX_MAX_SKINS       32

/** Maximum string length of skin texture paths embedded in the header. */
#define MDX_MAX_SKINNAME    64

/**
 * @brief Kingpin .MDX file header structure on disk.
 *
 * All multi-byte integer offsets and counts are stored in little-endian format.
 * Offsets are measured in bytes from the start of the file.
 */
typedef struct {
    uint32_t    ident;          /**< Magic identifier: MDX_IDENT ('IDPX') */
    uint32_t    version;        /**< Format version: MDX_VERSION (4) */

    uint32_t    skinwidth;      /**< Native texture width (pixels) */
    uint32_t    skinheight;     /**< Native texture height (pixels) */
    uint32_t    framesize;      /**< Byte size of a single frame (header + vertices) */

    uint32_t    num_skins;      /**< Number of skin texture names */
    uint32_t    num_xyz;        /**< Number of vertices per frame */
    uint32_t    num_tris;       /**< Number of triangles */
    uint32_t    num_glcmds;     /**< Number of dwords in the GL command list */
    uint32_t    num_frames;     /**< Total number of animation frames */
    uint32_t    num_sfxdef;     /**< Number of SFX definitions */
    uint32_t    num_sfxent;     /**< Number of SFX entity attachments */
    uint32_t    num_sfxobj;     /**< Number of SFX sub-objects */

    uint32_t    ofs_skins;      /**< Offset to skin names (char[num_skins][MDX_MAX_SKINNAME]) */
    uint32_t    ofs_tris;       /**< Offset to triangle list */
    uint32_t    ofs_frames;     /**< Offset to keyframe data (dmd2frame_t array) */
    uint32_t    ofs_glcmds;     /**< Offset to packed GL command stream */
    uint32_t    ofs_vertexi;    /**< Offset to vertex indices table */
    uint32_t    ofs_sfxdef;     /**< Offset to SFX definition table */
    uint32_t    ofs_sfxent;     /**< Offset to SFX entity attachment table */
    uint32_t    ofs_sfxbbox;    /**< Offset to SFX bounding boxes */
    uint32_t    ofs_dummyend;   /**< Reserved legacy padding offset */
    uint32_t    ofs_end;        /**< End-of-file byte offset */
} dmdxheader_t;
