layout(set=1,binding=10,std430) readonly buffer RoofCache {
    vec4 grid; uvec4 extent; vec4 cells[];
} roof_cache;
float roof_visibility(vec3 origin,out bool traced) {
    traced=false;
    if(lighting.light_control.y!=0u && roof_cache.extent.x!=0u) {
        ivec2 cell=ivec2(floor((origin.xz-roof_cache.grid.xy)/roof_cache.grid.z));
        if(all(greaterThanEqual(cell,ivec2(0))) && all(lessThan(cell,ivec2(roof_cache.extent.xy)))) {
            vec4 range=roof_cache.cells[uint(cell.y)*roof_cache.extent.x+uint(cell.x)];
            // Bounds include every primitive touching the column, with a numerical
            // margin. Full coverage is proven during construction, not sampled.
            // The software solid-box path also blocks origins just inside a box,
            // even when its exit is closer than triangle tMin. Keep those on rays.
            if(origin.y>range.z+0.05 || origin.y+131072.0<range.w-0.05)return 1.0;
            if(origin.y+0.5<range.x-0.05 && origin.y+131072.0>range.y+0.05)return 0.0;
        }
    }
    traced=true;return architecture_visibility(origin,vec3(0,1,0),131072.0);
}
layout(set=1,binding=11,std430) readonly buffer IndirectProbes {
    uvec4 header; vec4 params; vec4 minimum; vec4 maximum; uvec4 data[];
} probes;
uint probe_hash(ivec3 cell) {
    uint h=uint(cell.x)*73856093u^uint(cell.y)*19349663u^uint(cell.z)*83492791u;
    return h^(h>>16);
}
uint probe_find(ivec3 cell) {
    uint mask=probes.header.x-1u,at=probe_hash(cell)&mask;
    for(uint i=0u;i<probes.header.x;++i,at=(at+1u)&mask) {
        uvec4 slot=probes.data[at];
        if(slot.w==0u)return 0u;
        if(all(equal(ivec3(slot.xyz),cell)))return slot.w;
    }
    return 0u;
}
vec2 probe_moments(uint base,ivec2 p) {
    // Octahedron edges fold onto the opposite edge with reversed tangent.
    // Clamping duplicates an unrelated direction and creates visibility seams.
    if(p.x<0){p.x=-p.x-1;p.y=7-p.y;}
    else if(p.x>7){p.x=15-p.x;p.y=7-p.y;}
    if(p.y<0){p.y=-p.y-1;p.x=7-p.x;}
    else if(p.y>7){p.y=15-p.y;p.x=7-p.x;}
    uint index=uint(p.y*8+p.x);
    vec4 value=uintBitsToFloat(probes.data[base+6u+index/2u]);
    return (index&1u)==0u?value.xy:value.zw;
}
vec2 probe_depth(uint base,vec3 direction) {
    vec3 d=direction/(abs(direction.x)+abs(direction.y)+abs(direction.z));
    vec2 uv=d.xy;
    if(d.z<0.0)uv=(1.0-abs(uv.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(uv,vec2(0)));
    vec2 at=(uv*0.5+0.5)*8.0-0.5,f=fract(at);ivec2 lo=ivec2(floor(at));
    return mix(mix(probe_moments(base,lo),probe_moments(base,lo+ivec2(1,0)),f.x),
        mix(probe_moments(base,lo+ivec2(0,1)),probe_moments(base,lo+ivec2(1,1)),f.x),f.y);
}
vec3 probe_irradiance(vec3 p,vec3 n,vec3 view_direction) {
    if(lighting.light_control.z==0u || probes.header.y==0u ||
       any(lessThan(p,probes.minimum.xyz)) || any(greaterThan(p,probes.maximum.xyz)))return vec3(0);
    // Normal bias alone remains on the neighbouring wall/ceiling at a corner.
    // A small bias toward the visible free space keeps filtered distance
    // moments from treating the receiving edge itself as an occluder.
    vec3 receiver=p+n*probes.params.y+view_direction*(probes.params.x*0.08);
    vec3 grid=receiver/probes.params.x-0.37,f=fract(grid);ivec3 lo=ivec3(floor(grid));
    uint cell=probe_find(lo);if(cell==0u)return vec3(0);
    uvec4 neighbors0=probes.data[cell],neighbors1=probes.data[cell+1u];
    vec3 sum=vec3(0);float total=0.0;
    for(uint corner=0u;corner<8u;++corner) {
        ivec3 offset=ivec3(int(corner&1u),int((corner>>1u)&1u),int(corner>>2u));
        uint id=corner<4u?neighbors0[corner]:neighbors1[corner-4u];if(id==0u)continue;
        uint base=probes.header.w+(id-1u)*probes.header.z;
        if(uintBitsToFloat(probes.data[base]).w==0.0)continue;
        vec3 position=uintBitsToFloat(probes.data[base+38u]).xyz;
        // Do not blend irradiance from the back of this receiving plane. This
        // is particularly important at thin storey slabs where both floors
        // have valid probes but very different incident radiance.
        float side=smoothstep(-probes.params.y,0.0,dot(n,position-p));
        if(side==0.0)continue;
        vec3 delta=receiver-position;float distance=max(length(delta),0.001);
        vec2 moments=probe_depth(base,delta/distance);
        float difference=max(0.0,distance-moments.x-4.0);
        float variance=max(1.0,moments.y-moments.x*moments.x);
        float visibility=variance/(variance+difference*difference);
        visibility=visibility*visibility*visibility;
        // Suppress tiny leaking tails; directional weighting favours probes
        // on the receiving side of a surface. No room IDs or cutaway inputs.
        if(visibility<1e-5)continue;
        if(visibility<0.2)visibility*=visibility/0.2;
        vec3 trilinear=mix(1.0-f,f,vec3(offset));
        float normal_weight=max(0.05,0.5+0.5*dot(n,normalize(position-p+vec3(1e-6))));
        float weight=trilinear.x*trilinear.y*trilinear.z*visibility*normal_weight*normal_weight*side;
        vec3 irradiance=vec3(0);
        for(int axis=0;axis<3;++axis)if(abs(n[axis])>0.001) {
            uint lobe=uint(axis*2+(n[axis]<0.0?1:0));
            irradiance+=uintBitsToFloat(probes.data[base+lobe]).rgb*n[axis]*n[axis];
        }
        sum+=irradiance*weight;total+=weight;
    }
    // Visibility already rejects occluded neighbours. Normalize the surviving
    // weights even near a slab: a fixed denominator floor stamps the probe
    // lattice into ceilings as their trilinear weights approach zero.
    return sum/max(total,1e-8);
}
