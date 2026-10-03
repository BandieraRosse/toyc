#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"
layout(push_constant) uniform Draw {
    ivec4 instance; ivec4 rotation; ivec4 camera; ivec4 view;
    ivec4 projection; uvec4 material; ivec4 texture_info; ivec4 quality;
} d;
#include "sky_view.glsl"
layout(set=1,binding=4,std430) readonly buffer SkyImage { vec4 pixels[]; } sky_image;
vec4 sky_fetch(ivec2 p) {
    ivec2 size=ivec2(lighting.counts.zw);p=clamp(p,ivec2(0),size-1);
    return sky_image.pixels[p.y*size.x+p.x];
}
vec3 sky_sample() {
    vec2 p=gl_FragCoord.xy/vec2(d.projection.xy)*lighting.counts.zw-0.5;
    ivec2 lo=ivec2(floor(p));vec2 f=fract(p);
    vec4 sample_color=mix(mix(sky_fetch(lo),sky_fetch(lo+ivec2(1,0)),f.x),
        mix(sky_fetch(lo+ivec2(0,1)),sky_fetch(lo+ivec2(1,1)),f.x),f.y);
    return sample_color.rgb+sky_solar(sky_ray(gl_FragCoord.xy))*sample_color.a;
}
layout(set=0,binding=0,std430) readonly buffer Texture { uint texels[]; } tex;
layout(location=0) in vec2 texcoord;
layout(location=1) in vec3 world_position;
layout(location=2) in vec3 world_normal;
layout(location=3) flat in uint triangle_color;
layout(location=4) flat in float triangle_alpha;
layout(location=0) out vec4 color;
vec3 rgb(uint c) { return vec3((c>>16)&255u,(c>>8)&255u,c&255u)/255.0; }
vec3 fetch_repeat(ivec2 p) {
    p=(p%d.texture_info.xy+d.texture_info.xy)%d.texture_info.xy;
    return decode_srgb(rgb(tex.texels[p.y*d.texture_info.x+p.x]));
}
void main() {
    if((d.quality.x&64)!=0) { color=vec4(sky_sample(),1);return; }
    float alpha=d.texture_info.z==256 ? triangle_alpha : d.texture_info.z==0 ? 1.0:float(d.texture_info.z)/255.0;
    vec3 base=decode_srgb(rgb(triangle_color));
    if(d.material.z!=0u) {
        vec2 st=fract(texcoord)*vec2(d.texture_info.xy);base=fetch_repeat(ivec2(floor(st)));
        if(d.quality.w!=0) {
            st-=0.5;ivec2 lo=ivec2(floor(st));vec2 f=fract(st);
            base=mix(mix(fetch_repeat(lo),fetch_repeat(lo+ivec2(1,0)),f.x),
                mix(fetch_repeat(lo+ivec2(0,1)),fetch_repeat(lo+ivec2(1,1)),f.x),f.y);
        }
    }
    if((d.quality.x&16)!=0) { color=vec4(rgb(triangle_color),alpha);return; }
    if((d.texture_info.w!=0 && (d.quality.x&32)==0) || d.quality.z==3) { color=vec4(base,alpha);return; }
    vec3 geometric=normalize(cross(dFdx(world_position),dFdy(world_position)));
    vec3 n=d.quality.z!=0 && dot(world_normal,world_normal)>0.01 ? normalize(world_normal):geometric;
    vec3 v=normalize(vec3(d.camera.xyz)-world_position);if(dot(n,v)<0) n=-n;
    float rough=max(float(d.material.y&255u)/255.0,0.06);
    float metal=float((d.material.y>>8)&255u)/255.0;
    float emissive=float((d.material.y>>16)&255u)/16.0;
    bool stylized=d.quality.z==2;
    vec3 radiance=base*(1.0-metal)*environment_irradiance(n)+base*emissive;
    vec3 l=lighting.sun_direction.xyz;
    if(lighting.sun_color.w>0.0 && (stylized || dot(n,l)>0.0))
      radiance+=brdf(base,n,v,l,rough,metal,stylized)*lighting.sun_color.rgb*
        lighting.sun_color.w*sun_visibility(world_position,n);
    for(int i=0;i<int(lighting.counts.x);++i) {
        Light light=lighting.lights[i];vec3 delta=light.position_radius.xyz-world_position;
        float distance_squared=dot(delta,delta),radius_squared=light.position_radius.w*light.position_radius.w;
        if(distance_squared>=radius_squared) continue;
        float normalized_squared=distance_squared/radius_squared;
        float fade=1.0-normalized_squared*normalized_squared;
        l=delta*inversesqrt(max(distance_squared,0.000001));
        float spot=light.direction_outer.w<0.0 ? 1.0 : smoothstep(light.direction_outer.w,
            light.inner_shadow.x,dot(-l,light.direction_outer.xyz));
        if(spot<=0.0 || (!stylized && dot(n,l)<=0.0)) continue;
        float attenuation=fade*fade/max(distance_squared/(512.0*512.0),0.04);
        radiance+=brdf(base,n,v,l,rough,metal,stylized)*light.color_intensity.rgb*
            light.color_intensity.w*attenuation*spot*spot_visibility(light,world_position,n);
    }
    color=vec4(radiance,alpha);
}
