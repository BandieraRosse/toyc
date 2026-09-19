#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout(push_constant) uniform Draw {
    ivec4 instance; ivec4 rotation; ivec4 camera; ivec4 view;
    ivec4 projection; uvec4 material; ivec4 texture_info;
} d;
layout(set=0,binding=1,std430) readonly buffer Vertices { int words[]; } mesh;
layout(set=0,binding=2,std430) readonly buffer Indices { uint words[]; } source;
layout(location=0) flat out ivec3 screen0;
layout(location=1) flat out ivec3 screen1;
layout(location=2) flat out ivec3 screen2;
layout(location=3) flat out ivec2 uv0;
layout(location=4) flat out ivec2 uv1;
layout(location=5) flat out ivec2 uv2;
layout(location=6) flat out uint form_light;

ivec3 read3(uint at) { return ivec3(mesh.words[at],mesh.words[at+1],mesh.words[at+2]); }
ivec3 transform(ivec3 p) {
    p=ivec3((p.x*d.rotation.y+p.z*d.rotation.x)/1024,
            p.y-d.rotation.z,(p.z*d.rotation.y-p.x*d.rotation.x)/1024);
    p=p*d.instance.w/1000+d.instance.xyz-d.camera.xyz;
    int x=(p.x*d.view.y-p.z*d.view.x)/1024;
    int z=(p.x*d.view.x+p.z*d.view.y)/1024;
    return ivec3(x,(p.y*d.view.w-z*d.view.z)/1024,
                   (p.y*d.view.z+z*d.view.w)/1024);
}
ivec3 normal(ivec3 n) {
    ivec3 r=ivec3((n.x*d.rotation.y+n.z*d.rotation.x)/1024,n.y,
                 (n.z*d.rotation.y-n.x*d.rotation.x)/1024);
    return (r<<16)>>16;
}
struct Corner { ivec3 p; ivec2 uv; };
Corner intersect_near(Corner a,Corner b) {
    int64_t num=int64_t(d.projection.z-a.p.z),den=int64_t(b.p.z-a.p.z);
    Corner c;
    c.p=ivec3(a.p.xy+ivec2((i64vec2(b.p.xy)-i64vec2(a.p.xy))*num/den),d.projection.z);
    c.uv=a.uv+ivec2((i64vec2(b.uv)-i64vec2(a.uv))*num/den);
    return c;
}
ivec3 project(Corner c) {
    return ivec3(d.projection.x/2+int(int64_t(c.p.x)*d.projection.w/c.p.z),
                 d.projection.y/2-int(int64_t(c.p.y)*d.projection.w/c.p.z),1048576/c.p.z);
}
ivec2 perspective_uv(Corner c) { return ivec2(i64vec2(c.uv)*int64_t(1048576)/c.p.z); }
void main() {
    // Six immutable index slots per source primitive; no CPU frame expansion.
    uint triangle=uint(gl_VertexIndex)/6u,slot=uint(gl_VertexIndex)%6u;
    Corner input_c[3],polygon[4];
    for(uint i=0;i<3;i++) {
        uint at=source.words[triangle*3u+i]*14u;
        input_c[i].p=transform(read3(at));
        input_c[i].uv=ivec2(mesh.words[at+3],mesh.words[at+4]);
    }
    int count=0;
    for(int i=0;i<3;i++) {
        Corner a=input_c[(i+2)%3],b=input_c[i];
        if((a.p.z>=d.projection.z)!=(b.p.z>=d.projection.z))polygon[count++]=intersect_near(a,b);
        if(b.p.z>=d.projection.z)polygon[count++]=b;
    }
    screen0=screen1=screen2=ivec3(0); uv0=uv1=uv2=ivec2(0); form_light=256u;
    gl_Position=vec4(0,0,0,1);
    if(count<3 || (slot>=3u && count<4))return; // unused triangle is degenerate
    int fan=int(slot/3u)+1;
    Corner a=polygon[0],b=polygon[fan],c=polygon[fan+1];
    screen0=project(a);screen1=project(b);screen2=project(c);
    uv0=perspective_uv(a);uv1=perspective_uv(b);uv2=perspective_uv(c);
    ivec3 s=slot%3u==0u?screen0:(slot%3u==1u?screen1:screen2);
    // Integer CPU sample (x,y) coincides with Vulkan's (x+.5,y+.5).
    // Side clipping changes coverage only; flat integer planes stay unchanged.
    gl_Position=vec4((vec2(s.xy)+vec2(.5))*2.0/vec2(d.projection.xy)-1.0,0,1);
    uint at=source.words[triangle*3u]*14u;
    ivec3 n=(normal(read3(at+5))+normal(read3(at+8))+normal(read3(at+11)))/3;
    int dot_light=clamp((n.x*(-13377)+n.y*26755+n.z*(-13377))/32767,0,32767);
    form_light=d.rotation.w==0?256u:uint(clamp(136+dot_light*120/32767,136,256));
}
