#version 450
layout(location=0) in ivec3 position;
layout(location=1) in ivec2 uv;
#ifndef RF_SCENE_COLOR
layout(location=2) in ivec3 normal0;
layout(location=3) in ivec3 normal1;
layout(location=4) in ivec3 normal2;
#endif
layout(push_constant) uniform Draw {
    ivec4 instance; ivec4 rotation; ivec4 camera; ivec4 view;
    ivec4 projection; uvec4 material; ivec4 texture_info; ivec4 quality;
} d;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec3 world_position;
layout(location=2) out vec3 world_normal;
layout(location=3) flat out uint triangle_color;
void main() {
    triangle_color=d.material.w==2u ? uint(uv.y) : d.material.x;
    texcoord=vec2(uv)/65536.0;world_position=vec3(0);world_normal=vec3(0,1,0);
    if(d.texture_info.w!=0) {
        float w=1048576.0/float(max(position.z,1));
        gl_Position=vec4((2.0*float(position.x)/float(d.projection.x)-1.0)*w,
            (2.0*float(position.y)/float(d.projection.y)-1.0)*w,
            clamp(float(position.z)/16384.0,0.0,1.0)*w,w);
        if((d.quality.x&32)!=0) {
            world_normal=vec3(0);
            float x=(float(position.x)-float(d.projection.x)*0.5)*w/float(d.projection.w);
            float y=(float(d.projection.y)*0.5-float(position.y))*w/float(d.projection.w);
            float py=(y*float(d.view.w)+w*float(d.view.z))/1024.0;
            float pz=(w*float(d.view.w)-y*float(d.view.z))/1024.0;
            world_position=vec3((x*float(d.view.y)+pz*float(d.view.x))/1024.0,py,
                (pz*float(d.view.y)-x*float(d.view.x))/1024.0)+vec3(d.camera.xyz);
        }
        return;
    }
    vec3 q=vec3(position);q.y-=float(d.rotation.z);
    q=vec3(q.x*float(d.rotation.y)+q.z*float(d.rotation.x),q.y*1024.0,
        q.z*float(d.rotation.y)-q.x*float(d.rotation.x))/1024.0;
    q*=float(d.instance.w)/1000.0*512.0/float(max(d.quality.y,512));
    world_position=q+vec3(d.instance.xyz);
#ifndef RF_SCENE_COLOR
    ivec3 ni=gl_VertexIndex%3==0 ? normal0 : gl_VertexIndex%3==1 ? normal1 : normal2;
    vec3 n=vec3(ni);
    world_normal=vec3(n.x*float(d.rotation.y)+n.z*float(d.rotation.x),n.y*1024.0,
        n.z*float(d.rotation.y)-n.x*float(d.rotation.x))/33553408.0;
#else
    world_normal=vec3(0);
#endif
    q=world_position-vec3(d.camera.xyz);
    float vx=(q.x*float(d.view.y)-q.z*float(d.view.x))/1024.0;
    float vz=(q.x*float(d.view.x)+q.z*float(d.view.y))/1024.0;
    float vy=(q.y*float(d.view.w)-vz*float(d.view.z))/1024.0;
    vz=(q.y*float(d.view.z)+vz*float(d.view.w))/1024.0;
    gl_Position=vec4(2.0*vx*float(d.projection.w)/float(d.projection.x),
        -2.0*vy*float(d.projection.w)/float(d.projection.y),float(d.projection.z),vz);
}
