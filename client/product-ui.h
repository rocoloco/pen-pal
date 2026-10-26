#ifndef HERMES_PRODUCT_UI_H
#define HERMES_PRODUCT_UI_H

#include <dirent.h>
#include <ctype.h>
#include "daily-model.h"

#define PRODUCT_HISTORY_MAX 80
#define PRODUCT_BRIEF_MAX DAILY_WIRE_MAX

enum ProductView { PRODUCT_TODAY, PRODUCT_NOTEBOOK, PRODUCT_HISTORY, PRODUCT_ARCHIVE };
enum ProductAction {
    PRODUCT_NONE=-1, PRODUCT_HISTORY_ACTION=3, PRODUCT_CLOSE=7, PRODUCT_BACK=11,
    PRODUCT_HISTORY_0=100,
    /* Clear page: lives in the pen rail, files the page first, never discards ink. */
    PRODUCT_INK_CLEAR_PAGE=98,
    PRODUCT_INK_TOGGLE=200, PRODUCT_INK_COLLAPSE, PRODUCT_INK_PEN,
    PRODUCT_INK_HIGHLIGHTER, PRODUCT_INK_ERASER, PRODUCT_INK_UNDO,
    PRODUCT_INK_PEN_STYLE_0=220, PRODUCT_INK_PEN_STYLE_1, PRODUCT_INK_PEN_STYLE_2, PRODUCT_INK_PEN_STYLE_3,
    PRODUCT_INK_PEN_WIDTH_0=230, PRODUCT_INK_PEN_WIDTH_1, PRODUCT_INK_PEN_WIDTH_2, PRODUCT_INK_PEN_WIDTH_3, PRODUCT_INK_PEN_WIDTH_4,
    PRODUCT_INK_HIGHLIGHT_WIDTH_0=240, PRODUCT_INK_HIGHLIGHT_WIDTH_1, PRODUCT_INK_HIGHLIGHT_WIDTH_2, PRODUCT_INK_HIGHLIGHT_WIDTH_3,
    PRODUCT_INK_ERASER_WIDTH_0=250, PRODUCT_INK_ERASER_WIDTH_1, PRODUCT_INK_ERASER_WIDTH_2, PRODUCT_INK_ERASER_WIDTH_3
};

typedef struct { char ink[1024],reply[1024],title[100],date[48],space[24],kind[16],when_text[96]; time_t when; } ProductHistory;

static enum ProductView product_view=PRODUCT_TODAY;
static int product_history_selection, product_history_offset;
static int product_ink_toolbar_expanded, product_ink_menu;
static char product_root[1024],product_history_dir[1024],product_state_file[1024],product_reply_file[1024];
static ProductHistory product_history[PRODUCT_HISTORY_MAX];
static int product_history_count;

static void product_repaint(void);
static void product_url_encode(const char *in,char *out,size_t capacity);
static int product_load_daily_wire(const char *path);
static int product_join(char *out,size_t capacity,const char *base,const char *leaf){
    size_t a=strlen(base),b=strlen(leaf);if(a+1+b+1>capacity)return -1;
    memcpy(out,base,a);out[a]='/';memcpy(out+a+1,leaf,b+1);return 0;
}
static int product_append(char *out,size_t capacity,const char *base,const char *suffix){
    size_t a=strlen(base),b=strlen(suffix);if(a+b+1>capacity)return -1;
    memcpy(out,base,a);memcpy(out+a,suffix,b+1);return 0;
}

static int product_file_read(const char *path,char *out,size_t capacity){
    FILE *f=fopen(path,"rb");if(!f)return -1;
    size_t n=fread(out,1,capacity-1,f);int bad=ferror(f)||fgetc(f)!=EOF;fclose(f);
    if(bad)return -1;out[n]=0;return (int)n;
}
static int product_file_write(const char *path,const char *data,size_t length){
    char tmp[1050];if(snprintf(tmp,sizeof tmp,"%s.tmp",path)>=(int)sizeof tmp)return -1;
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd<0)return -1;
    size_t done=0;while(done<length){ssize_t n=write(fd,data+done,length-done);if(n<=0){close(fd);return -1;}done+=(size_t)n;}
    int bad=fsync(fd);if(close(fd))bad=1;if(bad||rename(tmp,path))return -1;
    char dir[1050];if(snprintf(dir,sizeof dir,"%s",path)>=(int)sizeof dir)return -1;char *slash=strrchr(dir,'/');if(slash){if(slash==dir)slash[1]=0;else *slash=0;}else strcpy(dir,".");
    fd=open(dir,O_RDONLY|O_DIRECTORY);if(fd<0)return -1;bad=fsync(fd);close(fd);return bad;
}
static int product_setting_int(const char *body,const char *key,int fallback){
    const char *p=strstr(body,key);if(!p||p[strlen(key)]!='=')return fallback;
    char *end;long value=strtol(p+strlen(key)+1,&end,10);if(end==p+strlen(key)+1)return fallback;return (int)value;
}
static void product_load_ink_settings(void){
    char path[1024],body[256];if(product_join(path,sizeof path,product_root,"ink-settings")||product_file_read(path,body,sizeof body)<0)return;
    product_ink_tool=product_setting_int(body,"tool",product_ink_tool);product_ink_style=product_setting_int(body,"style",product_ink_style);
    product_ink_pen_width_index=product_setting_int(body,"pen-width",product_ink_pen_width_index);
    product_ink_highlighter_width_index=product_setting_int(body,"highlight-width",product_ink_highlighter_width_index);
    product_ink_eraser_width_index=product_setting_int(body,"eraser-width",product_ink_eraser_width_index);
    if(product_ink_tool<HERMES_TOOL_PEN||product_ink_tool>HERMES_TOOL_ERASER)product_ink_tool=HERMES_TOOL_PEN;
    if(product_ink_style<HERMES_STYLE_BALLPOINT||product_ink_style>HERMES_STYLE_PENCIL)product_ink_style=HERMES_STYLE_BALLPOINT;
    if(product_ink_pen_width_index<0||product_ink_pen_width_index>=5)product_ink_pen_width_index=2;
    if(product_ink_highlighter_width_index<0||product_ink_highlighter_width_index>=4)product_ink_highlighter_width_index=1;
    if(product_ink_eraser_width_index<0||product_ink_eraser_width_index>=4)product_ink_eraser_width_index=2;
    erasing=product_ink_tool==HERMES_TOOL_ERASER;
}
static void product_save_ink_settings(void){
    char path[1024],body[256];if(product_join(path,sizeof path,product_root,"ink-settings"))return;
    int n=snprintf(body,sizeof body,"tool=%d\nstyle=%d\npen-width=%d\nhighlight-width=%d\neraser-width=%d\n",product_ink_tool,product_ink_style,product_ink_pen_width_index,product_ink_highlighter_width_index,product_ink_eraser_width_index);
    if(n>0&&n<(int)sizeof body)product_file_write(path,body,(size_t)n);
}
static int product_set_paths(void){
    if(product_join(product_state_file,sizeof product_state_file,product_root,"inbox.ink")||product_join(product_reply_file,sizeof product_reply_file,product_root,"inbox.reply"))return -1;
    state_path=product_state_file;return 0;
}
static void product_display_date(time_t when,char out[48]){
    struct tm local;localtime_r(&when,&local);strftime(out,48,"%A · %B %d",&local);
    for(char *p=out;*p;++p)*p=(char)toupper((unsigned char)*p);
}
static int product_prepare_storage(void){
    if(product_join(product_root,sizeof product_root,live_dir,"daily-workspace"))return -1;
    if(product_join(product_history_dir,sizeof product_history_dir,product_root,"history"))return -1;
    if(mkdir(product_root,0700)&&errno!=EEXIST)return -1;
    if(mkdir(product_history_dir,0700)&&errno!=EEXIST)return -1;
    if(product_set_paths())return -1;product_load_ink_settings();
    if(product_load_daily_wire(getenv("HERMES_DAILY_FILE"))||!daily_enabled)return -1;
    snprintf(status,sizeof status,"Ink saves on this Scribe.");
    /* Today is the front door on every fresh launch. */
    product_view=PRODUCT_TODAY;return 0;
}
static void product_save_reply_current(void){
    if(live_body[0])product_file_write(product_reply_file,live_body,strlen(live_body));
}
static int product_history_compare(const void *a,const void *b){
    const ProductHistory *x=a,*y=b;return x->when<y->when?1:x->when>y->when?-1:0;
}
static void product_history_keep(const ProductHistory *item){
    int slot=product_history_count;
    if(slot==PRODUCT_HISTORY_MAX){slot=0;for(int i=1;i<PRODUCT_HISTORY_MAX;++i)if(product_history[i].when<product_history[slot].when)slot=i;if(item->when<=product_history[slot].when)return;}
    else ++product_history_count;
    product_history[slot]=*item;
}
static void product_scan_daily_history(void){
    char root[1024];if(product_join(root,sizeof root,product_root,"daily"))return;DIR *documents=opendir(root);if(!documents)return;struct dirent *document;
    /* ponytail: scan local daily folders on open; add an index if page volume makes this lag. */
    while((document=readdir(documents))){if(!daily_uuid(document->d_name))continue;char folder[1024];if(product_join(folder,sizeof folder,root,document->d_name))continue;DIR *pages=opendir(folder);if(!pages)continue;struct dirent *page_file;
        while((page_file=readdir(pages))){size_t n=strlen(page_file->d_name);char id[37];if(n<41||strcmp(page_file->d_name+n-5,".meta"))continue;
            /* <uuid>.meta, or <uuid>-<n>.meta for Today's later sheets */
            if(n>41&&(page_file->d_name[36]!='-'||strspn(page_file->d_name+37,"0123456789")!=n-42||n>44))continue;memcpy(id,page_file->d_name,36);id[36]=0;if(!daily_uuid(id))continue;
            ProductHistory item={0};char meta[1024],base[1024],text[1200];if(product_join(meta,sizeof meta,folder,page_file->d_name)||product_file_read(meta,text,sizeof text)<0)continue;
            char *space=strtok(text,"\n"),*title=strtok(NULL,"\n"),*date=strtok(NULL,"\n"),*kind=strtok(NULL,"\n"),*when=strtok(NULL,"\n");if(!space||strcmp(space,"daily")||!title||!date)continue;
            snprintf(item.kind,sizeof item.kind,"%s",kind?kind:"");snprintf(item.when_text,sizeof item.when_text,"%s",when&&strcmp(when,"-")?when:"");
            snprintf(item.space,sizeof item.space,"daily");snprintf(item.title,sizeof item.title,"%s",title);snprintf(item.date,sizeof item.date,"%s",date);
            if(n-5>=sizeof base)continue;memcpy(base,page_file->d_name,n-5);base[n-5]=0;char page[1024];if(product_join(page,sizeof page,folder,base)||product_append(item.ink,sizeof item.ink,page,".ink")||product_append(item.reply,sizeof item.reply,page,".reply"))continue;
            struct stat st;if(stat(item.ink,&st))continue;item.when=st.st_mtime;product_history_keep(&item);
        }
        closedir(pages);
    }
    closedir(documents);
}
static void product_scan_history(void){
    product_history_count=0;product_scan_daily_history();DIR *dir=opendir(product_history_dir);if(!dir){qsort(product_history,(size_t)product_history_count,sizeof product_history[0],product_history_compare);return;}struct dirent *entry;
    while((entry=readdir(dir))){size_t n=strlen(entry->d_name);if(n<6||strcmp(entry->d_name+n-5,".meta"))continue;
        ProductHistory item={0};char meta[1024],text[400];if(product_join(meta,sizeof meta,product_history_dir,entry->d_name))continue;
        if(product_file_read(meta,text,sizeof text)<0)continue;char *one=strtok(text,"\n"),*two=strtok(NULL,"\n"),*three=strtok(NULL,"\n"),*four=strtok(NULL,"\n"),*five=strtok(NULL,"\n");if(!one||!two||!three)continue;
        snprintf(item.kind,sizeof item.kind,"%s",four?four:"");snprintf(item.when_text,sizeof item.when_text,"%s",five&&strcmp(five,"-")?five:"");
        snprintf(item.space,sizeof item.space,"%s",one);snprintf(item.title,sizeof item.title,"%s",two);snprintf(item.date,sizeof item.date,"%s",three);
        char stem[256],base[1024];if(n-5>=sizeof stem)continue;memcpy(stem,entry->d_name,n-5);stem[n-5]=0;if(product_join(base,sizeof base,product_history_dir,stem)||product_append(item.ink,sizeof item.ink,base,".ink")||product_append(item.reply,sizeof item.reply,base,".reply"))continue;
        struct stat st;if(stat(item.ink,&st))continue;item.when=st.st_mtime;product_history_keep(&item);
    }closedir(dir);qsort(product_history,(size_t)product_history_count,sizeof product_history[0],product_history_compare);
}
static int product_open_history(int index){
    if(index<0||index>=product_history_count)return -1;if(dirty)store_stroke();product_save_reply_current();
    if(load_page(product_history[index].ink))return -1;ink_cache_invalidate();ink_undo_valid=0;
    live_body[0]=0;product_file_read(product_history[index].reply,live_body,sizeof live_body);product_history_selection=index;daily_history_context=0;daily_history_context_page=0;product_view=PRODUCT_ARCHIVE;return 0;
}

static int product_load_daily_wire(const char *path){
    char wire[PRODUCT_BRIEF_MAX];int n=product_file_read(path,wire,sizeof wire);if(n<0||memchr(wire,0,(size_t)n))return -1;char *newline=strchr(wire,'\n');if(!newline)return -1;*newline=0;
    if(strcmp(wire,"ready"))return 0;
    if(strncmp(newline+1,"FORMAT\tHINKDAILY1\n",18))return -1;
    return daily_parse(newline+1)?-1:0;
}

static void product_small_at(int x,int y,const char *s){XftDrawStringUtf8(textdraw,&black,small,x,y,(const FcChar8 *)s,(int)strlen(s));}
static void product_rule(int y){XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,45,y,width-45,y);}
/* The app-level close button, top-right of the header. The drawn box and the tap
 * target come from these helpers, never from a second set of numbers. Restricting the target to the box (instead of the whole right third of
 * the header) also stops a tap near the corner from exiting the app by accident. */
#define PRODUCT_HEADER_CLOSE 66
static int product_header_close_left(void){return width-34-PRODUCT_HEADER_CLOSE;}
static int product_header_close_top(void){return 24;}
static int product_header_close_hit(int x,int y){
    int left=product_header_close_left(),top=product_header_close_top();
    return x>=left-14&&x<left+PRODUCT_HEADER_CLOSE+14&&y>=top-14&&y<top+PRODUCT_HEADER_CLOSE+14;
}
static void product_header(const char *title,const char *subtitle){
    product_small_at(50,58,"HERMES");product_small_at(width-520,58,"READY");
    text_at(50,135,title,1);product_small_at(50,190,subtitle);
    int bx=product_header_close_left(),by=product_header_close_top();
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,3,LineSolid,CapButt,JoinMiter);
    XDrawRectangle(display,backing,gc,bx,by,PRODUCT_HEADER_CLOSE,PRODUCT_HEADER_CLOSE);
    text_at(bx+22,by+47,"×",0);
    product_rule(225);
}
#define PRODUCT_INK_RAIL_W 126
#define PRODUCT_INK_TOP_H 68
#define PRODUCT_INK_ROW_H 82
#define PRODUCT_INK_MENU_W 390
#define PRODUCT_INK_MENU_ROW_H 60
static const char *product_ink_tool_labels[3]={"PEN","HILITE","ERASE"};
static const char *product_ink_style_labels[4]={"BALLPOINT","FOUNTAIN","FELT","PENCIL"};
static const char *product_ink_pen_width_labels[5]={"FINE","THIN","MEDIUM","THICK","HEAVY"};
static const char *product_ink_high_width_labels[4]={"NARROW","MEDIUM","WIDE","BROAD"};
static const char *product_ink_eraser_width_labels[4]={"SMALL","MEDIUM","LARGE","XL"};

/* Where the pen rail lives: any writable canvas. In daily mode that includes
 * Today's page, and the footer's Pen replaces the floating circle. */
static int product_ink_view(void){
    if(product_view==PRODUCT_NOTEBOOK)return daily_selected>=0;
    return product_view==PRODUCT_TODAY&&daily_selected>=0;
}
static int product_ink_rail_top(void){return canvas_top+35;}
static int product_ink_rail_left(void){return width-170;}
static int product_ink_menu_top(void){return product_ink_rail_top()+PRODUCT_INK_TOP_H;}
static int product_ink_menu_height(void){
    return product_ink_menu==1?75+4*PRODUCT_INK_MENU_ROW_H+55+5*PRODUCT_INK_MENU_ROW_H+20:
        75+4*PRODUCT_INK_MENU_ROW_H+20;
}
static void product_ink_text_at(int x,int y,const char *s,int big,int inverse){
    XftDrawStringUtf8(textdraw,inverse?&white_text:&black,big?font:small,x,y,(const FcChar8 *)s,(int)strlen(s));
}
static int product_ink_inside_circle(int x,int y){
    int cx=width-105,cy=product_ink_rail_top()+PRODUCT_INK_TOP_H/2,dx=x-cx,dy=y-cy;
    return dx*dx+dy*dy<=48*48;
}
static int product_ink_region_at(int x,int y){
    if(!product_ink_view())return 0;
    if(!product_ink_toolbar_expanded)return daily_enabled?0:product_ink_inside_circle(x,y);
    int left=product_ink_rail_left(),top=product_ink_rail_top();
    if(x>=left&&x<left+PRODUCT_INK_RAIL_W&&y>=top&&y<top+PRODUCT_INK_TOP_H+3*PRODUCT_INK_ROW_H+8+2*PRODUCT_INK_ROW_H)return 1;
    if(product_ink_menu&&x>=left-PRODUCT_INK_MENU_W&&x<left&&y>=product_ink_menu_top()&&y<product_ink_menu_top()+product_ink_menu_height())return 1;
    return 0;
}
static int product_ink_menu_action_at(int x,int y){
    if(!product_ink_menu)return PRODUCT_NONE;
    int left=product_ink_rail_left()-PRODUCT_INK_MENU_W,top=product_ink_menu_top();
    if(x<left||x>=left+PRODUCT_INK_MENU_W||y<top||y>=top+product_ink_menu_height())return PRODUCT_NONE;
    int row_top=top+75;
    if(product_ink_menu==1){
        if(y>=row_top&&y<row_top+4*PRODUCT_INK_MENU_ROW_H)return PRODUCT_INK_PEN_STYLE_0+(y-row_top)/PRODUCT_INK_MENU_ROW_H;
        row_top+=4*PRODUCT_INK_MENU_ROW_H+55;
        if(y>=row_top&&y<row_top+5*PRODUCT_INK_MENU_ROW_H)return PRODUCT_INK_PEN_WIDTH_0+(y-row_top)/PRODUCT_INK_MENU_ROW_H;
    } else if(product_ink_menu==2) {
        if(y>=row_top&&y<row_top+4*PRODUCT_INK_MENU_ROW_H)return PRODUCT_INK_HIGHLIGHT_WIDTH_0+(y-row_top)/PRODUCT_INK_MENU_ROW_H;
    } else if(product_ink_menu==3) {
        if(y>=row_top&&y<row_top+4*PRODUCT_INK_MENU_ROW_H)return PRODUCT_INK_ERASER_WIDTH_0+(y-row_top)/PRODUCT_INK_MENU_ROW_H;
    }
    return PRODUCT_NONE;
}
static int product_ink_button_at(int x,int y){
    if(!product_ink_view())return PRODUCT_NONE;
    int menu_action=product_ink_menu_action_at(x,y);if(menu_action!=PRODUCT_NONE)return menu_action;
    int top=product_ink_rail_top();
    if(!product_ink_toolbar_expanded)return !daily_enabled&&product_ink_inside_circle(x,y)?PRODUCT_INK_TOGGLE:PRODUCT_NONE;
    int left=product_ink_rail_left();
    if(x<left||x>=left+PRODUCT_INK_RAIL_W)return PRODUCT_NONE;
    if(y>=top&&y<top+PRODUCT_INK_TOP_H)return PRODUCT_INK_COLLAPSE;
    int tool_top=top+PRODUCT_INK_TOP_H;
    if(y>=tool_top&&y<tool_top+3*PRODUCT_INK_ROW_H)return PRODUCT_INK_PEN+(y-tool_top)/PRODUCT_INK_ROW_H;
    int undo_top=tool_top+3*PRODUCT_INK_ROW_H+8;
    if(y>=undo_top&&y<undo_top+PRODUCT_INK_ROW_H)return PRODUCT_INK_UNDO;
    if(y>=undo_top+PRODUCT_INK_ROW_H&&y<undo_top+2*PRODUCT_INK_ROW_H)return PRODUCT_INK_CLEAR_PAGE;
    return PRODUCT_NONE;
}
static void product_ink_menu_row(int left,int y,int w,const char *label,int active){
    if(active){XSetForeground(display,gc,black_pixel);XFillRectangle(display,backing,gc,left,y,w,PRODUCT_INK_MENU_ROW_H);}
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);XDrawRectangle(display,backing,gc,left,y,w,PRODUCT_INK_MENU_ROW_H);
    product_ink_text_at(left+15,y+39,label,0,active);
}
static void product_ink_menu_render(void){
    int left=product_ink_rail_left()-PRODUCT_INK_MENU_W,top=product_ink_menu_top(),height_menu=product_ink_menu_height();
    XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,left,top,PRODUCT_INK_MENU_W,height_menu);
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,2,LineSolid,CapButt,JoinMiter);XDrawRectangle(display,backing,gc,left,top,PRODUCT_INK_MENU_W,height_menu);
    product_small_at(left+20,top+45,product_ink_menu==1?"PEN TYPE":product_ink_menu==2?"HIGHLIGHTER":"ERASER");
    int row_top=top+75;
    if(product_ink_menu==1){
        for(int i=0;i<4;++i)product_ink_menu_row(left+15,row_top+i*PRODUCT_INK_MENU_ROW_H,PRODUCT_INK_MENU_W-30,product_ink_style_labels[i],product_ink_tool==HERMES_TOOL_PEN&&product_ink_style==i);
        row_top+=4*PRODUCT_INK_MENU_ROW_H+55;product_small_at(left+20,row_top-15,"STROKE WIDTH");
        for(int i=0;i<5;++i){char label[80];snprintf(label,sizeof label,"%s  %u PX",product_ink_pen_width_labels[i],(unsigned)ink_style_width(product_ink_style,ink_pen_widths[i]));product_ink_menu_row(left+15,row_top+i*PRODUCT_INK_MENU_ROW_H,PRODUCT_INK_MENU_W-30,label,product_ink_tool==HERMES_TOOL_PEN&&product_ink_pen_width_index==i);}
    } else if(product_ink_menu==2) {
        for(int i=0;i<4;++i){char label[80];snprintf(label,sizeof label,"%s  %u PX",product_ink_high_width_labels[i],(unsigned)ink_highlighter_widths[i]);product_ink_menu_row(left+15,row_top+i*PRODUCT_INK_MENU_ROW_H,PRODUCT_INK_MENU_W-30,label,product_ink_tool==HERMES_TOOL_HIGHLIGHTER&&product_ink_highlighter_width_index==i);}
    } else {
        for(int i=0;i<4;++i){char label[80];snprintf(label,sizeof label,"%s  %u PX",product_ink_eraser_width_labels[i],(unsigned)ink_eraser_widths[i]);product_ink_menu_row(left+15,row_top+i*PRODUCT_INK_MENU_ROW_H,PRODUCT_INK_MENU_W-30,label,product_ink_tool==HERMES_TOOL_ERASER&&product_ink_eraser_width_index==i);}
    }
}
static void product_ink_toolbar_render(void){
    if(!product_ink_view())return;
    int top=product_ink_rail_top(),left=product_ink_rail_left();
    if(!product_ink_toolbar_expanded&&daily_enabled)return;
    if(!product_ink_toolbar_expanded){
        int cx=width-105,cy=top+PRODUCT_INK_TOP_H/2;XSetForeground(display,gc,black_pixel);XFillArc(display,backing,gc,cx-48,cy-48,96,96,0,360*64);
        product_ink_text_at(cx-13,cy+12,product_ink_tool==HERMES_TOOL_HIGHLIGHTER?"H":product_ink_tool==HERMES_TOOL_ERASER?"E":"P",1,1);return;
    }
    int rail_height=PRODUCT_INK_TOP_H+3*PRODUCT_INK_ROW_H+8+2*PRODUCT_INK_ROW_H;
    XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,left,top,PRODUCT_INK_RAIL_W,rail_height);
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,2,LineSolid,CapButt,JoinMiter);XDrawRectangle(display,backing,gc,left,top,PRODUCT_INK_RAIL_W,rail_height);
    product_ink_text_at(left+49,top+45,"^",0,0);
    int tool_top=top+PRODUCT_INK_TOP_H;
    for(int i=0;i<3;++i){
        int y=tool_top+i*PRODUCT_INK_ROW_H,active=product_ink_tool==i;
        if(active){XSetForeground(display,gc,black_pixel);XFillRectangle(display,backing,gc,left+2,y,PRODUCT_INK_RAIL_W-4,PRODUCT_INK_ROW_H);}
        product_ink_text_at(left+15,y+51,product_ink_tool_labels[i],0,active);
    }
    int divider=tool_top+3*PRODUCT_INK_ROW_H;XDrawLine(display,backing,gc,left+10,divider+4,left+PRODUCT_INK_RAIL_W-10,divider+4);
    product_ink_text_at(left+19,divider+8+51,"UNDO",0,0);
    /* Clear page lives here, with the pens, rather than next to the star: it is the one
     * action that empties a page, so it has to be looked for on purpose. It files the
     * page to History first, so nothing is lost by choosing it. */
    product_ink_text_at(left+19,divider+8+PRODUCT_INK_ROW_H+51,"CLEAR",0,0);
    if(product_ink_menu)product_ink_menu_render();
}
static void product_draw_page(void){
    /* Completed strokes come from the cache; only the stroke still under the pen
     * is drawn live. When the pen is up, everything is baked and this is a blit.
     *
     * The blit is clipped to the CANVAS. Copying the whole cache to the whole
     * backing pixmap wiped every line the view had drawn before it -- the header
     * (HERMES, the connection status, the page title, the close X and the rule
     * under it) simply vanished, and the tap target stayed live, so the X was
     * invisible but still quit the app when touched. Chrome drawn before this
     * must survive it. */
    uint32_t split=drawing?product_open_stroke_start():page.count;
    ink_cache_bake(split);
    int top=canvas_top;if(top<0)top=0;
    int bottom=canvas_bottom>height?height:canvas_bottom;
    if(bottom>top)XCopyArea(display,ink_cache,backing,gc,0,top,width,bottom-top,0,top);
    int px=0,py=0;
    if(split){Point q=page.points[split-1];px=pixel_x(q);py=pixel_y(q);}
    for(uint32_t i=split;i<page.count;++i){
        Point p=page.points[i];int x=pixel_x(p),y=pixel_y(p);
        draw_segment(x,y,px,py,p.start,p.erase,p.tool,p.style,p.width);
        px=x;py=y;
    }
}
/* Percent-encode a header value: a topic can contain the middle dot and other
 * non-ASCII, and a raw byte above 0x7f is not valid in an HTTP header. The worker
 * re-checks the charset, so anything unexpected stops there rather than on the wire. */
static void product_url_encode(const char *in,char *out,size_t capacity){
    static const char *hex="0123456789ABCDEF";size_t n=0;
    for(const unsigned char *p=(const unsigned char *)in;*p&&n+4<capacity;++p){
        if((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='.'||*p=='_'||*p=='~')out[n++]=(char)*p;
        else{out[n++]='%';out[n++]=hex[*p>>4];out[n++]=hex[*p&15];}
    }
    out[n]=0;
}
static int product_evening(void){
    /* Evening is time-dependent, so the probes and the self-test pin it with
     * PRODUCT_EVENING instead of waiting for 19:00. */
    const char *forced=getenv("PRODUCT_EVENING");
    if(forced)return atoi(forced)!=0;
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);return local.tm_hour>=19;
}
static void product_center_at(int y,const char *s,int big){
    XftFont *f=big?heading:small;XGlyphInfo ext;
    XftTextExtentsUtf8(display,f,(const FcChar8 *)s,(int)strlen(s),&ext);
    int x=(width-(int)ext.xOff)/2;if(x<0)x=0;
    if(big)text_at(x,y,s,1);else product_small_at(x,y,s);
}
static void product_sleep_render(void){
    /* What the panel shows while the device is asleep. Without it the last frame of
     * the app stays on screen and the device reads as frozen rather than asleep. */
    char date[48];product_display_date(time(NULL),date);
    XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,0,0,width,height);
    XSetForeground(display,gc,black_pixel);
    int mid=height/2;
    product_header("INBOX","SLEEPING");
    product_rule(mid-210);
    product_center_at(mid-60,"PEN PAL",0);
    product_center_at(mid+60,"ASLEEP",1);
    product_center_at(mid+180,"PRESS POWER TO WAKE",0);
    product_rule(mid+290);
    product_center_at(mid+370,"INBOX",0);
    product_center_at(mid+420,date,0);
    present(0,0,width,height);
}
static void product_repaint(void){
    /* While asleep the sleep screen owns the panel -- but only once we have decided
     * to paint it: until then we must not repaint the app over the system's own
     * lock screen. */
    if(sleeping){if(sleep_painted)product_sleep_render();return;}
    XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,0,0,width,height);XSetForeground(display,gc,black_pixel);
    daily_render();present(0,0,width,height);
}
static int product_can_draw_at(int x,int y){return !(daily_context||slip_kind)&&product_ink_view()&&!product_ink_region_at(x,y)&&x>=45&&x<=width-45&&y>=canvas_top+20&&y<=canvas_bottom-20;}
static int product_button_at(int x,int y){return daily_button_at(x,y);}
static void product_activate(int action){
    if(daily_activate(action))return;
    if(action==PRODUCT_INK_TOGGLE){product_ink_toolbar_expanded=!product_ink_toolbar_expanded;product_ink_menu=0;product_repaint();return;}
    if(action==PRODUCT_INK_COLLAPSE){product_ink_toolbar_expanded=0;product_ink_menu=0;product_repaint();return;}
    if(action==PRODUCT_INK_PEN||action==PRODUCT_INK_HIGHLIGHTER||action==PRODUCT_INK_ERASER){
        int tool=action==PRODUCT_INK_PEN?HERMES_TOOL_PEN:action==PRODUCT_INK_HIGHLIGHTER?HERMES_TOOL_HIGHLIGHTER:HERMES_TOOL_ERASER;
        if(product_ink_tool==tool&&product_ink_menu==0)product_ink_menu=tool+1;
        else {product_ink_tool=tool;erasing=tool==HERMES_TOOL_ERASER;product_ink_menu=0;product_save_ink_settings();}
        product_repaint();return;
    }
    if(action>=PRODUCT_INK_PEN_STYLE_0&&action<=PRODUCT_INK_PEN_STYLE_3){product_ink_style=action-PRODUCT_INK_PEN_STYLE_0;product_ink_tool=HERMES_TOOL_PEN;erasing=0;product_ink_menu=0;product_save_ink_settings();snprintf(status,sizeof status,"Pen: %s",product_ink_style_labels[product_ink_style]);product_repaint();return;}
    if(action>=PRODUCT_INK_PEN_WIDTH_0&&action<=PRODUCT_INK_PEN_WIDTH_4){product_ink_pen_width_index=action-PRODUCT_INK_PEN_WIDTH_0;product_ink_tool=HERMES_TOOL_PEN;erasing=0;product_ink_menu=0;product_save_ink_settings();product_repaint();return;}
    if(action>=PRODUCT_INK_HIGHLIGHT_WIDTH_0&&action<=PRODUCT_INK_HIGHLIGHT_WIDTH_3){product_ink_highlighter_width_index=action-PRODUCT_INK_HIGHLIGHT_WIDTH_0;product_ink_tool=HERMES_TOOL_HIGHLIGHTER;erasing=0;product_ink_menu=0;product_save_ink_settings();product_repaint();return;}
    if(action>=PRODUCT_INK_ERASER_WIDTH_0&&action<=PRODUCT_INK_ERASER_WIDTH_3){product_ink_eraser_width_index=action-PRODUCT_INK_ERASER_WIDTH_0;product_ink_tool=HERMES_TOOL_ERASER;erasing=1;product_ink_menu=0;product_save_ink_settings();product_repaint();return;}
    if(action==PRODUCT_INK_UNDO){uint32_t sent=page.sent;undo_stroke();page.sent=sent;dirty=1;store_stroke();product_repaint();return;}
    if(action==PRODUCT_CLOSE){
        /* The header X. Refuse to vanish with unsaved ink: if the save fails, say
         * so and stay open rather than exiting and losing the page. */
        if(dirty)store_stroke();
        if(!dirty)quitting=1;
        else product_repaint();
        return;
    }
}

#include "daily-ui.h"
#endif
