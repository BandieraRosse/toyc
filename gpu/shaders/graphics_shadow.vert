#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"
layout(location=0) in ivec3 position;
layout(push_constant) uniform Draw {
    ivec4 instance; ivec4 rotation; ivec4 camera; ivec4 view;
    ivec4 projection; uvec4 material; ivec4 texture_info; ivec4 quality;
} d;
void main() {
    vec3 q=vec3(position);q.y-=float(d.rotation.z);
    q=vec3(q.x*float(d.rotation.y)+q.z*float(d.rotation.x),q.y*1024.0,
        q.z*float(d.rotation.y)-q.x*float(d.rotation.x))/1024.0;
    q*=float(d.instance.w)/1000.0*512.0/float(max(d.quality.y,512));
    gl_Position=lighting.shadow_matrix[d.camera.w]*vec4(q+vec3(d.instance.xyz),1);
}
