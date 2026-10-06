// Fold the eight-texel octahedral distance map for the legacy reference path.
ivec2 probe_fold(ivec2 p) {
    if(p.x<0){p.x=-p.x-1;p.y=7-p.y;}
    else if(p.x>7){p.x=15-p.x;p.y=7-p.y;}
    if(p.y<0){p.y=-p.y-1;p.x=7-p.x;}
    else if(p.y>7){p.y=15-p.y;p.x=7-p.x;}
    return p;
}
