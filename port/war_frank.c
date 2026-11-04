// frank-war1 — hooks into war1 for the platform (included at the end of
// war1.c, so it sees the game's internals).
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib

// Recording and replaying: with war_fixedStep set, every frame is 1/60 s of
// game time whatever the clock says (war_game.c, wg_beginFrame), so the
// same input at the same frames gives the same game on every build.
bool war_fixedStep;

extern uint64_t fsdl_rngState(void);

static u32 wfr_mix(u32 h, u32 v)
{
    h ^= v;
    h *= 16777619u;
    return h;
}

static u32 wfr_float(f32 f)
{
    u32 u;
    memcpy(&u, &f, 4);
    return u;
}

// A hash of what the game is: the scene or map, every unit's type, owner,
// position and hit points, the players' gold and wood, the random state.
// The recording stores it every second; the replay compares.
u32 war_stateHash(void* ctx)
{
    WarContext* context = (WarContext*)ctx;
    u32 h = 2166136261u;
    uint64_t rng = fsdl_rngState();
    h = wfr_mix(h, (u32)rng);
    h = wfr_mix(h, (u32)(rng >> 32));
    h = wfr_mix(h, context->scene ? 1000u + (u32)context->scene->type : 0u);

    WarMap* map = context->map;
    if (map && !context->scene)
    {
        for (s32 p = 0; p < MAX_PLAYERS_COUNT; p++)
        {
            h = wfr_mix(h, (u32)map->players[p].gold);
            h = wfr_mix(h, (u32)map->players[p].wood);
        }

        WarEntityManager* manager = &map->entityManager;
        for (s32 i = 1; i < MAX_ENTITIES_COUNT; i++)
        {
            WarEntity* entity = &manager->entities[i];
            if (!entity->id || entity->type != WAR_ENTITY_TYPE_UNIT)
                continue;
            WarUnitComponent* unit = we_getUnitComponent(context, entity);
            WarTransformComponent* transform = we_getTransformComponent(context, entity);
            h = wfr_mix(h, (u32)entity->id);
            if (unit)
            {
                h = wfr_mix(h, (u32)unit->type | (u32)unit->player << 8);
                h = wfr_mix(h, (u32)unit->hp);
            }
            if (transform)
            {
                h = wfr_mix(h, wfr_float(transform->position.x));
                h = wfr_mix(h, wfr_float(transform->position.y));
            }
        }
    }
    return h;
}
