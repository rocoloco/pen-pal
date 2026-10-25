#ifndef HERMES_PEN_INPUT_H
#define HERMES_PEN_INPUT_H
#include <math.h>

typedef struct { double x, y; unsigned int known; Time time; } PenPosition;

/* First-generation Scribe, portrait/full-screen: stylus valuators 0/1 are
 * already display pixels. event_x/y are NOT pen-local: palm input can move
 * the shared pointer while these device valuators remain correct.
 * Never fall back to pointer coordinates. Button events may omit axes, so
 * retain only a recent position from this same pen; reset on focus loss.
 */
static int input_position(PenPosition *s, const XIDeviceEvent *e, int device,
                          int screen_w, int screen_h, int *x, int *y,
                          unsigned int stale_ms) {
    if(e->sourceid!=device)return 0;
    if(e->evtype!=XI_Motion&&e->evtype!=XI_ButtonPress&&e->evtype!=XI_ButtonRelease&&
       e->evtype!=XI_TouchBegin&&e->evtype!=XI_TouchUpdate&&e->evtype!=XI_TouchEnd)return 0;
    if(stale_ms&&s->known&&(uint32_t)(e->time-s->time)>stale_ms)s->known=0;
    unsigned int changed=0;int j=0;
    for(int i=0;i<e->valuators.mask_len*8;++i) {
        if(!XIMaskIsSet(e->valuators.mask,i))continue;
        double value=e->valuators.values[j++];
        if(i>1)continue;
        if(!isfinite(value)||value<0||value>=(i==0?screen_w:screen_h)) {
            s->known=0;return 0;
        }
        if(i==0)s->x=value;else s->y=value;
        changed|=1u<<i;
    }
    if(changed){s->known|=changed;s->time=e->time;}
    if(s->known!=3||((e->evtype==XI_Motion||e->evtype==XI_TouchUpdate)&&!changed))return 0;
    *x=(int)s->x;*y=(int)s->y;return 1;
}

static int pen_position(PenPosition *s, const XIDeviceEvent *e, int pen,
                        int screen_w, int screen_h, int *x, int *y) {
    return input_position(s,e,pen,screen_w,screen_h,x,y,250);
}

/* Touch is used only for controls. Keep its down position for the entire
 * contact so a slow finger tap can still be released without axes. */
static int touch_position(PenPosition *s, const XIDeviceEvent *e, int touch,
                          int screen_w, int screen_h, int *x, int *y) {
    int positioned=input_position(s,e,touch,screen_w,screen_h,x,y,0);
    if(positioned)return 1;
    if((e->evtype==XI_TouchBegin||e->evtype==XI_ButtonPress)&&
       isfinite(e->event_x)&&isfinite(e->event_y)&&e->event_x>=0&&e->event_x<screen_w&&
       e->event_y>=0&&e->event_y<screen_h) {
        *x=(int)e->event_x;*y=(int)e->event_y;return 1;
    }
    return 0;
}
#endif
