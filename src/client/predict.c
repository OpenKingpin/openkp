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

#include "client.h"

/*
===================
CL_CheckPredictionError
===================
*/
void CL_CheckPredictionError(void)
{
    int         frame;
    int         delta[3];
    unsigned    cmd;
    int         len;

    if (cls.demo.playback) {
        return;
    }

    if (sv_paused->integer) {
        VectorClear(cl.prediction_error);
        return;
    }

    if (!cl_predict->integer || (cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION))
        return;

    // calculate the last usercmd_t we sent that the server has processed
    frame = cls.netchan.incoming_acknowledged & CMD_MASK;
    cmd = cl.history[frame].cmdNumber;

    // compare what the server returned with what we had predicted it to be
    VectorSubtract(cl.frame.ps.pmove.origin, cl.predicted_origins[cmd & CMD_MASK], delta);

    // save the prediction error for interpolation
    len = abs(delta[0]) + abs(delta[1]) + abs(delta[2]);
    if (len <= 1 || len > 640) {
        // > 80 world units is a teleport or something
        VectorClear(cl.prediction_error);
        return;
    }

    SHOWMISS("prediction miss on %i: %i (%d %d %d)\n",
             cl.frame.number, len, delta[0], delta[1], delta[2]);

    // don't predict steps against server returned data
    if (cl.predicted_step_frame <= cmd)
        cl.predicted_step_frame = cmd + 1;

    VectorCopy(cl.frame.ps.pmove.origin, cl.predicted_origins[cmd & CMD_MASK]);

    // save for error interpolation
    VectorScale(delta, 0.125f, cl.prediction_error);
}

/*
====================
CL_ClipMoveToEntities
====================
*/
static void CL_ClipMoveToEntities(trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, int contentmask)
{
    int         i;
    trace_t     trace;
    const mnode_t   *headnode;
    const centity_t *ent;
    const mmodel_t  *cmodel;

    for (i = 0; i < cl.numSolidEntities; i++) {
        ent = cl.solidEntities[i];

        if (cl.csr.extended && ent->current.number <= cl.maxclients && !(contentmask & CONTENTS_PLAYER))
            continue;

        if (ent->current.solid == PACKED_BSP) {
            // special value for bmodel
            cmodel = cl.model_clip[ent->current.modelindex];
            if (!cmodel)
                continue;
            headnode = cmodel->headnode;
        } else {
            headnode = CM_HeadnodeForBox(ent->mins, ent->maxs);
        }

        if (tr->allsolid)
            return;

        CM_TransformedBoxTrace(&trace, start, end,
                               mins, maxs, headnode, contentmask,
                               ent->current.origin, ent->current.angles,
                               cl.csr.extended);

        CM_ClipEntity(tr, &trace, (struct edict_s *)ent);
    }
}

/*
================
CL_Trace
================
*/
void CL_Trace(trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, int contentmask)
{
    // check against world
    CM_BoxTrace(tr, start, end, mins, maxs, cl.bsp->nodes, contentmask, cl.csr.extended);
    tr->ent = (struct edict_s *)cl_entities;
    if (tr->fraction == 0)
        return;     // blocked by the world

    // check all other solid models
    CL_ClipMoveToEntities(tr, start, end, mins, maxs, contentmask);
}

static int pm_clipmask;

static trace_t q_gameabi CL_PMTrace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int contentmask)
{
    trace_t t;
    CL_Trace(&t, start, end, mins, maxs, (cl.csr.extended && contentmask) ? contentmask : pm_clipmask);
    return t;
}

static int CL_PointContents(const vec3_t point)
{
    const centity_t *ent;
    const mmodel_t  *cmodel;
    int i, contents;

    contents = CM_PointContents(point, cl.bsp->nodes, cl.csr.extended);

    for (i = 0; i < cl.numSolidEntities; i++) {
        ent = cl.solidEntities[i];

        if (ent->current.solid != PACKED_BSP) // special value for bmodel
            continue;

        cmodel = cl.model_clip[ent->current.modelindex];
        if (!cmodel)
            continue;

        contents |= CM_TransformedPointContents(point, cmodel->headnode, ent->current.origin,
                                                ent->current.angles, cl.csr.extended);
    }

    return contents;
}

/**
 * @brief Performs zero-latency client-side audio prediction for Kingpin movement events.
 *
 * Runs immediately following local player physics simulation to eliminate net latency for:
 * 1. Landing sounds: triggers surface-specific landing thuds upon ground contact with
 *    downward velocity between -140 and -600 units/s (severe falls leave screams to the server).
 * 2. Jump grunts: plays randomized voice jump efforts at the exact moment of liftoff.
 * 3. Ladder climbing: tracks vertical travel distance, playing light metal rung steps every 32 units.
 * 4. Ground footsteps: tracks horizontal travel distance (60 units running, 36 units crouched),
 *    evaluating ground polygon textures in real-time to trigger responsive material footstep SFX.
 *
 * @param pm Active pmove state structure.
 * @param was_on_ground Ground contact state prior to current pmove step.
 * @param was_ladder Ladder attachment state prior to current pmove step.
 * @param pre_z_vel Vertical velocity prior to collision resolution.
 * @param old_origin World position prior to current pmove step.
 */
static void CL_PredictAudioKingpin(const pmove_t *pm, qboolean was_on_ground, qboolean was_ladder,
                                   float pre_z_vel, const vec3_t old_origin)
{
    vec3_t cur_origin;
    VectorScale(pm->s.origin, 0.125f, cur_origin);

    // 1. Landing sound
    if (!was_on_ground && (pm->s.pm_flags & PMF_ON_GROUND) && pre_z_vel < -140.0f) {
        if (pre_z_vel >= -600.0f) { // Higher falls trigger EV_KP_FALL / EV_KP_FALLFAR server damage screams
            int surf_type = CL_SurfaceToKingpinStep(pm->groundsurface, 0, pm->waterlevel);
            if (surf_type == KP_SURF_PAVEMENT && !pm->groundsurface)
                surf_type = CL_TraceSurfaceFootstep(cur_origin, pm->mins, pm->maxs);
            CL_PlayKingpinLand(cl.clientNum + 1, surf_type, 1.0f, ATTN_NORM);
        }
    }

    // 2. Jumping sound
    if (was_on_ground && !(pm->s.pm_flags & PMF_ON_GROUND) && pm->cmd.upmove > 10 && (pm->s.velocity[2] * 0.125f) > 150.0f) {
        CL_PlayKingpinJump(cl.clientNum + 1);
        cl.predicted_jump_time = cls.realtime;
    }

    // 3. Ladder footsteps
    if (pm->s.pm_flags & PMF_ON_LADDER) {
        if (cl_footsteps->integer) {
            vec3_t delta;
            VectorSubtract(cur_origin, old_origin, delta);
            float ldist = VectorLength(delta);
            if (ldist > 0.1f && ldist < 50.0f) {
                cl.predicted_ladder_dist += ldist;
                if (cl.predicted_ladder_dist >= 32.0f) {
                    cl.predicted_ladder_dist = 0.0f;
                    CL_PlayKingpinFootstep(cl.clientNum + 1, KP_SURF_METAL_LIGHT, 0.5f, ATTN_NORM);
                }
            }
        }
        cl.predicted_step_dist = 0.0f;
    } else {
        cl.predicted_ladder_dist = 0.0f;

        // 4. Ground footsteps
        if (pm->s.pm_type == PM_NORMAL && (pm->s.pm_flags & PMF_ON_GROUND)) {
            if (cl_footsteps->integer) {
                float dx = cur_origin[0] - old_origin[0];
                float dy = cur_origin[1] - old_origin[1];
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > 0.1f && dist < 100.0f) {
                    cl.predicted_step_dist += dist;
                    float step_threshold = (pm->s.pm_flags & PMF_DUCKED) ? 36.0f : 60.0f;
                    if (cl.predicted_step_dist >= step_threshold) {
                        cl.predicted_step_dist = 0.0f;
                        int surf_type = CL_SurfaceToKingpinStep(pm->groundsurface, 0, pm->waterlevel);
                        if (surf_type == KP_SURF_PAVEMENT && !pm->groundsurface)
                            surf_type = CL_TraceSurfaceFootstep(cur_origin, pm->mins, pm->maxs);
                        CL_PlayKingpinFootstep(cl.clientNum + 1, surf_type, 0.5f, ATTN_NORM);
                    }
                } else if (dist <= 0.1f) {
                    cl.predicted_step_dist = 0.0f;
                }
            }
        } else {
            cl.predicted_step_dist = 0.0f;
        }
    }
}

/**
 * @brief Predicts weapon handling audio effects client-side.
 *
 * 1. Weapon draw/switch cocking sound: detects changes in the active gun model index
 *    and immediately plays the weapon's distinctive cocking/loading SFX (shotgun pump,
 *    tommygun charging bolt, heavy machinegun cock, pistol slide rack, etc.).
 * 2. Dry-fire click: detects the leading edge of the attack button when the active weapon's
 *    ammo count is exhausted, immediately playing an empty weapon hammer click.
 */
static void CL_PredictWeaponAudioKingpin(void)
{
    // 1. Weapon draw / switch cocking sound
    int current_gun_model = 0;
    if (cl.frame.ps.gun_parts[0].modelindex > 0 && cl.frame.ps.gun_parts[0].modelindex < cl.csr.max_models) {
        current_gun_model = cl.frame.ps.gun_parts[0].modelindex;
    }

    if (cl.predicted_gun_model == 0) {
        cl.predicted_gun_model = current_gun_model;
    } else if (current_gun_model != 0 && current_gun_model != cl.predicted_gun_model) {
        cl.predicted_gun_model = current_gun_model;
        const char *mname = cl.configstrings[cl.csr.models + current_gun_model];
        if (mname && *mname) {
            qhandle_t cock_sfx = 0;
            if (Q_strcasestr(mname, "shotgun"))
                cock_sfx = cl_kp_shotgload;
            else if (Q_strcasestr(mname, "tomgun") || Q_strcasestr(mname, "mach"))
                cock_sfx = cl_kp_machgcock;
            else if (Q_strcasestr(mname, "hmg"))
                cock_sfx = cl_kp_hmgcock;
            else if (Q_strcasestr(mname, "colt") || Q_strcasestr(mname, "pistol"))
                cock_sfx = cl_kp_pistol_clip;
            else if (Q_strcasestr(mname, "rocket"))
                cock_sfx = cl_kp_rocket_reload;
            else if (Q_strcasestr(mname, "grenade"))
                cock_sfx = cl_kp_grenade_reload;

            if (cock_sfx)
                S_StartSound(NULL, cl.clientNum + 1, CHAN_WEAPON, cock_sfx, 1.0f, ATTN_NORM, 0);
        }
    }

    // 2. Empty weapon click on attack button edge
    int attack_now = (cl.cmd.buttons & BUTTON_ATTACK);
    if (attack_now && !(cl.predicted_attack_button & BUTTON_ATTACK)) {
        if (cl.frame.ps.stats[STAT_AMMO] <= 0 && cl.frame.ps.stats[STAT_AMMO_ICON] > 0) {
            if (cl_kp_noammo)
                S_StartSound(NULL, cl.clientNum + 1, CHAN_WEAPON, cl_kp_noammo, 1.0f, ATTN_NORM, 0);
        }
    }
    cl.predicted_attack_button = cl.cmd.buttons;
}

/*
=================
CL_PredictMovement

Sets cl.predicted_origin and cl.predicted_angles
=================
*/
void CL_PredictAngles(void)
{
    cl.predicted_angles[0] = cl.viewangles[0] + SHORT2ANGLE(cl.frame.ps.pmove.delta_angles[0]);
    cl.predicted_angles[1] = cl.viewangles[1] + SHORT2ANGLE(cl.frame.ps.pmove.delta_angles[1]);
    cl.predicted_angles[2] = cl.viewangles[2] + SHORT2ANGLE(cl.frame.ps.pmove.delta_angles[2]);
}

void CL_PredictMovement(void)
{
    unsigned    ack, current, frame;
    pmove_t     pm;
    int         step, oldz;

    if (cls.state != ca_active) {
        return;
    }

    if (cls.demo.playback) {
        return;
    }

    if (sv_paused->integer) {
        return;
    }

    if (!cl_predict->integer || (cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
        // just set angles
        CL_PredictAngles();
        return;
    }

    ack = cl.history[cls.netchan.incoming_acknowledged & CMD_MASK].cmdNumber;
    current = cl.cmdNumber;

    // if we are too far out of date, just freeze
    if (current - ack > CMD_BACKUP - 1) {
        SHOWMISS("%i: exceeded CMD_BACKUP\n", cl.frame.number);
        return;
    }

    if (!cl.cmd.msec && current == ack) {
        SHOWMISS("%i: not moved\n", cl.frame.number);
        return;
    }

    pm_clipmask = MASK_PLAYERSOLID;

    // remaster player collision rules
    if (cl.csr.extended) {
        if (cl.frame.ps.pmove.pm_type == PM_DEAD || cl.frame.ps.pmove.pm_type == PM_GIB)
            pm_clipmask = MASK_DEADSOLID;

        if (!(cl.frame.ps.pmove.pm_flags & PMF_IGNORE_PLAYER_COLLISION))
            pm_clipmask |= CONTENTS_PLAYER;
    }

    // copy current state to pmove
    memset(&pm, 0, sizeof(pm));
    pm.trace = CL_PMTrace;
    pm.pointcontents = CL_PointContents;
    pm.s = cl.frame.ps.pmove;
    pm.snapinitial = qtrue;

    if (cl.predicted_sound_cmd < ack || cl.predicted_sound_cmd > current) {
        cl.predicted_sound_cmd = ack;
    }

    // run frames
    while (++ack <= current) {
        qboolean was_on_ground = (pm.s.pm_flags & PMF_ON_GROUND) != 0;
        qboolean was_ladder = (pm.s.pm_flags & PMF_ON_LADDER) != 0;
        float pre_z_vel = pm.s.velocity[2] * 0.125f;
        vec3_t old_origin;
        VectorScale(pm.s.origin, 0.125f, old_origin);

        pm.cmd = cl.cmds[ack & CMD_MASK];
        PmoveNew(&pm, &cl.pmp);
        pm.snapinitial = qfalse;

        // save for debug checking
        VectorCopy(pm.s.origin, cl.predicted_origins[ack & CMD_MASK]);

        if (cls.serverProtocol == PROTOCOL_VERSION_KINGPIN) {
            if (ack > cl.predicted_sound_cmd) {
                cl.predicted_sound_cmd = ack;
                CL_PredictAudioKingpin(&pm, was_on_ground, was_ladder, pre_z_vel, old_origin);
            }
        }
    }

    // run pending cmd
    if (cl.cmd.msec) {
        pm.cmd = cl.cmd;
        pm.cmd.forwardmove = cl.localmove[0];
        pm.cmd.sidemove = cl.localmove[1];
        pm.cmd.upmove = cl.localmove[2];
        PmoveNew(&pm, &cl.pmp);
        frame = current;

        // save for debug checking
        VectorCopy(pm.s.origin, cl.predicted_origins[(current + 1) & CMD_MASK]);
    } else {
        frame = current - 1;
    }

    if (pm.s.pm_type != PM_SPECTATOR && (pm.s.pm_flags & PMF_ON_GROUND)) {
        oldz = cl.predicted_origins[cl.predicted_step_frame & CMD_MASK][2];
        step = pm.s.origin[2] - oldz;
        if (step >= 63 && step < 160) {
            // check for stepping up before a previous step is completed
            unsigned delta = cls.realtime - cl.predicted_step_time;
            float prev_step = 0;
            if (delta < 100)
                prev_step = cl.predicted_step * (100 - delta) * 0.01f;

            cl.predicted_step = min(prev_step + step * 0.125f, 32);
            cl.predicted_step_time = cls.realtime;
            cl.predicted_step_frame = frame + 1;    // don't double step
        }
    }

    if (cl.predicted_step_frame < frame) {
        cl.predicted_step_frame = frame;
    }

    // copy results out for rendering
    VectorScale(pm.s.origin, 0.125f, cl.predicted_origin);
    VectorScale(pm.s.velocity, 0.125f, cl.predicted_velocity);
    VectorCopy(pm.viewangles, cl.predicted_angles);

    if (cls.serverProtocol == PROTOCOL_VERSION_KINGPIN) {
        CL_PredictWeaponAudioKingpin();
    }
}
