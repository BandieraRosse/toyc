/* Shared numeric contract lives in game.c so all existing Game consumers,
 * including tactical and self targets, use exactly the same implementation. */
#if defined(TOYC_WINDOWS) || defined(RF_GAME_HOSTED)
#include <stdio.h>
#else
#include "core.h"
#endif
static long long numeric_sqrt(long long value)
{
    long long result=0,bit=1LL<<62;
    while(bit>value)bit>>=2;
    while(bit) {
        if(value>=result+bit) { value-=result+bit;result=(result>>1)+bit; }
        else result>>=1;
        bit>>=2;
    }
    return result;
}
int rf_direction_q10(long long dx, long long dz, int *sy, int *cy)
{
    long long ax, az, maximum, length;
    int x, z;
    if (!sy || !cy || dx < -2147483647LL || dx > 2147483647LL ||
        dz < -2147483647LL || dz > 2147483647LL) return -1;
    if (!dx && !dz) return 0;
    ax = dx < 0 ? -dx : dx; az = dz < 0 ? -dz : dz;
    maximum = ax > az ? ax : az;
    /* Expand small inputs before sqrt: at least 29 fractional magnitude bits.
     * Large accepted inputs need no expansion; sum of two INT_MAX squares
     * still fits signed 64 bits. Rounded numerator also fits signed 64 bits. */
    while (maximum < 536870912LL) {
        ax *= 2; az *= 2; maximum *= 2;
    }
    length = numeric_sqrt(ax * ax + az * az);
    x = (int)((ax * 1024 + length / 2) / length);
    z = (int)((az * 1024 + length / 2) / length);
    *sy = dx < 0 ? -x : x; *cy = dz < 0 ? -z : z;
    return 1;
}

int rf_direction_valid(int sy, int cy)
{
    long long length;
    if (sy < -1034 || sy > 1034 || cy < -1034 || cy > 1034) return 0;
    length = (long long)sy * sy + (long long)cy * cy;
    return length >= 1014LL * 1014 && length <= 1034LL * 1034;
}

static struct rf_numeric_event numeric_events[32];
static unsigned numeric_count, numeric_next;
static unsigned numeric_printed;
static unsigned long long numeric_total;
static int numeric_detailed;
static int numeric_strict_mode;

static void numeric_print(const struct rf_numeric_event *e)
{
#if defined(TOYC_WINDOWS) || defined(RF_GAME_HOSTED)
    fprintf(stderr,
        "RF-NUMERIC build=%s/%s source=%s category=%d result=%d count=%llu "
        "tick=%llu frame=%llu world=%llu kind=%d slot=%d id=%d generation=%d "
        "position=(%d,%d) delta=(%lld,%lld) old=(%d,%d) output=(%d,%d) length2=%llu raw=(%.9g,%.9g) strict=%d\n",
        __DATE__, __TIME__, e->source, e->category, e->result, e->count,
        e->context.tick,e->context.frame,e->context.world,e->context.kind,
        e->context.slot,e->context.id,e->context.generation,e->context.x,e->context.z,
        e->dx,e->dz,e->old_sy,e->old_cy,e->sy,e->cy,
        (unsigned long long)((long long)e->sy*e->sy)+(unsigned long long)((long long)e->cy*e->cy),
        e->raw_sy,e->raw_cy,numeric_strict_mode);
#else
    __printf("RF-NUMERIC source=%s category=%d result=%d count=%llu\n",
        e->source,e->category,e->result,e->count);
#endif
}

void rf_numeric_record(const struct rf_numeric_event *event)
{
    unsigned i;
    if (!event || !event->source) return;
    numeric_total++;
    for (i=0;i<numeric_count;i++) {
        struct rf_numeric_event *old=&numeric_events[i];
        if (!strcmp(old->source,event->source) && old->category==event->category &&
            old->context.kind==event->context.kind && old->context.id==event->context.id &&
            old->context.slot==event->context.slot &&
            old->context.generation==event->context.generation &&
            old->context.world==event->context.world) {
            unsigned long long count=old->count;
            memcpy(old,event,sizeof(*old));old->count=count+1;
            return;
        }
    }
    memcpy(&numeric_events[numeric_next],event,sizeof(*event));
    numeric_events[numeric_next].count=1;
    if(numeric_printed<32) { numeric_print(&numeric_events[numeric_next]);numeric_printed++; }
    numeric_next=(numeric_next+1)%32;
    if (numeric_count<32) numeric_count++;
}
void rf_numeric_dump(void)
{
    unsigned i;
#if defined(TOYC_WINDOWS) || defined(RF_GAME_HOSTED)
    if(numeric_total)fprintf(stderr,"RF-NUMERIC summary events=%llu retained=%u first-records=%u detailed=%d\n",
        numeric_total,numeric_count,numeric_printed,numeric_detailed);
#endif
    for(i=0;i<numeric_count;i++) {
        unsigned index=numeric_count==32 ? (numeric_next+i)%32 : i;
        if(numeric_detailed || numeric_events[index].count>1)
            numeric_print(&numeric_events[index]);
    }
}
void rf_numeric_reset(int detailed)
{
    numeric_count=numeric_next=numeric_printed=0;numeric_total=0;numeric_detailed=detailed!=0;
}
void rf_numeric_dump_fatal(void)
{
    int detailed=numeric_detailed;
    numeric_detailed=1;rf_numeric_dump();numeric_detailed=detailed;
}
unsigned rf_numeric_event_count(void) { return numeric_count; }
void rf_numeric_set_strict(int strict) { numeric_strict_mode=strict!=0; }
int rf_numeric_strict(void) { return numeric_strict_mode; }
int rf_numeric_event_get(unsigned index,struct rf_numeric_event *event)
{
    if(!event || index>=numeric_count)return -1;
    if(numeric_count==32)index=(numeric_next+index)%32;
    memcpy(event,&numeric_events[index],sizeof(*event));return 0;
}
int rf_direction_set(const char *source,long long dx,long long dz,
    int *sy,int *cy,const struct rf_numeric_context *context)
{
    struct rf_numeric_event e;
    int result;
    memset(&e,0,sizeof(e));
    e.source=source;e.dx=dx;e.dz=dz;
    e.context.kind=e.context.slot=e.context.id=e.context.generation=-1;
    if(context)memcpy(&e.context,context,sizeof(*context));
    e.old_sy=sy?*sy:0;e.old_cy=cy?*cy:0;
    result=rf_direction_q10(dx,dz,sy,cy);
    if(result<0) {
        e.category=RF_NUMERIC_RANGE;e.result=result;
        e.sy=e.old_sy;e.cy=e.old_cy;rf_numeric_record(&e);
    }
    return result;
}
void rf_direction_check(const char *source,int sy,int cy,
    const struct rf_numeric_context *context)
{
    struct rf_numeric_event e;
    if(rf_direction_valid(sy,cy))return;
    memset(&e,0,sizeof(e));e.source=source;
    e.category=(!sy && !cy)?RF_NUMERIC_ZERO:RF_NUMERIC_DIRECTION;
    e.old_sy=e.sy=sy;e.old_cy=e.cy=cy;
    e.dx=sy;e.dz=cy;e.raw_sy=sy;e.raw_cy=cy;
    e.context.kind=e.context.slot=e.context.id=e.context.generation=-1;
    if(context)memcpy(&e.context,context,sizeof(*context));
    rf_numeric_record(&e);
}
