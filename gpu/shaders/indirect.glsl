#include "probe_layout.glsl"
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
layout(set=1,binding=15,std430) readonly buffer DaylightProbes {
    uvec4 header; vec4 params; vec4 minimum; vec4 maximum; uvec4 data[];
} daylight_probes;
#define PROBE_FIELD probes
#define probe_load fixture_probe_load
#define probe_hash fixture_probe_hash
#define probe_find fixture_probe_find
#define probe_moments fixture_probe_moments
#define probe_depth fixture_probe_depth
#define probe_visibility fixture_probe_visibility
#define probe_weight fixture_probe_weight
#define probe_lobes fixture_probe_lobes
#define sample_probe_field fixture_sample_probe_field
#include "probe_sample.glsl"
#undef probe_load
#undef probe_hash
#undef probe_find
#undef probe_moments
#undef probe_depth
#undef probe_visibility
#undef probe_weight
#undef probe_lobes
#undef sample_probe_field
#undef PROBE_FIELD

#define PROBE_FIELD daylight_probes
#define probe_load daylight_probe_load
#define probe_hash daylight_probe_hash
#define probe_find daylight_probe_find
#define probe_moments daylight_probe_moments
#define probe_depth daylight_probe_depth
#define probe_visibility daylight_probe_visibility
#define probe_weight daylight_probe_weight
#define probe_lobes daylight_probe_lobes
#define sample_probe_field daylight_sample_probe_field
#include "probe_sample.glsl"
#undef probe_load
#undef probe_hash
#undef probe_find
#undef probe_moments
#undef probe_depth
#undef probe_visibility
#undef probe_weight
#undef probe_lobes
#undef sample_probe_field
#undef PROBE_FIELD

vec3 probe_irradiance(vec3 p,vec3 n,vec3 view_direction) {
    float covered;return fixture_sample_probe_field(p,n,view_direction,covered);
}
vec3 direct_sky_irradiance(vec3 p,vec3 n) {
    if(lighting.daylight.z==0.0)return environment_diffuse(n);
    // Boundary/disabled/invalid field: visibility-tested direct sky only. Never
    // inject unoccluded IBL through a roof just because interpolation failed.
    vec3 tangent=normalize(cross(abs(n.y)<0.9?vec3(0,1,0):vec3(1,0,0),n));
    vec3 bitangent=cross(n,tangent),sum=vec3(0);
    for(int i=0;i<8;++i) {
        float u=(float(i)+0.5)/8.0,phi=float(i)*2.39996323;
        vec3 d=tangent*(sqrt(u)*cos(phi))+bitangent*(sqrt(u)*sin(phi))+n*sqrt(1.0-u);
        if(d.y>0.0)sum+=environment_direction(d)*architecture_visibility(p+n*1.5,d,lighting.daylight.y);
    }
    return sum*(1.0/8.0);
}
vec3 daylight_irradiance(vec3 p,vec3 n,vec3 view_direction) {
    float covered;vec3 value=daylight_sample_probe_field(p,n,view_direction,covered);
    return covered>0.0?value:direct_sky_irradiance(p,n);
}
vec3 combined_irradiance(vec3 p,vec3 n,vec3 view_direction) {
    // Reuse only the geometric weights of exactly coincident probes. The two
    // radiance fields retain their own coverage, history and normalization.
    if(lighting.daylight.x<=0.0)return probe_irradiance(p,n,view_direction);
    if((lighting.light_control.w&1024u)==0u || lighting.light_control.z==0u ||
       (lighting.light_control.w&256u)==0u || (lighting.light_control.w&255u)==2u ||
       probes.header.y==0u || daylight_probes.header.y==0u ||
       any(notEqual(probes.params.xy,daylight_probes.params.xy)) ||
       any(lessThan(p,probes.minimum.xyz)) || any(greaterThan(p,probes.maximum.xyz)) ||
       any(lessThan(p,daylight_probes.minimum.xyz)) || any(greaterThan(p,daylight_probes.maximum.xyz)))
        return probe_irradiance(p,n,view_direction)+daylight_irradiance(p,n,view_direction);
    vec3 receiver=p+n*probes.params.y+view_direction*(probes.params.x*0.08);
    vec3 grid=receiver/probes.params.x-0.37,f=fract(grid);ivec3 lo=ivec3(floor(grid));
    uint fc=fixture_probe_find(lo),dc=daylight_probe_find(lo);
    if(fc==0u || dc==0u)return probe_irradiance(p,n,view_direction)+daylight_irradiance(p,n,view_direction);
    uvec4 fn0=fixture_probe_load(fc),fn1=fixture_probe_load(fc+1u);
    uvec4 dn0=daylight_probe_load(dc),dn1=daylight_probe_load(dc+1u);
    uvec2 fg=fixture_probe_load(fc+2u).xy,dg=daylight_probe_load(dc+2u).xy;
    vec3 fs=vec3(0),ds=vec3(0);float ft=0.0,dt=0.0;
    for(uint corner=0u;corner<8u;++corner) {
        uint fi=corner<4u?fn0[corner]:fn1[corner-4u];
        uint di=corner<4u?dn0[corner]:dn1[corner-4u];
        uint fb=0u,db=0u;bool fv=false,dv=false;
        vec3 fp=vec3(0),dp=vec3(0);float fw=0.0;
        if(fi!=0u) {
            fb=probes.header.w+(fi-1u)*probes.header.z;
            fv=uintBitsToFloat(fixture_probe_load(fb)).w!=0.0;
            if(fv)fp=uintBitsToFloat(fixture_probe_load(fb+probes.header.z-1u)).xyz;
        }
        if(di!=0u) {
            db=daylight_probes.header.w+(di-1u)*daylight_probes.header.z;
            dv=uintBitsToFloat(daylight_probe_load(db)).w!=0.0;
            if(dv)dp=uintBitsToFloat(daylight_probe_load(db+daylight_probes.header.z-1u)).xyz;
        }
        if(fv) {
            fw=fixture_probe_weight(fb,fg,fp,receiver,p,n,f,corner);
            if(fw!=0.0){fs+=fixture_probe_lobes(fb,n)*fw;ft+=fw;}
        }
        if(dv) {
            float dw=fv && all(equal(fp,dp))?fw:daylight_probe_weight(db,dg,dp,receiver,p,n,f,corner);
            if(dw!=0.0){ds+=daylight_probe_lobes(db,n)*dw;dt+=dw;}
        }
    }
    return fs/max(ft,1e-8)+(dt>1e-8?ds/max(dt,1e-8):direct_sky_irradiance(p,n));
}
