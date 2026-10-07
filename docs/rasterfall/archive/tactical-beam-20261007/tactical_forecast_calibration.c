/* Diagnostic-only private access keeps HP/risk out of the strategy API. */
#include "../../../../rasterfall/lib/rf_tactical.c"
#include <stdio.h>

struct calibration_position {
    int source, target, exposure, found;
    float distance, hit_probability, quality;
};
struct calibration_stats {
    double mean, m2;
    int count;
};

static void calibration_add(struct calibration_stats *s,double value)
{
    double delta; ++s->count; delta=value-s->mean; s->mean+=delta/s->count;
    s->m2+=delta*(value-s->mean);
}

static void calibration_stats_json(FILE *out,const struct calibration_stats *s,double predicted)
{
    double sd=s->count>1?sqrt(s->m2/(s->count-1)):0.0;
    double se=sd/sqrt((double)s->count);
    fprintf(out,"{\"predicted\":%.9g,\"actual_mean\":%.9g,\"actual_sd\":%.9g,\"standard_error\":%.9g,"
                "\"prediction_minus_actual\":%.9g,\"error_in_standard_errors\":",predicted,s->mean,sd,se,predicted-s->mean);
    if(se>1e-12) fprintf(out,"%.9g",(predicted-s->mean)/se); else fputs("null",out);
    fputc('}',out);
}

static void calibration_find(const struct rf_tac_map *map,struct calibration_position *positions)
{
    int i,j,k;
    for(k=0;k<3;++k) { positions[k].found=0; positions[k].quality=1e20f; }
    for(i=0;i<map->node_count;++i) for(j=0;j<map->node_count;++j) {
        struct rf_tac_vec from=map->nodes[i].pos,to=map->nodes[j].pos;
        float distance=tac_distance(from,to),exposure,quality; int category=-1;
        rf_tw_context context; rf_tw_metrics metrics;
        if(distance<8.0f || distance>60.0f) continue;
        if(distance>12.0f && distance<35.0f) continue;
        exposure=rf_tac_exposure(map,from,to);
        if(exposure>0.99f && distance<=12.0f) category=0;
        else if(exposure>0.99f && distance>=50.0f) category=1;
        else if(exposure>0.32f && exposure<0.34f && distance>=35.0f) category=2;
        if(category<0) continue;
        rf_tw_context_reset(&context); context.distance_m=distance;
        context.exposure=(int)roundf(exposure*3); rf_tw_query(rf_tw_profile_get(RF_TW_RIFLE),&context,&metrics);
        quality=category==0?fabsf(distance-10.0f):category==1?fabsf(distance-55.0f):
                fabsf(metrics.hit_probability-0.05f)+0.0001f*fabsf(distance-45.0f);
        if(quality<positions[category].quality) {
            struct calibration_position *p=&positions[category];
            p->source=i; p->target=j; p->exposure=context.exposure; p->distance=distance;
            p->hit_probability=metrics.hit_probability; p->quality=quality; p->found=1;
        }
    }
}

static int calibration_condition(FILE *out,const struct rf_tac_map *map,
                                   const struct calibration_position *position,
                                   const char *name,float hp,float risk,int samples)
{
    struct rf_tac_world source,expected,actual;
    struct rf_tac_observation observation;
    struct rf_tac_prediction *prediction;
    struct rf_tac_predictor provider;
    struct rf_tac_plan plan;
    struct rf_tac_forecast forecast,repeat;
    struct calibration_stats health_stats={0},hp_stats={0},risk_stats={0},shot_stats={0},elapsed_stats={0};
    int i,step,deaths=0; unsigned int source_hash;
    const struct rf_tac_unit *mean_target;
    double probability,z=1.959963984540054,centre,half,denominator;
    if(!rf_tac_world_init(&source,map,700000u,1,RF_TW_RIFLE,5000)) return 0;
    source.units[0].pos=source.units[0].destination=map->nodes[position->source].pos;
    source.units[RF_TAC_MAX_SQUAD].pos=source.units[RF_TAC_MAX_SQUAD].destination=map->nodes[position->target].pos;
    source.units[RF_TAC_MAX_SQUAD].hp=hp; source.units[RF_TAC_MAX_SQUAD].evasion=risk;
    source.units[RF_TAC_MAX_SQUAD].recovery_ms=RF_TW_BASE_RECOVERY_DELAY_MS;
    source.units[RF_TAC_MAX_SQUAD].reload_ms=5000;
    source_hash=rf_tac_hash(&source);
    rf_tac_observe(&source,0,&observation); rf_tac_plan_hold(&observation,&plan);
    prediction=rf_tac_prediction_create(&source,0,100);
    if(!prediction) return 0;
    rf_tac_prediction_provider(prediction,&provider);
    if(provider.evaluate(provider.opaque,&plan,NULL,800,&forecast)!=RF_TAC_PRED_OK ||
       provider.evaluate(provider.opaque,&plan,NULL,800,&repeat)!=RF_TAC_PRED_OK ||
       memcmp(&forecast,&repeat,sizeof(forecast)) || rf_tac_hash(&source)!=source_hash) {
        rf_tac_prediction_destroy(prediction); return 0;
    }
    expected=source;
    for(step=0;step<800/RF_TAC_DT_MS && !expected.finished;++step) tac_step_mode(&expected,1);
    mean_target=&expected.units[RF_TAC_MAX_SQUAD];
    if(fabsf(forecast.enemy[0].after.effective_health-(mean_target->alive?mean_target->hp+mean_target->evasion:0))>0.0001f ||
       expected.rng!=source.rng || expected.time_ms!=forecast.elapsed_ms) {
        rf_tac_prediction_destroy(prediction); return 0;
    }
    actual=source; actual.seed=987654321u; actual.rng=123456789u;
    rf_tac_observe(&actual,0,&observation); rf_tac_plan_hold(&observation,&plan);
    if(!rf_tac_prediction_reset(prediction,&actual,0,100) ||
       provider.evaluate(provider.opaque,&plan,NULL,800,&repeat)!=RF_TAC_PRED_OK ||
       memcmp(&forecast,&repeat,sizeof(forecast))) {
        rf_tac_prediction_destroy(prediction); return 0;
    }
    for(i=0;i<samples;++i) {
        actual=source; actual.seed=700000u+(unsigned int)i*7919u; actual.rng=actual.seed;
        for(step=0;step<800/RF_TAC_DT_MS && !actual.finished;++step) rf_tac_step(&actual);
        {
            const struct rf_tac_unit *target=&actual.units[RF_TAC_MAX_SQUAD];
            calibration_add(&health_stats,target->alive?target->hp+target->evasion:0.0f);
            calibration_add(&hp_stats,target->hp); calibration_add(&risk_stats,target->evasion);
            calibration_add(&shot_stats,actual.units[0].shots); calibration_add(&elapsed_stats,actual.time_ms);
            deaths+=!target->alive;
        }
    }
    probability=(double)deaths/samples; denominator=1+z*z/samples;
    centre=(probability+z*z/(2*samples))/denominator;
    half=z*sqrt((probability*(1-probability)+z*z/(4*samples))/samples)/denominator;
    fprintf(out,"{\"name\":\"%s\",\"samples\":%d,\"position\":{\"shooter_node\":%d,\"target_node\":%d,"
                "\"shooter\":[%.9g,%.9g],\"target\":[%.9g,%.9g],\"distance_m\":%.9g,"
                "\"public_exposure\":%.9g,\"exposure_kind\":%d,\"first_shot_hit_probability\":%.9g},"
                "\"initial_target_hp\":%.9g,\"initial_target_risk\":%.9g,\"health\":",
                name,samples,position->source,position->target,
                source.units[0].pos.x,source.units[0].pos.y,source.units[RF_TAC_MAX_SQUAD].pos.x,
                source.units[RF_TAC_MAX_SQUAD].pos.y,position->distance,
                rf_tac_exposure(map,source.units[0].pos,source.units[RF_TAC_MAX_SQUAD].pos),
                position->exposure,position->hit_probability,hp,risk);
    calibration_stats_json(out,&health_stats,forecast.enemy[0].after.effective_health);
    fputs(",\"hp\":",out); calibration_stats_json(out,&hp_stats,mean_target->hp);
    fputs(",\"risk\":",out); calibration_stats_json(out,&risk_stats,mean_target->evasion);
    fputs(",\"shots_fired\":",out); calibration_stats_json(out,&shot_stats,expected.units[0].shots);
    fputs(",\"elapsed_ms\":",out); calibration_stats_json(out,&elapsed_stats,expected.time_ms);
    fprintf(out,",\"predicted_dead\":%s,\"actual_deaths\":%d,\"actual_death_rate\":%.9g,"
                "\"death_rate_standard_error\":%.9g,\"death_rate_wilson_95\":[%.9g,%.9g],"
                "\"source_unchanged\":true,\"forecast_rng_independent\":true,\"private_and_public_health_match\":true}",
                mean_target->alive?"false":"true",deaths,probability,sqrt(probability*(1-probability)/samples),
                centre-half,centre+half);
    fprintf(stderr,"%s: %.3fm exposure %.3f; health predicted %.3f vs actual %.3f; death %.3f\n",
            name,position->distance,position->exposure/3.0f,forecast.enemy[0].after.effective_health,
            health_stats.mean,probability);
    rf_tac_prediction_destroy(prediction); return 1;
}

int main(int argc,char **argv)
{
    struct rf_tac_map *map=(struct rf_tac_map *)malloc(sizeof(*map));
    struct calibration_position positions[3]; FILE *out; int k,ok=1,samples=4096;
    if(argc!=2 || !map || !rf_tac_map_generate(map,100u)) { free(map); return 1; }
    calibration_find(map,positions);
    for(k=0;k<3;++k) if(!positions[k].found) { fprintf(stderr,"missing exposure category %d\n",k); free(map); return 1; }
    out=fopen(argv[1],"wb"); if(!out) { free(map); return 1; }
    fprintf(out,"{\"schema\":1,\"experiment\":\"800ms repeated-shot mean-state prediction calibration\","
                "\"map_seed\":100,\"map_content_hash\":\"%08x\",\"simulation_version\":%d,"
                "\"weapon\":\"rifle\",\"squad_size\":1,\"horizon_ms\":800,\"dt_ms\":20,"
                "\"world_time_limit_ms\":5000,\"enemy_initial_reload_ms\":5000,"
                "\"target_initial_recovery_delay_ms\":3000,\"samples_per_condition\":%d,"
                "\"actual_seed_formula\":\"700000 + sample_index * 7919 modulo 2^32\","
                "\"actions\":\"both HOLD; enemy is passive during horizon\","
                "\"conditions\":[",map->content_hash,RF_TAC_VERSION,samples);
    ok=calibration_condition(out,map,&positions[0],"near_full_risk45",100,45,samples);
    if(ok) { fputc(',',out); ok=calibration_condition(out,map,&positions[1],"far_full_risk45",100,45,samples); }
    if(ok) { fputc(',',out); ok=calibration_condition(out,map,&positions[2],"peek_risk2",100,2,samples); }
    if(ok) { fputc(',',out); ok=calibration_condition(out,map,&positions[0],"near_full_low_hp",5,2,samples); }
    if(ok) { fputc(',',out); ok=calibration_condition(out,map,&positions[2],"peek_low_hp",5,2,samples); }
    fputs("],\"scope\":\"Single-tick hit-mask settlement handles simultaneous shared-risk nonlinearity. Across ticks only mean HP/risk are retained; survival and conservative pressure recovery are approximate. Death ends a native match before 800ms; resources stop at that terminal event, and effective health is zero for dead units. HP/risk are diagnostic-only private reads; the public provider returns effective health only. This run overlaps the larger benchmark and makes no performance claim.\"}\n",out);
    if(fclose(out)) ok=0;
    free(map); return ok?0:1;
}
