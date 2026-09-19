#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout(push_constant) uniform Draw {
    ivec4 instance; ivec4 rotation; ivec4 camera; ivec4 view;
    ivec4 projection; uvec4 material; ivec4 texture_info;
} d;
layout(set=0,binding=0,std430) readonly buffer Texture { uint texels[]; } tex;
layout(location=0) flat in ivec3 screen0;
layout(location=1) flat in ivec3 screen1;
layout(location=2) flat in ivec3 screen2;
layout(location=3) flat in ivec2 uv0;
layout(location=4) flat in ivec2 uv1;
layout(location=5) flat in ivec2 uv2;
layout(location=6) flat in uint form_light;
layout(location=0) out vec4 color;
int64_t edge(ivec2 a,ivec2 b,ivec2 p) {
    return int64_t(b.x-a.x)*(p.y-a.y)-int64_t(b.y-a.y)*(p.x-a.x);
}
uvec3 rgb(uint c) { return uvec3((c>>16)&255u,(c>>8)&255u,c&255u); }
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy);
    int64_t area=edge(screen0.xy,screen1.xy,screen2.xy);
    if(area==0)discard;
    int64_t a=edge(screen1.xy,screen2.xy,p),b=edge(screen2.xy,screen0.xy,p),c=edge(screen0.xy,screen1.xy,p);
    if((area>0 && (a<0 || b<0 || c<0)) || (area<0 && (a>0 || b>0 || c>0)))discard;
    int64_t inverse_sum=a*screen0.z+b*screen1.z+c*screen2.z;
    int64_t iz=inverse_sum/area;
    uvec3 result;
    if(d.material.z!=0u) {
        i64vec2 uv=inverse_sum!=0 ? (a*i64vec2(uv0)+b*i64vec2(uv1)+c*i64vec2(uv2))/inverse_sum : i64vec2(0);
        ivec2 cell=ivec2((uv & i64vec2(65535))*i64vec2(d.texture_info.xy)/int64_t(65536));
        uint light=min(d.material.y*form_light/256u,256u);
        result=rgb(tex.texels[cell.y*d.texture_info.x+cell.x])*light/256u;
    } else result=(rgb(d.material.x)*form_light/256u)*d.material.y/256u;
    color=vec4(vec3(min(result,uvec3(255)))/255.0,1);
    gl_FragDepth=float(iz)/16384.0; // exact D32 mapping, hardware >= compare/write
}
