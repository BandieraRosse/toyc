/* Shared inverse camera projection and analytic, pixel-sized solar highlight.
 * Clouds remain low resolution; the small solar disc must not share that grid. */
vec3 sky_ray(vec2 pixel) {
    vec3 q=vec3((pixel.x-float(d.projection.x)*0.5)/float(d.projection.w),
        (float(d.projection.y)*0.5-pixel.y)/float(d.projection.w),1);
    float y=(q.y*float(d.view.w)+q.z*float(d.view.z))/1024.0;
    float z=(q.z*float(d.view.w)-q.y*float(d.view.z))/1024.0;
    return normalize(vec3((q.x*float(d.view.y)+z*float(d.view.x))/1024.0,y,
        (z*float(d.view.y)-q.x*float(d.view.x))/1024.0));
}
vec3 sky_solar(vec3 ray) {
    vec3 sun=lighting.sun_direction.xyz;
    // Chord length retains precision at the centre unlike acos(dot(ray,sun)).
    float angle=length(ray-sun);
    if(angle>0.24 || ray.y<=-0.015) return vec3(0);
    float radius=0.00465,footprint=0.70/float(d.projection.w);
    float disc=1.0-smoothstep(radius-footprint,radius+footprint,angle);
    // A restrained aureole provides a smooth glint while turning toward the
    // sun. No frame noise, auto exposure pulse, or full-screen white overlay.
    float halo=0.38*exp(-angle*angle/0.00038)+
        0.065*exp(-angle*angle/0.009);
    float air=1.0/sqrt(max(ray.y,0.0)*max(ray.y,0.0)+0.012);
    vec3 trans=exp(-(vec3(0.055,0.105,0.205)+vec3(0.035*lighting.sky_weather.w))*air);
    float daylight=smoothstep(-0.08,0.15,sun.y)*smoothstep(-0.015,0.015,ray.y);
    return (disc*12.0+halo)*trans*lighting.sun_color.rgb*
        (lighting.sun_color.w/3.5)*daylight;
}
