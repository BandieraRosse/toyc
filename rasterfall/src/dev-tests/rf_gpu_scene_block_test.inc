/* Compare new bind/palette output against the independent existing CPU
 * triangle producer, including ordering, colors, equipment and fallen poses. */
struct scene_block_test {
    struct { int points[3][3];unsigned color; } reference[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES];
    struct rf_gpu_scene_block_pose pose;
    unsigned count,visited;
    int maximum_error,fail;
};
static int scene_block_reference(void *opaque,const struct rf_gpu_scene_enemy_point *a,
    const struct rf_gpu_scene_enemy_point *b,const struct rf_gpu_scene_enemy_point *c,unsigned color)
{
    struct scene_block_test *test=opaque;
    const struct rf_gpu_scene_enemy_point *points[3]={a,b,c};
    if(test->count>=RF_GPU_SCENE_ENEMY_MAX_TRIANGLES)return -1;
    for(int k=0;k<3;++k) {
        test->reference[test->count].points[k][0]=points[k]->x;
        test->reference[test->count].points[k][1]=points[k]->y;
        test->reference[test->count].points[k][2]=points[k]->z;
    }
    test->reference[test->count++].color=color;return 0;
}
static int scene_block_compare(void *opaque,const struct rf_gpu_scene_block_vertex *v,
    unsigned color,int flash)
{
    struct scene_block_test *test=opaque;
    if(test->fail)return -1;
    if(flash && !test->pose.flash)return 0;
    if(test->visited>=test->count || color!=test->reference[test->visited].color)return -1;
    for(unsigned k=0;k<3;++k) {
        if(v[k].bone0>=test->pose.bone_count || v[k].bone1>=test->pose.bone_count ||
            v[k].type>1 || v[k].weight>65535)return -1;
        for(unsigned axis=0;axis<3;++axis) {
            float point[2];
            for(unsigned influence=0;influence<2;++influence) {
                const struct rasterfall_model_skin_palette_bone *p=&test->pose.palette[influence?v[k].bone1:v[k].bone0];
                point[influence]=(float)p->position[axis];
                for(unsigned n=0;n<3;++n)point[influence]+=(float)p->rotation[axis*3+n]*(v[k].position[n]-p->rest[n]);
            }
            float value=v[k].type?(point[0]*v[k].weight+point[1]*(65535-v[k].weight))/65535.0f:point[0];
            int actual=(int)(value<0?value-.5f:value+.5f)+test->pose.origin[axis];
            int error=abs(actual-test->reference[test->visited].points[k][axis]);
            if(error>test->maximum_error)test->maximum_error=error;
            if(error>3) {
                __printf("block retained mismatch triangle=%u vertex=%u axis=%u actual=%d expected=%d\n",
                    test->visited,k,axis,actual,test->reference[test->visited].points[k][axis]);return -1;
            }
        }
    }
    test->visited++;return 0;
}
static int scene_block_retained_test(void)
{
    struct scene_block_test *test=tlibc_malloc(sizeof(*test));
    int result=1,samples=0,maximum_error=0;
    if(!test)return 1;
    struct rf_gpu_scene_procedural_item_v1 item={0};
    item.body_color=0x705060;item.leg_color=0x304050;item.skin_color=0xc09070;item.hair_color=0x302010;
    item.scene_light_q8=256;item.double_sided=1;
    item.state.x=123;item.state.z=-2456;item.state.lift=175;item.state.sy=384;item.state.cy=-949;
    for(int profession=0;profession<RASTERFALL_PROFESSION_COUNT;++profession)
    for(int weapon=-1;weapon<TOY_GAME_WEAPON_COUNT;++weapon)
    for(int action=0;action<TOY_GAME_ANIM_COUNT;++action) {
        item.state.profession_id=profession;item.state.weapon=weapon;
        item.state.animation_id=action;item.state.animation_time_ms=190;
        item.state.moving=action==TOY_GAME_ANIM_MOVE;item.state.locomotion_time_ms=253;
        item.state.downed=action==TOY_GAME_ANIM_DOWNED;
        item.state.muzzle_flash=action==TOY_GAME_ANIM_FIRE;
        item.state.aim_pitch_mdeg=profession%2?37000:-23000;item.state.aim_yaw_mdeg=profession%2?17000:-11000;
        memset(test,0,sizeof(*test));
        if(!rf_gpu_scene_block_supported(&item) ||
            rf_gpu_scene_procedural_triangles(&item,scene_block_reference,test)<0 ||
            rf_gpu_scene_block_sample(&item,&test->pose)<0 ||
            rf_gpu_scene_block_bind(&item,scene_block_compare,test)<0 || test->count!=test->visited) {
            __printf("block retained FAIL profession=%d weapon=%d action=%d count=%u visited=%u\n",
                profession,weapon,action,test->count,test->visited);goto done;
        }
        if(test->maximum_error>maximum_error)maximum_error=test->maximum_error;
        samples++;
    }
    test->fail=1;
    if(rf_gpu_scene_block_bind(&item,scene_block_compare,test)>=0)goto done;
    item.target_half_width=50;if(rf_gpu_scene_block_supported(&item))goto done;
    item.target_half_width=0;item.vertex_lighting=1;if(rf_gpu_scene_block_supported(&item))goto done;
    item.vertex_lighting=0;item.state.animation_id=-1;if(rf_gpu_scene_block_supported(&item))goto done;
    __printf("block retained PASS samples=%d maximum_position_error=%d RFU colors/order/actions/weapons/professions/flash/failure\n",
        samples,maximum_error);result=0;
done:
    tlibc_free(test);return result;
}
