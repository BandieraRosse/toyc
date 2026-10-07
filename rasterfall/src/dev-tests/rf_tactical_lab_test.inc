static void test_interactive_lab(void)
{
    struct rf_tac_match *m=calloc(1,sizeof(*m));
    struct rf_tac_world reference;
    struct rf_tac_policy policies[2];
    rf_tac_policy_default(&policies[0],RF_TAC_MECHANICAL);policies[1]=policies[0];
    CHECK(m!=NULL,"interactive lab allocation");if(!m)return;
    CHECK(rf_tac_match_reset(m,100,1337,4,RF_TW_RIFLE,policies),"interactive lab reset");
    reference=m->world;
    /* Compare each complete step to the existing headless decision path,
     * independent of render cadence or pauses in the caller. */
    for(int i=0;i<100 && !reference.finished;++i) {
        if(reference.time_ms%RF_TAC_THINK_MS==0)test_think(&reference,RF_TAC_MECHANICAL,256);
        rf_tac_step(&reference);CHECK(rf_tac_match_step(m)==1,"interactive step");
        CHECK(rf_tac_hash(&reference)==rf_tac_hash(&m->world),"headless and interactive hashes agree");
    }
    unsigned hash=rf_tac_hash(&m->world);
    CHECK(rf_tac_match_reset(m,100,1337,4,RF_TW_RIFLE,policies),"same-seed restart");
    for(int i=0;i<100 && !m->world.finished;++i)rf_tac_match_step(m);
    CHECK(hash==rf_tac_hash(&m->world),"restart reproduces result");
    rf_tac_match_destroy(m);free(m);
    {
        rf_tw_context c;rf_tw_shot old,aimed;unsigned a=73,b=73;
        rf_tw_context_reset(&c);c.distance_m=40;
        for(int i=0;i<100;++i) {
            rf_tw_sample_shot(rf_tw_profile_get(0),&c,&a,&old);
            rf_tw_sample_aim(rf_tw_profile_get(0),&c,&b,0,0,&aimed);
            CHECK(old.hit==aimed.hit && old.head==aimed.head && old.damage_milli==aimed.damage_milli &&
                old.offset_x_m==aimed.offset_x_m && old.offset_y_m==aimed.offset_y_m && a==b,
                "zero aim preserves sample and RNG contract");
        }
        rf_tw_sample_aim(rf_tw_profile_get(0),&c,&b,100,100,&aimed);
        CHECK(!aimed.hit,"off-target player aim cannot hit");
    }
    {
        struct rf_range_lab range;
        rf_range_reset(&range,0,RF_TW_AUTO,3,0,77);
        CHECK(rf_range_fire(&range,1,3,100,0),"player miss consumes a shot");
        CHECK(range.stats[1][3].shots==1 && range.stats[1][3].hits==0 && !range.stats[0][3].shots,
            "AI and player statistics isolated");
        unsigned rng=range.rng[1];
        CHECK(!rf_range_fire(&range,1,3,0,0) && rng==range.rng[1],"cooldown rejection does not consume RNG");
        rf_range_reset(&range,0,RF_TW_AUTO,3,0,77);
        CHECK(!range.mark_count && !range.stats[1][3].shots,"range reset clears marks and stats");
        range.running=1;rf_range_step(&range,20);range.running=0;
        int cooldown=range.weapons[0].cooldown_ms;
        rf_range_step(&range,200);
        CHECK(!range.stats[0][3].elapsed_ms && range.weapons[0].cooldown_ms==cooldown,
            "paused AI freezes measurement time and weapon clock");
        CHECK(rf_range_fire_distance(&range,1,3,0,0,7.5f),"moving player uses actual distance");
        CHECK(range.stats[1][3].distance_min==7.5f && range.stats[1][3].distance_max==7.5f,
            "actual distance retained independently of lane label");
    }
}
