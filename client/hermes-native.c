#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <X11/Xft/Xft.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "pen-input.h"

#define MAX_POINTS 120000

/* Rendered-ink cache. See ink_cache_bake() for why this exists. */
static void ink_cache_invalidate(void);
static long isqrt_long(long value);   /* defined below; the eraser geometry needs it */
#define TITLE "L:A_N:application_PC:N_O:U_ID:org.hermes.inkprototype"
enum HermesInkTool { HERMES_TOOL_PEN=0, HERMES_TOOL_HIGHLIGHTER=1, HERMES_TOOL_ERASER=2 };
enum HermesPenStyle { HERMES_STYLE_BALLPOINT=0, HERMES_STYLE_FOUNTAIN=1, HERMES_STYLE_FELT=2, HERMES_STYLE_PENCIL=3 };
typedef struct { uint16_t x, y; uint8_t start, erase, tool, style, width; } Point;
typedef struct { uint32_t count, sent, replies; Point points[MAX_POINTS]; } Page;
static Page page;
static Display *display;
static Window window;
static GC gc;
static Pixmap backing;
static XftDraw *textdraw;
static XftFont *font, *heading, *small;
static XftColor black, white_text;
static unsigned long white_pixel, black_pixel, gray_pixel, pencil_pixel;
/* Dither geometry. CELL is the grain size and is deliberately small and
 * independent of the stroke thickness; SPAN (the thickness) is covered by
 * walking several cell-sized rows across it.
 *
 * Two rounds of "the pencil looks like dots", with different causes:
 *
 *  - First the cell was as large as the stroke (base/2 = 4px at THICK), so the
 *    line was a chain of 4x4 blocks. Dropping to a 2px grain made the holes
 *    smaller but did not remove the real defect: at the THIN/FINE settings the
 *    band is only 2px tall, so a 2px cell means ONE cell per column, and the
 *    30% of columns the dither skipped were 2px BREAKS in the line. Measured on
 *    the user's own ink: 16% of sampled points had no ink at all across the
 *    stroke (grain-probe.c prints the region as text and counts these).
 *
 * So the grain must stay FINER than the band (1px here) and no column may be
 * left empty (see the `continuous` path in draw_dither). Texture comes from
 * holes INSIDE a continuous band, which reads as graphite; holes that sever the
 * band read as dots. Both are pure black/white, which is what lets them survive
 * the A2 waveform used while the pen is moving. */
#ifndef PENCIL_CELL
#define PENCIL_CELL 1
#endif
#define PENCIL_COVERAGE 70u
#define HIGHLIGHT_CELL 3
#define HIGHLIGHT_COVERAGE 25u
static long fountain_dirx, fountain_diry;   /* smoothed direction, x1000 */
static double fountain_width;
static int width, height, xi_opcode, pen_id = -1, touch_id = -1;
static int drawing, erasing, pen_button, stroke_started, last_x, last_y, dirty;
static int stylus_event_fd = -1, hardware_eraser, stroke_erasing;
/* Sleep handling. Verified on this device: a power-button press makes powerd go
 * ACTIVE -> SCREEN SAVER, turn the frontlight off, and publish goingToScreenSaver
 * on com.lab126.powerd. Amazon's UI draws a lock screen in response -- and with
 * Hermes Ink holding the display nobody drew one, so the e-ink panel kept the last
 * frame of the app while the device was already asleep. That is why it looked like
 * "it only dims the display". So: watch the same powerd events, draw our own sleep
 * screen on the way down, and repaint on the way back up.
 *
 * lipc-wait-event -m prints one line per event and keeps going, so it runs as a
 * child and its pipe is one more fd in the select() set. */
static FILE *power_watch;
static pid_t power_watch_pid = -1;
static char power_buf[512];
static size_t power_buf_len;

/* Which action a powerd event line means. Pure, so self_test() can cover the
 * exact strings the device emits without needing a display or a power button. */
enum { POWER_LINE_NONE, POWER_LINE_SLEEP, POWER_LINE_WAKE };
static int power_line_action(const char *line) {
    if(strstr(line,"goingToScreenSaver"))return POWER_LINE_SLEEP;
    if(strstr(line,"exitingScreenSaver")||strstr(line,"outOfScreenSaver"))return POWER_LINE_WAKE;
    return POWER_LINE_NONE;
}
static void sleep_enter(void);
static void sleep_exit(const char *why);
static int sleeping;
static int power_key_fd = -1;
static int sleep_painted;      /* 1 = we painted our own screen */
static int sleep_step;         /* 0 waiting for the WM, 1 asked blanket directly, 2 resolved */
static double sleep_check_at;
static void product_sleep_render(void);
static void product_repaint(void);
static void product_ink_toolbar_render(void);
static double seconds(void);
static int native_lock_screen_visible(void);
static Window find_native_lock_screen(int *viewable);

static void power_watch_start(void) {
    int fds[2];
    if(pipe(fds))return;
    fflush(NULL);
    power_watch_pid=fork();
    if(power_watch_pid==0) {
        dup2(fds[1],STDOUT_FILENO);
        close(fds[0]);close(fds[1]);
        execl("/usr/bin/lipc-wait-event","lipc-wait-event","-m","com.lab126.powerd",
              "goingToScreenSaver,exitingScreenSaver,outOfScreenSaver",(char *)0);
        _exit(127);
    }
    close(fds[1]);
    if(power_watch_pid<0){close(fds[0]);return;}
    /* NON-BLOCKING, and read with read() rather than fgets(). This pipe is empty
     * almost all of the time, and the reader runs on every pass of the main loop:
     * a blocking read here froze the whole app the moment powerd went quiet -- the
     * UI ignored every tap while the process sat waiting for an event. */
    int flags=fcntl(fds[0],F_GETFL,0);
    if(flags>=0)fcntl(fds[0],F_SETFL,flags|O_NONBLOCK);
    power_watch=fdopen(fds[0],"r");
    if(!power_watch){close(fds[0]);return;}
    setvbuf(power_watch,NULL,_IONBF,0);
}

/* The power key itself, read straight from the kernel. The device's own
 * frameworks turn a press into "sleep" via powerd, and lipc events are the
 * official signal, but this is the one input path we have already proved works
 * (captured on this unit: EV_KEY code 116, press and release 50us apart), so the
 * power button drives the sleep screen even if the event bus does not. */
static int open_power_key_device(void) {
    for(int i=0;i<32;++i) {
        char path[64];snprintf(path,sizeof path,"/dev/input/event%d",i);
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if(fd<0)continue;
        char name[64]={0};
        if(ioctl(fd,EVIOCGNAME(sizeof name-1),name)<0||strcmp(name,"bd71828-pwrkey")){close(fd);continue;}
        fprintf(stderr,"power key: watching %s\n",path);
        return fd;
    }
    return -1;
}

static void sleep_enter(void) {
    if(sleeping)return;
    sleeping=1;
    /* Step aside so the system's lock screen can actually be seen and drawn. Its
     * window gets mapped either way, but while our window still covers the screen
     * the lock screen renders nothing (measured: /dev/fb0 stays blank instead of
     * gaining the lock screen's art) and the panel just keeps our last frame -- the
     * "it only dims" symptom. Unmapped, the app is out of the way; if the lock
     * screen turns out not to appear we remap and paint our own screen below. */
    XUnmapWindow(display,window);
    XFlush(display);
    sleep_painted=0;
    sleep_step=0;
    sleep_check_at=seconds()+1.5;
    fprintf(stderr,"power: asleep - stepped aside, waiting for the native lock screen\n");
}
static void sleep_exit(const char *why) {
    if(!sleeping)return;
    sleeping=0;
    fprintf(stderr,"power: awake (%s) - repainting\n",why);
    /* sleep_enter() unmapped us; come back before repainting, or nothing shows. Take
     * the input focus too: an unmapped window loses it, and the pen would come back
     * with nothing to deliver events to. */
    XMapRaised(display,window);
    XSetInputFocus(display,window,RevertToParent,CurrentTime);
    XFlush(display);
    product_repaint();
}
static void power_key_drain(void) {
    if(power_key_fd<0)return;
    unsigned char buf[16*16];
    for(;;) {
        ssize_t got=read(power_key_fd,buf,sizeof buf);
        if(got<=0)return;
        for(ssize_t i=0;i+16<=got;i+=16) {
            unsigned short type,code;int value;
            memcpy(&type,buf+i+8,2);memcpy(&code,buf+i+10,2);memcpy(&value,buf+i+12,4);
            if(type!=1||code!=116||value!=1)continue;   /* EV_KEY / KEY_POWER / press */
            if(sleeping)sleep_exit("power key");else sleep_enter();
        }
    }
}
static void power_watch_tick(void) {
    if(power_watch) {
        int fd=fileno(power_watch);
        for(;;) {
            ssize_t got=read(fd,power_buf+power_buf_len,sizeof power_buf-power_buf_len-1);
            if(got<=0)break;
            power_buf_len+=(size_t)got;
            power_buf[power_buf_len]=0;
            char *nl;
            while((nl=strchr(power_buf,'\n'))!=NULL) {
                *nl=0;
                fprintf(stderr,"power event: %s\n",power_buf);
                int action=power_line_action(power_buf);
                if(action==POWER_LINE_SLEEP)sleep_enter();
                else if(action==POWER_LINE_WAKE)sleep_exit("powerd event");
                size_t rest=power_buf_len-(size_t)(nl+1-power_buf);
                memmove(power_buf,nl+1,rest);
                power_buf_len=rest;
                power_buf[power_buf_len]=0;
            }
            if(power_buf_len>=sizeof power_buf-1)power_buf_len=0;   /* unterminated junk */
        }
    }
    if(sleeping&&sleep_step<2&&seconds()>=sleep_check_at) {
        if(native_lock_screen_visible()) {
            fprintf(stderr,"power: the system's own lock screen is up; leaving it alone\n");
            sleep_step=2;
        } else if(sleep_step==0) {
            /* Mapping blanket's window ourselves was tried here and it is WRONG: once
             * the window is viewable the window manager skips its own screensaver
             * trigger, so the lock screen is mapped but never drawn -- and the panel
             * keeps the app's last frame, which is exactly the "it only dims"
             * symptom. Let the WM do the whole job and only paint ours if it doesn't. */
            fprintf(stderr,"power: no lock screen after the first wait - giving the WM another moment\n");
            sleep_step=1;
            sleep_check_at=seconds()+1.0;
        } else {
            /* No lock screen: take the screen back and show our own. Remapping first
             * matters -- sleep_enter() unmapped us so the lock screen could be drawn. */
            fprintf(stderr,"power: no native lock screen - taking the screen back and drawing our own\n");
            XMapRaised(display,window);
            XFlush(display);
            sleep_painted=1;
            product_sleep_render();
            sleep_step=2;
        }
    }
}
/* Any input at all means the user is back, even if the wake event never arrived. */
static double last_input_at;      /* the last pen or finger event: a refresh waits for a pause */
static void wake_on_input(void) {
    last_input_at=seconds();
    if(!sleeping)return;
    sleep_exit("input");
}
/* Is the system's OWN lock screen on screen? blanket (/usr/sbin/blanket) owns a
 * full-screen window named "...module:screensaver_ID:blanket-screensaver", and the
 * window manager maps it when powerd enters the screensaver. Verified on the
 * device: with the WM frozen (what our launcher used to do) that window stays
 * IsUnMapped, which is why we had to paint our own screen; with the WM alive it is
 * mapped. So ask the window system rather than assuming, and only paint our own
 * screen when the native one did not appear. */
static Window find_native_lock_screen(int *viewable) {
    Window root=DefaultRootWindow(display),parent,*kids=NULL;
    unsigned count=0;
    Window found=0;
    if(viewable)*viewable=0;
    if(!XQueryTree(display,root,&root,&parent,&kids,&count))return 0;
    for(unsigned i=0;i<count&&!found;++i) {
        char *name=NULL;
        if(XFetchName(display,kids[i],&name)&&name) {
            if(strstr(name,"blanket-screensaver")) {
                found=kids[i];
                XWindowAttributes a;
                if(viewable&&XGetWindowAttributes(display,found,&a)&&a.map_state==IsViewable)*viewable=1;
            }
            XFree(name);
        }
    }
    if(kids)XFree(kids);
    return found;
}
static int native_lock_screen_visible(void) {
    int viewable=0;
    find_native_lock_screen(&viewable);
    return viewable;
}
/* Let the window manager keep us fullscreen and above the Kindle UI instead of
 * freezing it: the frozen WM cannot map the native lock screen. */
static void request_fullscreen_above(void) {
    Atom type=XInternAtom(display,"_NET_WM_WINDOW_TYPE",False);
    Atom normal=XInternAtom(display,"_NET_WM_WINDOW_TYPE_NORMAL",False);
    XChangeProperty(display,window,type,XA_ATOM,32,PropModeReplace,(unsigned char *)&normal,1);
    Atom state=XInternAtom(display,"_NET_WM_STATE",False);
    Atom states[2];
    states[0]=XInternAtom(display,"_NET_WM_STATE_FULLSCREEN",False);
    states[1]=XInternAtom(display,"_NET_WM_STATE_ABOVE",False);
    XChangeProperty(display,window,state,XA_ATOM,32,PropModeReplace,(unsigned char *)states,2);
}
static int product_ink_tool = HERMES_TOOL_PEN;
static int product_ink_style = HERMES_STYLE_BALLPOINT;
static int product_ink_pen_width_index = 2;
static int product_ink_highlighter_width_index = 1;
static int product_ink_eraser_width_index = 2;
static const unsigned char ink_pen_widths[5] = {2,4,6,9,13};
static const unsigned char ink_highlighter_widths[4] = {12,18,26,36};
static const unsigned char ink_eraser_widths[4] = {18,28,38,52};
static PenPosition pen_position_state;
static PenPosition touch_position_state;
static int touch_active, touch_contact = -1, touch_button = -1, touch_x, touch_y;
static int touch_start_x = -1, touch_start_y;   /* where a finger came down, for swipes */
static double touch_start_at;
/* Palm rejection. The hand resting while he writes lands on Hermes' answers, and a
 * touch there opened the slip. Opening an answer or a page link by finger now takes a
 * quick tap (a resting palm stays down) with the pen away for a moment. The pen still
 * opens them at once, and every other control is unchanged. */
#define PALM_HOLD_S 0.45
#define PALM_PEN_S 2.0
static double last_pen_at = -1e9;
static int touch_kinds, touch_moves, touch_placed;   /* per stroke, for the swipe log */
static unsigned int pen_coordinate_corrections;
static volatile sig_atomic_t quitting;
static const char *state_path;
static char status[160] = "Local demo - use the pen. Nothing is sent online.";
static int canvas_top, canvas_bottom;
static void product_repaint(void);
static volatile sig_atomic_t reopen_requested;
static int stroke_on_blank;          /* the stroke began on blank daily paper, under its hint */

static void on_signal(int sig) { (void)sig; quitting = 1; }
static void on_reopen(int sig) { (void)sig; reopen_requested = 1; }
static double seconds(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}
static int ink_width_for_tool(int tool) {
    if(tool==HERMES_TOOL_HIGHLIGHTER)return ink_highlighter_widths[product_ink_highlighter_width_index];
    if(tool==HERMES_TOOL_ERASER)return ink_eraser_widths[product_ink_eraser_width_index];
    return ink_pen_widths[product_ink_pen_width_index];
}

/* XInput gives us the reliable pen coordinates, while this firmware's X11
 * device does not expose which end of the pen is in use. Read only the
 * standard evdev tool-state event directly so the Premium Pen eraser can
 * select erase mode without changing the coordinate path. */
static int input_bit_set(const unsigned long *bits, unsigned int bit) {
    const unsigned int word_bits = (unsigned int)(8 * sizeof(unsigned long));
    return (bits[bit / word_bits] >> (bit % word_bits)) & 1UL;
}
static int open_stylus_event_device(void) {
    int fallback = -1;
    for(int i=0;i<32;++i) {
        char path[64], name[256]={0};
        unsigned long key_bits[16]={0};
        snprintf(path,sizeof path,"/dev/input/event%d",i);
        int fd=open(path,O_RDONLY|O_NONBLOCK);
        if(fd<0)continue;
        if(ioctl(fd,EVIOCGNAME((int)sizeof name),name)<=0||
           ioctl(fd,EVIOCGBIT(EV_KEY,(int)sizeof key_bits),key_bits)<0||
           !input_bit_set(key_bits,BTN_TOOL_RUBBER)) {close(fd);continue;}
        if(!strcmp(name,"stylus-custom")) {
            if(fallback>=0)close(fallback);
            return fd;
        }
        if(fallback<0)fallback=fd;else close(fd);
    }
    return fallback;
}
static void apply_stylus_tool_event(const struct input_event *event) {
    if(event->type==EV_KEY&&event->code==BTN_TOOL_RUBBER)
        hardware_eraser=event->value!=0;
}
static void drain_stylus_tool_events(void) {
    if(stylus_event_fd<0)return;
    struct input_event event;
    for(;;) {
        ssize_t got=read(stylus_event_fd,&event,sizeof event);
        if(got==(ssize_t)sizeof event) {apply_stylus_tool_event(&event);continue;}
        if(got<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return;
        return;
    }
}

/* Portable, bounded disk format; no raw structs or compiler padding on disk. */
static int put32(FILE *f, uint32_t n) {
    for (int i=0; i<4; ++i) if (fputc((n >> (8*i)) & 255, f)==EOF) return -1;
    return 0;
}
static int get32(FILE *f, uint32_t *n) {
    *n=0; for(int i=0;i<4;++i) { int c=fgetc(f); if(c==EOF)return -1; *n|=(uint32_t)c<<(8*i); }
    return 0;
}
static unsigned char point_flags(Point p) {
    return (unsigned char)((p.start?1:0)|(p.erase?2:0)|((p.tool&7)<<2)|((p.style&7)<<5));
}
static int save_page(const char *path) {
    /* The send watermark can never exceed the ink. A reply acks the cutoff taken at
     * submit, and an erase while that answer was in flight shrinks the page under the
     * cutoff; load_page() rejects sent>count, so persisting one would lock him out of
     * his own notebook on the next start. Clamp here, the single place that writes the file. */
    if(page.sent>page.count)page.sent=page.count;
    char tmp[1024], dir[1024];
    if (snprintf(tmp,sizeof tmp,"%s.tmp",path)>=(int)sizeof tmp) return -1;
    if (snprintf(dir,sizeof dir,"%s",path)>=(int)sizeof dir) return -1;
    char *slash=strrchr(dir,'/'); if(slash) { if(slash==dir) slash[1]=0; else *slash=0; } else strcpy(dir,".");
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0600); if(fd<0)return -1;
    FILE *f=fdopen(fd,"wb"); if(!f){close(fd);return -1;}
    int bad=fwrite("HINK0002",1,8,f)!=8;
    bad|=put32(f,page.count);bad|=put32(f,page.sent);bad|=put32(f,page.replies);
    for(uint32_t i=0;i<page.count&&!bad;++i) {
        Point p=page.points[i];
        bad|=put32(f,(uint32_t)p.x|((uint32_t)p.y<<16));
        bad|=fputc(point_flags(p),f)==EOF;
        bad|=fputc(p.width,f)==EOF;
    }
    if(fflush(f)||fsync(fd))bad=1;
    if(fclose(f))bad=1;
    if(bad||rename(tmp,path))return -1;
    int dfd=open(dir,O_RDONLY|O_DIRECTORY); if(dfd<0)return -1;
    int result=fsync(dfd); close(dfd); return result;
}
/* Send only the ink written since the last acknowledged send. A follow-up note is a
 * NEW note: re-sending the whole page makes Hermes re-read text he already handled,
 * and the vision model re-words it ("2-6" came back as "2-0" on the very next send),
 * so he watches his own earlier line get mangled in "I read:". The watermark that
 * live_acknowledge_page() already maintains is exactly the split point. */
static int save_page_from(const char *path, uint32_t from) {
    if(from>page.count)from=page.count;
    uint32_t count=page.count-from;
    if(!count)return -1;
    char tmp[1024], dir[1024];
    if (snprintf(tmp,sizeof tmp,"%s.tmp",path)>=(int)sizeof tmp) return -1;
    if (snprintf(dir,sizeof dir,"%s",path)>=(int)sizeof dir) return -1;
    char *slash=strrchr(dir,'/'); if(slash) { if(slash==dir) slash[1]=0; else *slash=0; } else strcpy(dir,".");
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0600); if(fd<0)return -1;
    FILE *f=fdopen(fd,"wb"); if(!f){close(fd);return -1;}
    int bad=fwrite("HINK0002",1,8,f)!=8;
    bad|=put32(f,count);bad|=put32(f,0);bad|=put32(f,0);
    for(uint32_t i=0;i<count&&!bad;++i) {
        Point p=page.points[from+i];
        unsigned char flags=point_flags(p);
        if(i==0)flags|=1;   /* the reader requires the first point to open a stroke */
        bad|=put32(f,(uint32_t)p.x|((uint32_t)p.y<<16));
        bad|=fputc(flags,f)==EOF;
        bad|=fputc(p.width,f)==EOF;
    }
    if(fflush(f)||fsync(fd))bad=1;
    if(fclose(f))bad=1;
    if(bad||rename(tmp,path))return -1;
    int dfd=open(dir,O_RDONLY|O_DIRECTORY); if(dfd<0)return -1;
    int result=fsync(dfd); close(dfd); return result;
}
/* ---- A real eraser -------------------------------------------------------
 * The eraser used to be a white paintbrush: the ink stayed on the page and in the
 * file sent to Hermes, and the transcription render (straight line segments) left
 * hairline cracks at every joint -- enough for the vision model to read text the
 * user had erased. On the page he erased, 44% of the samples were eraser samples and
 * Hermes was shown 15,427 dark pixels INSIDE the eraser's own path.
 *
 * Now erasing DELETES the ink it covers, in order, so ink written after an erase
 * survives. Two consequences worth keeping: nothing is left for any renderer to
 * show, and the page's point budget is actually freed (which is what he was erasing
 * for). The page as it was before the last erase lives in ink_undo_page, so a single
 * Undo takes an erase back.
 */
static Page *ink_undo_page;                 /* page as it was before the last erase */
static int ink_undo_valid;
static int ink_erase_snapshot(void){
    if(!ink_undo_page){ink_undo_page=malloc(sizeof *ink_undo_page);if(!ink_undo_page)return -1;}
    *ink_undo_page=page;ink_undo_valid=1;return 0;
}
/* Screen mapping that INVERTS move_pen(): ink coordinates are relative to the canvas
 * rect, not the whole screen, so the erase geometry matches what he sees. */
static int ink_screen_x(uint32_t x){return 45+(int)((uint64_t)x*(width-90)/65535u);}
static int ink_screen_y(uint32_t y){return canvas_top+25+(int)((uint64_t)y*(canvas_bottom-canvas_top-50)/65535u);}
static int ink_eraser_radius(void){return (int)(ink_width_for_tool(HERMES_TOOL_ERASER)+1)/2;}
/* Distance from a point to the eraser's polyline, in screen pixels. */
static int ink_segment_distance(int px,int py,int ax,int ay,int bx,int by){
    long dx=(long)bx-ax,dy=(long)by-ay;
    long t=0;
    if(dx*dx+dy*dy>0){
        t=(((long)px-ax)*dx+((long)py-ay)*dy)*1000/(dx*dx+dy*dy);
        if(t<0)t=0; if(t>1000)t=1000;
    }
    long cx=ax+dx*t/1000,cy=ay+dy*t/1000;
    long ex=(long)px-cx,ey=(long)py-cy;
    return (int)isqrt_long(ex*ex+ey*ey);
}
typedef struct { uint32_t first,count; int x0,y0,x1,y1; } InkEraseStroke;
/* Compact `src` into the live page: drop every eraser point, and drop ink covered by
 * an eraser stroke that came BEFORE it. Returns how many points were removed. */
static uint32_t ink_compact_from(const Page *src){
    InkEraseStroke strokes[64];
    int stroke_count=0;
    int radius=ink_eraser_radius();
    for(uint32_t i=0;i<src->count;++i){                 /* pass 1: where are the erasures */
        const Point *p=&src->points[i];
        if(!p->erase)continue;
        int sx=ink_screen_x(p->x),sy=ink_screen_y(p->y);
        int fresh=p->start||stroke_count==0||strokes[stroke_count-1].first+strokes[stroke_count-1].count!=i;
        if(fresh){
            if(stroke_count==(int)(sizeof strokes/sizeof strokes[0]))continue;
            strokes[stroke_count++]=(InkEraseStroke){i,1,sx-radius,sy-radius,sx+radius,sy+radius};
        }else{
            InkEraseStroke *s=&strokes[stroke_count-1];
            ++s->count;
            if(sx-radius<s->x0)s->x0=sx-radius; if(sx+radius>s->x1)s->x1=sx+radius;
            if(sy-radius<s->y0)s->y0=sy-radius; if(sy+radius>s->y1)s->y1=sy+radius;
        }
    }
    uint32_t kept=0,sent_kept=0;
    int previous_dropped=1;                 /* nothing kept yet: the first survivor starts a stroke */
    for(uint32_t i=0;i<src->count;++i){                 /* pass 2: keep only live ink */
        const Point *p=&src->points[i];
        if(p->erase){previous_dropped=1;continue;}
        int sx=ink_screen_x(p->x),sy=ink_screen_y(p->y),half=(p->width+1)/2,covered=0;
        for(int s=0;s<stroke_count&&!covered;++s){
            const InkEraseStroke *stroke=&strokes[s];
            /* Points are stored in the order they were written, so an eraser stroke with
             * HIGHER indices than this ink is the one that erased it. A stroke with
             * lower indices happened before the ink was written: that ink survives. */
            if(stroke->first<=i)continue;
            if(sx+half<stroke->x0||sx-half>stroke->x1||sy+half<stroke->y0||sy-half>stroke->y1)continue;
            uint32_t end=stroke->first+stroke->count;
            for(uint32_t k=stroke->first;k<end&&!covered;++k){
                const Point *e=&src->points[k];
                if(!e->erase)continue;
                if(k==stroke->first||src->points[k].start){
                    if(ink_segment_distance(sx,sy,ink_screen_x(e->x),ink_screen_y(e->y),ink_screen_x(e->x),ink_screen_y(e->y))<=radius+half)covered=1;
                    continue;
                }
                const Point *before=&src->points[k-1];
                if(ink_segment_distance(sx,sy,ink_screen_x(before->x),ink_screen_y(before->y),ink_screen_x(e->x),ink_screen_y(e->y))<=radius+half)covered=1;
            }
        }
        if(covered){previous_dropped=1;continue;}
        /* A surviving point whose predecessor was deleted must open a new stroke, or the
         * renderer draws a line across the gap -- and a page whose first point is not a
         * stroke start is rejected by both this app and the transcriber. */
        Point survivor=*p;
        if(previous_dropped)survivor.start=1;
        page.points[kept++]=survivor;
        if(i<src->sent)++sent_kept;
        previous_dropped=0;
    }
    uint32_t removed=src->count-kept;
    page.count=kept;
    /* The watermark counts points already asked about. Erasing some of them shifts
     * everything after down, so move it by how many asked-about points survived --
     * clamping alone would count fresh ink as already sent. */
    page.sent=sent_kept;
    if(page.sent>page.count)page.sent=page.count;        /* the watermark cannot exceed the ink */
    ink_cache_invalidate();
    return removed;
}
static int load_page(const char *path) {
    FILE *f=fopen(path,"rb"); if(!f)return errno==ENOENT?0:-1;
    Page *next=calloc(1,sizeof *next); if(!next){fclose(f);return -1;}
    char magic[8]; int bad=fread(magic,1,8,f)!=8; int version=0;
    if(!bad&&!memcmp(magic,"HINK0001",8))version=1;
    else if(!bad&&!memcmp(magic,"HINK0002",8))version=2;
    else bad=1;
    if(!bad)bad=get32(f,&next->count)||get32(f,&next->sent)||get32(f,&next->replies);
    if(next->count>MAX_POINTS||next->sent>next->count)bad=1;
    for(uint32_t i=0;i<next->count&&!bad;++i) {
        uint32_t xy; int flags,width_byte,tool,style;
        if(get32(f,&xy)||(flags=fgetc(f))==EOF||flags<0) {bad=1;break;}
        if(version==1) {
            if(flags>3){bad=1;break;}
            tool=(flags&2)?HERMES_TOOL_ERASER:HERMES_TOOL_PEN;style=HERMES_STYLE_BALLPOINT;
            width_byte=(flags&2)?ink_eraser_widths[2]:ink_pen_widths[2];
        } else {
            if((width_byte=fgetc(f))==EOF){bad=1;break;}
            tool=(flags>>2)&7;style=(flags>>5)&7;
            if(tool>HERMES_TOOL_ERASER||style>HERMES_STYLE_PENCIL||width_byte<1||width_byte>100){bad=1;break;}
        }
        next->points[i]=(Point){(uint16_t)(xy&65535u),(uint16_t)(xy>>16),!!(flags&1),!!(flags&2),(uint8_t)tool,(uint8_t)style,(uint8_t)width_byte};
        if(i==0&&!next->points[i].start)bad=1;
    }
    if(!bad&&(fgetc(f)!=EOF||ferror(f)))bad=1;
    fclose(f);
    if(!bad){
        page=*next;
        /* Pages written before the eraser became real still carry eraser strokes and
         * the ink they were meant to remove. Heal them once, on open, and persist. */
        int has_erase=0;
        for(uint32_t i=0;i<page.count;++i)if(page.points[i].erase){has_erase=1;break;}
        if(has_erase){
            if(!ink_erase_snapshot()){ink_compact_from(ink_undo_page);save_page(path);}
            ink_undo_valid=0;                    /* healing an old page is not an undoable action */
        }
        ink_cache_invalidate();
    }
    free(next); return bad?-1:0;
}
static int append_point(int x,int y,int begin,int erase) {
    if(page.count==MAX_POINTS)return -1;
    int tool=erase?HERMES_TOOL_ERASER:product_ink_tool;
    if(tool<HERMES_TOOL_PEN||tool>HERMES_TOOL_ERASER)tool=HERMES_TOOL_PEN;
    int style=product_ink_style;
    if(style<HERMES_STYLE_BALLPOINT||style>HERMES_STYLE_PENCIL)style=HERMES_STYLE_BALLPOINT;
    page.points[page.count++]=(Point){(uint16_t)x,(uint16_t)y,!!begin,!!erase,(uint8_t)tool,(uint8_t)style,(uint8_t)ink_width_for_tool(erase?HERMES_TOOL_ERASER:tool)};return 0;
}
static void undo_stroke(void) {
    if(ink_undo_valid&&ink_undo_page){
        /* One level of undo covers an erase: hand the page back exactly as it was.
         * Deleted ink is NOT kept anywhere else on the device -- hidden copies are
         * what produced ghost text in the first place. */
        page=*ink_undo_page;ink_undo_valid=0;ink_cache_invalidate();return;
    }
    if(page.count<=page.sent)return;
    /* Removing points means the baked cache no longer matches the page; it has
     * to be rebuilt from scratch on the next repaint. */
    ink_cache_invalidate();
    do { --page.count; } while(page.count>page.sent&&!page.points[page.count].start);
}

static void text_at(int x,int y,const char *s,int big) {
    XftDrawStringUtf8(textdraw,&black,big?heading:font,x,y,(const FcChar8 *)s,(int)strlen(s));
}
static void present(int x,int y,int w,int h) {
    /* An override-redirect window is never raised by the WM, so raise ourselves on
     * every push: that keeps us above Home while the WM stays alive to map the lock
     * screen. Nothing is pushed while we are asleep waiting for it, so this cannot
     * cover the system's lock screen. */
    XRaiseWindow(display,window);
    XCopyArea(display,backing,window,gc,x,y,w,h,x,y); XFlush(display);
}
static void status_only(void) {product_repaint();}
/* Style rendering. Each pen must differ by something the eye can resolve on
 * e-ink, not by one pixel of width:
 *   BALLPOINT uniform solid line, the reference pen
 *   FOUNTAIN  width modulated by pen speed (fast = thin, slow = thick), smoothed
 *   FELT      a marker: solid and noticeably wider, so it never reads as a ballpoint
 *   PENCIL    black/white dithered stipple; fine tip, never a solid line
 *             (dithered, not grey -- see the note in main() about A2)
 * The old implementation offset width by 1-3px and used a 2-on/2-off X dash with
 * round caps, which round caps fill in at any real width, so all four pens drew
 * the same stroke. Measured: fountain and felt differed by exactly 1px, and
 * pencil rendered solid. See style-probe.c / dash-probe.c.
 */
/* Fountain: width comes from DIRECTION, not from speed.
 *
 * Speed was tried twice and cannot work on this device. The stylus reports only
 * Abs X and Abs Y -- verified on the Scribe with xinput; there is no pressure or
 * tilt valuator at all -- and it samples at ~1px spacing, so the "speed" between
 * two samples is a single pixel of quantisation noise that changes every sample.
 * Modulating on it produced a width that smoothed to a constant: a dead straight
 * line, which is what the user reported as "the exact same as the regular
 * ballpoint". Real hand speed changes over tens of pixels, so the signal has to
 * be low-passed before it means anything -- and even then it yields "a line whose
 * width drifts", not a pen.
 *
 * Direction is a property of the STORED geometry: stable, identical on every
 * repaint, replayable from a saved page, and needing no input the hardware does
 * not have. A nib held at an angle is thin along its edge and thick across it,
 * which is why real handwriting reads as thick downstrokes and thin horizontals.
 *
 * HORIZ is the width factor moving horizontally (along the nib's thin edge),
 * VERT moving vertically (across it). DIR_SMOOTH is per-cent per sample: the
 * direction is quantised just as coarsely as the step, so it needs a ~16-sample
 * window before it is meaningful.
 *
 * Overridable with -D so a tuning sweep can build the SAME source with different
 * constants (realink-probe.c) instead of editing the values and hoping. */
#ifndef FOUNTAIN_HORIZ
#define FOUNTAIN_HORIZ 0.55
#endif
#ifndef FOUNTAIN_VERT
#define FOUNTAIN_VERT  1.70
#endif
#ifndef FOUNTAIN_DIR_SMOOTH
#define FOUNTAIN_DIR_SMOOTH 6
#endif
#ifndef FOUNTAIN_WIDTH_SMOOTH
#define FOUNTAIN_WIDTH_SMOOTH 0.30
#endif
/* The nib's thin edge never goes below 2px (1px is a hairline e-ink cannot
 * resolve) and the thick edge is always at least GAP_MIN fatter. Without that
 * floor the two edges converge at small bases -- 0.6x and 1.5x of a 2px base are
 * 1px and 3px, i.e. a 2px difference at the width the pen is most often set to,
 * which the user reported twice as "barely noticeable" / "the same as the
 * ballpoint". */
#ifndef FOUNTAIN_THIN_MIN
#define FOUNTAIN_THIN_MIN 2
#endif
#ifndef FOUNTAIN_GAP_MIN
#define FOUNTAIN_GAP_MIN 4
#endif
/* Thresholds are set from the measured pen-sampling distribution on this Scribe,
 * taken from 28711 sample steps across 343 real PEN strokes:  p10 1.0px,
 * p25 2.2px, p50 3.6px, p75 6.0px, p90 9.0px, p99 15.0px. An earlier guess of
 * 3px/12px pinned the p10..p90 range to 3.5px..5.0px (1.5px of visible range at
 * a 4px base) which reads as no modulation at all. 2px/9px spreads the same
 * distribution across 2.0px..5.4px. Re-measure before changing these. */
static int ink_style_width(int style,int base) {
    if(style==HERMES_STYLE_FELT)return base*3/2+3;
    /* Pencil reports its dot size, which is what the eye sees as thickness.
     * Never below 2px: a 1px mark at 62% coverage was invisible on e-ink at the
     * THIN setting and read as "pencil does nothing". */
    if(style==HERMES_STYLE_PENCIL){int d=base/2;return d<2?2:d;}
    return base;
}
/* Position hash, so graphite grain is identical while drawing and on replay. */
static unsigned int dither_hash(int x,int y) {
    unsigned int h=(unsigned int)(x*73856093)^(unsigned int)(y*19349663);
    h^=h>>13;h*=1274126177u;h^=h>>16;
    return h;
}
static void draw_solid(int x,int y,int px,int py,int begin,int line,unsigned long color) {
    XSetForeground(display,gc,color);
    XSetLineAttributes(display,gc,line,LineSolid,CapRound,JoinRound);
    if(begin) XFillArc(display,backing,gc,x-line/2,y-line/2,line,line,0,360*64);
    else XDrawLine(display,backing,gc,px,py,x,y);
}
/* Dither a band of `span` thickness along the segment, using CELL-sized grain at
 * the given coverage. The hash is taken on the CELL coordinates, so the pattern
 * is a function of absolute position: identical on every repaint (no shimmer)
 * and identical when a saved page is replayed.
 *
 * `continuous` guarantees at least one grain per cell-column. A skipped cell in
 * a band one or two cells tall removes the ENTIRE column, and a run of those is
 * a broken line -- the "dots" the pencil was reported as. With continuity the
 * skipped cells stay inside the band, which is what makes texture read as
 * texture. The highlighter does not use it: a translucent band is allowed to be
 * porous. */
static void draw_dither(int x,int y,int px,int py,int begin,int span,int cell,int coverage,int continuous) {
    int dx=begin?0:x-px, dy=begin?0:y-py;
    int steps=(dx<0?-dx:dx)>(dy<0?-dy:dy)?(dx<0?-dx:dx):(dy<0?-dy:dy);
    if(steps<1)steps=1;
    if(cell<1)cell=1;
    if(span<cell)span=cell;
    int half=span/2;
    /* Walk the grain along the stroke's DOMINANT axis and paint the full band at
     * every step. Walking columns only (the old code) starved steep strokes: a
     * segment 1px across and 3px down painted its band at the two endpoint rows
     * and left the rows in between bare, so a mostly-vertical pencil stroke came
     * out as a sparse trail of single pixels -- the "dots" -- while a horizontal
     * one looked fine. */
    int by_row=((dy<0?-dy:dy)>(dx<0?-dx:dx));
    XSetForeground(display,gc,black_pixel);
    int prev_key=-1;
    for(int i=0;i<=steps;++i) {
        int sx=px+(int)((long)dx*i/steps), sy=py+(int)((long)dy*i/steps);
        if(sx<0)sx=0;if(sy<0)sy=0;
        int key=by_row?sy/cell:sx/cell;
        if(key==prev_key)continue;    /* one band per cell travelled */
        prev_key=key;
        int lo,hi;
        if(by_row){lo=sx-half;hi=sx+half;}else{lo=sy-half;hi=sy+half;}
        if(lo<0)lo=0;
        int drawn=0,fallback=lo;unsigned int best=~0u;
        for(int v=lo;v<=hi;v+=cell) {
            int cx=by_row?(v/cell):key, cy=by_row?key:(v/cell);
            unsigned int h=dither_hash(cx,cy);
            if(h%100u>=(unsigned)coverage){
                if(h<best){best=h;fallback=v;}   /* darkest grain in this band */
                continue;
            }
            XFillRectangle(display,backing,gc,cx*cell,cy*cell,cell,cell);
            ++drawn;
        }
        if(!drawn&&continuous) {
            int cx=by_row?(fallback/cell):key, cy=by_row?key:(fallback/cell);
            XFillRectangle(display,backing,gc,cx*cell,cy*cell,cell,cell);
        }
    }
}
/* Integer square root (Newton). The app links without libm, and the fountain's
 * direction maths only needs the magnitude of the smoothed direction vector. */
static long isqrt_long(long v) {
    if (v <= 0) return 0;
    long x = v, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return x;
}
static void draw_segment(int x,int y,int px,int py,int begin,int erase,unsigned int tool,unsigned int style,unsigned int width_value) {
    int line=(int)ink_style_width((int)style,(int)width_value);if(line<1)line=1;
    if(erase||tool!=HERMES_TOOL_PEN) {
        if(!erase&&tool==HERMES_TOOL_HIGHLIGHTER) draw_dither(x,y,px,py,begin,line,HIGHLIGHT_CELL,HIGHLIGHT_COVERAGE,0);
        else draw_solid(x,y,px,py,begin,line,erase?white_pixel:gray_pixel);
        return;
    }
    if(style==HERMES_STYLE_PENCIL) {draw_dither(x,y,px,py,begin,line,PENCIL_CELL,PENCIL_COVERAGE,1);return;}
    if(style==HERMES_STYLE_FOUNTAIN) {
        int dx=x-px, dy=y-py;
        /* On begin, px/py is the previous STROKE's last point, so x-px is a jump
         * across the page and seeding the direction from it would leave every
         * stroke's opening ~16 samples pointing the wrong way. Start at zero and
         * seed from the first real step instead; 15% of samples are duplicate
         * points, so seeding waits for a genuinely non-zero step. */
        if(begin) { fountain_dirx=0; fountain_diry=0; }
        else if(fountain_dirx==0&&fountain_diry==0) {
            fountain_dirx=(long)dx*1000; fountain_diry=(long)dy*1000;
        } else {
            fountain_dirx+=((long)dx*1000-fountain_dirx)*FOUNTAIN_DIR_SMOOTH/100;
            fountain_diry+=((long)dy*1000-fountain_diry)*FOUNTAIN_DIR_SMOOTH/100;
        }
        long v=fountain_dirx*fountain_dirx+fountain_diry*fountain_diry;
        long mag=isqrt_long(v);
        long vertical=(mag>0)?(labs(fountain_diry)*1000/mag):500;   /* |dy|/len, 0..1000 */
        /* The two edges are computed, then floored. A 0.6x factor on the FINE base
         * (2px) is 1px: a hairline e-ink cannot resolve, so the nib angle vanishes
         * exactly at the setting a thin pen gets used at. Flooring the EDGES rather
         * than the base keeps FINE and THIN distinct in their thick edge. */
        int thin_edge=(int)((double)line*FOUNTAIN_HORIZ+0.5);  if(thin_edge<FOUNTAIN_THIN_MIN)thin_edge=FOUNTAIN_THIN_MIN;
        int thick_edge=(int)((double)line*FOUNTAIN_VERT+0.5);  if(thick_edge<thin_edge+FOUNTAIN_GAP_MIN)thick_edge=thin_edge+FOUNTAIN_GAP_MIN;
        double target=thin_edge+(thick_edge-thin_edge)*(double)vertical/1000.0;
        if(begin||fountain_width<=0.0)fountain_width=target;
        else fountain_width+=(target-fountain_width)*FOUNTAIN_WIDTH_SMOOTH;
        line=(int)(fountain_width+0.5);if(line<1)line=1;
    }
    draw_solid(x,y,px,py,begin,line,black_pixel);
}
static int pixel_x(Point p) {return 45+(int)((uint32_t)p.x*(width-90)/65535);}
static int pixel_y(Point p) {return canvas_top+25+(int)((uint32_t)p.y*(canvas_bottom-canvas_top-50)/65535);}
static void store_stroke(void);
static void product_save_reply_current(void);
/* ---------------------------------------------------------------- *
 * Rendered-ink cache
 *
 * product_draw_page() used to replay EVERY stored point on every repaint, so a
 * repaint cost O(page). A repaint happens on every menu tap, view change and
 * pen-up, and a page is capped at 120000 points -- measured at ~2.6us per point
 * on a fast x86 host, and the Scribe's ARM is far slower. That is why a page
 * with a lot of ink made the whole app feel laggy: every interaction rebuilt the
 * notebook from scratch.
 *
 * Completed strokes are now baked ONCE into a pixmap. A repaint blits that and
 * then draws only the stroke still under the pen, so its cost is the current
 * stroke rather than the whole page. Baking is append-only (points[i] never
 * changes once written), which is why anything that removes or replaces points
 * must call ink_cache_invalidate().
 * ---------------------------------------------------------------- */
static Pixmap ink_cache = 0;
static uint32_t ink_cache_count = 0;
static int ink_cache_ready = 0;

static void ink_cache_invalidate(void){ ink_cache_ready=0; ink_cache_count=0; }

static void ink_cache_bake(uint32_t upto){
    if(upto>page.count)upto=page.count;
    if(!ink_cache)
        ink_cache=XCreatePixmap(display,window,width,height,DefaultDepth(display,DefaultScreen(display)));
    if(!ink_cache_ready){
        XSetForeground(display,gc,white_pixel);
        XFillRectangle(display,ink_cache,gc,0,0,width,height);
        ink_cache_count=0;ink_cache_ready=1;
    }
    if(upto<=ink_cache_count)return;
    int px=0,py=0;
    if(ink_cache_count){Point q=page.points[ink_cache_count-1];px=pixel_x(q);py=pixel_y(q);}
    Pixmap target=backing;
    backing=ink_cache;                 /* draw_segment draws to the global backing */
    for(uint32_t i=ink_cache_count;i<upto;++i){
        Point pt=page.points[i];int x=pixel_x(pt),y=pixel_y(pt);
        draw_segment(x,y,px,py,pt.start,pt.erase,pt.tool,pt.style,pt.width);
        px=x;py=y;
    }
    XSync(display,False);
    backing=target;
    ink_cache_count=upto;
}

/* Index of the first point of the stroke currently under the pen: everything
 * before it is complete and can be baked. */
static uint32_t product_open_stroke_start(void){
    for(uint32_t i=page.count;i>0;--i) if(page.points[i-1].start) return i-1;
    return 0;
}

/* panel_refresh() is defined below but called from the app layer (product-ui.h), so it
 * has to be declared before those headers are included. */
static void panel_refresh(void);
#include "device-files.h"
#include "product-ui.h"
static void repaint(void) {product_repaint();}
static void store_stroke(void) {
    if(!dirty)return;
    if(save_page(state_path))snprintf(status,sizeof status,"SAVE FAILED - keep this window open.");
    else {dirty=0;snprintf(status,sizeof status,"Ink saved on this Scribe.");}
    if(dirty)status_only();
}
/* ---- Clearing the panel ghost -------------------------------------------------
 * E-ink keeps a faint image of whatever sat under a white-out, and erasing IS
 * white-out, so the erased text lingers on the panel as a ghost until the display
 * does a full refresh -- which is why the framework's own apps flash the screen
 * periodically. A full-screen XClearArea makes the X server repaint the entire
 * window (our Expose handler re-presents the backing pixmap), and a whole-screen
 * repaint is what earns a full refresh from the panel driver.
 * Rate limited: a run of eraser strokes must not strobe the display.
 */
static double panel_refresh_next;
static void panel_refresh(void){
    if(seconds()<panel_refresh_next)return;
    panel_refresh_next=seconds()+2.5;
    XClearArea(display,window,0,0,width,height,True);
    XFlush(display);
}
static void finish_stroke(void) {
    /* An eraser stroke is not ink to be remembered: the ink it covered goes away for
     * good, right here, so nothing downstream can ever show it again. */
    if(stroke_erasing){
        if(!ink_erase_snapshot())ink_compact_from(ink_undo_page);
        dirty=1;
        panel_refresh();          /* and wipe the ghost the white-out left on the panel */
    }
    if(dirty)store_stroke();
}
static void move_pen(int x,int y,int erase) {
    if(x<45||x>width-45||y<canvas_top+25||y>canvas_bottom-25) {stroke_started=0;return;}
    if(!stroke_started)ink_undo_valid=0;     /* a new stroke ends the erase's one-level undo */
    int nx=(int)((long)(x-45)*65535/(width-90));
    int ny=(int)((long)(y-canvas_top-25)*65535/(canvas_bottom-canvas_top-50));
    if(append_point(nx,ny,!stroke_started,erase)) {
        /* A full page used to disable drawing SILENTLY. The only signal was one
         * small footer line, easily missed, so the app looked broken -- "nothing
         * happens when I draw" -- while it was in fact refusing by design, and
         * with 120000 points already on the page every repaint was also slow.
         * Roll the page over instead: archive it and keep writing. */
        drawing=0;store_stroke();
        if(product_view==PRODUCT_TODAY)daily_turn();else daily_clear_page();
        product_repaint();
        return;
    }
    Point point=page.points[page.count-1];
    draw_segment(x,y,last_x,last_y,!stroke_started,erase,point.tool,point.style,point.width);
    int pad=42, left=stroke_started&&last_x<x?last_x:x, top=stroke_started&&last_y<y?last_y:y;
    int right=stroke_started&&last_x>x?last_x:x, bottom=stroke_started&&last_y>y?last_y:y;
    left-=pad;top-=pad;right+=pad;bottom+=pad;
    if(left<0)left=0;if(top<0)top=0;if(right>width)right=width;if(bottom>height)bottom=height;
    present(left,top,right-left,bottom-top);
    last_x=x;last_y=y;stroke_started=1;dirty=1;
}
static int button_at(int x,int y) {return product_button_at(x,y);}
static void activate(int button) {product_activate(button);}
static void touch_reset(void) {
    touch_active=0;touch_contact=-1;touch_button=-1;touch_x=touch_y=0;touch_start_x=-1;
    touch_position_state=(PenPosition){0};touch_kinds=touch_moves=touch_placed=0;
}
static void handle_touch_event(const XIDeviceEvent *p) {
    if(p->sourceid!=touch_id)return;
    int begin=p->evtype==XI_TouchBegin||(p->evtype==XI_ButtonPress&&p->detail==1);
    /* The Scribe may deliver a finger as an emulated pointer: Press, Motion, Release. */
    int update=p->evtype==XI_TouchUpdate||p->evtype==XI_Motion;
    int end=p->evtype==XI_TouchEnd||(p->evtype==XI_ButtonRelease&&p->detail==1);
    if(!begin&&!update&&!end)return;
    if(p->evtype==XI_TouchBegin) {
        if(touch_active)return;
        touch_active=1;touch_contact=p->detail;touch_button=-1;touch_position_state=(PenPosition){0};
    } else if(p->evtype==XI_TouchUpdate) {
        if(!touch_active||touch_contact<0||p->detail!=touch_contact)return;
    } else if(p->evtype==XI_Motion) {
        if(!touch_active||touch_contact>=0)return;
    } else if(p->evtype==XI_TouchEnd) {
        if(!touch_active||touch_contact<0||p->detail!=touch_contact)return;
    } else if(p->evtype==XI_ButtonPress) {
        if(touch_active)return;
        touch_active=1;touch_contact=-1;touch_button=-1;touch_position_state=(PenPosition){0};
    } else if(p->evtype==XI_ButtonRelease) {
        if(!touch_active)return;
    }
    int x=touch_x,y=touch_y;
    int positioned=touch_position(&touch_position_state,p,touch_id,width,height,&x,&y);
    touch_kinds|=1<<(p->evtype&15);
    if(begin) {
        touch_x=x;touch_y=y;touch_button=positioned?button_at(x,y):-1;
        if(positioned){touch_start_x=x;touch_start_y=y;touch_start_at=seconds();}
        return;
    }
    if(update) {
        ++touch_moves;touch_placed+=positioned;
        if(positioned){touch_x=x;touch_y=y;if(touch_start_x<0){touch_start_x=x;touch_start_y=y;touch_start_at=seconds();}}
        return;
    }
    if(end) {
        if(positioned){touch_x=x;touch_y=y;}
        /* A sideways swipe is a gesture, not a tap on whatever it started over. */
        int swipe=daily_enabled&&!drawing&&touch_start_x>=0?daily_swipe(touch_x-touch_start_x,touch_y-touch_start_y,seconds()-touch_start_at):0;
        /* One line per finger stroke long enough to be a swipe: taps stay silent. */
        if(touch_moves>0&&touch_start_x>=0&&abs(touch_x-touch_start_x)>=40)
            fprintf(stderr,"swipe: dx=%d dy=%d %.2fs kinds=0x%x moves=%d placed=%d end=%d view=%d sheet=%d/%d drawing=%d -> %d\n",
                    touch_x-touch_start_x,touch_y-touch_start_y,seconds()-touch_start_at,touch_kinds,touch_moves,touch_placed,
                    p->evtype,(int)product_view,daily_sheet+1,daily_sheets,drawing,swipe);
        touch_kinds=touch_moves=touch_placed=0;
        if(swipe){touch_reset();if(swipe!=DAILY_NOOP)activate(swipe);return;}
        int hit=positioned?button_at(x,y):touch_button;
        int matches=touch_button>=0&&(!positioned||hit==touch_button);
        double held=touch_start_x>=0?seconds()-touch_start_at:0,pen_ago=seconds()-last_pen_at;
        if(matches&&daily_palm_guarded(touch_button)&&(held>PALM_HOLD_S||pen_ago<PALM_PEN_S)){
            fprintf(stderr,"palm: ignored touch on %d (held %.2fs, pen %.1fs ago)\n",touch_button,held,pen_ago);
            touch_reset();return;
        }
        if(matches)activate(touch_button);
        touch_reset();
    }
}
static int self_test(void) {
    char dir[]="/tmp/hermes-ink-test-XXXXXX",path[256];
    if(!mkdtemp(dir))return 1;snprintf(path,sizeof path,"%s/page",dir);state_path=path;
    #define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
    PenPosition ps={0};int tx=-1,ty=-1;
    unsigned char pm[1]={3};double pv[3]={1121,1166,999};
    XIDeviceEvent pe={0};pe.sourceid=7;pe.deviceid=7;pe.evtype=XI_Motion;
    pe.event_x=442;pe.event_y=937;pe.time=1000;
    pe.valuators=(XIValuatorState){1,pm,pv};
    CHECK(pen_position(&ps,&pe,7,1860,2480,&tx,&ty)&&tx==1121&&ty==1166);
    pm[0]=0;pe.evtype=XI_ButtonPress;pe.time=1001;
    CHECK(pen_position(&ps,&pe,7,1860,2480,&tx,&ty)&&tx==1121&&ty==1166);
    pe.evtype=XI_ButtonRelease;pe.time=1002;
    CHECK(pen_position(&ps,&pe,7,1860,2480,&tx,&ty)&&tx==1121&&ty==1166);
    pe.time=2000;CHECK(!pen_position(&ps,&pe,7,1860,2480,&tx,&ty));
    pe.evtype=XI_Motion;pm[0]=1;pv[0]=100;
    CHECK(!pen_position(&ps,&pe,7,1860,2480,&tx,&ty));
    pm[0]=2;pv[0]=200;pe.time=2001;
    CHECK(pen_position(&ps,&pe,7,1860,2480,&tx,&ty)&&tx==100&&ty==200);
    pe.sourceid=6;pm[0]=3;pv[0]=1700;pv[1]=2300;
    CHECK(!pen_position(&ps,&pe,7,1860,2480,&tx,&ty)&&ps.x==100&&ps.y==200);
    pe.sourceid=7;pm[0]=7;pv[0]=NAN;
    CHECK(!pen_position(&ps,&pe,7,1860,2480,&tx,&ty)&&ps.known==0);
    pv[0]=1860;CHECK(!pen_position(&ps,&pe,7,1860,2480,&tx,&ty));
    pv[0]=1121;pv[1]=1166;
    CHECK(pen_position(&ps,&pe,7,1860,2480,&tx,&ty)&&tx==1121&&ty==1166);
    ps=(PenPosition){0};pm[0]=0;pe.evtype=XI_ButtonPress;
    CHECK(!pen_position(&ps,&pe,7,1860,2480,&tx,&ty));
    hardware_eraser=0;
    struct input_event tool={.type=EV_KEY,.code=BTN_TOOL_RUBBER,.value=1};
    apply_stylus_tool_event(&tool);CHECK(hardware_eraser==1);
    tool.value=0;apply_stylus_tool_event(&tool);CHECK(hardware_eraser==0);
    puts("PASS: palm-coordinate regression, button coordinates, stale/missing axes, packed axes, non-pen rejection, invalid coordinates, focus reset");
    width=1860;height=2480;canvas_top=270;canvas_bottom=2290;
    /* The eraser now DELETES what it covers: nothing is left for any renderer to show,
     * the page's point budget is freed, and ink written after an erase survives. */
    {
        Page *kept_buffer=ink_undo_page;
        int kept_valid=ink_undo_valid;
        ink_undo_page=0;ink_undo_valid=0;
        memset(&page,0,sizeof page);
        ink_undo_page=malloc(sizeof *ink_undo_page);
        CHECK(ink_undo_page!=0);
        /* four ink points in a row, then an eraser stroke across the third one */
        uint32_t xs[4]={10000,20000,30000,40000};
        page.count=0;
        for(int i=0;i<4;++i)page.points[page.count++]=(Point){(uint16_t)xs[i],30000,1,0,0,0,4};
        page.sent=2;page.replies=1;
        uint32_t before=page.count;
        page.points[page.count++]=(Point){30000,30000,1,1,2,0,38};      /* eraser start */
        page.points[page.count++]=(Point){30000,30100,0,1,2,0,38};      /* eraser drag */
        uint32_t with_erase=page.count;
        CHECK(ink_erase_snapshot()==0);
        uint32_t removed=ink_compact_from(ink_undo_page);
        CHECK(removed==3);                       /* the eraser's own two points + the ink it covered */
        CHECK(page.count==3);
        int still_there=0,covered_survived=0,others_kept=0;
        for(uint32_t i=0;i<page.count;++i){
            if(page.points[i].erase)still_there=1;
            if(page.points[i].x==30000)covered_survived=1;
            if(page.points[i].x==10000||page.points[i].x==20000||page.points[i].x==40000)++others_kept;
        }
        CHECK(page.points[0].start==1);          /* the page still opens: first point opens a stroke */
        CHECK(!still_there);                     /* no eraser points are kept */
        CHECK(!covered_survived);                /* the ink the eraser covered is gone for good */
        CHECK(others_kept==3);                   /* and the rest is untouched */
        CHECK(page.sent==2&&page.replies==1);    /* the send watermark is preserved */
        /* ink written AFTER the erase is safe: it was not there to be erased */
        page.points[page.count++]=(Point){30000,30050,1,0,0,0,4};
        ink_undo_valid=1;*ink_undo_page=page;
        ink_compact_from(ink_undo_page);
        CHECK(page.points[page.count-1].x==30000&&page.count==4);
        /* Undo puts the page back exactly as it was, then is spent (one level). */
        page=*ink_undo_page;ink_undo_valid=0;
        CHECK(page.count==4);
        CHECK(!ink_undo_valid);
        free(ink_undo_page);ink_undo_page=kept_buffer;ink_undo_valid=kept_valid;
        memset(&page,0,sizeof page);
        (void)before;(void)with_erase;
    }
    /* Erasing while an answer is in flight must not persist a page the loader refuses.
     * An ask snapshots the watermark (page.sent) when it is sent; if the writer erases before
     * the answer lands, ink_compact_from() shrinks page.count under that watermark. Saving the
     * stale, larger value would be rejected by load_page(), locking the writer out of their notebook. */
    {
        Page *kept_buffer=ink_undo_page;
        int kept_valid=ink_undo_valid;
        ink_undo_page=0;ink_undo_valid=0;
        memset(&page,0,sizeof page);
        /* 40 pen points: a cluster of 20 the eraser will trace over, and 20 far away that
         * it cannot reach -- so the compaction provably has something to keep. */
        for(int i=0;i<20;++i)page.points[page.count++]=(Point){(uint16_t)(10000+i*50),(uint16_t)30000,1,0,0,0,4};
        for(int i=0;i<20;++i)page.points[page.count++]=(Point){(uint16_t)(40000+i*50),(uint16_t)30000,1,0,0,0,4};
        page.sent=page.count;page.replies=0;
        uint32_t cutoff=page.count;                  /* the watermark the ask captured */
        /* The writer erases the first cluster while the answer is in flight. */
        for(int i=0;i<20;++i)page.points[page.count++]=(Point){(uint16_t)(10000+i*50),(uint16_t)30000,i==0,1,2,0,28};
        stroke_erasing=1;
        CHECK(ink_erase_snapshot()==0);
        CHECK(ink_compact_from(ink_undo_page)==40);  /* the 20 eraser points + the 20 they covered */
        stroke_erasing=0;
        CHECK(page.count==20);                       /* the page really did shrink under the cutoff */
        page.sent=cutoff;                            /* the reply lands and acks that stale cutoff */
        CHECK(save_page(path)==0);
        CHECK(page.sent<=page.count);                /* the writer clamps the watermark to the ink */
        CHECK(load_page(path)==0);                   /* so the page still opens: no lockout */
        CHECK(page.count==20);
        free(ink_undo_page);ink_undo_page=kept_buffer;ink_undo_valid=kept_valid;
        memset(&page,0,sizeof page);
        unlink(path);
    }
    /* Powerd event lines, exactly as the device emits them (from app.log on the
     * Scribe). The reader must map these without blocking on the pipe: the first
     * version used fgets() on a pipe that is empty almost always, which froze the
     * whole UI the moment powerd went quiet. */
    CHECK(power_line_action("goingToScreenSaver 2")==POWER_LINE_SLEEP);
    CHECK(power_line_action("outOfScreenSaver 1")==POWER_LINE_WAKE);
    CHECK(power_line_action("exitingScreenSaver")==POWER_LINE_WAKE);
    CHECK(power_line_action("battLevelChanged")==POWER_LINE_NONE);
    CHECK(power_line_action("")==POWER_LINE_NONE);
    sleeping=0;
    {
        /* The event pipe must be non-blocking: a read with nothing to read has to
         * return immediately, not park the loop. */
        int fds[2];
        CHECK(pipe(fds)==0);
        CHECK(fcntl(fds[0],F_SETFL,fcntl(fds[0],F_GETFL,0)|O_NONBLOCK)==0);
        char scratch[8];
        CHECK(read(fds[0],scratch,sizeof scratch)<0&&(errno==EAGAIN||errno==EWOULDBLOCK));
        close(fds[0]);close(fds[1]);
    }
    CHECK(load_page(path)==0);CHECK(page.count==0);
    CHECK(append_point(1,2,1,0)==0);CHECK(append_point(3,4,0,0)==0);
    CHECK(save_page(path)==0);memset(&page,0,sizeof page);CHECK(load_page(path)==0);
    CHECK(page.count==2&&page.points[1].x==3&&page.points[1].y==4);
    page.sent=page.count;CHECK(save_page(path)==0);                /* sent ink is never undone */
    undo_stroke();CHECK(page.count==2);
    CHECK(append_point(5,6,1,1)==0);CHECK(append_point(7,8,0,1)==0);
    undo_stroke();CHECK(page.count==2);CHECK(append_point(9,10,1,0)==0);
    state_path="/nonexistent/hermes-demo/page";CHECK(save_page(state_path)==-1);   /* a failed save reports it */
    state_path=path;CHECK(save_page(path)==0);memset(&page,0,sizeof page);CHECK(load_page(path)==0);
    CHECK(page.count==3&&page.sent==2);
    FILE *f=fopen(path,"ab");CHECK(f!=NULL);fputc('x',f);fclose(f);
    CHECK(load_page(path)==-1);CHECK(page.count==3);
    page.count=MAX_POINTS;CHECK(append_point(1,1,1,0)==-1);
    unlink(path);rmdir(dir);puts("PASS: save/reload, undo boundary, save failure, malformed state, ink bound");return 0;
}
/* Detach a forked child from the framework's view of this document.
 *
 * The Kindle framework launches the launcher script and treats it as finished when the
 * pipe it holds to that process reports EOF. Anything we leave running inherits that pipe
 * and keeps the framework believing the document is still open -- which is what produces
 * an empty grey page with a close button instead of the library, until the screen is
 * tapped. Leaving the session and dropping the inherited descriptors is the whole fix:
 * the app used to work because nothing outlived it. */
static void detach_from_document(void) {
    setsid();                      /* best effort: fails if we are already a group leader */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}
/* ---- Handing the panel back ------------------------------------------------
 * Nothing on this device pushes framebuffer content onto the e-ink PANEL except
 * fbink_xdamage: it is what turns X damage into a panel refresh, and it does one
 * full-screen refresh when it starts. So a quit has to leave one behind, or the
 * Scribe keeps its last blank panel image while the Kindle UI sits alive and
 * correct underneath, until the next power cycle -- reported three times, twice
 * with the launcher's own restore already attempting it (that path stopped the
 * refresh bridge before repainting, which strands the repaint in the framebuffer).
 * Doing it here means the app hands the screen back no matter who launched it, even
 * if the copy of the launcher on the device is older than the one in the repo. */
static void hand_back_screen(void) {
    fprintf(stderr,"handing the panel back (blank unmapped, refresh bridge for 8s)\n");
    pid_t child=fork();
    if(child<0)return;
    if(child==0){
        detach_from_document();
        char cmd[1280];
        if(snprintf(cmd,sizeof cmd,
            /* Leave the document the way the Home button does. The library opened this
             * launcher as a file document (appmgrd: activeApp=tech.hackerdude.
             * shell_integration.launcher) and, being a plain X client, nothing ever tells
             * the framework the document is finished -- so it sat on an empty document
             * view, chrome drawn and content blank, until the screen was tapped. peekApp
             * HistoryURI names the way out: app://com.lab126.KPPMainApp?view=KPP_HOME,
             * i.e. `start` the library app on its home view. Verified through the
             * run.cmd harness first, and NOT appmgrd close, which deletes the launcher
             * file (see 2026-09-16). */
            "lipc-set-prop com.lab126.appmgrd start \"app://com.lab126.KPPMainApp?view=KPP_HOME\" 2>/dev/null;"
            /* Screen dumps are evidence, not a feature: they happen only while
             * hermes-native-live/dump-on-exit exists, a file created or removed over USB. */
            "[ -e %s/dump-on-exit ] && /usr/bin/xwd -root -silent -out %s/exit-now.xwd 2>/dev/null;"
            "lipc-set-prop com.lab126.pillow disableEnablePillow enable 2>/dev/null;"
            "/usr/bin/xrefresh -d :0.0 2>/dev/null;"
            "sleep 2;"
            "[ -e %s/dump-on-exit ] && /usr/bin/xwd -root -silent -out %s/exit-after2s.xwd 2>/dev/null;"
            /* The bridge is what pushes framebuffer content onto the panel; it also
             * watches X damage, so later redraws reach the panel too. */
            "( env DISPLAY=:0 timeout 12 %s/fbink_xdamage >>%s/fbink-xdamage.log 2>&1 & );"
            "sleep 4;"
            "[ -e %s/dump-on-exit ] && /usr/bin/xwd -root -silent -out %s/exit-after6s.xwd 2>/dev/null",
            live_dir,live_dir,live_dir,live_dir,live_dir,live_dir,live_dir,live_dir)>=(int)sizeof cmd)_exit(127);
        execl("/bin/sh","sh","-c",cmd,(char*)NULL);
        _exit(127);
    }
}
int main(int argc,char **argv) {
    int probe=argc>1&&!strcmp(argv[1],"--probe");
    if(argc>1&&!strcmp(argv[1],"--self-test"))return self_test();
    state_path=getenv("HERMES_INK_STATE");if(!state_path)state_path="/mnt/us/hermes-native-live/page.ink";
    display=XOpenDisplay(NULL);if(!display){fprintf(stderr,"Cannot open native X display\n");return 1;}
    int screen=DefaultScreen(display);width=DisplayWidth(display,screen);height=DisplayHeight(display,screen);
    printf("screen=%dx%d depth=%d\n",width,height,DefaultDepth(display,screen));
    int event,error,major=2,minor=0;
    if(!XQueryExtension(display,"XInputExtension",&xi_opcode,&event,&error)||XIQueryVersion(display,&major,&minor)!=Success){fprintf(stderr,"XInput2 unavailable\n");return 1;}
    int n=0;XIDeviceInfo *devices=XIQueryDevice(display,XIAllDevices,&n);
    for(int i=0;i<n;++i){printf("input id=%d name=%s use=%d\n",devices[i].deviceid,devices[i].name,devices[i].use);if(!strcmp(devices[i].name,"stylus"))pen_id=devices[i].deviceid;if(!strcmp(devices[i].name,"multitouch"))touch_id=devices[i].deviceid;}
    XIFreeDeviceInfo(devices);
    if(probe){XCloseDisplay(display);return pen_id<0?2:0;}
    if(!getenv("HERMES_DAILY_FILE")){fprintf(stderr,"HERMES_DAILY_FILE is not set: the launcher passes today's bundle\n");return 1;}
    if(pen_id<0){fprintf(stderr,"No known stylus device; refusing finger-only fallback\n");return 1;}
    if(width<900||height<1400||width>=height){fprintf(stderr,"First prototype requires portrait layout\n");return 1;}
    if(product_prepare_storage()){fprintf(stderr,"Could not prepare Hermes workspace; existing notes were not changed\n");return 1;}
    if(pen_id>=0) {
        stylus_event_fd=open_stylus_event_device();
        if(stylus_event_fd>=0)fprintf(stderr,"hardware eraser: direct evdev tool detection enabled\n");
        else fprintf(stderr,"hardware eraser: evdev tool detection unavailable; toolbar eraser remains available\n");
    }
    int lock=-1;
    {
        char lockpath[1024];const char *lock_target=product_root;
        if(snprintf(lockpath,sizeof lockpath,"%s.lock",lock_target)>=(int)sizeof lockpath)return 1;
        lock=open(lockpath,O_CREAT|O_RDWR,0600);
        if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){fprintf(stderr,"State unavailable or prototype already open\n");return 1;}
        fcntl(lock,F_SETFD,FD_CLOEXEC);
        if(load_page(state_path)){fprintf(stderr,"State damaged; preserved without overwrite\n");return 1;}
    }
    white_pixel=WhitePixel(display,screen);black_pixel=BlackPixel(display,screen);gray_pixel=black_pixel;pencil_pixel=black_pixel;
    window=XCreateSimpleWindow(display,RootWindow(display,screen),0,0,width,height,0,black_pixel,white_pixel);
    /* Override-redirect: the Kindle window manager will not manage this window, so
     * it cannot put Home's surface above us. That replaces our old trick of freezing
     * the WM (killall -STOP awesome), which also froze the component that maps the
     * system's lock screen -- and the lock screen is exactly what we now want. Set
     * before the window is mapped, as the server requires. */
    {XSetWindowAttributes or_attrs;or_attrs.override_redirect=True;
     XChangeWindowAttributes(display,window,CWOverrideRedirect,&or_attrs);}
    XStoreName(display,window,TITLE);
    XClassHint hints={"hermes-inkprototype","HermesInkPrototype"};XSetClassHint(display,window,&hints);
    Atom close_atom=XInternAtom(display,"WM_DELETE_WINDOW",False);XSetWMProtocols(display,window,&close_atom,1);
    XSelectInput(display,window,ExposureMask|StructureNotifyMask|FocusChangeMask);
    {unsigned char mask[XIMaskLen(XI_LASTEVENT)]={0};XISetMask(mask,XI_ButtonPress);XISetMask(mask,XI_ButtonRelease);XISetMask(mask,XI_Motion);XISetMask(mask,XI_TouchBegin);XISetMask(mask,XI_TouchUpdate);XISetMask(mask,XI_TouchEnd);XIEventMask masks[2];int mask_count=0;if(pen_id>=0)masks[mask_count++]=(XIEventMask){pen_id,sizeof mask,mask};if(touch_id>=0)masks[mask_count++]=(XIEventMask){touch_id,sizeof mask,mask};XISelectEvents(display,window,masks,mask_count);}
    gc=XCreateGC(display,window,0,NULL);
    backing=XCreatePixmap(display,window,width,height,DefaultDepth(display,screen));
    textdraw=XftDrawCreate(display,backing,DefaultVisual(display,screen),DefaultColormap(display,screen));
    font=XftFontOpenName(display,screen,"sans:pixelsize=34");heading=XftFontOpenName(display,screen,"sans:bold:pixelsize=54");small=XftFontOpenName(display,screen,"sans:pixelsize=27");
    if(!textdraw||!font||!heading||!small||!XftColorAllocName(display,DefaultVisual(display,screen),DefaultColormap(display,screen),"black",&black)||!XftColorAllocName(display,DefaultVisual(display,screen),DefaultColormap(display,screen),"white",&white_text)){fprintf(stderr,"Font setup failed\n");return 1;}
    XColor gray_color;if(XParseColor(display,DefaultColormap(display,screen),"gray70",&gray_color)&&XAllocColor(display,DefaultColormap(display,screen),&gray_color))gray_pixel=gray_color.pixel;
    /* Pencil is deliberately NOT a grey. WFM_A2 (the waveform used while the pen
     * is moving) is monochrome-only, and on Kindle MTK the driver sets
     * EPDC_FLAG_FORCE_MONOCHROME, which posterizes the region at a 50%
     * threshold. gray50 is 128/255 -- exactly that threshold -- so it collapsed
     * and the pencil vanished. Dithering pure black over white is 1-bit safe
     * under every waveform and the eye integrates it back to grey. */
    pencil_pixel=black_pixel;
    canvas_top=430;canvas_bottom=height*60/100;
    hermes_hand_open(screen);ask_queue_load();daily_enter_today();
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);signal(SIGHUP,on_signal);
    signal(SIGUSR1,on_reopen);
    power_watch_start();
    power_key_fd=open_power_key_device();
    if(power_key_fd<0)fprintf(stderr,"power key: no bd71828-pwrkey device; sleep screen follows powerd events only\n");
    XMapRaised(display,window);request_fullscreen_above();XFlush(display);repaint();
    while(!quitting) {
        drain_stylus_tool_events();
        while(XPending(display)) {
            XEvent e;XNextEvent(display,&e);
            if(e.type==Expose&&e.xexpose.count==0)present(0,0,width,height);
            if(e.type==ClientMessage&&(Atom)e.xclient.data.l[0]==close_atom)quitting=1;
            if(e.type==FocusOut){drawing=0;stroke_started=0;hardware_eraser=0;pen_button=-1;pen_position_state=(PenPosition){0};touch_reset();finish_stroke();stroke_erasing=0;}
            if(e.type==GenericEvent&&e.xcookie.extension==xi_opcode&&XGetEventData(display,&e.xcookie)) {
                XIDeviceEvent *p=e.xcookie.data;
                if(p->sourceid==pen_id||(touch_id>=0&&p->sourceid==touch_id))wake_on_input();
                if(p->sourceid==pen_id) {
                    last_pen_at=seconds();
                    int x=0,y=0;
                    int positioned=pen_position(&pen_position_state,p,pen_id,width,height,&x,&y);
                    if(positioned&&(fabs(p->event_x-x)>2||fabs(p->event_y-y)>2))++pen_coordinate_corrections;
                    if(e.xcookie.evtype==XI_ButtonPress&&p->detail==1){drain_stylus_tool_events();stroke_erasing=erasing||hardware_eraser;pen_button=positioned?button_at(x,y):-1;drawing=positioned&&pen_button<0&&product_can_draw_at(x,y);stroke_on_blank=drawing&&!page.count&&!answer_count;stroke_started=0;if(drawing)move_pen(x,y,stroke_erasing);}
                    if(e.xcookie.evtype==XI_Motion&&drawing){if(positioned)move_pen(x,y,stroke_erasing);else stroke_started=0;}
                    /* Always finish on pen-up, even if its coordinates are missing. */
                    if(e.xcookie.evtype==XI_ButtonRelease&&p->detail==1){
                        int pressed_button=pen_button;
                        int release_matches=positioned&&pressed_button>=0&&button_at(x,y)==pressed_button;
                        drawing=0;stroke_started=0;finish_stroke();stroke_erasing=0;
                        if(stroke_on_blank&&page.count)daily_first_stroke();stroke_on_blank=0;
                        /* Scribe release events may omit axes or report an invalid shared-pointer coordinate.
                         * The pen-down hit test is authoritative for a control in that case. */
                        if(pressed_button>=0&&(!positioned||release_matches))activate(pressed_button);
                        pen_button=-1;if(pen_coordinate_corrections){fprintf(stderr,"pen input: ignored %u shared-pointer coordinate discrepancies\n",pen_coordinate_corrections);pen_coordinate_corrections=0;}
                    }
                } else if(touch_id>=0&&p->sourceid==touch_id) {
                    handle_touch_event(p);
                }
                XFreeEventData(display,&e.xcookie);
            }
        }
        if(reopen_requested){reopen_requested=0;XMapRaised(display,window);XSetInputFocus(display,window,RevertToParent,CurrentTime);XFlush(display);repaint();}
        power_key_drain();
        power_watch_tick();
        if(!drawing&&!sleeping)daily_tick();
        fd_set fds;FD_ZERO(&fds);int maxfd=ConnectionNumber(display);FD_SET(maxfd,&fds);
        if(stylus_event_fd>=0){FD_SET(stylus_event_fd,&fds);if(stylus_event_fd>maxfd)maxfd=stylus_event_fd;}
        if(power_key_fd>=0){FD_SET(power_key_fd,&fds);if(power_key_fd>maxfd)maxfd=power_key_fd;}
        if(power_watch){int pfd=fileno(power_watch);FD_SET(pfd,&fds);if(pfd>maxfd)maxfd=pfd;}
        struct timeval wait={0,50000};select(maxfd+1,&fds,NULL,NULL,&wait);
    }
    if(dirty&&save_page(state_path))fprintf(stderr,"Final save failed\n");
    if(ask_child>0){kill(ask_child,SIGTERM);while(waitpid(ask_child,NULL,0)<0&&errno==EINTR){};}
    if(power_watch_pid>0){kill(power_watch_pid,SIGTERM);while(waitpid(power_watch_pid,NULL,0)<0&&errno==EINTR){};}
    if(power_watch)fclose(power_watch);
    XftDrawDestroy(textdraw);XftFontClose(display,font);XftFontClose(display,heading);XftFontClose(display,small);
    XftColorFree(display,DefaultVisual(display,screen),DefaultColormap(display,screen),&black);XftColorFree(display,DefaultVisual(display,screen),DefaultColormap(display,screen),&white_text);
    XFreePixmap(display,backing);XFreeGC(display,gc);
    XDestroyWindow(display,window);XSync(display,False);XCloseDisplay(display);if(stylus_event_fd>=0)close(stylus_event_fd);if(power_key_fd>=0)close(power_key_fd);if(lock>=0)close(lock);
    hand_back_screen();     /* our window is gone: give the panel one full refresh */
    puts("prototype closed normally");return 0;
}
