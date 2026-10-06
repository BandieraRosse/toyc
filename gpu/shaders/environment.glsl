// Camera-independent upper-hemisphere radiance, excluding the solar disc.
// Header: radiance normalization then six diffuse E/pi lobes, all /100 HDR.
layout(set=1,binding=14,std430) readonly buffer SkyEnvironment { vec4 data[]; } sky_environment;
vec3 environment_direction(vec3 direction) {
    if(direction.y<=0.0 || lighting.daylight.x<=0.0)return vec3(0);
    float u=atan(direction.z,direction.x)*0.15915494309+0.5;
    vec2 uv=vec2(u,clamp(direction.y,0.0,1.0))*vec2(32,16)-0.5;
    ivec2 p=ivec2(floor(uv));vec2 f=fract(uv);
    vec3 value=vec3(0);
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        uint at=7u+uint(clamp(p.y+y,0,15))*32u+uint((p.x+x+32)%32);
        value+=sky_environment.data[at].rgb*(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);
    }
    return value*sky_environment.data[0].x;
}
vec3 environment_diffuse(vec3 n) {
    vec3 value=vec3(0);
    for(int axis=0;axis<3;++axis)
        value+=sky_environment.data[1+axis*2+(n[axis]<0.0?1:0)].rgb*n[axis]*n[axis];
    return value;
}
