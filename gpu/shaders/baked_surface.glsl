// Per-triangle, two-sided 4x4 lightmap. Visibility is filtered separately for
// each lamp; live shadow maps supply moving occluders and the BRDF stays live.
layout(set=1,binding=18,std430) readonly buffer BakedSurface {uvec4 data[];} baked_surface;
layout(set=1,binding=13,std430) readonly buffer BakedLamps {uvec4 header;Light lights[];} baked_lamps;
layout(location=5) in vec2 baked_uv;
layout(location=6) flat in uint baked_triangle;
uint surface_taps[4];vec4 surface_weights;
vec3 baked_surface_read(out float sun,out bool continuous) {
    vec2 uv=clamp(baked_uv,0.0,1.0)*3.0;uvec2 lo=uvec2(floor(uv)),hi=min(lo+1u,uvec2(3));vec2 f=fract(uv);
    uint base=uint(d.rotation.w)-1u+baked_triangle*32u+(gl_FrontFacing?0u:16u);
    surface_taps[0]=(base+lo.y*4u+lo.x)*2u;surface_taps[1]=(base+lo.y*4u+hi.x)*2u;
    surface_taps[2]=(base+hi.y*4u+lo.x)*2u;surface_taps[3]=(base+hi.y*4u+hi.x)*2u;
    surface_weights=vec4((1.0-f.x)*(1.0-f.y),f.x*(1.0-f.y),(1.0-f.x)*f.y,f.x*f.y);
    vec3 gi=vec3(0);sun=0.0;continuous=true;uint region=0u;
    for(uint i=0u;i<4u;++i) {
        uvec4 sample_value=baked_surface.data[surface_taps[i]];
        vec2 bs=unpackHalf2x16(sample_value.y);
        if(surface_weights[i]>0.00001) {
            if(sample_value.z==0u || (region!=0u && region!=sample_value.z))continuous=false;
            region=sample_value.z;
        }
        gi+=vec3(unpackHalf2x16(sample_value.x),bs.x)*surface_weights[i];sun+=bs.y*surface_weights[i];
    }
    return gi;
}
// A fixed light can only use its cached bit when its full identity matches.
// Diagnostics and direct-only edits are therefore never given stale shadows.
bool baked_lamp_matches(uint i,Light lamp) {
    if(i>=baked_lamps.header.x || lamp.inner_shadow.z<0.5)return false;
    Light baked=baked_lamps.lights[i];
    return all(equal(lamp.position_radius,baked.position_radius)) &&
        all(equal(lamp.direction_outer,baked.direction_outer)) && lamp.inner_shadow.x==baked.inner_shadow.x;
}
float baked_lamp_visibility(uint lamp) {
    float visible=0.0;
    for(uint i=0u;i<4u;++i)if((baked_surface.data[surface_taps[i]+1u][lamp/32u]&(1u<<(lamp%32u)))!=0u)
        visible+=surface_weights[i];
    return visible;
}
