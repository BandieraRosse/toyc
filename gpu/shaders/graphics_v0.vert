#version 450
layout(location=0) in ivec3 position;
layout(location=1) in ivec2 uv;
layout(location=2) in ivec3 normal0;
layout(location=3) in ivec3 normal1;
layout(location=4) in ivec3 normal2;
layout(push_constant) uniform Draw {
    ivec4 instance; ivec4 rotation; ivec4 camera; ivec4 view;
    ivec4 projection; uvec4 material; ivec4 texture_info;
} d;
layout(location=0) out vec2 texcoord;
layout(location=1) flat out uint form_light;
layout(location=2) noperspective out float inverse_z;
layout(location=3) noperspective out float vertex_light;

ivec3 rotate_normal(ivec3 n) {
    ivec3 r = ivec3((n.x*d.rotation.y+n.z*d.rotation.x)/1024,
                   n.y, (n.z*d.rotation.y-n.x*d.rotation.x)/1024);
    // Match conversion to signed short in the reference vertex cache.
    return (r << 16) >> 16;
}
void main() {
    if (d.texture_info.w != 0) {
        float w = 1048576.0 / float(max(position.z,1));
        gl_Position = vec4((2.0*float(position.x)/float(d.projection.x)-1.0)*w,
                          (2.0*float(position.y)/float(d.projection.y)-1.0)*w,
                          0.5*w,w);
        inverse_z = float(position.z);
        texcoord = vec2(uv)/65536.0;
        vertex_light = 256.0;
        form_light = 256u;
        return;
    }
    ivec3 p = ivec3((position.x*d.rotation.y+position.z*d.rotation.x)/1024,
                   position.y-d.rotation.z,
                   (position.z*d.rotation.y-position.x*d.rotation.x)/1024);
    p = p*d.instance.w/1000+d.instance.xyz-d.camera.xyz;
    int x = (p.x*d.view.y-p.z*d.view.x)/1024;
    int z = (p.x*d.view.x+p.z*d.view.y)/1024;
    int y = (p.y*d.view.w-z*d.view.z)/1024;
    z = (p.y*d.view.z+z*d.view.w)/1024;
    // Homogeneous near clip is performed by fixed-function graphics.
    gl_Position = vec4(2.0*float(x)*float(d.projection.w)/float(d.projection.x),
                     -2.0*float(y)*float(d.projection.w)/float(d.projection.y),
                     float(d.projection.z), float(z));
    inverse_z = float(1048576/max(z,1));
    texcoord = vec2(uv)/65536.0;
    vertex_light = float(uv.x);
    ivec3 n = (rotate_normal(normal0)+rotate_normal(normal1)+rotate_normal(normal2))/3;
    int dot_light = clamp((n.x*(-13377)+n.y*26755+n.z*(-13377))/32767,0,32767);
    form_light = d.rotation.w == 0 ? 256u : uint(clamp(136+dot_light*120/32767,136,256));
}
