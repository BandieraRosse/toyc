/* Authoring-only composites lower into ordinary IR records before Runtime Map.
 * No gameplay callbacks, nested transforms or record-order dependency. */
static int parse_assembly(struct rasterfall_map_ir *ir,int line,struct map_line *record)
{
    const char *keys[]={"id","x","y","z"},*value;
    struct rasterfall_map_ir_assembly *a;
    if (allowed(ir,line,record,keys,4)<0) return -1;
    if (ir->assembly_count>=64) return fail(ir,line,"assembly capacity exceeded");
    for (int i=0;i<record->count;++i)
        if (extension_key(record->fields[i].key) && strcmp(record->fields[i].key,"attr.lab"))
            return fail_field(ir,line,"unknown assembly field %s",record->fields[i].key);
    if (!value_of(record,"id",&value) || !valid_name(value,1)) return fail(ir,line,"invalid assembly id");
    if (id_exists(ir,value)) return fail_field(ir,line,"duplicate id %s",value);
    a=&ir->assemblies[ir->assembly_count++];a->line=line;
    if (copy_text(a->id,sizeof(a->id),value)<0) return fail(ir,line,"assembly id too long");
#define ASSEMBLY_INT(key) if (!value_of(record,#key,&value) || int_value(value,&a->key)<0 || a->key < -1000000 || a->key > 1000000) return fail(ir,line,"invalid assembly origin")
    ASSEMBLY_INT(x);ASSEMBLY_INT(y);ASSEMBLY_INT(z);
#undef ASSEMBLY_INT
    if (copy_extensions(a->attributes,&a->attribute_count,record)<0) return fail(ir,line,"assembly attributes exceeded");
    return 0;
}

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
#define NO_ASSEMBLY(collection,count) for (int i=0;i<ir->count;++i) \
    for (int k=0;k<ir->collection[i].attribute_count;++k) \
        if (!strcmp(ir->collection[i].attributes[k].key,"assembly")) \
            return fail(ir,ir->collection[i].line,"assembly child must be object or sign")
    NO_ASSEMBLY(regions,region_count);NO_ASSEMBLY(collisions,collision_count);
    NO_ASSEMBLY(surfaces,surface_count);NO_ASSEMBLY(interactions,interaction_count);
    NO_ASSEMBLY(actor_spawns,actor_spawn_count);NO_ASSEMBLY(pickups,pickup_count);
#undef NO_ASSEMBLY
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
    LAB_POSITION(assemblies,assembly_count);
    /* Each component chooses exactly one local frame. Assemblies can be in a
     * lab; nesting, rotation and implicit scaling are deliberately unsupported. */
    for (int pass=0;pass<2;++pass) {
        int count=pass ? ir->render_count : ir->object_count;
        for (int i=0;i<count;++i) {
            struct rasterfall_map_ir_object *o=pass ? 0 : &ir->objects[i];
            struct rasterfall_map_ir_render *r=pass ? &ir->renders[i] : 0;
            struct rasterfall_map_ir_attribute *attrs=pass ? r->attributes : o->attributes;
            int n=pass ? r->attribute_count : o->attribute_count;
            int line=pass ? r->line : o->line,has_lab=0;
            const char *id=0;
            struct rasterfall_map_ir_assembly *a=0;
            for (int k=0;k<n;++k) {
                if (!strcmp(attrs[k].key,"assembly")) id=attrs[k].value;
                if (!strcmp(attrs[k].key,"lab")) has_lab=1;
            }
            if (!id) continue;
            if (has_lab) return fail(ir,line,"component cannot have both lab and assembly");
            for (int k=0;k<ir->assembly_count;++k)
                if (!strcmp(ir->assemblies[k].id,id)) a=&ir->assemblies[k];
            if (!a) return fail_field(ir,line,"unknown assembly reference %s",id);
            if (o) {
                if (lab_add(ir,line,&o->x,a->x)<0 || lab_add(ir,line,&o->y,a->y)<0 ||
                    lab_add(ir,line,&o->z,a->z)<0) return -1;
            } else {
                if (strcmp(r->kind,"sign")) return fail(ir,line,"assembly render must be sign");
                int has_top=0;
                for (int k=0;k<n;++k) if (!strcmp(attrs[k].key,"height2")) has_top=1;
                if (!r->has_height || !has_top) return fail(ir,line,"assembly sign requires both heights");
                if (lab_add(ir,line,&r->bounds.min_x,a->x)<0 || lab_add(ir,line,&r->bounds.max_x,a->x)<0 ||
                    lab_add(ir,line,&r->bounds.min_z,a->z)<0 || lab_add(ir,line,&r->bounds.max_z,a->z)<0 ||
                    lab_add(ir,line,&r->height,a->y-900)<0) return -1;
                for (int k=0;k<n;++k) if (!strcmp(attrs[k].key,"height2")) {
                    int top;
                    if (int_value(attrs[k].value,&top)<0) return fail(ir,line,"invalid assembly sign height2");
                    if (lab_add(ir,line,&top,a->y-900)<0) return -1;
                    snprintf(attrs[k].value,sizeof(attrs[k].value),"%d",top);
                }
            }
        }
    }
#undef LAB_POSITION
#undef LAB_BOUNDS
#undef LAB_ADD
#undef LAB_OFFSET
    return 0;
}
