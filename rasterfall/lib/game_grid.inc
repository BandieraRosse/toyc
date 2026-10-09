/* Game-owned authored metre grid. Cells retain primitive references at every
 * height; a thin wall does not erase a whole metre of interior floor space. */
static int grid_floor(int value)
{
    int q = value / TOY_GAME_GRID_CELL_SIZE;
    return q - (value < 0 && value % TOY_GAME_GRID_CELL_SIZE != 0);
}

static int grid_build(struct toy_game *g)
{
    int i, x, z, minx=0, maxx=0, minz=0, maxz=0, found=0, count;
    g->grid_valid=0;g->grid_reference_count=0;
    memset(g->grid_buildings,0,sizeof(g->grid_buildings));
    if(!g->grid_enabled)return 1;
    for(i=0;i<g->primitive_count;++i) {
        const struct toy_map_primitive *p=&g->primitives[i];
        if(!found || p->minx<minx)minx=p->minx;
        if(!found || p->maxx>maxx)maxx=p->maxx;
        if(!found || p->minz<minz)minz=p->minz;
        if(!found || p->maxz>maxz)maxz=p->maxz;
        found=1;
    }
    g->nav_origin=grid_floor(minx)*TOY_GAME_GRID_CELL_SIZE;
    g->nav_origin_z=grid_floor(minz)*TOY_GAME_GRID_CELL_SIZE;
    g->nav_cell_size=TOY_GAME_GRID_CELL_SIZE;
    /* Include the closed maximum boundary in its own tile. */
    g->nav_width=grid_floor(maxx)-grid_floor(minx)+1;
    g->nav_height=grid_floor(maxz)-grid_floor(minz)+1;
    count=g->nav_width*g->nav_height;
    if(g->nav_width>TOY_GAME_GRID_MAX_SIDE || g->nav_height>TOY_GAME_GRID_MAX_SIDE ||
       count>TOY_GAME_NAV_MAX_CELLS) {
        g->nav_width=g->nav_height=0;return 0;
    }
    memset(g->grid_offsets,0,(count+1)*sizeof(g->grid_offsets[0]));
    for(i=0;i<g->primitive_count;++i) {
        const struct toy_map_primitive *p=&g->primitives[i];
        int x0=grid_floor(p->minx)-grid_floor(minx),x1=grid_floor(p->maxx)-grid_floor(minx);
        int z0=grid_floor(p->minz)-grid_floor(minz),z1=grid_floor(p->maxz)-grid_floor(minz);
        for(z=z0;z<=z1;++z)for(x=x0;x<=x1;++x)g->grid_offsets[z*g->nav_width+x+1]++;
    }
    for(i=1;i<=count;++i)g->grid_offsets[i]+=g->grid_offsets[i-1];
    if(g->grid_offsets[count]>TOY_GAME_GRID_MAX_REFERENCES)return 0;
    /* Fill backwards using offsets as cursors; ascending source traversal
     * later restores deterministic authored ordering in each query. */
    for(i=0;i<g->primitive_count;++i) {
        const struct toy_map_primitive *p=&g->primitives[i];
        int x0=grid_floor(p->minx)-grid_floor(minx),x1=grid_floor(p->maxx)-grid_floor(minx);
        int z0=grid_floor(p->minz)-grid_floor(minz),z1=grid_floor(p->maxz)-grid_floor(minz);
        for(z=z0;z<=z1;++z)for(x=x0;x<=x1;++x) {
            int cell=z*g->nav_width+x;
            g->grid_references[--g->grid_offsets[cell+1]]=(unsigned short)i;
        }
    }
    for(i=0;i<count;++i)g->grid_offsets[i]=g->grid_offsets[i+1];
    g->grid_reference_count=0;
    for(i=0;i<g->primitive_count;++i) {
        const struct toy_map_primitive *p=&g->primitives[i];
        g->grid_reference_count+=(grid_floor(p->maxx)-grid_floor(p->minx)+1)*
                                 (grid_floor(p->maxz)-grid_floor(p->minz)+1);
    }
    g->grid_offsets[count]=(unsigned int)g->grid_reference_count;
    g->grid_valid=1;
    for(i=0;i<g->grid_building_count;++i) {
        const struct toy_game_box *b=&g->grid_building_bounds[i];
        if(!toy_game_grid_mark_building(g,i,b->minx,b->minz,b->maxx,b->maxz)) {
            g->grid_valid=0;return 0;
        }
    }
    return 1;
}

int toy_game_grid_cell(const struct toy_game *g,int x,int z)
{
    int cx,cz;
    if(!g || !g->grid_enabled || !g->grid_valid)return -1;
    cx=grid_floor(x)-grid_floor(g->nav_origin);
    cz=grid_floor(z)-grid_floor(g->nav_origin_z);
    if(cx<0 || cz<0 || cx>=g->nav_width || cz>=g->nav_height)return -1;
    return cz*g->nav_width+cx;
}

int toy_game_grid_mark_building(struct toy_game *g,int owner,
    int minx,int minz,int maxx,int maxz)
{
    int first=toy_game_grid_cell(g,minx,minz),last=toy_game_grid_cell(g,maxx-1,maxz-1);
    if(owner<0 || owner>=32 || minx>=maxx || minz>=maxz || first<0 || last<0)return 0;
    /* Replacing one footprint removes only that owner's old bits. */
    for(int cell=0;cell<g->nav_width*g->nav_height;++cell)
        g->grid_buildings[cell]&=~(1u<<owner);
    g->grid_building_bounds[owner].minx=minx;g->grid_building_bounds[owner].maxx=maxx;
    g->grid_building_bounds[owner].minz=minz;g->grid_building_bounds[owner].maxz=maxz;
    if(owner>=g->grid_building_count)g->grid_building_count=owner+1;
    for(int z=first/g->nav_width;z<=last/g->nav_width;++z)
        for(int x=first%g->nav_width;x<=last%g->nav_width;++x)
            g->grid_buildings[z*g->nav_width+x]|=1u<<owner;
    return 1;
}

int toy_game_grid_walkable(const struct toy_game *g,int x,int y,int z)
{
    int cell=toy_game_grid_cell(g,x,z);
    if(cell<0)return 0;
    /* Ramp height varies inside a metre; neighbouring floor profiles remain
     * separated by their actual support/body test at the queried point. */
    for(int n=g->flow_cells[cell];n;n=g->flow_nodes[n].sibling)
        if(abs(g->flow_nodes[n].y-y)<=TOY_GAME_GRID_CELL_SIZE) {
            struct toy_game_ground_query q=toy_game_query_ground(g,x,z,0,y);
            /* This is a cell-layer query. The formal movement proof owns
             * footprint transitions, including stepping over a platform lip;
             * testing a stationary body here would reject that same seam. */
            return q.has_support && abs(q.support_y-y)<=TOY_CONFIG_GROUND_STEP_HEIGHT;
        }
    return 0;
}

static int grid_segment_allowed(struct toy_game *g,int x,int z,int y,int tx,int tz)
{
    int dx=tx-x,dz=tz-z,steps=(abs(dx)>abs(dz)?abs(dx):abs(dz))/32+1;
    int previous=toy_game_grid_cell(g,x,z);
    if(!g->grid_enabled || g->grid_building)return 1;
    for(int i=1;i<=steps;++i) {
        if(g->actor_layer_search.active && !g->flow_probe_active)
            g->actor_layer_search.samples++;
        int px=x+(int)((long long)dx*i/steps),pz=z+(int)((long long)dz*i/steps);
        struct toy_game_ground_query q=toy_game_query_ground(g,px,pz,0,y);
        int cell=toy_game_grid_cell(g,px,pz);
        if(!q.has_support || !toy_game_grid_walkable(g,px,q.support_y,pz))return 0;
        if(previous>=0 && cell>=0 && previous%g->nav_width!=cell%g->nav_width &&
           previous/g->nav_width!=cell/g->nav_width) {
            int sx=g->nav_origin+(cell%g->nav_width)*512+256;
            int sz=g->nav_origin_z+(previous/g->nav_width)*512+256;
            int ox=g->nav_origin+(previous%g->nav_width)*512+256;
            int oz=g->nav_origin_z+(cell/g->nav_width)*512+256;
            if(!toy_game_grid_walkable(g,sx,q.support_y,sz) ||
               !toy_game_grid_walkable(g,ox,q.support_y,oz))return 0;
        }
        previous=cell;y=q.support_y;
    }
    return 1;
}

static int grid_candidates(const struct toy_game *g,int x,int z,int radius,
                           unsigned short *indices)
{
    unsigned char seen[TOY_GAME_MAX_PRIMITIVES]={0};
    int x0=grid_floor(x-radius)-grid_floor(g->nav_origin);
    int x1=grid_floor(x+radius)-grid_floor(g->nav_origin);
    int z0=grid_floor(z-radius)-grid_floor(g->nav_origin_z);
    int z1=grid_floor(z+radius)-grid_floor(g->nav_origin_z),count=0,i;
    if(x0<0)x0=0;
    if(z0<0)z0=0;
    if(x1>=g->nav_width)x1=g->nav_width-1;
    if(z1>=g->nav_height)z1=g->nav_height-1;
    for(int cz=z0;cz<=z1;++cz)for(int cx=x0;cx<=x1;++cx) {
        int cell=cz*g->nav_width+cx;
        for(unsigned int r=g->grid_offsets[cell];r<g->grid_offsets[cell+1];++r)
            seen[g->grid_references[r]]=1;
    }
    for(i=0;i<g->primitive_count;++i)if(seen[i])indices[count++]=(unsigned short)i;
    return count;
}
