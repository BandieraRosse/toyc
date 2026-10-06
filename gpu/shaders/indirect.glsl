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
#define sample_probe_field fixture_sample_probe_field
#include "probe_sample.glsl"
#undef probe_load
#undef probe_hash
#undef probe_find
#undef probe_moments
#undef probe_depth
#undef probe_visibility
#undef sample_probe_field
#undef PROBE_FIELD

#define PROBE_FIELD daylight_probes
#define probe_load daylight_probe_load
#define probe_hash daylight_probe_hash
#define probe_find daylight_probe_find
#define probe_moments daylight_probe_moments
#define probe_depth daylight_probe_depth
#define probe_visibility daylight_probe_visibility
#define sample_probe_field daylight_sample_probe_field
#include "probe_sample.glsl"
#undef probe_load
#undef probe_hash
#undef probe_find
#undef probe_moments
#undef probe_depth
#undef probe_visibility
#undef sample_probe_field
#undef PROBE_FIELD

vec3 probe_irradiance(vec3 p,vec3 n,vec3 view_direction) {
    float covered;return fixture_sample_probe_field(p,n,view_direction,covered);
}
vec3 daylight_irradiance(vec3 p,vec3 n,vec3 view_direction) {
    float covered;vec3 value=daylight_sample_probe_field(p,n,view_direction,covered);
    if(covered>0.0)return value;
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
