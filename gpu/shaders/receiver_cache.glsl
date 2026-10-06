// E/pi, /100 HDR, before reflectance, metalness or exposure. The two light
// sources have independent lanes. A valid dark leaf is not a cache miss.
layout(set=1,binding=16,std430) readonly buffer ReceiverCache {
    uvec4 header;uvec4 data[];
} receiver_cache;
vec3 cached_receiver_irradiance(vec3 p,vec3 n) {
    if(lighting.light_control.z==0u)return vec3(0);
    if(lighting.daylight.x>0.0 && lighting.daylight.z==0.0)return environment_diffuse(n);
    if(receiver_cache.header.x!=0u) {
        uint node=0u;vec3 q=p+n*1.5;
        vec3 root_lo=uintBitsToFloat(receiver_cache.data[2]).xyz;
        vec3 root_hi=uintBitsToFloat(receiver_cache.data[3]).xyz;
        if(lighting.daylight.x>0.0 && (q.x<root_lo.x || q.x>root_hi.x || q.z<root_lo.z || q.z>root_hi.z || q.y>root_hi.y))
            return environment_diffuse(n);
        if(all(greaterThanEqual(q,root_lo)) && all(lessThanEqual(q,root_hi)))
        for(uint depth=0u;depth<=48u;++depth) {
            uint at=node*4u;vec4 plane=uintBitsToFloat(receiver_cache.data[at]);
            uvec4 children=receiver_cache.data[at+1u];
            if(children.x!=0u) {node=(dot(plane.xyz,q)<=plane.w?children.x:children.y)-1u;continue;}
            if(children.w==0u)break;
            vec3 lo=uintBitsToFloat(receiver_cache.data[at+2u]).xyz;
            vec3 hi=uintBitsToFloat(receiver_cache.data[at+3u]).xyz;
            vec3 f=clamp((q-lo)/max(hi-lo,vec3(0.001)),0.0,1.0),value=vec3(0);
            for(uint corner=0u;corner<8u;++corner) {
                vec3 t=mix(1.0-f,f,vec3(ivec3(int(corner&1u),int((corner>>1u)&1u),int(corner>>2u))));
                uint sample_at=receiver_cache.header.z+(children.w-1u+corner)*22u;
                vec3 irradiance=vec3(0);
                for(uint axis=0u;axis<3u;++axis)if(abs(n[axis])>0.001) {
                    uint lobe=axis*2u+(n[axis]<0.0?1u:0u);
                    irradiance+=uintBitsToFloat(receiver_cache.data[sample_at+10u+lobe]).rgb*n[axis]*n[axis];
                    if(lighting.daylight.x>0.0)irradiance+=uintBitsToFloat(receiver_cache.data[sample_at+16u+lobe]).rgb*n[axis]*n[axis];
                }
                value+=irradiance*t.x*t.y*t.z;
            }
            return value;
        }
    }
    // Only a geometrically proven empty roof column can supply exterior sky.
    // Missing/solid regions do not restore the reference sky/probe ray chain.
    if(lighting.daylight.x>0.0 && roof_cache.extent.x!=0u) {
        ivec2 c=ivec2(floor((p.xz-roof_cache.grid.xy)/roof_cache.grid.z));
        if(all(greaterThanEqual(c,ivec2(0))) && all(lessThan(c,ivec2(roof_cache.extent.xy)))) {
            vec4 bounds=roof_cache.cells[uint(c.y)*roof_cache.extent.x+uint(c.x)];
            if(p.y>bounds.z+0.05)return environment_diffuse(n);
        }
    }
    return vec3(0);
}
