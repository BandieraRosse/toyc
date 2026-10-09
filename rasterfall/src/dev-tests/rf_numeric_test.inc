#include "rf_numeric_display.h"

static int numeric_reference(long long dx,long long dz)
{
    int sy=0,cy=1024, opposite_sy=0,opposite_cy=0,swap_sy=0,swap_cy=0;
    double length=sqrt((double)dx*dx+(double)dz*dz);
    if(!length)return rf_direction_q10(dx,dz,&sy,&cy)==0 && sy==0 && cy==1024 ? 0:1;
    if(rf_direction_q10(dx,dz,&sy,&cy)!=1)return 2;
    if(fabs(sy-(double)dx/length*1024)>1 || fabs(cy-(double)dz/length*1024)>1)return 3;
    long long squared=(long long)sy*sy+(long long)cy*cy;
    if(squared<1022LL*1022 || squared>1026LL*1026 || !rf_direction_valid(sy,cy))return 4;
    rf_direction_q10(-dx,-dz,&opposite_sy,&opposite_cy);
    rf_direction_q10(dz,dx,&swap_sy,&swap_cy);
    if(sy!=-opposite_sy || cy!=-opposite_cy || sy!=swap_cy || cy!=swap_sy)return 5;
    return 0;
}

static int rf_numeric_logic_test(void)
{
    static struct toy_game numeric_game,reference;
    struct rf_numeric_context context={0};
    struct rf_numeric_event event={0},retained;
    int x,z,i,sy,cy;
    unsigned random=712367;
    const long long extremes[]={536870911,536870912,536870913,1073741823,1073741824,2147483647};
    for(x=-64;x<=64;x++)for(z=-64;z<=64;z++) {
        if(numeric_reference(x,z))return 1;
        if(x || z) {
            int sx=0,sz=0;
            rf_direction_q10(x,z,&sx,&sz);
            for(i=1;i<=1024;i*=2) {
                rf_direction_q10((long long)x*i,(long long)z*i,&sy,&cy);
                if(abs(sx-sy)>1 || abs(sz-cy)>1)return 2;
            }
        }
    }
    for(i=0;i<6;i++) {
        if(numeric_reference(extremes[i],extremes[i]) ||
           numeric_reference(extremes[i],-1) || numeric_reference(1,-extremes[i]))return 3;
    }
    for(i=0;i<2000;i++) {
        random=random*1664525U+1013904223U;x=(int)(random&2147483647U);
        random=random*1664525U+1013904223U;z=-(int)(random&2147483647U);
        if(numeric_reference(x,z))return 4;
    }
    sy=30;cy=-19;
    if(rf_direction_q10(sy,cy,&sy,&cy)!=1 || !rf_direction_valid(sy,cy))return 5;
    x=sy;z=cy;
    if(rf_direction_q10(2147483648LL,0,&sy,&cy)!=-1 || sy!=x || cy!=z ||
       rf_direction_q10(-2147483648LL,0,&sy,&cy)!=-1)return 6;
    if(rf_direction_q10(1,0,NULL,&cy)!=-1)return 7;

    context.kind=1;context.slot=2;context.id=17;context.generation=9;context.tick=42;
    rf_numeric_reset(1);
    event.source="numeric-injection";event.category=RF_NUMERIC_RANGE;
    event.context=context;event.dx=2147483648LL;event.old_cy=event.cy=1024;
    for(i=0;i<1000;i++)rf_numeric_record(&event);
    if(rf_numeric_event_count()!=1 || rf_numeric_event_get(0,&retained) ||
       retained.count!=1000 || retained.context.id!=17 || retained.context.tick!=42)return 8;
    rf_numeric_dump();
    for(i=0;i<40;i++) { event.context.id=100+i;rf_numeric_record(&event); }
    if(rf_numeric_event_count()!=32 || rf_numeric_event_get(0,&retained) || retained.context.id!=108)return 9;
    rf_numeric_reset(0);

    double us=9,uc=9;
    if(rf_direction_display("numeric-display",877,-555,&us,&uc,&context)!=1 ||
       fabs(us*us+uc*uc-1)>1e-12)return 10;
    if(rf_direction_display("numeric-display",0,0,&us,&uc,&context)!=0 || us!=0 || uc!=1)return 11;
    if(rf_direction_display("numeric-display",__builtin_nan(""),1,&us,&uc,&context)!=-1 || us!=0 || uc!=1)return 12;
    if(rf_direction_display("numeric-display",__builtin_inf(),1,&us,&uc,&context)!=-1)return 13;
    rf_numeric_set_strict(1);
    if(rf_direction_display("numeric-display",877,-555,&us,&uc,&context)!=-1)return 16;
    rf_numeric_set_strict(0);
    rf_numeric_reset(0);

    /* Diagnostics do not affect commands, authority, events or RNG. */
    toy_game_init(&numeric_game,712367);memcpy(&reference,&numeric_game,sizeof(numeric_game));
    struct toy_game_actor_command command={0};command.switch_slot=-1;command.aim_active=1;
    command.aim_sy=30;command.aim_cy=-19;
    for(i=0;i<2;i++) {
        struct toy_game *g=i?&reference:&numeric_game;
        rf_numeric_reset(i);
        toy_game_execute_actor_command(g,&g->actors[0],&command,16,100);
        if(g->actors[0].sy!=0 || g->actors[0].cy!=1024 || rf_numeric_event_count()!=1)return 14;
        command.aim_sy=724;command.aim_cy=-724;
        toy_game_execute_actor_command(g,&g->actors[0],&command,16,100);
        command.aim_sy=30;command.aim_cy=-19;
    }
    if(memcmp(&numeric_game,&reference,sizeof(numeric_game)))return 15;
    rf_numeric_reset(0);
    __printf("NUMERIC short/extreme/reference/symmetry/alias/range/display/bounded-diagnostics/determinism passed\n");
    return 0;
}
