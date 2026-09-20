#version 450
layout(push_constant) uniform Draw {
    ivec4 instance; ivec4 rotation; ivec4 camera; ivec4 view;
    ivec4 projection; uvec4 material; ivec4 texture_info;
} d;
layout(set=0,binding=0,std430) readonly buffer Texture { uint texels[]; } tex;
layout(location=0) in vec2 texcoord;
layout(location=1) flat in uint form_light;
layout(location=2) noperspective in float inverse_z;
layout(location=3) noperspective in float vertex_light;
layout(location=0) out vec4 color;
uvec3 rgb(uint c) { return uvec3((c>>16)&255u,(c>>8)&255u,c&255u); }
void main() {
    uvec3 c;
    if (d.material.z != 0u) {
        ivec2 p = ivec2(floor(fract(texcoord)*vec2(d.texture_info.xy)));
        uint t = tex.texels[p.y*d.texture_info.x+p.x];
        // Texture nearest/repeat, opaque RGB. No alpha or material effects.
        uint light = min(d.material.y*form_light/256u,256u);
        c = rgb(t)*light/256u;
    } else {
        // Flat path performs two separate truncations, form then scene.
        uint light = d.material.w != 0u ? uint(clamp(vertex_light,0.0,256.0)) : form_light;
        c = (rgb(d.material.x)*light/256u)*d.material.y/256u;
    }
    color = vec4(vec3(min(c,uvec3(255)))/255.0,1.0);
    // D32 hardware compare/write owns occlusion; power-of-two mapping is exact.
    // Near-crossing equivalence with CPU clipping is an HG-2B gate.
    gl_FragDepth = clamp(floor(inverse_z)/16384.0,0.0,1.0);
}
