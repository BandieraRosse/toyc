/* Distant, camera-translation-independent sky. All distances here are km.
 * Analytic single-scattering atmosphere (not the Hillaire LUT implementation),
 * curved cumulus volume and a separate thin cirrus layer, in linear HDR. */
layout(set=1,binding=5) uniform sampler3D noise_volume;
#ifndef RF_SKY_DIRECTION_ONLY
#include "sky_view.glsl"
#endif
vec2 sky_noise(vec3 p) {
    return textureLod(noise_volume,p/8.0+vec3(0.5/64.0),0.0).rg;
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
    // The solar disc is composed at display resolution after cloud filtering.
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
    if(group<=0.0) return 0.0;
    // Resolve smaller rising billows nearby; merge distant cloud structure
    // smoothly before the oblique march becomes too coarse to represent it.
    float frequency=mix(0.82,1.55,detail);
    // Keep seed/wind phase outside the radial frequency change: a large seed
    // offset must not amplify that smooth change into concentric noise bands.
    p=p*vec3(frequency,1.2,frequency)+sky_cloud_space(vec3(0))*vec3(1.55,1.2,1.55);
    vec4 shape=textureLod(noise_volume,p/8.0+vec3(0.5/64.0),0.0);
    // Weather controls cloud mass and height; erosion only touches the edge.
    // Larger connected billows share a flat base and individually rising tops.
    float top=mix(0.48,1.0,group);
    float profile=smoothstep(0.0,0.10,h)*(1.0-smoothstep(0.48,1.0,h/top));
    // Coverage groups select banks, but their interiors still need gaps and
    // individual rising lobes. The former threshold fell below almost every
    // noise sample at full coverage, producing a uniform grey ceiling.
    float body=mix(shape.a,shape.r,detail)-(0.70-0.31*group*profile)-0.05*(shape.b-0.5)*detail;
    // Preserve gradients through the interior instead of clipping almost the
    // entire cloud to one density. Short solar probes then resolve billows,
    // and thin edges converge continuously as the march direction changes.
    // Multiplying the height envelope also makes density reach zero at both
    // shell boundaries; adjusting only the threshold left bright noise peaks
    // abruptly cut by h<=0 / h>=1, visible as moving horizontal slices.
    float empty=1.0-clamp(body*1.25,0.0,1.0);empty*=empty;
    return (1.0-empty*empty)*profile*group*lighting.sky_cloud.y*2.5*
        (1.0-smoothstep(15.0,25.0,distance_to_observer));
}
float sky_shell_distance(vec3 ray,float height) {
    float b=6360.0*ray.y;
    float c=height*(12720.0+height);
    // Rationalized positive root for upward rays.
    return c/(sqrt(b*b+c)+b);
}
vec4 sky_direction(vec3 ray,int steps,bool reference_scan) {
    vec3 sun=lighting.sun_direction.xyz;
    vec3 background=sky_atmosphere(ray);
    if(ray.y<=0.0) return vec4(background,1);
    float daylight=smoothstep(-0.08,0.15,sun.y);
    vec3 sunlit=lighting.sun_color.rgb*(lighting.sun_color.w/3.5)*daylight;
    // A zero-cloud atmosphere is also the solar quality reference: neither
    // cumulus nor a separately sampled cirrus veil may change its visibility.
    if(lighting.sky_cloud.y<=0.0 || lighting.sky_cloud.x<=0.0) return vec4(background,1);
    // Sparse high cloud, evaluated in world direction before the lower volume.
    vec3 high=ray*sky_shell_distance(ray,7.0);
    high.xz+=lighting.sky_weather.xy*0.6;
    high.xz=mat2(0.8,-0.6,0.6,0.8)*high.xz;
    float cirrus=sky_noise(high*vec3(0.5,0.0,1.1)+lighting.sky_weather.z).x;
    float veil=smoothstep(0.60,0.80,cirrus)*0.035*smoothstep(0.08,0.25,ray.y);
    background=mix(background,vec3(0.7,0.76,0.82)*sunlit,veil);
    float start=sky_shell_distance(ray,lighting.sky_cloud.z);
    float end=min(sky_shell_distance(ray,lighting.sky_cloud.z+lighting.sky_cloud.w),25.0);
    if(end<=start) return vec4(background,1.0-veil);
    // Quality is uniform for a dispatch: no per-row step-count discontinuities.
    float stride=(end-start)/float(steps),trans=1.0;
    // Midpoint quadrature is continuous under camera rotation. High-frequency
    // direction jitter changed the entire march phase between adjacent rays,
    // making low-resolution clouds crawl and jump when the camera turned.
    const float offset=0.5;
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
        if(!reference_scan && weather+3.0*weather_step_bound+0.001<1.0-lighting.sky_cloud.x-0.18) {
            i+=3;
            continue;
        }
        float group=sky_cloud_group(weather);
        float detail=1.0-smoothstep(4.0,14.0,length(p.xz));
        float density=sky_density(p,group,detail);
        if(density>0.001) {
            // Weather spans kilometres: reuse its local coverage for the short
            // light probes, while preserving each probe's shape and height.
            float optical=sky_density(p+sun*0.10,group,detail)*0.30+sky_density(p+sun*0.45,group,detail)*0.60;
            float visibility=exp(-optical*3.0);
            float height=clamp((sky_height(p)-lighting.sky_cloud.z)/lighting.sky_cloud.w,0.0,1.0);
            // The lower mass receives cooler, weaker fill while exposed lobes
            // retain the same shared sunlight. Reuse two probes, not more steps.
            vec3 fill=mix(vec3(0.10,0.15,0.23),vec3(0.38,0.44,0.51),
                smoothstep(0.08,0.85,height))*daylight;
            vec3 light=fill+sunlit*(0.05+0.89*visibility)*phase;
            // Distant cloud fades into sky haze, not a white horizon wall.
            light=mix(light,background,1.0-exp(-length(p)*0.018));
            float alpha=1.0-exp(-density*stride*2.4);
            scattered+=trans*alpha*light;
            trans*=1.0-alpha;
            if(trans<0.015) break;
        }
    }
    // Alpha carries solar visibility, not opacity of the final SKY draw.
    return vec4(scattered+trans*background,trans*(1.0-veil));
}
#ifndef RF_SKY_DIRECTION_ONLY
vec4 rf_sky(vec2 pixel) {
    return sky_direction(sky_ray(pixel),lighting.counts.y<1.5?64:40,(d.quality.x&128)!=0);
}
#endif
