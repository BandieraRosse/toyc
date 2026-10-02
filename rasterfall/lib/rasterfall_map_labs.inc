/* Authoring-only composites lower into ordinary IR records before Runtime Map.
 * No gameplay callbacks, renderer types, nesting or record-order dependency. */
static int parse_lab(struct rasterfall_map_ir *ir,int line,struct map_line *record)
{
    const char *keys[]={"id","x","z","width","depth","category","enclosure"};
    const char *value;
    char id[RASTERFALL_MAP_IR_ID_SIZE];
    int x,z,width,depth;
    struct rasterfall_map_ir_region *r;
    if (allowed(ir,line,record,keys,7)<0) return -1;
    for (int i=0;i<record->count;++i) if (extension_key(record->fields[i].key))
        return fail_field(ir,line,"unknown lab field %s",record->fields[i].key);
    if (!value_of(record,"id",&value) || !valid_name(value,1) ||
        copy_text(id,sizeof(id),value)<0) return fail(ir,line,"invalid lab id");
    if (id_exists(ir,id)) return fail_field(ir,line,"duplicate id %s",id);
#define LAB_INT(key,dest) if (!value_of(record,key,&value) || int_value(value,&dest)<0) return fail_missing(ir,line,"lab",key)
    LAB_INT("x",x); LAB_INT("z",z); LAB_INT("width",width); LAB_INT("depth",depth);
#undef LAB_INT
    /* Conservative range makes subsequent integer additions safe. */
    if (x < -1000000 || x > 1000000 || z < -1000000 || z > 1000000 ||
        width<512 || width>1000000 || depth<512 || depth>1000000 ||
        width%2 || depth%2) return fail(ir,line,"invalid lab dimensions or origin");
    if (ir->region_count>=RASTERFALL_MAP_IR_MAX_REGIONS) return fail(ir,line,"region capacity exceeded");
    r=&ir->regions[ir->region_count++];
    strcpy(r->id,id);strcpy(r->kind,"experiment");r->line=line;r->is_lab=1;
    r->bounds.min_x=x-width/2;r->bounds.max_x=x+width/2;
    r->bounds.min_z=z-depth/2;r->bounds.max_z=z+depth/2;
    if (!value_of(record,"category",&value) ||
        (strcmp(value,"model") && strcmp(value,"animation") &&
         strcmp(value,"lighting") && strcmp(value,"performance")))
        return fail(ir,line,"invalid lab category");
    strcpy(r->attributes[0].key,"category");strcpy(r->attributes[0].value,value);
    if (!value_of(record,"enclosure",&value) ||
        (strcmp(value,"open") && strcmp(value,"backdrop") && strcmp(value,"walled")))
        return fail(ir,line,"invalid lab enclosure");
    strcpy(r->attributes[1].key,"enclosure");strcpy(r->attributes[1].value,value);
    strcpy(r->attributes[2].key,"origin_x");snprintf(r->attributes[2].value,sizeof(r->attributes[2].value),"%d",x);
    strcpy(r->attributes[3].key,"origin_z");snprintf(r->attributes[3].value,sizeof(r->attributes[3].value),"%d",z);
    r->attribute_count=4;
    return 0;
}

static int lab_offset(struct rasterfall_map_ir *ir,
    struct rasterfall_map_ir_attribute *attrs,int count,int line,int *x,int *z)
{
    const char *id=0;
    *x=0;*z=0;
    for (int i=0;i<count;++i) if (!strcmp(attrs[i].key,"lab")) id=attrs[i].value;
    if (!id) return 0;
    for (int i=0;i<ir->region_count;++i) {
        struct rasterfall_map_ir_region *r=&ir->regions[i];
        if (!strcmp(r->id,id) && r->is_lab) {
            int_value(r->attributes[2].value,x);int_value(r->attributes[3].value,z);
            return 0;
        }
    }
    return fail_field(ir,line,"unknown lab reference %s",id);
}
static int lab_add(struct rasterfall_map_ir *ir,int line,int *value,int offset)
{
    if ((offset>0 && *value>2147483647-offset) ||
        (offset<0 && *value<(-2147483647-1)-offset))
        return fail(ir,line,"lab coordinate overflow");
    *value+=offset;return 0;
}
static int map_labs_resolve(struct rasterfall_map_ir *ir)
{
    int x,z;
#define LAB_OFFSET(o) if (lab_offset(ir,o->attributes,o->attribute_count,o->line,&x,&z)<0) return -1
#define LAB_ADD(o,v,d) if (lab_add(ir,o->line,&o->v,d)<0) return -1
#define LAB_BOUNDS(collection,count) for (int i=0;i<ir->count;++i) { \
    LAB_OFFSET((&ir->collection[i])); \
    LAB_ADD((&ir->collection[i]),bounds.min_x,x); LAB_ADD((&ir->collection[i]),bounds.max_x,x); \
    LAB_ADD((&ir->collection[i]),bounds.min_z,z); LAB_ADD((&ir->collection[i]),bounds.max_z,z); }
    LAB_BOUNDS(regions,region_count);
    LAB_BOUNDS(collisions,collision_count);
    LAB_BOUNDS(surfaces,surface_count);
    LAB_BOUNDS(renders,render_count);
#define LAB_POSITION(collection,count) for (int i=0;i<ir->count;++i) { \
    LAB_OFFSET((&ir->collection[i])); \
    LAB_ADD((&ir->collection[i]),x,x); LAB_ADD((&ir->collection[i]),z,z); }
    LAB_POSITION(renders,render_count);
    LAB_POSITION(objects,object_count);
    LAB_POSITION(interactions,interaction_count);
    LAB_POSITION(actor_spawns,actor_spawn_count);
    LAB_POSITION(pickups,pickup_count);
#undef LAB_POSITION
#undef LAB_BOUNDS
#undef LAB_ADD
#undef LAB_OFFSET
    return 0;
}
