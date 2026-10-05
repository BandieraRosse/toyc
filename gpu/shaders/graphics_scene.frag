#version 460
#extension GL_GOOGLE_include_directive : require
#ifdef RF_ARCHITECTURE_RAY_QUERY
#extension GL_EXT_ray_query : require
#endif
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
vec3 texture_mip(uint entry,int level,vec2 uv) {
    ivec2 size=ivec2(tex.texels[entry],tex.texels[entry+1u]);
    uint offset=tex.texels[entry+2u];
    for(int i=0;i<level;++i){offset+=uint(size.x*size.y);size=max(size/2,ivec2(1));}
    vec2 p=clamp(uv,0.0,1.0)*vec2(size)-0.5;
    ivec2 lo=ivec2(floor(p));vec2 f=fract(p);vec3 samples[4];
    for(int i=0;i<4;++i){
        ivec2 q=clamp(lo+ivec2(i&1,i>>1),ivec2(0),size-1);
        samples[i]=decode_srgb(rgb(tex.texels[offset+uint(q.y*size.x+q.x)]));
    }
    return mix(mix(samples[0],samples[1],f.x),mix(samples[2],samples[3],f.x),f.y);
}
vec3 texture_filtered(uint index,vec2 uv) {
    uint entry=index*4u;
    vec2 size=vec2(tex.texels[entry],tex.texels[entry+1u]);
    vec2 dx=dFdx(uv)*size,dy=dFdy(uv)*size;
    float level=clamp(0.5*log2(max(max(dot(dx,dx),dot(dy,dy)),1.0)),0.0,float(tex.texels[entry+3u]-1u));
    int lo=int(floor(level)),hi=min(lo+1,int(tex.texels[entry+3u])-1);
    return mix(texture_mip(entry,lo,uv),texture_mip(entry,hi,uv),fract(level));
}
void main() {
    if((d.quality.x&64)!=0) { color=vec4(sky_sample(),1);return; }
    if((d.quality.x&16)==0 && lighting.cutaway_height.y>0.0 && world_position.y>lighting.cutaway_height.x &&
       world_position.x>=lighting.cutaway_bounds.x && world_position.x<=lighting.cutaway_bounds.y &&
       world_position.z>=lighting.cutaway_bounds.z && world_position.z<=lighting.cutaway_bounds.w) {
        /* Deterministic ordered dissolve keeps opaque depth correct. The
         * independent shadow pass continues to draw the complete building. */
        const int pattern[16]=int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
        ivec2 pixel=ivec2(gl_FragCoord.xy)&3;
        if(lighting.cutaway_height.y>(float(pattern[pixel.y*4+pixel.x])+0.5)/16.0)discard;
    }
    float alpha=d.texture_info.z==256 ? triangle_alpha : d.texture_info.z==0 ? 1.0:float(d.texture_info.z)/255.0;
    vec3 base=decode_srgb(rgb(triangle_color));
    if(d.material.z!=0u) {
      if(d.quality.w==2) base*=texture_filtered(d.material.z-1u,texcoord);
      else {
        vec2 st=fract(texcoord)*vec2(d.texture_info.xy);base=fetch_repeat(ivec2(floor(st)));
        if(d.quality.w!=0) {
            st-=0.5;ivec2 lo=ivec2(floor(st));vec2 f=fract(st);
            base=mix(mix(fetch_repeat(lo),fetch_repeat(lo+ivec2(1,0)),f.x),
                mix(fetch_repeat(lo+ivec2(0,1)),fetch_repeat(lo+ivec2(1,1)),f.x),f.y);
        }
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
    vec3 origin=world_position+n*1.5;
    // A roof blocks outdoor fill even in RTS cutaway. Retain a small artistic
    // interior floor; this is visibility, not GI or bounced artificial light.
    float sky_access=architecture_visibility(origin,vec3(0,1,0),131072.0);
    vec3 radiance=base*(1.0-metal)*environment_irradiance(n)*mix(0.10,1.0,sky_access)+base*emissive;
    vec3 l=lighting.sun_direction.xyz;
    if(lighting.sun_color.w>0.0 && (stylized || dot(n,l)>0.0) &&
       architecture_visibility(origin,l,131072.0)>0.0)
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
        if(architecture_visibility(origin,l,max(sqrt(distance_squared)-2.0,0.0))==0.0)continue;
        float attenuation=fade*fade/max(distance_squared/(512.0*512.0),0.04);
        radiance+=brdf(base,n,v,l,rough,metal,stylized)*light.color_intensity.rgb*
            light.color_intensity.w*attenuation*spot*spot_visibility(light,world_position,n);
    }
    color=vec4(radiance,alpha);
}
