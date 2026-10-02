/* RF Gen1 dynamic presentation. Units and fan centers match the asset authoring
 * contract; no clock reads, hardware polling, or gameplay state here. */
static struct rasterfall_electronics_frame electronics_frame;
void rasterfall_electronics_set_frame(const struct rasterfall_electronics_frame *frame)
{
    if (frame) electronics_frame=*frame;
    else memset(&electronics_frame,0,sizeof(electronics_frame));
}
struct rasterfall_electronics_frame rasterfall_electronics_get_frame(void)
{
    return electronics_frame;
}
static int electronics_rotor(const struct rasterfall_prop_instance *p,
    unsigned phase,double width,double height,double depth,double radius,
    rasterfall_host_quad_fn emit,void *context)
{
    double angle=(phase%60000000u)*(6.283185307179586/60000000.0);
    for (int blade=0;blade<7;++blade) {
        double a=angle+blade*6.283185307179586/7;
        const double rr[4]={.2,.76,.79,.35},offset[4]={0,.18,.62,.8};
        double q[4][3];
        for (int k=0;k<4;++k) {
            q[k][0]=-depth+(k==1 ? 3 : k>=2 ? -3 : 0);
            q[k][1]=height+radius*rr[k]*sin(a+offset[k]);
            q[k][2]=width+radius*rr[k]*cos(a+offset[k]);
        }
        if (host_quad(p,emit,context,q,blade ? 0x8C9DAA : 0xBCA16C)<0) return -1;
    }
    return 0;
}
int rasterfall_electronics_activity(const struct rasterfall_prop_instance *p,
    const struct rasterfall_electronics_frame *frame,rasterfall_host_quad_fn emit,void *context)
{
    if (!p || !frame || !emit) return -1;
    unsigned group=p->length>0 && p->length<RASTERFALL_ELECTRONICS_GROUPS ? (unsigned)p->length : 0;
    unsigned phase=frame->phase[group];
    if (p->asset_id==RASTERFALL_PROP_ASSET_LAB_COMPUTER_COOLING)
        return electronics_rotor(p,phase,0,72,102,51,emit,context);
    if (p->asset_id==RASTERFALL_PROP_ASSET_LAB_COMPUTER_COMPUTE) {
        if (electronics_rotor(p,phase,-55,37,150,31,emit,context)<0) return -1;
        return electronics_rotor(p,phase,55,37,150,31,emit,context);
    }
    if (p->asset_id!=RASTERFALL_PROP_ASSET_LAB_COMPUTER_CASE) return 0;
    int powered=group && (frame->powered_mask & (1u<<group));
    unsigned time=frame->time_ms[group];
    unsigned seed=(unsigned)p->x*7u+(unsigned)p->z*11u;
    unsigned colors[3]={powered ? 0x9DFFC1 : 0x213D36,
        powered && (time+seed)%830u<160u ? 0x65CFFF : 0x223A47,
        powered && (time+seed+191u)%1310u<240u ? 0xFFC66F : 0x42372B};
    for (int k=0;k<3;++k) {
        double x=2+k*16;
        double front[4][3]={{x,374,181},{x+9,374,181},{x+9,382,181},{x,382,181}};
        double z=-34+k*28;
        double side[4][3]={{-146,28,z},{-146,36,z},{-146,36,z+12},{-146,28,z+12}};
        if (host_quad(p,emit,context,front,colors[k])<0 ||
            host_quad(p,emit,context,side,colors[k])<0) return -1;
    }
    return 0;
}
