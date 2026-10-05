/* Pure, bounded config parsing and deterministic 60 Hz unit conversion. */
void toy_game_player_movement_defaults(struct toy_game_player_movement *out)
{
    if (!out) return;
    out->move_step = TOY_CONFIG_PLAYER_MOVE_STEP;
    out->move_accel = (int)(TOY_CONFIG_PLAYER_MOVE_ACCEL*512LL*1024/3600);
    out->move_brake = (int)(TOY_CONFIG_PLAYER_MOVE_BRAKE*512LL*1024/3600);
    out->turn_accel = (int)(TOY_CONFIG_PLAYER_MOVE_TURN_ACCEL*512LL*1024/3600);
    out->air_accel = (int)(TOY_CONFIG_PLAYER_AIR_MOVE_ACCEL*512LL*1024/3600);
    out->jump_velocity = TOY_GAME_JUMP_VELOCITY;
    out->gravity = TOY_GAME_AIRBORNE_GRAVITY;
    out->fall_terminal = TOY_GAME_FALL_TERMINAL_VELOCITY;
    out->coyote_steps = TOY_CONFIG_PLAYER_JUMP_COYOTE_STEPS;
    out->buffer_steps = TOY_CONFIG_PLAYER_JUMP_BUFFER_STEPS;
}

int toy_game_player_move_step(const struct toy_game *g,
                              const struct toy_game_actor *actor)
{
    return toy_game_actor_move_step(actor, g->player_movement.move_step);
}

static char *game_config_trim(char *text)
{
    char *end;
    while (*text==' ' || *text=='\t' || *text=='\r') text++;
    end=text+strlen(text);
    while (end>text && (end[-1]==' ' || end[-1]=='\t' || end[-1]=='\r')) *--end=0;
    return text;
}

/* Decimal SI value to thousandths, without floating point or locale. */
static int movement_number(const char *text, int *value)
{
    int whole=0, fraction=0, digits=0;
    if (*text<'0' || *text>'9') return 0;
    while (*text>='0' && *text<='9') {
        whole=whole*10+(*text++-'0');
        if (whole>1000) return 0;
    }
    if (*text=='.') {
        text++;
        while (*text>='0' && *text<='9') {
            if (++digits>3) return 0;
            fraction=fraction*10+(*text++-'0');
        }
        if (!digits) return 0;
    }
    if (*text) return 0;
    while (digits++<3) fraction*=10;
    *value=whole*1000+fraction;
    return 1;
}

int toy_game_player_movement_parse(const char *text, int size,
    struct toy_game_player_movement *out, int *error_line, const char **error)
{
    static const char *keys[]={"move_speed_mps","ground_accel_mps2",
        "ground_brake_mps2","turn_accel_mps2","air_accel_mps2",
        "jump_speed_mps","gravity_mps2","fall_terminal_mps",
        "coyote_ms","jump_buffer_ms"};
    static const int minimum[]={500,1000,1000,1000,0,1000,4000,1000,0,0};
    static const int maximum[]={30000,300000,300000,300000,100000,20000,
        100000,100000,250000,250000};
    struct toy_game_player_movement parsed;
    int *values[]={&parsed.move_step,&parsed.move_accel,&parsed.move_brake,
        &parsed.turn_accel,&parsed.air_accel,&parsed.jump_velocity,
        &parsed.gravity,&parsed.fall_terminal,&parsed.coyote_steps,&parsed.buffer_steps};
    int position=0, line_number=0, seen=0;
    const char *reason="invalid input";
    if (error_line) *error_line=0;
    if (error) *error=NULL;
    if (!text || !out || size<0 || size>8192) goto invalid;
    toy_game_player_movement_defaults(&parsed);
    if (size>=3 && (unsigned char)text[0]==239 &&
        (unsigned char)text[1]==187 && (unsigned char)text[2]==191) position=3;
    while (position<size) {
        char line[256], *key, *equal, *comment, *value_text;
        int count=0, index, value;
        line_number++;
        while (position<size && text[position]!='\n') {
            if (count>=255) { reason="line too long";goto invalid; }
            if (!text[position]) { reason="embedded NUL";goto invalid; }
            line[count++]=text[position++];
        }
        if (position<size) position++;
        line[count]=0;
        comment=strchr(line,'#');if(comment)*comment=0;
        key=game_config_trim(line);if(!*key)continue;
        equal=strchr(key,'=');
        if(!equal){reason="expected key = value";goto invalid;}
        *equal=0;key=game_config_trim(key);value_text=game_config_trim(equal+1);
        for(index=0;index<10;++index)if(!strcmp(key,keys[index]))break;
        if(index==10){reason="unknown parameter";goto invalid;}
        if(seen&(1<<index)){reason="duplicate parameter";goto invalid;}
        if(!movement_number(value_text,&value)) {
            reason="expected nonnegative decimal with at most 3 fractional digits";goto invalid;
        }
        if(value<minimum[index] || value>maximum[index]) {
            reason="parameter out of range";goto invalid;
        }
        if(index==0 || index==5 || index==7)
            *values[index]=(int)(((long long)value*512+30000)/60000);
        else if(index>=1 && index<=4)
            *values[index]=(int)((long long)value*512*1024/3600000);
        else if(index==6)
            *values[index]=(int)(((long long)value*512+1800000)/3600000);
        else {
            if(value%1000){reason="timings require whole milliseconds";goto invalid;}
            *values[index]=(value/1000*60+999)/1000;
        }
        seen|=1<<index;
    }
    memcpy(out,&parsed,sizeof(parsed));
    return 0;
invalid:
    if(error_line)*error_line=line_number;
    if(error)*error=reason;
    return -1;
}
