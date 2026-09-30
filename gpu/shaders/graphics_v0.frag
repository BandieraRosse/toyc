#version 450
layout(push_constant) uniform Draw {
    ivec4 instance; ivec4 rotation; ivec4 camera; ivec4 view;
    ivec4 projection; uvec4 material; ivec4 texture_info;
    ivec4 quality;
} d;
layout(set=0,binding=0,std430) readonly buffer Texture { uint texels[]; } tex;
layout(location=0) in vec2 texcoord;
layout(location=1) flat in uint form_light;
layout(location=2) noperspective in float inverse_z;
layout(location=3) noperspective in float vertex_light;
layout(location=4) flat in uint triangle_color;
layout(location=5) in vec3 smooth_normal;
layout(location=0) out vec4 color;
uvec3 rgb(uint c) { return uvec3((c>>16)&255u,(c>>8)&255u,c&255u); }
vec3 fetch_repeat(ivec2 p) {
    p = (p % d.texture_info.xy + d.texture_info.xy) % d.texture_info.xy;
    return vec3(rgb(tex.texels[p.y*d.texture_info.x+p.x]));
}
void main() {
    uvec3 c;
    uint shading=form_light;
    if (d.quality.z == 3) shading=256u;
    else if (d.quality.z != 0) {
        vec3 n=smooth_normal/max(length(smooth_normal),0.000001);
        float lambert=max(dot(n,normalize(vec3(-1,2,-1))),0.0);
        float ambient=d.quality.z == 2 ? 0.72 : 0.53125;
        shading=uint(256.0*(ambient+(1.0-ambient)*lambert));
    }
    if (d.material.z != 0u) {
        ivec2 p = ivec2(floor(fract(texcoord)*vec2(d.texture_info.xy)));
        uint t = tex.texels[p.y*d.texture_info.x+p.x];
        // Texture nearest/repeat, opaque RGB. No alpha or material effects.
        uint light = min(d.material.y*shading/256u,256u);
        c = rgb(t)*light/256u;
        if (d.quality.w != 0) {
            vec2 st=fract(texcoord)*vec2(d.texture_info.xy)-0.5;
            ivec2 lo=ivec2(floor(st)); vec2 f=fract(st);
            vec3 sample_color=mix(mix(fetch_repeat(lo),fetch_repeat(lo+ivec2(1,0)),f.x),
                                  mix(fetch_repeat(lo+ivec2(0,1)),fetch_repeat(lo+ivec2(1,1)),f.x),f.y);
            c=uvec3(sample_color*float(light)/256.0);
        }
    } else {
        // Flat path performs two separate truncations, form then scene.
        uint light = d.material.w != 0u ? uint(clamp(vertex_light,0.0,384.0)) : shading;
        c = (rgb(triangle_color)*light/256u)*d.material.y/256u;
    }
    color = vec4(vec3(min(c,uvec3(255)))/255.0,
                 d.texture_info.z == 0 ? 1.0 : float(d.texture_info.z)/255.0);
    // WORLD uses native reversed Z (64/z), including homogeneous clipping.
    // Screen-space layers retain their explicit inverse-depth contract.
    gl_FragDepth = d.texture_info.w == 0 && (d.quality.x & 1) == 0 ? gl_FragCoord.z :
        clamp(floor(inverse_z)/16384.0,0.0,1.0);
}
