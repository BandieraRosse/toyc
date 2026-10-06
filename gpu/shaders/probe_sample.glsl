// Included once per statically bound field; no per-load buffer selection.
uvec4 probe_load(uint index) { return PROBE_FIELD.data[index]; }
uint probe_hash(ivec3 cell) {
    uint h=uint(cell.x)*73856093u^uint(cell.y)*19349663u^uint(cell.z)*83492791u;
    return h^(h>>16);
}
uint probe_find(ivec3 cell) {
    uint mask=PROBE_FIELD.header.x-1u,at=probe_hash(cell)&mask;
    for(uint i=0u;i<PROBE_FIELD.header.x;++i,at=(at+1u)&mask) {
        uvec4 slot=probe_load(at);
        if(slot.w==0u)return 0u;
        if(all(equal(ivec3(slot.xyz),cell)))return slot.w;
    }
    return 0u;
}
vec2 probe_moments(uint base,ivec2 p) {
    // Octahedron edges fold onto the opposite edge with reversed tangent.
    // Clamping duplicates an unrelated direction and creates visibility seams.
    p=probe_fold(p);
    uint index=uint(p.y*8+p.x);
    vec4 value=uintBitsToFloat(probe_load(base+6u+index/2u));
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
float probe_visibility(uvec2 geometry,vec3 origin,vec3 direction,float limit) {
#ifdef RF_ARCHITECTURE_RAY_QUERY
    if(limit<=0.5)return 1.0;
#endif
    if(geometry.y==0xffffffffu || (lighting.light_control.w&512u)==0u ||
        (lighting.light_control.w&255u)==1u)return architecture_visibility(origin,direction,limit);
    vec3 inverse=1.0/mix(direction,vec3(1e-8),lessThan(abs(direction),vec3(1e-8)));
    for(uint i=0u;i<geometry.y;++i) {
        vec3 lo=uintBitsToFloat(probe_load(geometry.x+i*2u)).xyz;
        vec3 hi=uintBitsToFloat(probe_load(geometry.x+i*2u+1u)).xyz;
        vec3 a=(lo-origin)*inverse,b=(hi-origin)*inverse;
        vec3 near_t=min(a,b),far_t=max(a,b);
        float near_hit=max(max(near_t.x,near_t.y),near_t.z);
        float far_hit=min(min(far_t.x,far_t.y),far_t.z);
#ifdef RF_ARCHITECTURE_RAY_QUERY
        if(near_hit<=far_hit && ((near_hit>0.5 && near_hit<limit) ||
            (far_hit>0.5 && far_hit<limit)))return 0.0;
#else
        if(max(near_hit,0.0)<=min(far_hit,limit))return 0.0;
#endif
    }
    return 1.0;
}
vec3 sample_probe_field(vec3 p,vec3 n,vec3 view_direction,out float covered) {
    covered=0.0;
    if(lighting.light_control.z==0u || PROBE_FIELD.header.y==0u ||
       any(lessThan(p,PROBE_FIELD.minimum.xyz)) || any(greaterThan(p,PROBE_FIELD.maximum.xyz)))return vec3(0);
    // Normal bias alone remains on the neighbouring wall/ceiling at a corner.
    // A small bias toward the visible free space keeps filtered distance
    // moments from treating the receiving edge itself as an occluder.
    vec3 receiver=p+n*PROBE_FIELD.params.y+view_direction*(PROBE_FIELD.params.x*0.08);
    vec3 grid=receiver/PROBE_FIELD.params.x-0.37,f=fract(grid);ivec3 lo=ivec3(floor(grid));
    uint cell=probe_find(lo);if(cell==0u)return vec3(0);
    uvec4 neighbors0=probe_load(cell),neighbors1=probe_load(cell+1u);
    uvec2 geometry=probe_load(cell+2u).xy;
    vec3 sum=vec3(0);float total=0.0;
    for(uint corner=0u;corner<8u;++corner) {
        ivec3 offset=ivec3(int(corner&1u),int((corner>>1u)&1u),int(corner>>2u));
        uint id=corner<4u?neighbors0[corner]:neighbors1[corner-4u];if(id==0u)continue;
        uint base=PROBE_FIELD.header.w+(id-1u)*PROBE_FIELD.header.z;
        if(uintBitsToFloat(probe_load(base)).w==0.0)continue;
        vec3 position=uintBitsToFloat(probe_load(base+PROBE_FIELD.header.z-1u)).xyz;
        // Do not blend irradiance from the back of this receiving plane. This
        // is particularly important at thin storey slabs where both floors
        // have valid probes but very different incident radiance.
        float side=smoothstep(-PROBE_FIELD.params.y,0.0,dot(n,position-p));
        if(side==0.0)continue;
        vec3 delta=receiver-position;float distance=max(length(delta),0.001);
        float visibility;
        // Filtered moments cannot prove either visibility or occlusion at an
        // intersecting wall. Query the actual short segment; the default field
        // therefore needs no distance moments or their filtering bandwidth.
        if((lighting.light_control.w&255u)==1u || (lighting.light_control.w&256u)!=0u)
            visibility=probe_visibility(geometry,position,delta/distance,max(distance-1.0,0.0));
        else {
            vec2 moments=probe_depth(base,delta/distance);
            float difference=max(0.0,distance-moments.x-4.0);
            float variance=max(1.0,moments.y-moments.x*moments.x);
            visibility=variance/(variance+difference*difference);
            visibility=visibility*visibility*visibility;
        }
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
            irradiance+=uintBitsToFloat(probe_load(base+lobe)).rgb*n[axis]*n[axis];
        }
        sum+=irradiance*weight;total+=weight;
    }
    // Visibility already rejects occluded neighbours. Normalize the surviving
    // weights even near a slab: a fixed denominator floor stamps the probe
    // lattice into ceilings as their trilinear weights approach zero.
    covered=total>1e-8?1.0:0.0;
    if((lighting.light_control.w&255u)==2u)return vec3(total*8.0);
    return sum/max(total,1e-8);
}
