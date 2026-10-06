struct Light { vec4 position_radius; vec4 color_intensity; vec4 direction_outer; vec4 inner_shadow; };
layout(set=1,binding=0,std430) readonly buffer Lighting {
    vec4 sun_direction; vec4 sun_color; vec4 environment; vec4 counts;
    mat4 shadow_matrix[5]; vec4 cascade_center[3]; Light lights[160];
    vec4 sky_cloud; vec4 sky_weather;
    vec4 cutaway_bounds; vec4 cutaway_height;
    uvec4 tile_grid; // width, height, enabled, diagnostic counters
    ivec4 tile_camera; ivec4 tile_view; ivec4 tile_projection;
    uvec4 light_control; // diagnostic ablation mask
} lighting;
#ifdef RF_LIGHT_PROFILE
uint profile_pcf=0u;
#endif
layout(set=1,binding=1,std430) readonly buffer Shadows { float depth[]; } shadows;
#ifdef RF_ARCHITECTURE_RAY_QUERY
layout(set=1,binding=7) uniform accelerationStructureEXT architecture;
float architecture_visibility(vec3 origin,vec3 direction,float limit) {
    if(limit<=0.5)return 1.0;
    rayQueryEXT query;
    rayQueryInitializeEXT(query,architecture,
        gl_RayFlagsOpaqueEXT|gl_RayFlagsTerminateOnFirstHitEXT,
        255,origin,0.5,direction,limit);
    while(rayQueryProceedEXT(query)) {}
    return rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT?1.0:0.0;
}
#else
struct ArchitectureNode { vec4 lo; vec4 hi; vec4 a; vec4 b; vec4 c; };
layout(set=1,binding=7,std430) readonly buffer Architecture {
    uvec4 header; ArchitectureNode nodes[];
} architecture;
/* Ray/triangle visibility in world units, two-sided. A small origin offset
 * avoids self hits without jumping through thin floors or door lintels. */
float architecture_visibility(vec3 origin,vec3 direction,float limit) {
    vec3 safe_dir=mix(direction,vec3(1e-8),lessThan(abs(direction),vec3(1e-8)));
    vec3 inv=1.0/safe_dir;
    uint i=0u;
    while(i<architecture.header.x) {
        vec4 lo=architecture.nodes[i].lo,hi=architecture.nodes[i].hi;
        vec3 t0=(lo.xyz-origin)*inv,t1=(hi.xyz-origin)*inv;
        vec3 near_t=min(t0,t1),far_t=max(t0,t1);
        if(max(max(near_t.x,near_t.y),max(near_t.z,0.0))>
           min(min(far_t.x,far_t.y),min(far_t.z,limit))) { i=uint(lo.w);continue; }
        if(hi.w>0.0) {
            if(hi.w>1.5)return 0.0;
            vec3 a=architecture.nodes[i].a.xyz,b=architecture.nodes[i].b.xyz,c=architecture.nodes[i].c.xyz;
            vec3 h=cross(direction,c);float det=dot(b,h);
            if(abs(det)>1e-6) {
                vec3 s=origin-a;float u=dot(s,h)/det;
                vec3 q=cross(s,b);float v=dot(direction,q)/det;
                float t=dot(c,q)/det;
                // Include the shared edge in both triangles. Roundoff must
                // not open a bright diagonal crack through a closed wall.
                if(u>=-1e-6 && v>=-1e-6 && u+v<=1.000001 && t>0.5 && t<limit)return 0.0;
            }
        }
        ++i;
    }
    return 1.0;
}
#endif
vec3 decode_srgb(vec3 c) {
    return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),greaterThan(c,vec3(0.04045)));
}
/* Bilinearly translated tent kernel: continuous sub-texel coverage from
 * sixteen depth loads, without depth filtering extensions or random noise. */
float shadow_filter(int map,vec3 uv,float bias) {
    if((lighting.light_control.x&8u)!=0u)return 1.0;
#ifdef RF_LIGHT_PROFILE
    ++profile_pcf;
#endif
    vec2 at=uv.xy*1024.0-0.5;ivec2 lo=ivec2(floor(at));vec2 f=fract(at);
    vec4 wx=vec4(1.0-f.x,2.0-f.x,1.0+f.x,f.x);
    vec4 wy=vec4(1.0-f.y,2.0-f.y,1.0+f.y,f.y);
    float sum=0.0;
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) {
        ivec2 tap=clamp(lo+ivec2(x-1,y-1),ivec2(0),ivec2(1023));
        float z=shadows.depth[map*1048576+tap.y*1024+tap.x];
        sum+=wx[x]*wy[y]*step(uv.z-bias,z);
    }
    return sum*(1.0/16.0);
}
float spot_visibility(Light light,vec3 p,vec3 n) {
    int map=int(light.inner_shadow.y);
    if(map<3) return 1.0;
    vec4 q=lighting.shadow_matrix[map]*vec4(p+n*2.0,1);
    if(q.w<=0) return 0.0;
    vec3 uv=q.xyz/q.w*vec3(0.5,0.5,1)+vec3(0.5,0.5,0);
    if(any(lessThan(uv,vec3(0))) || any(greaterThan(uv,vec3(1)))) return 0.0;
    return shadow_filter(map,uv,0.00008);
}
float sun_visibility(vec3 p, vec3 n) {
    float result=1.0,remaining=1.0;
    float bias=0.000025+0.00010*(1.0-max(dot(n,lighting.sun_direction.xyz),0.0));
    for(int cascade=0;cascade<3;++cascade) {
        vec4 center=lighting.cascade_center[cascade];
        vec4 q=lighting.shadow_matrix[cascade]*vec4(p+n*center.w*(0.8/1024.0),1);
        vec3 uv=q.xyz*vec3(0.5,0.5,1)+vec3(0.5,0.5,0);
        if(any(lessThan(uv,vec3(0.005))) || any(greaterThan(uv,vec3(0.995)))) continue;
        float edge=max(abs(q.x),abs(q.y));
        float weight=1.0-smoothstep(0.82,0.98,edge);
        if(weight<=0.0) continue;
        result+=remaining*weight*(shadow_filter(cascade,uv,bias)-1.0);
        remaining*=1.0-weight;
        if(remaining<0.001) break;
    }
    return result;
}
vec3 environment_irradiance(vec3 n) {
    /* Broad sky/ground bounce only; no visibility or reflection probe claim. */
    float sky=0.5+0.5*n.y;
    vec3 ground=lighting.environment.rgb*vec3(0.55,0.46,0.36);
    return mix(ground,lighting.environment.rgb*1.3,sky);
}
vec3 brdf(vec3 base, vec3 n, vec3 v, vec3 l, float rough, float metal, bool stylized) {
    if((lighting.light_control.x&16u)!=0u)return base*max(dot(n,l),0.0)/3.14159265;
    float nl=max(dot(n,l),0.0),nv=max(dot(n,v),0.001);
    vec3 h=normalize(v+l+vec3(0.000001));
    float nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
    float a=rough*rough,a2=a*a,den=nh*nh*(a2-1.0)+1.0;
    float D=a2/max(3.14159265*den*den,0.000001),k=(rough+1.0)*(rough+1.0)/8.0;
    float G=(nl/(nl*(1.0-k)+k))*(nv/(nv*(1.0-k)+k));
    vec3 F=mix(vec3(0.04),base,metal);F=F+(1.0-F)*pow(1.0-vh,5.0);
    float diffuse=stylized ? mix(0.12,1.0,smoothstep(0.05,0.45,nl)) : nl;
    return (1.0-F)*(1.0-metal)*base*diffuse/3.14159265+D*G*F/max(4.0*nv,0.001);
}
