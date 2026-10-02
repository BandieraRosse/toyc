/* Distant, camera-translation-independent sky. All distances here are km.
 * Analytic single-scattering atmosphere (not the Hillaire LUT implementation),
 * curved cumulus volume and a separate thin cirrus layer, in linear HDR. */
layout(set=1,binding=5) uniform sampler3D noise_volume;
vec2 sky_noise(vec3 p) {
    return textureLod(noise_volume,p/8.0+vec3(0.5/64.0),0.0).rg;
}
vec3 sky_ray(vec2 pixel) {
    vec3 q=vec3((pixel.x-float(d.projection.x)*0.5)/float(d.projection.w),
        (float(d.projection.y)*0.5-pixel.y)/float(d.projection.w),1);
    float y=(q.y*float(d.view.w)+q.z*float(d.view.z))/1024.0;
    float z=(q.z*float(d.view.w)-q.y*float(d.view.z))/1024.0;
    return normalize(vec3((q.x*float(d.view.y)+z*float(d.view.x))/1024.0,y,
        (z*float(d.view.y)-q.x*float(d.view.x))/1024.0));
}
vec3 sky_atmosphere(vec3 ray) {
    vec3 sun=lighting.sun_direction.xyz;
    float mu=clamp(dot(ray,sun),-1.0,1.0);
    // Curved air-mass approximation stays finite at the horizon.
    float elevation=max(ray.y,0.0);
    float air=1.0/sqrt(elevation*elevation+0.012);
    vec3 beta=vec3(0.055,0.105,0.205);
    float haze=lighting.sky_weather.w;
    vec3 extinction=beta+vec3(0.035*haze);
    vec3 trans=exp(-extinction*air);
    float rayleigh=0.75*(1.0+mu*mu);
    float mie=0.035*(1.0-0.76*0.76)/pow(max(1.0+0.76*0.76-1.52*mu,0.01),1.5);
    float daylight=smoothstep(-0.08,0.15,sun.y);
    vec3 sunlight=lighting.sun_color.rgb*(lighting.sun_color.w/3.5);
    vec3 sky=(1.0-trans)*(beta*rayleigh+vec3(0.035*haze*mie))/extinction;
    sky*=sunlight*vec3(0.52,0.65,0.86)*daylight;
    sky+=vec3(0.16,0.21,0.26)*(1.0-exp(-0.10*air))*daylight;
    // Finite solar disk; analytic angular footprint works in compute too.
    float disc=cos(0.00465),edge=max(0.00465*lighting.counts.y/float(d.projection.w),0.000002);
    sky+=smoothstep(disc-edge,disc+edge,mu)*trans*sunlight*12.0*daylight;
    vec3 ground=vec3(0.095,0.12,0.15)*daylight;
    return mix(ground,sky,smoothstep(-0.16,0.015,ray.y));
}
float sky_height(vec3 p) {
    // Stable local form of spherical height (avoids subtracting two 6360s).
    return p.y+dot(p.xz,p.xz)/12720.0;
}
vec3 sky_cloud_space(vec3 p) {
    p.xz+=lighting.sky_weather.xy;
    return p+vec3(lighting.sky_weather.z*3.71,0,lighting.sky_weather.z*1.93);
}
float sky_weather_at(vec3 p) {
    p=sky_cloud_space(p);
    return sky_noise(vec3(p.x*0.19,3.7,p.z*0.19)).y;
}
float sky_cloud_group(float weather) {
    float coverage=lighting.sky_cloud.x;
    return smoothstep(1.0-coverage-0.18,1.0-coverage+0.20,weather);
}
float sky_density(vec3 p,float group,float detail) {
    float distance_to_observer=length(p.xz);
    float h=(sky_height(p)-lighting.sky_cloud.z)/lighting.sky_cloud.w;
    if(h<=0.0 || h>=1.0) return 0.0;
    if(group<0.01) return 0.0;
    p=sky_cloud_space(p);
    vec4 shape=textureLod(noise_volume,p*vec3(0.82,1.2,0.82)/8.0+vec3(0.5/64.0),0.0);
    // Weather controls cloud mass and height; erosion only touches the edge.
    // Larger connected billows share a flat base and individually rising tops.
    float top=mix(0.48,1.0,group);
    float profile=smoothstep(0.0,0.10,h)*(1.0-smoothstep(0.38,1.0,h/top));
    float body=mix(shape.a,shape.r,detail)-(0.66-0.46*group*profile)-0.05*(shape.b-0.5)*detail;
    return smoothstep(0.0,0.12,body)*lighting.sky_cloud.y*2.5*
        (1.0-smoothstep(15.0,25.0,distance_to_observer));
}
float sky_shell_distance(vec3 ray,float height) {
    float b=6360.0*ray.y;
    float c=height*(12720.0+height);
    // Rationalized positive root for upward rays.
    return c/(sqrt(b*b+c)+b);
}
vec3 rf_sky(vec2 pixel) {
    vec3 ray=sky_ray(pixel),sun=lighting.sun_direction.xyz;
    vec3 background=sky_atmosphere(ray);
    if(ray.y<=0.0) return background;
    float daylight=smoothstep(-0.08,0.15,sun.y);
    vec3 sunlit=lighting.sun_color.rgb*(lighting.sun_color.w/3.5)*daylight;
    // Sparse high cloud, evaluated in world direction before the lower volume.
    vec3 high=ray*sky_shell_distance(ray,7.0);
    high.xz+=lighting.sky_weather.xy*0.6;
    high.xz=mat2(0.8,-0.6,0.6,0.8)*high.xz;
    float cirrus=sky_noise(high*vec3(0.5,0.0,1.1)+lighting.sky_weather.z).x;
    float veil=smoothstep(0.60,0.80,cirrus)*0.035*smoothstep(0.08,0.25,ray.y);
    background=mix(background,vec3(0.7,0.76,0.82)*sunlit,veil);
    if(lighting.sky_cloud.y<=0.0 || lighting.sky_cloud.x<=0.0) return background;
    float start=sky_shell_distance(ray,lighting.sky_cloud.z);
    float end=min(sky_shell_distance(ray,lighting.sky_cloud.z+lighting.sky_cloud.w),25.0);
    if(end<=start) return background;
    // Quality is uniform for a dispatch: no per-row step-count discontinuities.
    int steps=lighting.counts.y<1.5?64:(lighting.counts.y<3.0?40:32);
    float stride=(end-start)/float(steps),trans=1.0;
    // World-direction stratification breaks marching slices without frame noise
    // or screen-locked patterns. The reconstruction filter softens the residual.
    float offset=mix(0.35,0.65,sky_noise(ray*256.0).y);
    vec3 scattered=vec3(0);
    float mu=max(dot(ray,sun),0.0);
    float phase=0.55+0.65*pow(mu,8.0);
    // The baked smooth value-noise weather has per-axis slope <= 1.5;
    // use 1.6 to cover RGBA16F rounding and trilinear interpolation.
    float weather_step_bound=1.6*0.19*stride*(abs(ray.x)+abs(ray.z));
    for(int i=0;i<steps;++i) {
        vec3 p=ray*(start+(float(i)+offset)*stride);
        float weather=sky_weather_at(p);
        // Skip only provably empty sample positions, keeping the exact original
        // march lattice. Bit 128 is an internal reference-scan diagnostic.
        if((d.quality.x&128)==0 && weather+3.0*weather_step_bound+0.001<1.0-lighting.sky_cloud.x-0.18) {
            i+=3;
            continue;
        }
        float group=sky_cloud_group(weather);
        float detail=1.0-smoothstep(4.0,14.0,length(p.xz));
        float density=sky_density(p,group,detail);
        if(density>0.001) {
            // Weather spans kilometres: reuse its local coverage for the short
            // light probes, while preserving each probe's shape and height.
            float optical=sky_density(p+sun*0.10,group,detail)*0.25+sky_density(p+sun*0.35,group,detail)*0.45;
            float visibility=exp(-optical*3.0);
            float height=clamp((sky_height(p)-lighting.sky_cloud.z)/lighting.sky_cloud.w,0.0,1.0);
            vec3 fill=mix(vec3(0.10,0.15,0.23),vec3(0.34,0.40,0.47),height)*daylight;
            vec3 light=fill+sunlit*(0.08+0.92*visibility)*phase;
            // Distant cloud fades into sky haze, not a white horizon wall.
            light=mix(light,background,1.0-exp(-length(p)*0.018));
            float alpha=1.0-exp(-density*stride*2.4);
            scattered+=trans*alpha*light;
            trans*=1.0-alpha;
            if(trans<0.015) break;
        }
    }
    return scattered+trans*background;
}
