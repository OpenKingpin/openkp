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

Frames match MD2. Texture coordinates live in the gl-command list as
normalized floats, and each command is preceded by a sub-object id.
========================================================================
*/

#define MDX_IDENT       MakeLittleLong('I','D','P','X')
#define MDX_VERSION     4

#define MDX_MAX_TRIANGLES   4096
#define MDX_MAX_VERTS       2048
#define MDX_MAX_FRAMES      1024
#define MDX_MAX_SKINS       32
#define MDX_MAX_SKINNAME    64

typedef struct {
    uint32_t    ident;
    uint32_t    version;

    uint32_t    skinwidth;
    uint32_t    skinheight;
    uint32_t    framesize;

    uint32_t    num_skins;
    uint32_t    num_xyz;
    uint32_t    num_tris;
    uint32_t    num_glcmds;
    uint32_t    num_frames;
    uint32_t    num_sfxdef;
    uint32_t    num_sfxent;
    uint32_t    num_sfxobj;

    uint32_t    ofs_skins;
    uint32_t    ofs_tris;
    uint32_t    ofs_frames;
    uint32_t    ofs_glcmds;
    uint32_t    ofs_vertexi;
    uint32_t    ofs_sfxdef;
    uint32_t    ofs_sfxent;
    uint32_t    ofs_sfxbbox;
    uint32_t    ofs_dummyend;
    uint32_t    ofs_end;
} dmdxheader_t;
