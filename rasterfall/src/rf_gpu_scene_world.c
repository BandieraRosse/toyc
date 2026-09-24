#include "tlibc_everything.h"
#include "rf_gpu_scene_world.h"
#include "rasterfall_prop.h"
#include "rasterfall_session.h"
#include "rasterfall_effects.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(RF_GPU_SCENE_FLAG_CAP==RASTERFALL_MAX_FLAGS,
    "Scene flag snapshot capacity must match session");
_Static_assert(RF_GPU_SCENE_PROJECTILE_CAP==TOY_GAME_MAX_PROJECTILES,
    "Scene projectile snapshot capacity must match game");

int rf_gpu_scene_interactable_freeze(const struct rasterfall_session *session,
    const struct rasterfall_effects *effects,
    const struct rasterfall_world_lighting *lighting,int visible,
    uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_interactable_frame_v1 *interactables)
{
    struct rf_gpu_scene_interactable_frame_v1 next={0};
    if (!session || !effects || !lighting || !interactables ||
        !frame_id || !world_generation || (visible!=0 && visible!=1) ||
        session->item_count<0 || session->item_count>TOY_MAP_MAX_PICKUPS)
        return -1;
    next.frame_id=frame_id;
    next.world_generation=world_generation;
    if (visible) for(int i=0;i<session->item_count;++i) {
        const struct rasterfall_interactable *source=&session->items[i];
        struct rf_gpu_scene_interactable_item_v1 *item=&next.items[next.count++];
        item->source_slot=(uint32_t)i;
        item->kind=source->kind;item->weapon=source->weapon;
        item->x=source->x;item->y=source->y;item->z=source->z;
        for(int effect_index=0;effect_index<RASTERFALL_EFFECT_INSTANCE_SLOTS;
            ++effect_index) {
            const struct rasterfall_effect_instance *effect=
                &effects->instances[effect_index];
            if (effect->active &&
                effect->kind==RASTERFALL_EFFECT_INSTANCE_KIND_INTERACTION_HIGHLIGHT &&
                effect->target_id==i) { item->highlight_on=1;break; }
        }
        item->scene_light_q8=rasterfall_world_light_v2_q8(
            rasterfall_world_light_at(lighting,source->x,source->y,source->z));
    }
    *interactables=next;
    return 0;
}

static int scene_world_has_nul(const char *value,size_t capacity)
{
    for(size_t i=0;i<capacity;++i) if (!value[i]) return 1;
    return 0;
}

int rf_gpu_scene_flag_freeze(const struct rasterfall_session *session,
    uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_flag_frame_v1 *flags)
{
    struct rf_gpu_scene_flag_frame_v1 next={0};
    if (!session || !flags || !frame_id || !world_generation ||
        session->flag_count<0 || session->flag_count>RF_GPU_SCENE_FLAG_CAP ||
        session->carried_flag<-1 || session->carried_flag>=session->flag_count)
        return -1;
    next.frame_id=frame_id;next.world_generation=world_generation;
    next.count=(uint32_t)session->flag_count;
    for(uint32_t i=0;i<next.count;++i) {
        const struct rasterfall_flag *source=&session->flags[i];
        struct rf_gpu_scene_flag_item_v1 *item=&next.items[i];
        if (source->active!=0 && source->active!=1) return -1;
        if (!memchr(source->label,0,sizeof(source->label))) return -1;
        item->active=source->active;item->x=source->x;item->z=source->z;
        item->color=source->color;item->selected=(int)i==session->carried_flag;
        memcpy(item->label,source->label,sizeof(item->label));
    }
    *flags=next;
    return 0;
}

int rf_gpu_scene_projectile_freeze(const struct toy_game *game,
    const struct rasterfall_world_lighting *lighting,
    uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_projectile_frame_v1 *projectiles)
{
    struct rf_gpu_scene_projectile_frame_v1 next={0};
    if (!game || !lighting || !projectiles || !frame_id || !world_generation)
        return -1;
    next.frame_id=frame_id;next.world_generation=world_generation;
    for(uint32_t i=0;i<TOY_GAME_MAX_PROJECTILES;++i) {
        const struct toy_game_projectile *source=&game->projectiles[i];
        struct rf_gpu_scene_projectile_item_v1 *item;
        if (source->active!=0 && source->active!=1) return -1;
        if (!source->active) continue;
        if (next.count>=RF_GPU_SCENE_PROJECTILE_CAP ||
            (source->kind!=TOY_GAME_WEAPON_BOMB &&
             source->kind!=TOY_GAME_WEAPON_MOLOTOV) ||
            source->age_ms<0 || source->flash_ms<0) return -1;
        item=&next.items[next.count++];
        item->source_slot=i;item->kind=source->kind;
        item->x=source->x;item->y=source->y;item->z=source->z;
        item->age_ms=source->age_ms;item->flash_ms=source->flash_ms;
        item->scene_light_q8=rasterfall_world_light_v2_q8(
            rasterfall_world_light_at(lighting,source->x,-900+source->y,source->z));
    }
    *projectiles=next;
    return 0;
}

static int scene_world_capture(const struct rasterfall_map_state *map,
    int air_walls_enabled,struct rf_gpu_scene_world_input_v2 *out,
    uint32_t capacity,uint32_t *count,
    struct rf_gpu_scene_world_render_frame_v1 *render,
    uint64_t frame_id,uint64_t world_generation)
{
    struct rf_gpu_scene_world_input_v2 next[RF_GPU_SCENE_MAX_WORLD_V2];
    struct rf_gpu_scene_world_render_frame_v1 *next_render=NULL;
    int total;
    if (!map || !map->runtime_loaded || !map->level || !out || !count ||
        (air_walls_enabled!=0 && air_walls_enabled!=1) ||
        (render && (!frame_id || !world_generation))) return -1;
    total=rf_map_runtime_render_count(&map->runtime);
    if (total<0 || (uint32_t)total>RF_GPU_SCENE_MAX_WORLD_V2 ||
        total>TOY_MAP_MAX_DRAW || (uint32_t)total>capacity ||
        map->level->draw_count!=total) return -1;
    memset(next,0,sizeof(next));
    if (render) {
        next_render=tlibc_malloc(sizeof(*next_render));
        if (!next_render) return -1;
        memset(next_render,0,sizeof(*next_render));
        next_render->abi_version=RF_GPU_SCENE_WORLD_RENDER_ABI_V1;
        next_render->byte_size=(uint32_t)sizeof(*next_render);
        next_render->frame_id=frame_id;
        next_render->world_generation=world_generation;
        next_render->map_generation=world_generation;
        next_render->count=(uint32_t)total;
    }
    for(int i=0;i<total;++i) {
        const struct rf_map_runtime_render *projection=rasterfall_map_render_projection_at(map,i);
        const struct toy_map_draw *draw=&map->level->draw[i];
        struct rf_gpu_scene_world_input_v2 *item=&next[i];
        int64_t center_x=(int64_t)draw->a+((int64_t)draw->b-draw->a)/2;
        int64_t center_z=(int64_t)draw->c+((int64_t)draw->d-draw->c)/2;
        int64_t y=(int64_t)draw->e-900;
        int gate=(draw->type==TOY_MAP_DRAW_BOX || draw->type==TOY_MAP_DRAW_PLATFORM) &&
            !strncmp(draw->text,"air_gate_",9);
        if (!projection || !projection->id[0] || !projection->kind[0] ||
            center_x<INT_MIN || center_x>INT_MAX || center_z<INT_MIN || center_z>INT_MAX ||
            y<INT_MIN || y>INT_MAX) { tlibc_free(next_render); return -1; }
        memcpy(item->id,projection->id,sizeof(item->id));
        memcpy(item->kind,projection->kind,sizeof(item->kind));
        item->submission_ordinal=(uint32_t)i;
        item->x=(int)center_x;item->y=(int)y;item->z=(int)center_z;
        item->visible=(!gate || air_walls_enabled) &&
            (draw->type!=TOY_MAP_DRAW_PLATFORM || draw->style!=0);
        item->alpha=draw->type==TOY_MAP_DRAW_PLATFORM && draw->style!=2 ? 96 :
            draw->type==TOY_MAP_DRAW_BOX && gate ? 48 : 255;
        if (next_render) {
            struct rf_gpu_scene_world_render_item_v1 *value=&next_render->items[i];
            memcpy(value->id,item->id,sizeof(value->id));
            value->submission_ordinal=item->submission_ordinal;
            value->visible=item->visible;value->alpha=item->alpha;
            value->draw=*draw;
        }
    }
    memcpy(out,next,(size_t)total*sizeof(next[0]));
    *count=(uint32_t)total;
    if (next_render) { *render=*next_render; tlibc_free(next_render); }
    return 0;
}

int rf_gpu_scene_world_capture(const struct rasterfall_map_state *map,
    int air_walls_enabled,struct rf_gpu_scene_world_input_v2 *out,
    uint32_t capacity,uint32_t *count)
{
    return scene_world_capture(map,air_walls_enabled,out,capacity,count,NULL,0,0);
}
int rf_gpu_scene_world_render_freeze(const struct rasterfall_map_state *map,
    int air_walls_enabled,uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_world_input_v2 *world,uint32_t capacity,
    uint32_t *count,struct rf_gpu_scene_world_render_frame_v1 *render)
{
    if (!render) return -1;
    return scene_world_capture(map,air_walls_enabled,world,capacity,count,
        render,frame_id,world_generation);
}
int rf_gpu_scene_world_render_validate(
    const struct rf_gpu_scene_snapshot_v2 *snapshot,
    const struct rf_gpu_scene_world_render_frame_v1 *render)
{
    if (!snapshot || !render ||
        snapshot->abi_version!=RF_GPU_SCENE_SNAPSHOT_ABI_V2 ||
        snapshot->byte_size!=sizeof(*snapshot) ||
        render->abi_version!=RF_GPU_SCENE_WORLD_RENDER_ABI_V1 ||
        render->byte_size!=sizeof(*render) ||
        render->frame_id!=snapshot->frame_id ||
        render->world_generation!=snapshot->world_generation ||
        render->map_generation!=snapshot->map_generation ||
        render->count!=snapshot->world_count ||
        render->count>RF_GPU_SCENE_MAX_WORLD_V2) return -1;
    for(uint32_t i=0;i<render->count;++i) {
        const struct rf_gpu_scene_world_render_item_v1 *value=&render->items[i];
        const struct rf_gpu_scene_world_input_v2 *world=&snapshot->world[i];
        if (!scene_world_has_nul(value->id,sizeof(value->id)) ||
            !scene_world_has_nul(world->id,sizeof(world->id)) ||
            strcmp(value->id,world->id) ||
            value->submission_ordinal!=world->submission_ordinal ||
            value->visible!=world->visible || value->alpha!=world->alpha)
            return -1;
    }
    return 0;
}

int rf_gpu_scene_world_floor_freeze(const struct rasterfall_map_state *map,
    int authored_ground,uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_world_floor_frame_v1 *floor)
{
    const struct toy_map *level;
    if (!map || !map->runtime_loaded || !map->level || !floor ||
        !frame_id || !world_generation ||
        (authored_ground!=0 && authored_ground!=1)) return -1;
    level=map->level;
    if (level->minx>=level->maxx || level->minz>=level->maxz ||
        level->spawn_count<0 || level->spawn_count>TOY_MAP_MAX_ZONES) return -1;
    memset(floor,0,sizeof(*floor));
    floor->frame_id=frame_id;
    floor->world_generation=world_generation;
    floor->map_generation=world_generation;
    floor->minx=level->minx;floor->maxx=level->maxx;
    floor->minz=level->minz;floor->maxz=level->maxz;
    floor->authored_ground=authored_ground;
    floor->spawn_count=(uint32_t)level->spawn_count;
    memcpy(floor->spawn_zones,level->spawn_zones,
        floor->spawn_count*sizeof(floor->spawn_zones[0]));
    rasterfall_diagnostic_world_light_bake_v1(&floor->model_light_v1,level);
    return 0;
}

int rf_gpu_scene_world_prop_freeze(const struct rasterfall_map_state *map,
    const struct rasterfall_world_lighting *lighting,
    uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_world_prop_frame_v1 *props)
{
    struct rf_gpu_scene_world_prop_frame_v1 *next;
    int count;
    if (!map || !map->runtime_loaded || !map->level || !props ||
        !frame_id || !world_generation) return -1;
    count=rf_map_runtime_object_count(&map->runtime);
    if (count<0 || count>TOY_MAP_MAX_PROPS ||
        map->level->prop_count!=count) return -1;
    next=tlibc_malloc(sizeof(*next));
    if (!next) return -1;
    memset(next,0,sizeof(*next));
    next->frame_id=frame_id;next->world_generation=world_generation;
    next->map_generation=world_generation;next->count=(uint32_t)count;
    for(int i=0;i<count;++i) {
        const struct rf_map_runtime_object *object=
            rasterfall_map_object_projection_at(map,i);
        const struct rasterfall_prop_asset_profile *profile=object ?
            rasterfall_prop_asset_by_name(object->kind) : NULL;
        const struct toy_map_prop *prop=&map->level->props[i];
        if (!object || !profile || !object->id[0] ||
            !scene_world_has_nul(object->id,sizeof(object->id)) ||
            profile->id!=prop->asset_id || object->x!=prop->x ||
            object->y!=prop->y || object->z!=prop->z ||
            object->yaw!=prop->yaw_degrees ||
            object->scale!=prop->scale_milli ||
            object->length!=prop->length) {
            tlibc_free(next);return -1;
        }
        for(int j=0;j<i;++j)
            if (!strcmp(next->items[j].id,object->id)) {
                tlibc_free(next);return -1;
            }
        memcpy(next->items[i].id,object->id,sizeof(object->id));
        next->items[i].submission_ordinal=(uint32_t)i;
        next->items[i].scene_light_q8=lighting ? rasterfall_world_light_v2_q8(
            rasterfall_world_light_at(lighting,prop->x,-900+prop->y,prop->z)) : 256;
        next->items[i].prop=*prop;
    }
    *props=*next;tlibc_free(next);
    return 0;
}

static int scene_world_floor_same(
    const struct rf_gpu_scene_world_floor_frame_v1 *a,
    const struct rf_gpu_scene_world_floor_frame_v1 *b)
{
    return a->minx==b->minx && a->maxx==b->maxx &&
        a->minz==b->minz && a->maxz==b->maxz &&
        a->authored_ground==b->authored_ground &&
        a->spawn_count==b->spawn_count &&
        !memcmp(&a->model_light_v1,&b->model_light_v1,
            sizeof(a->model_light_v1)) &&
        !memcmp(a->spawn_zones,b->spawn_zones,
            a->spawn_count*sizeof(a->spawn_zones[0]));
}

static int scene_world_item_same(
    const struct rf_gpu_scene_world_render_item_v1 *a,
    const struct rf_gpu_scene_world_render_item_v1 *b)
{
    const struct toy_map_draw *x=&a->draw,*y=&b->draw;
    return !strcmp(a->id,b->id) &&
        a->submission_ordinal==b->submission_ordinal &&
        a->visible==b->visible && a->alpha==b->alpha &&
        x->type==y->type && x->a==y->a && x->b==y->b &&
        x->c==y->c && x->d==y->d && x->e==y->e && x->f==y->f &&
        x->color==y->color && x->texture_u==y->texture_u &&
        x->texture_v==y->texture_v && x->style==y->style &&
        !memcmp(x->text,y->text,sizeof(x->text));
}

static int scene_world_prop_same(
    const struct rf_gpu_scene_world_prop_item_v1 *a,
    const struct rf_gpu_scene_world_prop_item_v1 *b)
{
    const struct toy_map_prop *x=&a->prop,*y=&b->prop;
    return !strcmp(a->id,b->id) &&
        a->submission_ordinal==b->submission_ordinal &&
        a->scene_light_q8==b->scene_light_q8 &&
        x->asset_id==y->asset_id && x->x==y->x && x->y==y->y &&
        x->z==y->z && x->yaw_degrees==y->yaw_degrees &&
        x->scale_milli==y->scale_milli && x->length==y->length;
}

int rf_gpu_scene_world_resources_prepare(struct rf_gpu_scene_world_resources *owner,
    const struct rf_gpu_scene_snapshot_v2 *snapshot,
    const struct rf_gpu_scene_world_render_frame_v1 *render,
    const struct rf_gpu_scene_world_floor_frame_v1 *floor,
    const struct rf_gpu_scene_world_prop_frame_v1 *props,
    uint64_t light_generation)
{
    static const char *const identities[RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT]={
        "@scene/world/map-wall","@scene/world/map-box",
        "@scene/world/map-ramp","@scene/world/map-platform",
        "@scene/world/floor","@scene/world/boundary",
        "@scene/world/model-box","@scene/world/sign",
        "@scene/world/model-legacy","@scene/world/model-special",
        "@scene/world/model-infected"};
    struct rasterfall_model_asset *models[RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT]={0};
    struct rasterfall_resource_handle handles[RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT]={0};
    struct rasterfall_resource_handle prop_handles[RASTERFALL_PROP_ASSET_COUNT+1]={0};
    unsigned char prop_used[RASTERFALL_PROP_ASSET_COUNT+1]={0};
    uint32_t accepted=0,deferred=0,transparent=0,prop_accepted=0,needed=0,available=0;
    uint32_t prop_asset_count=0;
    int same_render,same_props;
    if (!owner || !floor || !props || !light_generation ||
        rf_gpu_scene_world_render_validate(snapshot,render)<0 ||
        floor->frame_id!=render->frame_id ||
        floor->world_generation!=render->world_generation ||
        floor->map_generation!=render->map_generation ||
        floor->spawn_count>TOY_MAP_MAX_ZONES ||
        floor->minx>=floor->maxx || floor->minz>=floor->maxz ||
        props->frame_id!=render->frame_id ||
        props->world_generation!=render->world_generation ||
        props->map_generation!=render->map_generation ||
        props->count>TOY_MAP_MAX_PROPS) return -1;
    same_render=owner->render_count==render->count;
    for(uint32_t i=0;same_render && i<render->count;++i)
        if (!scene_world_item_same(&owner->render_items[i],&render->items[i]))
            same_render=0;
    same_props=owner->prop_count==props->count;
    for(uint32_t i=0;same_props && i<props->count;++i)
        if (!scene_world_prop_same(&owner->prop_items[i],&props->items[i]))
            same_props=0;
    if (owner->map_generation==render->map_generation &&
        owner->world_generation==render->world_generation &&
        owner->light_generation==light_generation && same_render && same_props &&
        scene_world_floor_same(&owner->floor,floor)) {
        for(uint32_t i=0;i<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++i)
            if (owner->opaque[i].generation &&
                !rasterfall_resources_resolve_active(&owner->registry,owner->opaque[i])) return -1;
        for(int asset=1;asset<=RASTERFALL_PROP_ASSET_COUNT;++asset)
            if (owner->prop_asset[asset].generation &&
                !rasterfall_resources_resolve_active(&owner->registry,
                    owner->prop_asset[asset])) return -1;
        return 0;
    }
    if (owner->registry.frame_active ||
        rf_gpu_scene_world_opaque_mesh_build(snapshot,render,floor,props,models,
            &accepted,&deferred,&transparent,&prop_accepted)<0) return -1;
    for(uint32_t i=0;i<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++i)
        needed+=models[i]!=NULL;
    for(uint32_t i=0;i<props->count;++i) {
        int asset=props->items[i].prop.asset_id;
        const struct rasterfall_prop_asset_profile *profile;
        if (asset<1 || asset>RASTERFALL_PROP_ASSET_COUNT) goto failed;
        if (asset==RASTERFALL_PROP_ASSET_BOUNDARY_WALL) continue;
        profile=rasterfall_prop_asset_profile(asset);
        if (!profile || !profile->model_path || !*profile->model_path) goto failed;
        if (!prop_used[asset]) { prop_used[asset]=1;needed++;prop_asset_count++; }
    }
    for(uint32_t i=0;i<RASTERFALL_RESOURCE_CAPACITY;++i) {
        const struct rasterfall_resource_slot *slot=&owner->registry.slots[i];
        if (slot->generation!=UINT_MAX && !slot->pinned) available++;
    }
    if (available<needed) goto failed;
    owner->world_generation=0;
    rasterfall_resources_invalidate(&owner->registry);
    for(uint32_t i=0;i<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++i) {
        if (!models[i]) continue;
        if (rasterfall_resources_adopt(&owner->registry,identities[i],models[i],
                &handles[i])<0) {
            rasterfall_resources_invalidate(&owner->registry);
            goto failed;
        }
        models[i]=NULL;
    }
    for(int asset=1;asset<=RASTERFALL_PROP_ASSET_COUNT;++asset) {
        const struct rasterfall_prop_asset_profile *profile;
        if (!prop_used[asset]) continue;
        profile=rasterfall_prop_asset_profile(asset);
        if (rasterfall_resources_load(&owner->registry,profile->model_path,
                &prop_handles[asset])<0) {
            rasterfall_resources_invalidate(&owner->registry);
            goto failed;
        }
    }
    memcpy(owner->opaque,handles,sizeof(handles));
    memcpy(owner->prop_asset,prop_handles,sizeof(prop_handles));
    owner->world_generation=render->world_generation;
    owner->map_generation=render->map_generation;
    owner->light_generation=light_generation;
    owner->floor=*floor;
    owner->render_count=render->count;
    owner->prop_count=props->count;
    memcpy(owner->render_items,render->items,
        render->count*sizeof(render->items[0]));
    memcpy(owner->prop_items,props->items,
        props->count*sizeof(props->items[0]));
    owner->accepted=accepted;owner->deferred=deferred;
    owner->transparent=transparent;owner->prop_accepted=prop_accepted;
    owner->prop_asset_count=prop_asset_count;
    return 0;
failed:
    for(uint32_t i=0;i<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++i)
        if (models[i]) { rasterfall_model_unload(models[i]);tlibc_free(models[i]); }
    return -1;
}

void rf_gpu_scene_world_resources_invalidate(struct rf_gpu_scene_world_resources *owner)
{
    if (!owner) return;
    rasterfall_resources_invalidate(&owner->registry);
    memset(owner->opaque,0,sizeof(owner->opaque));
    owner->world_generation=owner->map_generation=owner->light_generation=0;
    memset(&owner->floor,0,sizeof(owner->floor));
    owner->render_count=0;
    owner->prop_count=0;
    owner->accepted=owner->deferred=owner->transparent=owner->prop_accepted=0;
    owner->prop_asset_count=0;
    owner->prop_draws=owner->prop_deferred=owner->prop_culled=0;
    owner->prop_numeric_deferred=owner->prop_material_deferred=0;
    owner->prop_transparent_deferred=0;
    memset(owner->prop_asset,0,sizeof(owner->prop_asset));
}
