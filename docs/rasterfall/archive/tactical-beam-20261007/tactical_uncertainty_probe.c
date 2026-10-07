#define main calibration_old_main
#include "tactical_forecast_calibration.c"
#undef main

int main(int argc,char **argv)
{
    const char *names[5]={"near_full_risk45","far_full_risk45","peek_risk2","near_full_low_hp","peek_low_hp"};
    const int categories[5]={0,1,2,0,2};
    const float hp[5]={100,100,100,5,5},risk[5]={45,45,2,2,2};
    struct rf_tac_map *map=(struct rf_tac_map *)malloc(sizeof(*map));
    struct calibration_position positions[3]; FILE *out; int i;
    if(argc!=2 || !map || !rf_tac_map_generate(map,100u)) { free(map); return 1; }
    calibration_find(map,positions); out=fopen(argv[1],"wb"); if(!out) { free(map); return 1; }
    fputs("{\"schema\":1,\"experiment\":\"uncertain_shots followup; unchanged mean-state model\",\"conditions\":[",out);
    for(i=0;i<5;++i) {
        struct rf_tac_world source; struct rf_tac_observation observation;
        struct rf_tac_plan plan; struct rf_tac_prediction *prediction;
        struct rf_tac_predictor provider; struct rf_tac_forecast f,repeat;
        const struct calibration_position *p=&positions[categories[i]];
        if(!p->found || !rf_tac_world_init(&source,map,700000u,1,RF_TW_RIFLE,5000)) return 1;
        source.units[0].pos=source.units[0].destination=map->nodes[p->source].pos;
        source.units[6].pos=source.units[6].destination=map->nodes[p->target].pos;
        source.units[6].hp=hp[i]; source.units[6].evasion=risk[i];
        source.units[6].recovery_ms=3000; source.units[6].reload_ms=5000;
        rf_tac_observe(&source,0,&observation); rf_tac_plan_hold(&observation,&plan);
        prediction=rf_tac_prediction_create(&source,0,100); if(!prediction) return 1;
        rf_tac_prediction_provider(prediction,&provider);
        if(provider.evaluate(provider.opaque,&plan,NULL,800,&f)!=RF_TAC_PRED_OK ||
           provider.evaluate(provider.opaque,&plan,NULL,800,&repeat)!=RF_TAC_PRED_OK ||
           memcmp(&f,&repeat,sizeof(f)) || (i==0 && f.uncertain_shots) ||
           (i==3 && f.uncertain_shots) || ((i==1 || i==2 || i==4) && !f.uncertain_shots)) return 1;
        fprintf(out,"%s{\"name\":\"%s\",\"uncertain_shots\":%d,\"predicted_effective_health\":%.9g,"
                    "\"elapsed_ms\":%d,\"finished\":%s}",i?",":"",names[i],f.uncertain_shots,
                    f.enemy[0].after.effective_health,f.elapsed_ms,f.finished?"true":"false");
        fprintf(stderr,"%s: uncertain_shots=%d health=%.6f elapsed=%d\n",names[i],f.uncertain_shots,
                f.enemy[0].after.effective_health,f.elapsed_ms);
        rf_tac_prediction_destroy(prediction);
    }
    fputs("]}\n",out); if(fclose(out)) return 1; free(map); return 0;
}
