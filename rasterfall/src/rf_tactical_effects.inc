#if RF_TACTICAL_INTERACTIVE
/* Read authoritative shot counters once; presentation never advances a match. */
#undef effects
static void rf_tactical_effects_sync(struct rf_game_runtime *r)
{
    struct rf_tactical_lab *lab=r->session->tactical;
    if(!lab || r->lifecycle_paused)return;
    if(lab->kind==1 && lab->match.running) {
        rf_tac_finish(&lab->match.world,lab->accumulator_us);
        if(lab->match.world.finished) {
            lab->match.running=0;rf_tac_prepare(&lab->match.world,1);
            if(!rf_tactical_lab_export(lab)) {
                r->session->banner_text="TACTICAL EXPORT FAILED";r->session->banner_ms=5000;
            }
        }
    } else if(lab->kind==2) rf_range_collect(&lab->range,lab->accumulator_us);
}
#define effects (*active_effects)

#else
static void rf_tactical_effects_sync(struct rf_game_runtime *r){(void)r;}
#endif
