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
// cl_newfx.c -- MORE entity effects parsing and management

#include "client.h"

void CL_Flashlight(int ent, const vec3_t pos)
{
    cdlight_t   *dl;

    dl = CL_AllocDlight(ent);
    VectorCopy(pos, dl->origin);
    dl->radius = 400;
    dl->die = cl.time + 100;
    VectorSet(dl->color, 1, 1, 1);
}

/*
======
CL_ColorFlash - flash of light
======
*/
void CL_ColorFlash(const vec3_t pos, int ent, int intensity, float r, float g, float b)
{
    cdlight_t   *dl;

    dl = CL_AllocDlight(ent);
    VectorCopy(pos, dl->origin);
    dl->radius = intensity;
    dl->die = cl.time + 100;
    VectorSet(dl->color, r, g, b);
}

/*
======
CL_DebugTrail
======
*/
void CL_DebugTrail(const vec3_t start, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    cparticle_t *p;
    float       dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 3;
    VectorScale(vec, dec, vec);
    VectorCopy(start, move);

    while (len > 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        VectorClear(p->accel);
        VectorClear(p->vel);
        p->alpha = 1.0f;
        p->alphavel = -0.1f;
        p->color = 0x74 + (Q_rand() & 7);
        VectorCopy(move, p->org);
        VectorAdd(move, vec, move);
    }
}

void CL_ForceWall(const vec3_t start, const vec3_t end, int color)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    VectorScale(vec, 4, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= 4;

        if (frand() > 0.3f) {
            p = CL_AllocParticle();
            if (!p)
                return;
            VectorClear(p->accel);

            p->time = cl.time;

            p->alpha = 1.0f;
            p->alphavel =  -1.0f / (3.0f + frand() * 0.5f);
            p->color = color;
            for (j = 0; j < 3; j++)
                p->org[j] = move[j] + crand() * 3;
            p->vel[0] = 0;
            p->vel[1] = 0;
            p->vel[2] = -40 - (crand() * 10);
        }

        VectorAdd(move, vec, move);
    }
}


/*
===============
CL_BubbleTrail2 (lets you control the # of bubbles by setting the distance between the spawns)

===============
*/
void CL_BubbleTrail2(const vec3_t start, const vec3_t end, int dist)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         i, j;
    cparticle_t *p;
    float       dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = dist;
    VectorScale(vec, dec, vec);

    for (i = 0; i < len; i += dec) {
        p = CL_AllocParticle();
        if (!p)
            return;

        VectorClear(p->accel);
        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (1 + frand() * 0.1f);
        p->color = 4 + (Q_rand() & 7);
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 2;
            p->vel[j] = crand() * 10;
        }
        p->org[2] -= 4;
        p->vel[2] += 20;

        VectorAdd(move, vec, move);
    }
}

void CL_Heatbeam(const vec3_t start, const vec3_t forward)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    int         i;
    float       c, s;
    vec3_t      dir;
    float       ltime;
    float       step = 32.0f, rstep;
    float       start_pt;
    float       rot;
    float       variance;

    VectorCopy(start, move);
    len = VectorNormalize2(forward, vec);

    ltime = cl.time * 0.001f;
    start_pt = fmodf(ltime * 96.0f, step);
    VectorMA(move, start_pt, vec, move);

    VectorScale(vec, step, vec);

    rstep = M_PIf / 10.0f;
    for (i = start_pt; i < len; i += step) {
        for (rot = 0; rot < M_PIf * 2; rot += rstep) {
            p = CL_AllocParticle();
            if (!p)
                return;

            p->time = cl.time;
            VectorClear(p->accel);
            variance = 0.5f;
            c = cosf(rot) * variance;
            s = sinf(rot) * variance;

            // trim it so it looks like it's starting at the origin
            if (i < 10) {
                VectorScale(cl.v_right, c * (i / 10.0f), dir);
                VectorMA(dir, s * (i / 10.0f), cl.v_up, dir);
            } else {
                VectorScale(cl.v_right, c, dir);
                VectorMA(dir, s, cl.v_up, dir);
            }

            p->alpha = 0.5f;
            p->alphavel = -1000.0f;
            p->color = 223 - (Q_rand() & 7);
            for (j = 0; j < 3; j++) {
                p->org[j] = move[j] + dir[j] * 3;
                p->vel[j] = 0;
            }
        }

        VectorAdd(move, vec, move);
    }
}


/*
===============
CL_ParticleSteamEffect

Puffs with velocity along direction, with some randomness thrown in
===============
*/
void CL_ParticleSteamEffect(const vec3_t org, const vec3_t dir, int color, int count, int magnitude)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (Q_rand() & 7);

        for (j = 0; j < 3; j++)
            p->org[j] = org[j] + magnitude * 0.1f * crand();

        VectorScale(dir, magnitude, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, u, p->vel);

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY / 2;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

void CL_ParticleSteamEffect2(cl_sustain_t *self)
{
    CL_ParticleSteamEffect(self->org, self->dir, self->color, self->count, self->magnitude);
    self->nextthink += 100;
}

/*
===============
CL_TrackerTrail
===============
*/
void CL_TrackerTrail(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    vec3_t      forward, up, angle_dir;
    int         i, count, sign;
    cparticle_t *p;
    const int   dec = 3;
    float       dist;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(vec, forward);
    vectoangles2(forward, angle_dir);
    AngleVectors(angle_dir, NULL, NULL, up);

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    sign = ent->trailcount;
    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -2.0f;
        p->color = 0;
        dist = 8 * cosf(DotProduct(move, forward) * M_PIf / 64);
        if (sign & 1)
            dist = -dist;
        VectorMA(move, dist, up, p->org);
        VectorSet(p->vel, 0, 0, 5);

        VectorAdd(move, vec, move);
        sign ^= 1;
    }

    ent->trailcount = sign;
    VectorCopy(move, ent->lerp_origin);
}

// Marsaglia 1972 rejection method
static void RandomDir(vec3_t dir)
{
    float x, y, s, a;

    do {
        x = crand();
        y = crand();
        s = x * x + y * y;
    } while (s > 1);

    a = 2 * sqrtf(1 - s);
    dir[0] = x * a;
    dir[1] = y * a;
    dir[2] = -1 + 2 * s;
}

void CL_Tracker_Shell(const centity_t *ent, const vec3_t origin)
{
    vec3_t          org, dir, mid;
    int             i, count;
    cparticle_t     *p;
    float           radius, scale;

    if (cl.csr.extended) {
        VectorAvg(ent->mins, ent->maxs, mid);
        VectorAdd(origin, mid, org);
        radius = ent->radius;
        scale = Q_clipf(ent->radius / 40.0f, 1, 2);
        count = 300 * scale;
    } else {
        VectorCopy(origin, org);
        radius = 40.0f;
        scale = 1.0f;
        count = 300;
    }

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = 0;
        p->scale = scale;

        RandomDir(dir);
        VectorMA(org, radius, dir, p->org);
    }
}

void CL_MonsterPlasma_Shell(const vec3_t origin)
{
    vec3_t          dir;
    int             i;
    cparticle_t     *p;

    for (i = 0; i < 40; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = 0xe0;

        RandomDir(dir);
        VectorMA(origin, 10, dir, p->org);
    }
}

void CL_Widowbeamout(cl_sustain_t *self)
{
    static const byte   colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    vec3_t          dir;
    int             i;
    cparticle_t     *p;
    float           ratio;

    ratio = 1.0f - (self->endtime - cl.time) / 2100.0f;

    for (i = 0; i < 300; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(self->org, (45.0f * ratio), dir, p->org);
    }
}

void CL_Nukeblast(cl_sustain_t *self)
{
    static const byte   colortable[4] = {110, 112, 114, 116};
    vec3_t          dir;
    int             i;
    cparticle_t     *p;
    float           ratio;

    ratio = 1.0f - (self->endtime - cl.time) / 1000.0f;

    for (i = 0; i < 700; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(self->org, (200.0f * ratio), dir, p->org);
    }
}

void CL_WidowSplash(void)
{
    static const byte   colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    int         i;
    cparticle_t *p;
    vec3_t      dir;

    for (i = 0; i < 256; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(te.pos1, 45.0f, dir, p->org);
        VectorScale(dir, 40.0f, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -0.8f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_TagTrail

===============
*/
void CL_TagTrail(centity_t *ent, const vec3_t end, int color)
{
    vec3_t      move;
    vec3_t      vec;
    int         i, j, count;
    cparticle_t *p;
    const int   dec = 5;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.8f + frand() * 0.2f);
        p->color = color;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 16;
            p->vel[j] = crand() * 5;
        }

        VectorAdd(move, vec, move);
    }

    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CL_ColorExplosionParticles
===============
*/
void CL_ColorExplosionParticles(const vec3_t org, int color, int run)
{
    int         i, j;
    cparticle_t *p;

    for (i = 0; i < 128; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (Q_rand() % run);

        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() % 32) - 16);
            p->vel[j] = (int)(Q_rand() % 256) - 128;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -0.4f / (0.6f + frand() * 0.2f);
    }
}

/*
===============
CL_ParticleSmokeEffect - like the steam effect, but unaffected by gravity
===============
*/
void CL_ParticleSmokeEffect(const vec3_t org, const vec3_t dir, int color, int count, int magnitude)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (Q_rand() & 7);

        for (j = 0; j < 3; j++)
            p->org[j] = org[j] + magnitude * 0.1f * crand();

        VectorScale(dir, magnitude, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, u, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_BlasterParticles2

Wall impact puffs (Green)
===============
*/
void CL_BlasterParticles2(const vec3_t org, const vec3_t dir, unsigned int color)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    int         count;

    count = 40;
    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (Q_rand() & 7);

        d = Q_rand() & 15;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() & 7) - 4) + d * dir[j];
            p->vel[j] = dir[j] * 30 + crand() * 40;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_BlasterTrail2

Green!
===============
*/
void CL_BlasterTrail2(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    int         i, j, count;
    cparticle_t *p;
    const int   dec = 5;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xd0;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand() * 5;
        }

        VectorAdd(move, vec, move);
    }

    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CL_IonripperTrail
===============
*/
void CL_IonripperTrail(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    cparticle_t *p;
    const int   dec = 5;
    int         i, count, sign;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    sign = ent->trailcount;
    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cl.time;
        p->alpha = 0.5f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe4 + (Q_rand() & 3);

        VectorCopy(move, p->org);

        p->vel[0] = (sign & 1) ? 10 : -10;
        p->vel[1] = 0;
        p->vel[2] = 0;

        VectorAdd(move, vec, move);
        sign ^= 1;
    }

    ent->trailcount = sign;
    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CL_TrapParticles
===============
*/
void CL_TrapParticles(centity_t *ent, const vec3_t origin)
{
    vec3_t      move;
    vec3_t      vec;
    vec3_t      start, end;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    if (cl.time - ent->fly_stoptime < 10)
        return;
    ent->fly_stoptime = cl.time;

    VectorCopy(origin, start);
    VectorCopy(origin, end);
    start[2] -= 14;
    end[2] += 50;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 5;
    VectorScale(vec, 5, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe0;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand() * 15;
        }
        p->accel[2] = PARTICLE_GRAVITY;

        VectorAdd(move, vec, move);
    }

    {
        int         i, j, k;
        cparticle_t *p;
        float       vel;
        vec3_t      dir;
        vec3_t      org;

        VectorCopy(origin, org);

        for (i = -2; i <= 2; i += 4)
            for (j = -2; j <= 2; j += 4)
                for (k = -2; k <= 4; k += 4) {
                    p = CL_AllocParticle();
                    if (!p)
                        return;

                    p->time = cl.time;
                    p->color = 0xe0 + (Q_rand() & 3);

                    p->alpha = 1.0f;
                    p->alphavel = -1.0f / (0.3f + (Q_rand() & 7) * 0.02f);

                    p->org[0] = org[0] + i + ((Q_rand() & 23) * crand());
                    p->org[1] = org[1] + j + ((Q_rand() & 23) * crand());
                    p->org[2] = org[2] + k + ((Q_rand() & 23) * crand());

                    dir[0] = j * 8;
                    dir[1] = i * 8;
                    dir[2] = k * 8;

                    VectorNormalize(dir);
                    vel = 50 + (Q_rand() & 63);
                    VectorScale(dir, vel, p->vel);

                    p->accel[0] = p->accel[1] = 0;
                    p->accel[2] = -PARTICLE_GRAVITY;
                }
    }
}

/*
===============
CL_ParticleEffect3
===============
*/
void CL_ParticleEffect3(const vec3_t org, const vec3_t dir, int color, int count)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color;

        d = Q_rand() & 7;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() & 7) - 4) + d * dir[j];
            p->vel[j] = crand() * 20;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_BerserkSlamParticles
===============
*/
void CL_BerserkSlamParticles(const vec3_t org, const vec3_t dir)
{
    static const byte   colortable[4] = {110, 112, 114, 116};
    int         i;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < 700; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = colortable[Q_rand() & 3];

        VectorCopy(org, p->org);

        d = frand() * 192;
        VectorScale(dir, d, p->vel);
        d = crand() * 192;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * 192;
        VectorMA(p->vel, d, u, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_PowerSplash

TODO: differentiate screen/shield
===============
*/
void CL_PowerSplash(void)
{
    static const byte   colortable[4] = {208, 209, 210, 211};
    int         i;
    cparticle_t *p;
    vec3_t      org, dir, mid;
    centity_t   *ent;

    if ((unsigned)te.entity1 >= cl.csr.max_edicts)
        Com_Error(ERR_DROP, "%s: bad entity", __func__);

    ent = &cl_entities[te.entity1];
    if (ent->serverframe != cl.frame.number)
        return;

    VectorAvg(ent->mins, ent->maxs, mid);
    VectorAdd(ent->current.origin, mid, org);

    for (i = 0; i < 256; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(org, ent->radius, dir, p->org);
        VectorScale(dir, 40.0f, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CL_TeleporterParticles
===============
*/
void CL_TeleporterParticles2(const vec3_t org)
{
    int         i;
    cparticle_t *p;
    vec3_t      dir;

    for (i = 0; i < 8; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = 0xdb;

        RandomDir(dir);
        VectorMA(org, 30.0f, dir, p->org);
        p->org[2] += 20.0f;
        VectorScale(dir, -25.0f, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -0.8f;
    }
}

/*
===============
CL_HologramParticles
===============
*/
void CL_HologramParticles(const vec3_t org)
{
    int         i;
    cparticle_t *p;
    vec3_t      dir;
    float       ltime;
    vec3_t      axis[3];

    ltime = cl.time * 0.03f;
    VectorSet(dir, ltime, ltime, 0);
    AnglesToAxis(dir, axis);

    for (i = 0; i < NUMVERTEXNORMALS; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = 0xd0;

        VectorRotate(bytedirs[i], axis, dir);
        VectorMA(org, 100.0f, dir, p->org);

        VectorClear(p->vel);
        VectorClear(p->accel);

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
    }
}

/*
===============
CL_BarrelExplodingParticles
===============
*/
void CL_BarrelExplodingParticles(const vec3_t org)
{
    static const vec3_t ofs[6] = {
        { -10, 0, 40 },
        { 10, 0, 40 },
        { 0, 16, 30 },
        { 16, 0, 25 },
        { 0, -16, 20 },
        { -16, 0, 15 },
    };

    static const vec3_t dir[6] = {
        { 0, 0, 1 },
        { 0, 0, 1 },
        { 0, 1, 0 },
        { 1, 0, 0 },
        { 0, -1, 0 },
        { -1, 0, 0 },
    };

    static const byte color[4] = { 52, 64, 96, 112 };

    for (int i = 0; i < 6; i++) {
        vec3_t p;
        VectorAdd(org, ofs[i], p);
        for (int j = 0; j < 4; j++)
            CL_ParticleSmokeEffect(p, dir[i], color[j], 5, 40);
    }
}

/*
===============
CL_FlameEffects
Kingpin protocol 32 flamethrower active weapon fire jet & dynamic lighting.
Triggered when entity has RF2_FLAMETHROWER or RF2_FLAMESHOOTER.
===============
*/
void CL_FlameEffects(const centity_t *cent, const entity_t *ent)
{
    vec3_t forward, right, up;
    vec3_t muzzle, mid, flame_end;
    trace_t tr;
    cparticle_t *p;
    int i;
    bool is_firstperson = (cent->current.s.number == cl.frame.clientNum + 1 && !cl.thirdPersonView);

    if (is_firstperson) {
        VectorCopy(cl.v_forward, forward);
        VectorCopy(cl.v_right, right);
        VectorCopy(cl.v_up, up);

        float hand_mult = 1.0f;
        if (info_hand && info_hand->integer == 1)
            hand_mult = -1.0f;
        else if (info_hand && info_hand->integer == 2)
            hand_mult = 0.0f;

        VectorCopy(cl.refdef.vieworg, muzzle);
        VectorMA(muzzle, 18.0f, forward, muzzle);
        VectorMA(muzzle, hand_mult * 7.0f, right, muzzle);
        VectorMA(muzzle, -6.0f, up, muzzle);
    } else if (cent->current.s.modelindex == MODELINDEX_PLAYER) {
        AngleVectors(ent->angles, forward, right, up);
        VectorCopy(ent->origin, muzzle);
        // Position at held flamethrower height (waist/chest level: ~32.5 standing, ~16.0 ducking)
        float z_offset = (cent->maxs[2] > 0 && cent->maxs[2] < 20) ? 16.0f : 32.5f;
        muzzle[2] += z_offset;
        VectorMA(muzzle, 24.0f, forward, muzzle);
        VectorMA(muzzle, 7.0f, right, muzzle);
        VectorMA(muzzle, -5.5f, up, muzzle);
    } else {
        AngleVectors(ent->angles, forward, right, up);
        VectorCopy(ent->origin, muzzle);
        VectorMA(muzzle, 16.0f, forward, muzzle);
    }

    // Dynamic blue-cyan ignition light at nozzle, transitioning to warm flame mid-stream
    V_AddLight(muzzle, 150 + (Q_rand() & 31), 0.40f, 0.70f, 1.0f);
    VectorMA(muzzle, 90.0f, forward, mid);
    V_AddLight(mid, 180 + (Q_rand() & 31), 1.0f, 0.45f, 0.1f);

    // Trace ahead to check for solid obstacle impact
    VectorMA(muzzle, 550.0f, forward, flame_end);
    CL_Trace(&tr, muzzle, flame_end, vec3_origin, vec3_origin, MASK_SOLID);

    // If flame strikes a surface, add impact point light and bouncing flame splash
    if (tr.fraction < 1.0f) {
        vec3_t impact_light;
        VectorMA(tr.endpos, 10.0f, tr.plane.normal, impact_light);
        V_AddLight(impact_light, 130 + (Q_rand() & 31), 1.0f, 0.50f, 0.15f);

        for (i = 0; i < 2; i++) {
            p = CL_AllocParticle();
            if (!p)
                break;

            p->time = cl.time;
            p->color = -1;
            p->rgba.u32 = MakeColor(255, 140 + (Q_rand() & 40), 20, 220);
            p->scale = 1.6f + frand() * 1.0f;

            VectorCopy(tr.endpos, p->org);
            VectorMA(p->org, 2.0f, tr.plane.normal, p->org);

            VectorScale(tr.plane.normal, 140.0f + frand() * 80.0f, p->vel);
            VectorMA(p->vel, crand() * 60.0f, right, p->vel);
            VectorMA(p->vel, crand() * 60.0f, up, p->vel);

            VectorSet(p->accel, 0, 0, 25.0f);
            p->alpha = 1.0f;
            p->alphavel = -1.0f / (0.15f + frand() * 0.15f);
        }
    }

    // 1. High-heat incandescent blue combustion pilot core at nozzle (sprites/fthrow_blue4.tga)
    for (i = 0; i < 4; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;

        p->time = cl.time;
        p->color = -1;
        if (i < 2) {
            // Intense bright cyan-blue ignition core
            p->rgba.u32 = MakeColor(50 + (Q_rand() & 40), 170 + (Q_rand() & 60), 255, 245);
            p->scale = 1.1f + frand() * 0.4f;
        } else {
            // Deep incandescent cobalt blue flame
            p->rgba.u32 = MakeColor(30 + (Q_rand() & 30), 100 + (Q_rand() & 50), 240, 220);
            p->scale = 1.4f + frand() * 0.5f;
        }

        VectorCopy(muzzle, p->org);
        VectorMA(p->org, frand() * 10.0f, forward, p->org);
        VectorMA(p->org, crand() * 1.5f, right, p->org);
        VectorMA(p->org, crand() * 1.5f, up, p->org);

        float speed = 750.0f + frand() * 120.0f;
        VectorScale(forward, speed, p->vel);
        VectorMA(p->vel, crand() * 15.0f, right, p->vel);
        VectorMA(p->vel, crand() * 15.0f, up, p->vel);

        VectorSet(p->accel, 0, 0, 15.0f);
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.06f + frand() * 0.05f);
    }

    // 2. Main churning flame stream (rich golden yellow to fiery orange and red)
    float max_spawn_offset = tr.fraction * 240.0f;
    for (i = 0; i < 5; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;

        p->time = cl.time;
        p->color = -1;
        int r_pick = Q_rand() % 4;
        if (r_pick == 0) {
            // Intense bright yellow-orange core
            p->rgba.u32 = MakeColor(255, 215 + (Q_rand() & 25), 45 + (Q_rand() & 25), 240);
        } else if (r_pick == 1 || r_pick == 2) {
            // Roaring fiery orange
            p->rgba.u32 = MakeColor(255, 140 + (Q_rand() & 40), 20 + (Q_rand() & 15), 220);
        } else {
            // Deep incandescent crimson flame
            p->rgba.u32 = MakeColor(230, 60 + (Q_rand() & 35), 10, 200);
        }

        p->scale = 1.5f + frand() * 1.4f;

        float offset = frand() * max_spawn_offset;
        VectorCopy(muzzle, p->org);
        VectorMA(p->org, offset, forward, p->org);
        VectorMA(p->org, crand() * 3.5f, right, p->org);
        VectorMA(p->org, crand() * 3.5f, up, p->org);

        float speed = 720.0f + frand() * 180.0f;
        VectorScale(forward, speed, p->vel);
        VectorMA(p->vel, crand() * 30.0f, right, p->vel);
        VectorMA(p->vel, crand() * 30.0f, up, p->vel);

        // Buoyancy: flames billow and rise slightly as they expand
        VectorSet(p->accel, 0, 0, 35.0f + frand() * 25.0f);

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.28f + frand() * 0.20f);
    }

    // 3. Wispy glowing embers & soft translucent warm smoke (tail dissipation)
    if (frand() > 0.35f) {
        p = CL_AllocParticle();
        if (p) {
            p->time = cl.time;
            p->color = -1;

            if (Q_rand() & 1) {
                // Soft translucent warm dark smoke
                p->rgba.u32 = MakeColor(65, 55, 50, 60);
                p->scale = 2.4f + frand() * 1.2f;
                p->alpha = 0.55f;
            } else {
                // Glowing dying ember
                p->rgba.u32 = MakeColor(180 + (Q_rand() & 30), 45, 12, 130);
                p->scale = 1.4f + frand() * 0.8f;
                p->alpha = 0.75f;
            }

            float offset = 55.0f + frand() * (max_spawn_offset > 60.0f ? max_spawn_offset - 55.0f : 10.0f);
            VectorCopy(muzzle, p->org);
            VectorMA(p->org, offset, forward, p->org);
            VectorMA(p->org, crand() * 8.0f, right, p->org);
            VectorMA(p->org, crand() * 8.0f, up, p->org);

            float speed = 250.0f + frand() * 120.0f;
            VectorScale(forward, speed, p->vel);
            VectorMA(p->vel, crand() * 20.0f, right, p->vel);
            VectorMA(p->vel, 25.0f + frand() * 20.0f, up, p->vel);

            VectorSet(p->accel, 0, 0, 25.0f);
            p->alphavel = -1.0f / (0.38f + frand() * 0.22f);
        }
    }
}

/*
===============
CL_FlamePilotViewEffect
Idle blue pilot light at the first-person flamethrower nozzle.
Flickering blue flame tongue and gentle blue glow matching flamepilot.wav.
===============
*/
void CL_FlamePilotViewEffect(void)
{
    vec3_t forward, right, up, muzzle;
    cparticle_t *p;

    VectorCopy(cl.v_forward, forward);
    VectorCopy(cl.v_right, right);
    VectorCopy(cl.v_up, up);

    float hand_mult = 1.0f;
    if (info_hand && info_hand->integer == 1)
        hand_mult = -1.0f;
    else if (info_hand && info_hand->integer == 2)
        hand_mult = 0.0f;

    VectorCopy(cl.refdef.vieworg, muzzle);
    VectorMA(muzzle, 19.5f, forward, muzzle);
    VectorMA(muzzle, hand_mult * 7.2f, right, muzzle);
    VectorMA(muzzle, -6.0f, up, muzzle);

    // Subtle blue pilot flicker light
    V_AddLight(muzzle, 35.0f + (Q_rand() & 15), 0.25f, 0.55f, 1.0f);

    // Spawn 1-2 small dancing blue flame particles
    for (int i = 0; i < 2; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;

        p->time = cl.time;
        p->color = -1;
        if (i == 0) {
            // Bright electric cyan-blue core
            p->rgba.u32 = MakeColor(60 + (Q_rand() & 30), 180 + (Q_rand() & 40), 255, 235);
            p->scale = 0.35f + frand() * 0.15f;
        } else {
            // Soft blue flame tip
            p->rgba.u32 = MakeColor(30 + (Q_rand() & 20), 110 + (Q_rand() & 30), 240, 180);
            p->scale = 0.45f + frand() * 0.20f;
        }

        VectorCopy(muzzle, p->org);
        VectorMA(p->org, frand() * 1.5f, forward, p->org);
        VectorMA(p->org, crand() * 0.8f, right, p->org);
        VectorMA(p->org, crand() * 0.8f, up, p->org);

        // Gentle puff and upward thermal draft
        VectorScale(forward, 15.0f + frand() * 10.0f, p->vel);
        VectorMA(p->vel, crand() * 5.0f, right, p->vel);
        p->vel[2] += 20.0f + frand() * 12.0f;

        VectorSet(p->accel, 0, 0, 10.0f);
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.07f + frand() * 0.05f);
    }
}

/*
===============
CL_FlamePilotEntityEffect
Idle blue pilot light at the third-person held flamethrower nozzle.
===============
*/
void CL_FlamePilotEntityEffect(const centity_t *cent, const entity_t *ent)
{
    // First-person local player is handled by CL_FlamePilotViewEffect
    if (cent->current.s.number == cl.frame.clientNum + 1 && !cl.thirdPersonView)
        return;

    vec3_t forward, right, up, muzzle;
    cparticle_t *p;

    AngleVectors(ent->angles, forward, right, up);
    VectorCopy(ent->origin, muzzle);
    float z_offset = (cent->maxs[2] > 0 && cent->maxs[2] < 20) ? 16.0f : 32.5f;
    muzzle[2] += z_offset;
    VectorMA(muzzle, 25.0f, forward, muzzle);
    VectorMA(muzzle, 7.0f, right, muzzle);
    VectorMA(muzzle, -5.5f, up, muzzle);

    V_AddLight(muzzle, 30.0f + (Q_rand() & 15), 0.2f, 0.5f, 0.9f);

    p = CL_AllocParticle();
    if (!p)
        return;

    p->time = cl.time;
    p->color = -1;
    p->rgba.u32 = MakeColor(50 + (Q_rand() & 30), 160 + (Q_rand() & 50), 255, 210);
    p->scale = 0.5f + frand() * 0.25f;

    VectorCopy(muzzle, p->org);
    VectorMA(p->org, frand() * 2.0f, forward, p->org);
    VectorMA(p->org, crand() * 1.0f, right, p->org);
    VectorMA(p->org, crand() * 1.0f, up, p->org);

    VectorScale(forward, 12.0f + frand() * 8.0f, p->vel);
    p->vel[2] += 18.0f + frand() * 10.0f;

    VectorSet(p->accel, 0, 0, 8.0f);
    p->alpha = 1.0f;
    p->alphavel = -1.0f / (0.08f + frand() * 0.06f);
}

/*
===============
CL_BurningBodyEffects
Kingpin protocol 32 on-fire effect (EF_KP_FLAMETHROWER).
Engulfs a burning player or actor in flickering fire tongues and rising smoke.
===============
*/
void CL_BurningBodyEffects(const centity_t *cent, const entity_t *ent)
{
    cparticle_t *p;
    int i;
    float height = (cent->maxs[2] > 0) ? cent->maxs[2] : 48.0f;
    float rad = (cent->maxs[0] > 0) ? cent->maxs[0] * 0.7f : 12.0f;

    // Flickering warm orange illumination centered on the burning body
    V_AddLight(ent->origin, 140 + (Q_rand() & 31), 1.0f, 0.50f, 0.10f);

    // 1. Burning flame tongues erupting across torso and limbs
    for (i = 0; i < 3; i++) {
        p = CL_AllocParticle();
        if (!p)
            break;

        p->time = cl.time;
        p->color = -1;

        if (Q_rand() & 1)
            p->rgba.u32 = MakeColor(255, 190 + (Q_rand() & 40), 30, 230);
        else
            p->rgba.u32 = MakeColor(255, 120 + (Q_rand() & 50), 15, 210);

        p->scale = 1.3f + frand() * 0.9f;

        p->org[0] = ent->origin[0] + crand() * rad;
        p->org[1] = ent->origin[1] + crand() * rad;
        p->org[2] = ent->origin[2] + frand() * height;

        p->vel[0] = crand() * 15.0f;
        p->vel[1] = crand() * 15.0f;
        p->vel[2] = 45.0f + frand() * 45.0f;

        VectorSet(p->accel, 0, 0, 30.0f);
        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.22f + frand() * 0.15f);
    }

    // 2. Thick dark smoke billowing upward from the burning body
    p = CL_AllocParticle();
    if (p) {
        p->time = cl.time;
        p->color = -1;
        p->rgba.u32 = MakeColor(55, 48, 45, 55);
        p->scale = 2.2f + frand() * 1.3f;

        p->org[0] = ent->origin[0] + crand() * (rad * 0.8f);
        p->org[1] = ent->origin[1] + crand() * (rad * 0.8f);
        p->org[2] = ent->origin[2] + height * 0.7f + frand() * 10.0f;

        p->vel[0] = crand() * 18.0f;
        p->vel[1] = crand() * 18.0f;
        p->vel[2] = 65.0f + frand() * 35.0f;

        VectorSet(p->accel, 0, 0, 20.0f);
        p->alpha = 0.50f;
        p->alphavel = -1.0f / (0.45f + frand() * 0.25f);
    }
}
