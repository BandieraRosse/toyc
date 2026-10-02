/* Outpost presentation exhibit. No gameplay entities or baked light inputs. */
static int scene_lighting_lab_visible(const struct rf_gpu_scene_world_gpu_probe *p,
    const struct camera *c)
{ (void)c; return p->layers && p->layers->lighting_lab; }
static void scene_lighting_lab_position(unsigned i,unsigned time,float *p)
{
    double phase=(double)(time%12000)*6.28318530718/12000+i*3.14159265359;
    int x,z;
    rasterfall_render_lab_point("rf_light_lab_area",1252,-1074,&x,&z);
    p[0]=x+(float)sin(phase)*3600;p[1]=4200;p[2]=z+(float)cos(phase)*2200;
}
static int scene_lighting_lab_prepare(struct rf_gpu_scene_world_gpu_probe *p,
    const struct camera *c,uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item **items,uint32_t *count)
{
    if(!scene_lighting_lab_visible(p,c)) return 0;
    const unsigned segments=24,rings=16,n=segments*rings*6;
    if(!p->lighting_lab_sphere) {
        struct rf_gpu_graphics_vertex *v=calloc(n,sizeof(*v));
        uint32_t *index=malloc(n*sizeof(*index)),white=0xffffff;
        if(!v || !index) {free(v);free(index);return -1;}
        const unsigned corners[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
        unsigned at=0;
        for(unsigned y=0;y<rings;++y) for(unsigned x=0;x<segments;++x) {
            float normals[6][3];
            for(unsigned j=0;j<6;++j) {
                double lat=(y+corners[j][1])*3.14159265359/rings;
                double lon=(x+corners[j][0])*6.28318530718/segments;
                normals[j][0]=(float)(sin(lat)*cos(lon));normals[j][1]=(float)cos(lat);
                normals[j][2]=(float)(sin(lat)*sin(lon));
                for(unsigned k=0;k<3;++k) v[at+j].position[k]=(int)(normals[j][k]*650);
                index[at+j]=at+j;
            }
            for(unsigned j=0;j<6;++j) for(unsigned corner=0;corner<3;++corner)
                for(unsigned k=0;k<3;++k) v[at+j].normals[corner*3+k]=(int)(normals[j/3*3+corner][k]*32767);
            at+=6;
        }
        p->lighting_lab_sphere=rf_gpu_graphics_resource_create(p->graphics,v,n,index,n,&white,1,1);
        free(v);free(index);if(!p->lighting_lab_sphere) return -1;
    }
    if(scene_batch_reserve(items,&p->batch_capacity,*count+14)<0) return -1;
    for(unsigned i=0;i<14;++i) {
        struct rf_gpu_graphics_batch_item *item=&(*items)[(*count)++];memset(item,0,sizeof(*item));
        item->resource=p->lighting_lab_sphere;struct rf_gpu_graphics_draw *d=&item->draw;
        rasterfall_render_lab_point("rf_light_lab_area",-2198+(int)(i%4)*2300,
            1326-(int)(i/4)*2300,&d->translation_scale[0],&d->translation_scale[2]);
        d->translation_scale[3]=1000;d->rotation[1]=1024;
        d->camera[0]=c->x;d->camera[1]=c->y;d->camera[2]=c->z;
        d->view[0]=c->sy;d->view[1]=c->cy;d->view[2]=c->pitch_sy;d->view[3]=c->pitch_cy;
        d->projection[0]=width;d->projection[1]=height;d->projection[2]=64;d->projection[3]=width*3/4;
        d->texture[0]=d->texture[1]=1;d->index_count=n;d->double_sided=1;
        d->roughness=0.90f-(i%4)*0.26f;d->metallic=i/4==1?1:0;
        d->quality[2]=i/4==2?2:1;d->material[0]=i/4==1?0xdca477:0x509db8;
        if(i>=12) {
            float pos[3];scene_lighting_lab_position(i-12,p->layers->lighting_time_ms,pos);
            for(unsigned k=0;k<3;++k) d->translation_scale[k]=(int)pos[k];
            d->translation_scale[3]=180;d->material[0]=i==12?0xff9b48:0x488aff;
            d->quality[2]=3;d->scene_layer=RF_GPU_SCENE_EFFECTS;
        }
    }
    p->batch=*items;return 0;
}
