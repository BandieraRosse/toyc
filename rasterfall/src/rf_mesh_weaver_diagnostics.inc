/* Optional deterministic experiment driver. Include immediately after
 * rf_mesh_weaver_runtime.inc. This drives the real session/Game APIs and never
 * assigns phase, progress, elapsed time, energy or production counters. */
struct rf_weaver_diagnostic {
    int enabled, start_attempted, start_result, blueprint;
    int target_ms, pause_ms, resume_ms, menu, capture_frame, fixed_ms;
    int start_delay_ms, display_ms, timeline_ms, pause_applied, resume_applied;
    int previous_phase, previous_reason;
    unsigned ticks;
};
static struct rf_weaver_diagnostic rf_weaver_diagnostic;

#ifdef TOYC_WINDOWS
/* Explicit readback of an otherwise ordinary native frame. A request contains
 * only a monotonic integer; it cannot inject a path or change simulation/input.
 * Sequence mode is restricted to the deterministic visual driver. */
static struct {
    unsigned id, last_id, every;
    int initialized, enabled;
    int64_t next_poll;
    char directory[512], path[576];
} rf_weaver_capture;

static const char *rf_weaver_capture_path(int allowed,unsigned frame)
{
    if(!allowed)return NULL;
    if(!rf_weaver_capture.initialized) {
        const char *directory=rf_weaver_environment("RF_WEAVER_CAPTURE_DIRECTORY");
        if(!directory)directory=rf_weaver_environment("RF_UI_CAPTURE_DIRECTORY");
        rf_weaver_capture.initialized=1;
        if(directory && directory[0] && strlen(directory)<sizeof(rf_weaver_capture.directory)) {
            strcpy(rf_weaver_capture.directory,directory);rf_weaver_capture.enabled=1;
            const char *every=rf_weaver_environment("RF_WEAVER_CAPTURE_EVERY");
            if(every && rf_weaver_diagnostic.enabled) {
                char *end;long value=strtol(every,&end,10);
                if(!*end && value>=1 && value<=60)rf_weaver_capture.every=(unsigned)value;
            }
        }
    }
    if(!rf_weaver_capture.enabled)return NULL;
    unsigned id=0;
    if(rf_weaver_capture.every) {
        if(frame%rf_weaver_capture.every)return NULL;
        id=frame/rf_weaver_capture.every;
    } else {
        int64_t now=rf_core_clock_now_us();
        if(now<rf_weaver_capture.next_poll)return NULL;
        rf_weaver_capture.next_poll=now+100000;
        char request[576],value[32];
        snprintf(request,sizeof(request),"%s/capture.request",rf_weaver_capture.directory);
        FILE *file=fopen(request,"rb");
        if(!file)return NULL;
        int count=(int)fread(value,1,sizeof(value)-1,file);fclose(file);value[count]=0;
        char *end;long parsed=strtol(value,&end,10);
        if(end==value || (*end && *end!='\n' && *end!='\r') || parsed<1 || parsed>100000)return NULL;
        id=(unsigned)parsed;
        if(id<=rf_weaver_capture.last_id)return NULL;
    }
    rf_weaver_capture.id=id;rf_weaver_capture.last_id=id;
    snprintf(rf_weaver_capture.path,sizeof(rf_weaver_capture.path),
        "%s/frame-%06u",rf_weaver_capture.directory,id);
    return rf_weaver_capture.path;
}

static int rf_weaver_capture_complete(const struct rasterfall_session *s,unsigned frame)
{
    const struct toy_mesh_weaver *w=&s->game_state.weaver;
    __printf("MESH-WEAVER-CAPTURE id=%u frame=%u phase=%d progress=%.9f elapsed_ms=%.6f readback=1\n",
        rf_weaver_capture.id,frame,w->phase,w->progress,w->elapsed_ms);
    if(!rf_weaver_capture.every) {
        char ack[576];snprintf(ack,sizeof(ack),"%s/capture.complete",rf_weaver_capture.directory);
        FILE *file=fopen(ack,"wb");if(!file)return -1;
        int failed=fprintf(file,"%u %u\n",rf_weaver_capture.id,frame)<0;
        if(fclose(file))failed=1;
        if(failed)return -1;
    }
    return 0;
}
#endif

static int rf_weaver_diag_integer(const char *name,int fallback,int minimum,int maximum)
{
    const char *text=rf_weaver_environment(name);char *end=NULL;long value;
    if(!text || !text[0]) return fallback;
    value=strtol(text,&end,10);
    if(!end || *end || value<minimum || value>maximum) {
        __printf("MESH-WEAVER-ERROR invalid_environment=%s\n",name);
        return -2;
    }
    return (int)value;
}

static const char *rf_weaver_diag_blueprint(void)
{
    int index=rf_weaver_diagnostic.blueprint;
    return index>=0 && (unsigned)index<RF_WEAVER_BLUEPRINT_COUNT ?
        rf_weaver_blueprints[index].id : "idle";
}

static void rf_weaver_diag_audit(const struct rasterfall_session *s,
    const char *event,unsigned long long frame)
{
    const struct rf_weaver_diagnostic *d=&rf_weaver_diagnostic;
    if(!d->enabled || !s) return;
    const struct toy_mesh_weaver *w=&s->game_state.weaver;
    __printf("MESH-WEAVER-AUDIT {\"event\":\"%s\",\"blueprint\":\"%s\","
        "\"frame\":%llu,\"ticks\":%u,\"target_ms\":%d,\"timeline_ms\":%d,"
        "\"elapsed_ms\":%.6f,\"phase\":%d,\"phase_ms\":%.6f,\"pause_reason\":%d,"
        "\"progress\":%.9f,\"energy_kj\":%.9f,\"used_compute\":%.9f,"
        "\"used_form_kj\":%.9f,\"used_base_kj\":%.9f,\"power_on\":%d,"
        "\"cpu_on\":%d,\"x1_on\":%d,\"serial\":%u,\"produced\":%u,"
        "\"collected\":%u,\"output_slot\":%d,\"start_result\":%d,"
        "\"pause_applied\":%d,\"resume_applied\":%d,\"menu\":%d}\n",
        event,rf_weaver_diag_blueprint(),frame,d->ticks,d->target_ms,d->timeline_ms,
        w->elapsed_ms,w->phase,w->phase_ms,w->pause_reason,w->progress,
        w->supply.energy_kj,w->used_compute,w->used_form_kj,w->used_base_kj,
        w->supply.power_on,w->supply.cpu_on,w->supply.x1_on,w->job_serial,
        w->produced_count,w->collected_count,s->weaver_item_serial?s->weaver_item_index:-1,
        d->start_result,d->pause_applied,d->resume_applied,rf_weaver_terminal.open);
}

static void rf_weaver_diag_events(struct rasterfall_session *s)
{
    struct rf_weaver_diagnostic *d=&rf_weaver_diagnostic;
    struct toy_mesh_weaver *w=&s->game_state.weaver;
    if(!d->start_attempted || d->start_result || d->blueprint<0) return;
    if(!d->pause_applied && d->pause_ms>=0 && w->elapsed_ms+1e-6>=d->pause_ms &&
       w->phase!=TOY_WEAVER_IDLE && w->phase!=TOY_WEAVER_READY) {
        struct toy_mesh_weaver_supply supply=w->supply;
        supply.power_on=0;toy_game_weaver_set_supply(&s->game_state,&supply);
        /* A genuine no-power update sets its own pause reason without advancing
         * any work, elapsed time or energy. No phase field is assigned here. */
        toy_game_weaver_update(&s->game_state,1);
        d->pause_applied=1;rf_weaver_diag_audit(s,"power_off",0);
    }
    if(d->pause_applied && !d->resume_applied && d->resume_ms>=0 &&
       d->timeline_ms>=d->resume_ms) {
        struct toy_mesh_weaver_supply supply=w->supply;
        supply.power_on=1;toy_game_weaver_set_supply(&s->game_state,&supply);
        d->resume_applied=1;rf_weaver_diag_audit(s,"power_on",0);
        /* The next real update clears pause_reason and resumes accounting. */
    }
}

static void rf_weaver_diag_phase(struct rasterfall_session *s)
{
    struct rf_weaver_diagnostic *d=&rf_weaver_diagnostic;
    const struct toy_mesh_weaver *w=&s->game_state.weaver;
    if(w->phase!=d->previous_phase || w->pause_reason!=d->previous_reason) {
        rf_weaver_diag_audit(s,"phase",0);
        d->previous_phase=w->phase;d->previous_reason=w->pause_reason;
    }
}

static void rf_weaver_diag_start(struct rasterfall_session *s)
{
    struct rf_weaver_diagnostic *d=&rf_weaver_diagnostic;
    d->start_attempted=1;
    if(d->blueprint>=0) {
        const struct rf_weaver_blueprint *b=&rf_weaver_blueprints[d->blueprint];
        rf_weaver_terminal.selected=d->blueprint;
        if(rf_weaver_terminal.runtime)rf_weaver_terminal.runtime->player_controls.selected_blueprint=d->blueprint;
        d->start_result=rasterfall_session_weaver_start(s,&b->geometry);
        if(d->start_result) {
            const char *unavailable=rf_weaver_unavailable(b);
            rf_weaver_terminal.message=unavailable ? unavailable : toy_mesh_weaver_reason_name(d->start_result);
            rf_weaver_terminal.open=1;
        }
        __printf("MESH-WEAVER-BLUEPRINT {\"id\":\"%s\",\"vertices\":%u,"
            "\"triangles\":%u,\"volume_mm3\":%llu,\"runtime_sha256\":\"%s\","
            "\"manufacturable\":%d,\"size_supported\":%d,\"start_result\":%d,"
            "\"estimated_seconds\":%.9f}\n",b->id,b->geometry.vertex_count,
            b->geometry.triangle_count,b->geometry.volume_mm3,b->runtime_sha256,
            b->manufacturable,b->size_supported,d->start_result,
            d->start_result ? -1.0 : toy_mesh_weaver_estimate(&s->game_state.weaver,&s->game_state.weaver.cost));
    }
    rf_weaver_diag_events(s);rf_weaver_diag_audit(s,"start",0);
}

static void rf_weaver_diag_advance(struct rasterfall_session *s,int duration_ms)
{
    struct rf_weaver_diagnostic *d=&rf_weaver_diagnostic;
    while(duration_ms>0) {
        int step=duration_ms<d->fixed_ms?duration_ms:d->fixed_ms;
        const struct toy_mesh_weaver *w=&s->game_state.weaver;
        rf_weaver_diag_events(s);
        if(!d->pause_applied && d->pause_ms>=0 && w->supply.power_on) {
            double until=d->pause_ms-w->elapsed_ms;
            if(until>0 && until<step) {
                step=(int)until;
                if(step<until) ++step;
            }
        }
        if(d->pause_applied && !d->resume_applied && d->resume_ms>d->timeline_ms &&
           d->resume_ms-d->timeline_ms<step) step=d->resume_ms-d->timeline_ms;
        if(step<1) step=1;
        toy_game_weaver_update(&s->game_state,step);
        rasterfall_session_weaver_sync(s);
        /* Session banners are presentation clocks. Age the start notice during
         * diagnostic preroll just as the visible session ticks would. */
        if(s->banner_ms>0) {s->banner_ms-=step;if(s->banner_ms<0)s->banner_ms=0;}
        d->timeline_ms+=step;duration_ms-=step;
        rf_weaver_diag_events(s);rf_weaver_diag_phase(s);
    }
}

/* Call once after the world/camera and rf_weaver_configure are ready. Pass the
 * actual capture frame, not merely the process frame limit. The ordinary fixed
 * tick currently sends 16 ms to Game despite its 16667 us scheduling period. */
static int rf_weaver_diag_initial(struct rasterfall_session *s,int enabled,
    int frame_limit,int capture_frame,int fixed_step_ms)
{
    struct rf_weaver_diagnostic *d=&rf_weaver_diagnostic;
    memset(d,0,sizeof(*d));d->blueprint=-1;d->previous_phase=d->previous_reason=-1;
    if(!enabled) return 0;
    d->enabled=1;d->fixed_ms=fixed_step_ms;
    d->capture_frame=capture_frame>0?capture_frame:frame_limit;
    d->target_ms=rf_weaver_diag_integer("RF_WEAVER_TIME_MS",-1,-1,120000);
    d->pause_ms=rf_weaver_diag_integer("RF_WEAVER_PAUSE_MS",-1,-1,120000);
    d->resume_ms=rf_weaver_diag_integer("RF_WEAVER_RESUME_MS",-1,-1,120000);
    d->menu=rf_weaver_diag_integer("RF_WEAVER_MENU",0,0,1);
    if(d->target_ms<-1 || d->pause_ms<-1 || d->resume_ms<-1 || d->menu<0 ||
       fixed_step_ms<1 || fixed_step_ms>1000 || d->capture_frame<1 ||
       d->capture_frame>36000 || (d->resume_ms>=0 &&
       (d->pause_ms<0 || d->resume_ms<=d->pause_ms))) return -1;
    const char *name=rf_weaver_environment("RF_WEAVER_BLUEPRINT");
    if(name && name[0] && strcmp(name,"idle")) {
        for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i)
            if(!strcmp(name,rf_weaver_blueprints[i].id)) {d->blueprint=(int)i;break;}
        if(d->blueprint<0) {
            __printf("MESH-WEAVER-ERROR invalid_blueprint=%s\n",name);return -1;
        }
    }
    if(!s || !s->game_state.weaver.enabled || s->game_state.state!=TOY_GAME_PLAYING) {
        __printf("MESH-WEAVER-ERROR experiment_not_available\n");return -1;
    }
    if(d->blueprint>=0) {
        rf_weaver_terminal.selected=d->blueprint;
        if(rf_weaver_terminal.runtime)rf_weaver_terminal.runtime->player_controls.selected_blueprint=d->blueprint;
    }
    rf_weaver_terminal.open=d->menu;
    int visible_ms=d->capture_frame*fixed_step_ms,preroll=0;
    if(d->target_ms>=0) {
        if(d->target_ms>=visible_ms) preroll=d->target_ms-visible_ms;
        else d->start_delay_ms=visible_ms-d->target_ms;
    }
    __printf("MESH-WEAVER-DIAGNOSTIC {\"blueprint\":\"%s\",\"target_ms\":%d,"
        "\"pause_ms\":%d,\"resume_ms\":%d,\"capture_frame\":%d,"
        "\"fixed_step_ms\":%d,\"preroll_ms\":%d,\"start_delay_ms\":%d,"
        "\"time_basis\":\"task timeline includes power-off interval; elapsed_ms is Game active time\"}\n",
        rf_weaver_diag_blueprint(),d->target_ms,d->pause_ms,d->resume_ms,
        d->capture_frame,d->fixed_ms,preroll,d->start_delay_ms);
    if(!d->start_delay_ms) {rf_weaver_diag_start(s);rf_weaver_diag_advance(s,preroll);}
    rf_weaver_display(s);rf_weaver_diag_audit(s,"initial",0);return 0;
}

/* Call immediately AFTER each successful authoritative rf_game_update(dt_ms).
 * The normal update already advances manufacturing: this hook must not repeat
 * it. Only a delayed start has a sub-tick remainder, because that normal update
 * saw IDLE and therefore consumed no manufacturing time. */
static void rf_weaver_diag_step(struct rasterfall_session *s,int dt_ms)
{
    struct rf_weaver_diagnostic *d=&rf_weaver_diagnostic;
    if(!d->enabled || !s || dt_ms<=0) return;
    d->display_ms+=dt_ms;d->ticks++;
    if(!d->start_attempted) {
        if(d->display_ms>=d->start_delay_ms) {
            rf_weaver_diag_start(s);
            rf_weaver_diag_advance(s,d->display_ms-d->start_delay_ms);
        }
    } else {
        d->timeline_ms+=dt_ms;
        rf_weaver_diag_events(s);rf_weaver_diag_phase(s);
    }
}

/* Observation only for real Win32/SDL input checks. This never starts jobs or
 * supplies commands; the regular input handlers and fixed Game ticks do that. */
static void rf_weaver_interaction_audit(const struct rasterfall_session *s,
    int allowed,unsigned frame)
{
    static int64_t previous_time;
    static int previous_menu=-1,previous_row=-1,previous_selection=-1;
    static int previous_phase=-1,previous_reason=-1,previous_highlight=-2;
    static unsigned previous_fire,previous_collected;
    const char *requested=rf_weaver_environment("RF_WEAVER_INTERACTION_AUDIT");
    if(!allowed || !requested || strcmp(requested,"1"))return;
    const struct toy_mesh_weaver *w=&s->game_state.weaver;
    const struct toy_game_actor *a=toy_game_local_player_actor_const(&s->game_state);
    const struct toy_game_slot *slot=&a->slots[a->current_slot];
    int64_t now=rf_core_clock_now_us();
    if(now-previous_time<250000 && previous_menu==rf_weaver_terminal.open &&
       previous_row==rf_weaver_terminal.row && previous_selection==rf_weaver_terminal.selected &&
       previous_phase==w->phase && previous_reason==w->pause_reason &&
       previous_highlight==s->highlight_index && previous_fire==a->fire_seq &&
       previous_collected==w->collected_count)return;
    previous_time=now;previous_menu=rf_weaver_terminal.open;previous_row=rf_weaver_terminal.row;
    previous_selection=rf_weaver_terminal.selected;previous_phase=w->phase;
    previous_reason=w->pause_reason;previous_highlight=s->highlight_index;
    previous_fire=a->fire_seq;previous_collected=w->collected_count;
    const char *blueprint=rf_weaver_selection()->id;
    if(w->phase!=TOY_WEAVER_IDLE)for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i)
        if(rf_weaver_blueprints[i].geometry.weapon==w->blueprint.weapon)blueprint=rf_weaver_blueprints[i].id;
    __printf("MESH-WEAVER-INTERACTION {\"frame\":%u,\"normal_sim\":1,\"driver\":%d,"
        "\"menu\":%d,\"near\":%d,\"row\":%d,\"selected\":%d,\"blueprint\":\"%s\","
        "\"phase\":%d,\"pause_reason\":%d,\"progress\":%.9f,\"elapsed_ms\":%.6f,"
        "\"energy_kj\":%.9f,\"used_form_kj\":%.9f,\"used_base_kj\":%.9f,"
        "\"power_on\":%d,\"cpu_on\":%d,\"x1_on\":%d,\"serial\":%u,"
        "\"produced\":%u,\"collected\":%u,\"output_slot\":%d,\"highlight\":%d,"
        "\"weapon\":%d,\"standard_mag\":%d,\"mag\":%d,\"reserve\":%d,"
        "\"switch_ms\":%d,\"fire_seq\":%u,\"shots\":%d,\"ray_count\":%d,"
        "\"player_x\":%d,\"player_z\":%d}\n",
        frame,rf_weaver_diagnostic.enabled,rf_weaver_terminal.open,rf_weaver_terminal.near,
        rf_weaver_terminal.row,rf_weaver_terminal.selected,blueprint,w->phase,w->pause_reason,
        w->progress,w->elapsed_ms,w->supply.energy_kj,w->used_form_kj,w->used_base_kj,
        w->supply.power_on,w->supply.cpu_on,w->supply.x1_on,w->job_serial,
        w->produced_count,w->collected_count,s->weaver_item_serial?s->weaver_item_index:-1,
        s->highlight_index,slot->weapon,w->cost.initial_rounds,slot->mag,slot->reserve,
        a->weapon_switch_timer_ms,a->fire_seq,a->combat_stats.shots,a->ray_count,a->x,a->z);
}
