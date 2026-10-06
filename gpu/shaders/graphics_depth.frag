#version 450
#extension GL_GOOGLE_include_directive : require
#include "lighting.glsl"
layout(push_constant) uniform Draw {
    ivec4 instance; ivec4 rotation; ivec4 camera; ivec4 view;
    ivec4 projection; uvec4 material; ivec4 texture_info; ivec4 quality;
} d;
#include "scene_cutaway.glsl"
layout(location=1) in vec3 world_position;
void main() { scene_cutaway(world_position); }
