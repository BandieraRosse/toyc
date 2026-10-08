/* Contracts of the Game-backed lab, independent of rendering and wall time. */
#include "rf_tactical_lab.h"
#include "rf_tactical_beam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rasterfall_units.h"

static int checked,failed;
#define CHECK(c,n) do {++checked;if(!(c)){++failed;fprintf(stderr,"FAIL: %s\n",n);}} while(0)
static void think(struct rf_tac_world *w,int solver)
{
    struct rf_tac_observation obs[2];struct rf_tac_plan plan[2];struct rf_tac_policy policy;
    rf_tac_policy_default(&policy,solver);
    for(int t=0;t<2;++t){rf_tac_observe(w,t,&obs[t]);rf_tac_solve(&obs[t],&policy,&plan[t],NULL);}
    for(int t=0;t<2;++t) CHECK(rf_tac_apply(w,&plan[t]),"plans accepted against same snapshot");
}
static void test_game_execution(struct rf_tac_map *map)
{
    struct rf_tac_world a={0},b={0};
    CHECK(rf_tac_world_init(&a,map,1337,4,RF_TW_RIFLE,60000),"Game world init");
    if(!a.game)return;
    rf_tac_command(&a,0,RF_TAC_ATTACK,map->objective,map->objective_radius);
    CHECK(rf_tac_world_clone(&b,&a),"isolated world clone");
    if(!b.game){rf_tac_world_destroy(&a);return;}
    for(int step=0;step<160;++step) {
        if(a.time_ms%RF_TAC_THINK_MS<RF_TAC_DT_MS){think(&a,RF_TAC_MECHANICAL);think(&b,RF_TAC_MECHANICAL);}
        rf_tac_step(&a);
        rf_tac_prepare(&b,0);b.game->event_count=0;
        toy_game_update_world(b.game,RF_TAC_DT_MS);rf_tac_finish(&b,RF_TAC_DT_MS);
        CHECK(rf_tac_hash(&a)==rf_tac_hash(&b),"hosted phases and headless step agree");
        CHECK(a.game->rng==b.game->rng,"full Game RNG agrees");
        CHECK(!memcmp(a.game->actors,b.game->actors,sizeof(a.game->actors)),"complete actor execution agrees");
    }
    struct toy_game_actor *actor=toy_game_actor_by_id(a.game,a.units[0].actor_id);
    CHECK(actor && actor->controller_external && actor->character_id>=0,"real actor identity and presentation");
    struct toy_game_actor saved=*actor;
    rf_tac_prepare(&a,1);saved.simulation_paused=1;
    /* Intent submission may update requests, but all physical state freezes. */
    saved=*actor;
    toy_game_update_world(a.game,16);
    CHECK(!memcmp(&saved,actor,sizeof(saved)),"paused actor including weapon and recovery freezes");
    rf_tac_prepare(&a,0);
    struct rf_tac_observation obs;struct rf_tac_plan plan;struct rf_tac_policy p;
    rf_tac_observe(&a,0,&obs);rf_tac_policy_default(&p,RF_TAC_UTILITY);p.budget=0;
    rf_tac_solve(&obs,&p,&plan,NULL);
    CHECK(rf_tac_apply(&a,&plan),"zero budget yields legal HOLD");
    ++plan.generation;CHECK(!rf_tac_apply(&a,&plan),"stale generation rejected");
    rf_tac_world_destroy(&b);rf_tac_world_destroy(&a);
}
static void test_prediction(struct rf_tac_map *map)
{
    struct rf_tac_world w={0};struct rf_tac_observation obs;
    struct rf_tac_plan plan;struct rf_tac_predictor provider;
    struct rf_tac_forecast a,b;struct rf_tac_policy policy;
    CHECK(rf_tac_world_init(&w,map,42,4,RF_TW_RIFLE,60000),"prediction root init");
    if(!w.game)return;
    rf_tac_observe(&w,0,&obs);rf_tac_plan_hold(&obs,&plan);
    struct rf_tac_prediction *p=rf_tac_prediction_create(&w,0,200);
    CHECK(p!=NULL,"prediction owns deep Game snapshot");
    if(p) {
        unsigned hash=rf_tac_hash(&w);unsigned long long rng=w.game->rng;
        rf_tac_prediction_provider(p,&provider);
        CHECK(provider.evaluate(provider.opaque,&plan,NULL,800,&a)==RF_TAC_PRED_OK,"shared Game rollout succeeds");
        CHECK(provider.evaluate(provider.opaque,&plan,NULL,800,&b)==RF_TAC_PRED_OK,"repeat rollout succeeds");
        CHECK(!memcmp(&a,&b,sizeof(a)),"branches restart from identical independent root");
        CHECK(hash==rf_tac_hash(&w) && rng==w.game->rng,"prediction cannot mutate live state or RNG");
        CHECK(a.uncertain_shots!=0,"sampled forecast is not a certain terminal result");
        int remaining=provider.remaining_steps(provider.opaque);++plan.generation;
        CHECK(provider.evaluate(provider.opaque,&plan,NULL,800,&b)==RF_TAC_PRED_INVALID,"invalid forecast rejected");
        CHECK(provider.remaining_steps(provider.opaque)==remaining,"invalid forecast consumes no steps");
        rf_tac_plan_hold(&obs,&plan);
        rf_tac_policy_default(&policy,RF_TAC_BEAM);
        rf_tac_solve_with_predictor(&obs,&policy,&provider,&plan,NULL);
        CHECK(plan.evaluations<=policy.budget,"beam obeys work budget");
        CHECK(rf_tac_apply(&w,&plan),"beam returns executable Game plan");
        rf_tac_prediction_destroy(p);
    }
    rf_tac_world_destroy(&w);
}
static void test_range(void)
{
    struct rf_range_lab *r=calloc(1,sizeof(*r));
    CHECK(r!=NULL,"range allocation");if(!r)return;
    rf_range_reset(r,RF_TW_RIFLE,RF_TW_AUTO,0,0,77);
    CHECK(r->game!=NULL,"range owns ordinary Game");if(!r->game){free(r);return;}
    struct toy_game_actor *player=toy_game_actor_by_id(r->game,r->shooter_ids[1]);
    unsigned before=player->fire_seq;
    CHECK(rf_range_fire(r,1,0,100,0),"off-target normal shot fires");
    CHECK(player->fire_seq==before+1 && r->stats[1][0].hits==0,"miss keeps normal weapon sequence");
    unsigned long long rng=r->game->rng;
    CHECK(!rf_range_fire(r,1,0,0,0) && rng==r->game->rng,"cooldown rejection preserves RNG");
    r->running=1;
    for(int i=0;i<900;++i)rf_range_step(r,16);
    CHECK(r->stats[0][0].shots>0 && r->stats[0][0].hits>0 && r->stats[0][0].damage>0,"AI uses real hitscan and HP settlement");
    CHECK(r->stats[0][0].kills>0,"real target trials respawn and record kills");
    struct toy_game_actor *ai=toy_game_actor_by_id(r->game,r->shooter_ids[0]);
    r->running=0;rf_range_prepare(r,16);struct toy_game_actor saved=*ai;
    for(int i=0;i<20;++i)rf_range_step(r,16);
    CHECK(!memcmp(ai,&saved,sizeof(saved)),"range pause freezes AI");
    rf_range_reset(r,RF_TW_RIFLE,RF_TW_AUTO,0,2,77);
    player=toy_game_actor_by_id(r->game,r->shooter_ids[1]);
    CHECK(rf_range_fire(r,1,0,0,0),"covered body shot fires");
    CHECK(r->stats[1][0].hits==0 && player->rays[0].hit_world,"physical cover blocks body shot");
    r->running=1;for(int i=0;i<300;++i)rf_range_step(r,16);
    CHECK(r->stats[0][0].hits>0,"AI can aim above actual head cover");
    rf_range_reset(r,RF_TW_RIFLE,RF_TW_AUTO,0,0,77);
    struct toy_game_actor *target=toy_game_actor_by_id(r->game,r->target_ids[0][0]);
    target->hp=0;
    for(int s=0;s<2;++s) {
        struct toy_game_shot_event *e=&r->game->shot_history[s];
        memset(e,0,sizeof(*e));e->serial=s+1;e->source_id=r->shooter_ids[s];
        e->source_generation=r->shooter_generations[s];e->ray_count=1;
        e->rays[0].actor_index=(int)(target-r->game->actors);
        e->rays[0].actor_generation=target->combat_generation;
        e->rays[0].health_damage=10;e->killed_rays=s?1:0;
    }
    r->game->shot_serial=2;rf_range_collect(r,16);
    CHECK(r->stats[0][0].kills==0 && r->stats[1][0].kills==0,
          "same-frame mixed kill cannot be attributed from final HP");
    CHECK(target->hp>0 && r->stats[0][0].hits==1 && r->stats[1][0].hits==1,
          "target resets only after every confirmed event is consumed");
    rf_range_destroy(r);free(r);
}
#include "rf_ai_test.inc"
int rf_tac_run_tests(void)
{
    struct rf_tac_map *map=calloc(1,sizeof(*map));checked=failed=0;
    CHECK(map!=NULL,"map allocation");if(!map)return 0;
    CHECK(rf_tac_map_generate(map,100),"Game collision graph generation");
    if(map->geometry){test_game_execution(map);test_prediction(map);test_ai_abi();test_ai_game(map);test_ai_queries(map);}
    test_range();rf_tac_map_destroy(map);free(map);
    printf("TACTICAL GAME: %d checks, %d failures\n",checked,failed);return !failed;
}
