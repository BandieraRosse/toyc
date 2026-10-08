/* Bounded, redundant presentation results. HP remains in actor snapshots.
 * Identity, generation and phase deduplicate independently of packet order. */
#define NET_BULLET_EVENT_SIZE (30 + NET_PLAYER_RAY_SIZE)
static void net_encode_bullet(unsigned char *p,const struct toy_game_shot_event *e)
{
    put_u32(p,e->source_id);put_u32(p+4,e->source_generation);
    put_u32(p+8,e->fire_sequence);p[12]=(unsigned char)e->weapon;p[13]=(unsigned char)e->phase;
    put_u32(p+14,e->x);put_u32(p+18,e->y);put_u32(p+22,e->z);
    put_u32(p+26,e->time_ms);encode_ray(p+30,&e->rays[0],1);
}
static int net_decode_bullets(const unsigned char *p,int size,struct rasterfall_net *net)
{
    if(size<1 || p[0]>16 || size!=1+p[0]*NET_BULLET_EVENT_SIZE)return -1;
    /* Validate the whole packet before adding any event. */
    for(int i=0;i<p[0];++i){
        const unsigned char *v=p+1+i*NET_BULLET_EVENT_SIZE;
        if(!toy_game_weapon_ballistics(v[12]) ||
           (v[13]!=TOY_GAME_SHOT_FIRED && v[13]!=TOY_GAME_SHOT_IMPACT))return -1;
    }
    for(int i=0;i<p[0];++i){
        const unsigned char *v=p+1+i*NET_BULLET_EVENT_SIZE;int seen=0;
        struct rasterfall_net_shot_key key={get_u32(v),get_u32(v+4),get_u32(v+8),v[13]};
        for(int j=0;j<TOY_GAME_SHOT_HISTORY;++j){
            const struct rasterfall_net_shot_key *old=&net->shot_seen[j];
            if(old->source==key.source && old->generation==key.generation &&
               old->sequence==key.sequence && (old->phase==key.phase ||
                (key.phase==TOY_GAME_SHOT_FIRED && old->phase==TOY_GAME_SHOT_IMPACT))){seen=1;break;}
        }
        if(seen || net->remote_shot_count>=64)continue;
        net->shot_seen[net->shot_seen_next++%TOY_GAME_SHOT_HISTORY]=key;
        struct toy_game_shot_event *e=&net->remote_shots[net->remote_shot_count++];
        memset(e,0,sizeof(*e));e->source_id=key.source;e->source_generation=key.generation;
        e->fire_sequence=key.sequence;e->phase=key.phase;e->weapon=v[12];
        e->x=(int)get_u32(v+14);e->y=(int)get_u32(v+18);e->z=(int)get_u32(v+22);
        e->time_ms=(int)get_u32(v+26);e->ray_count=1;decode_ray(v+30,&e->rays[0],1);
    }
    return 0;
}
static int net_send_bullets(struct rasterfall_net *net,const struct toy_game *g)
{
    unsigned char packet[RASTERFALL_NET_MAX_PACKET],*p=packet+NET_HEADER_SIZE;
    unsigned count=g->shot_serial<TOY_GAME_SHOT_HISTORY?g->shot_serial:TOY_GAME_SHOT_HISTORY;
    p[0]=0;
    for(unsigned n=g->shot_serial-count;n<g->shot_serial;++n){
        const struct toy_game_shot_event *e=&g->shot_history[n%TOY_GAME_SHOT_HISTORY];
        if(e->phase==TOY_GAME_SHOT_RESOLVED || g->combat_time_ms-e->time_ms>300)continue;
        net_encode_bullet(p+1+p[0]*NET_BULLET_EVENT_SIZE,e);++p[0];
        if(p[0]==16 || n+1==g->shot_serial){
            int size=packet_begin(packet,RASTERFALL_NET_BULLET_EVENTS,1+p[0]*NET_BULLET_EVENT_SIZE,
                                  ++net->send_sequence,net->receive_sequence);
            if(size<0 || net_send_clients(net,packet,size)<0)return -1;
            p[0]=0;
        }
    }
    if(p[0]){
        int size=packet_begin(packet,RASTERFALL_NET_BULLET_EVENTS,1+p[0]*NET_BULLET_EVENT_SIZE,
                              ++net->send_sequence,net->receive_sequence);
        if(size<0 || net_send_clients(net,packet,size)<0)return -1;
    }
    return 0;
}

static int net_bullet_codec_test(void)
{
    static struct rasterfall_net net;
    unsigned char wire[1+NET_BULLET_EVENT_SIZE];struct toy_game_shot_event e={0};
    memset(&net,0,sizeof(net));wire[0]=1;
    e.source_id=91;e.source_generation=3;e.fire_sequence=17;e.weapon=TOY_GAME_WEAPON_AK;
    e.phase=TOY_GAME_SHOT_IMPACT;e.x=-123;e.y=768;e.z=70000;e.time_ms=900;
    e.rays[0].sy=1024;e.rays[0].ex=90000;e.rays[0].hit_y=777;
    e.rays[0].actor_index=9;e.rays[0].actor_generation=8;e.rays[0].health_damage=11;
    net_encode_bullet(wire+1,&e);
    if(net_decode_bullets(wire,sizeof(wire),&net) || net.remote_shot_count!=1 ||
       net.remote_shots[0].x!=-123 || net.remote_shots[0].z!=70000 ||
       net.remote_shots[0].rays[0].health_damage!=11)return 1;
    if(net_decode_bullets(wire,sizeof(wire),&net) || net.remote_shot_count!=1)return 2;
    e.phase=TOY_GAME_SHOT_FIRED;net_encode_bullet(wire+1,&e);
    if(net_decode_bullets(wire,sizeof(wire),&net) || net.remote_shot_count!=1)return 3;
    ++e.source_generation;net_encode_bullet(wire+1,&e);
    if(net_decode_bullets(wire,sizeof(wire),&net) || net.remote_shot_count!=2)return 4;
    wire[14]=9;
    if(!net_decode_bullets(wire,sizeof(wire),&net) ||
       !net_decode_bullets(wire,sizeof(wire)-1,&net) || net.remote_shot_count!=2)return 5;
    return 0;
}
