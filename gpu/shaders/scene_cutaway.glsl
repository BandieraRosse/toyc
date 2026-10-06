// Shared by WORLD depth and color passes; identical ordered coverage is required.
void scene_cutaway(vec3 p) {
    bool meter=(lighting.light_control.w&255u)==3u;
    if(meter && ((d.quality.x&(64|16))!=0 || d.texture_info.w!=0 || d.quality.z==3))return;
    if((d.quality.x&(64|16))==0 && lighting.cutaway_height.y>0.0 && p.y>lighting.cutaway_height.x &&
       p.x>=lighting.cutaway_bounds.x && p.x<=lighting.cutaway_bounds.y &&
       p.z>=lighting.cutaway_bounds.z && p.z<=lighting.cutaway_bounds.w) {
        const int pattern[16]=int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
        ivec2 pixel=ivec2(gl_FragCoord.xy)&3;
        if(lighting.cutaway_height.y>(float(pattern[pixel.y*4+pixel.x])+0.5)/16.0)discard;
    }
}
