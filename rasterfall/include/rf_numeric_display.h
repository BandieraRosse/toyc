#ifndef RF_NUMERIC_DISPLAY_H
#define RF_NUMERIC_DISPLAY_H
#include "rf_numeric.h"
#include "math.h"
#include "string.h"

/* Presentation-only. Diagnose the raw input before correcting its length;
 * zero uses +Z. Nonfinite input is rejected and outputs remain untouched. */
static int rf_direction_display(const char *source,double sy,double cy,
    double *out_sy,double *out_cy,const struct rf_numeric_context *context)
{
    double maximum, length, unit_sy=0,unit_cy=1;
    int valid,result;
    struct rf_numeric_event event;
    if(!__builtin_isfinite(sy) || !__builtin_isfinite(cy)) {
        memset(&event,0,sizeof(event));event.source=source;
        event.category=RF_NUMERIC_NONFINITE;event.result=-1;
        event.raw_sy=sy;event.raw_cy=cy;
        event.context.kind=event.context.slot=event.context.id=event.context.generation=-1;
        if(context)memcpy(&event.context,context,sizeof(*context));
        rf_numeric_record(&event);return -1;
    }
    maximum=fabs(sy)>fabs(cy)?fabs(sy):fabs(cy);
    valid=maximum<=1034 && sy*sy+cy*cy>=1014.0*1014 && sy*sy+cy*cy<=1034.0*1034;
    result=maximum?1:0;
    if(maximum) {
        double scaled_sy=sy/maximum,scaled_cy=cy/maximum;
        length=sqrt(scaled_sy*scaled_sy+scaled_cy*scaled_cy);
        unit_sy=scaled_sy/length;unit_cy=scaled_cy/length;
    }
    if(!valid) {
        memset(&event,0,sizeof(event));event.source=source;
        event.category=maximum?RF_NUMERIC_DIRECTION:RF_NUMERIC_ZERO;
        event.result=rf_numeric_strict()?-1:result;
        event.raw_sy=sy;event.raw_cy=cy;
        if(sy>=-2147483647.0 && sy<=2147483647.0)event.old_sy=(int)sy;
        if(cy>=-2147483647.0 && cy<=2147483647.0)event.old_cy=(int)cy;
        event.sy=(int)(unit_sy*1024);event.cy=(int)(unit_cy*1024);
        event.context.kind=event.context.slot=event.context.id=event.context.generation=-1;
        if(context)memcpy(&event.context,context,sizeof(*context));
        rf_numeric_record(&event);
        if(rf_numeric_strict())return -1;
    }
    *out_sy=unit_sy;*out_cy=unit_cy;return result;
}
#endif
