#ifndef HERMES_DAILY_UI_H
#define HERMES_DAILY_UI_H
#include "page-answers.h"
/* ---- Daily mode: four surfaces, one footer -------------------------------------
 * Today   a warm note from Hermes, the day's events and tasks, and blank paper
 *         (the day's own page) to write on straight away.
 * Page    a calendar event or task on a full-width canvas, with Hermes' prep above it.
 * Pages   every saved page, newest first.
 * Saved   one saved page, read-only, with Hermes' notes still on it.
 * The footer never changes meaning: going back on the left, the star (ask Hermes)
 * in the middle, the pen on the right. Geometry is fixed per surface so ink never
 * moves between opens: pages keep the branch's canvas at y=370, and Today's page
 * starts at DAILY_TODAY_TOP on every launch.
 *
 * Today turns like a notebook. When the writing reaches the bottom, "Next page"
 * appears under it; the next sheet is blank paper from DAILY_SHEET_TOP (the day's
 * list stays on sheet 1). Sheet n>1 lives beside sheet 1 as <capture>-n.ink, with
 * its own answers, and Today reopens the latest sheet. */

enum { DAILY_PREVIOUS=400, DAILY_NEXT, DAILY_WRITE, DAILY_TODAY, DAILY_CONTEXT, DAILY_SAVE, DAILY_OPEN=1000 };
enum { DAILY_ARCHIVE_CONTEXT=406, DAILY_ARCHIVE_TODAY=407, DAILY_ASK=410, DAILY_PEN, DAILY_ROWS_MORE, DAILY_PREP,
       DAILY_SLIP_CLOSE, DAILY_SLIP_NEXT, DAILY_PAGES_MORE, DAILY_ASK_ARCHIVE, DAILY_NOOP,
       DAILY_SHEET_PREV, DAILY_SHEET_NEXT, DAILY_TURN, DAILY_ANSWER_0=1200, DAILY_OPEN_PREP=1300,
       DAILY_PAGE_REF_0=1500, DAILY_TASK_DONE_0=1700 };
#define DAILY_PAGE_TOP 370
#define DAILY_TODAY_TOP 1330
#define DAILY_SHEET_TOP 170
#define DAILY_TURN_ROOM 260      /* less free paper than this under the last ink or answer: offer the turn */
#define DAILY_ROW_TOP 380
#define DAILY_ROW_H 150
#define DAILY_ROWS_FIT 5
#define DAILY_PAGES_PER 9

typedef struct { char ink[1024],title[512],when[96],kind[16]; } DailyPage;
static DailyPage daily_page;
static int daily_rows_offset;
static XftFont *daily_row_font;
static XftFont *daily_rows_face(void){if(!daily_row_font)daily_row_font=XftFontOpenName(display,DefaultScreen(display),"sans:pixelsize=42");return daily_row_font?daily_row_font:font;}
static char daily_status_seen[160];
static double daily_status_at;

static int daily_capture_index(void){for(int i=0;i<daily.count;++i)if(!strcmp(daily.entries[i].kind,"capture"))return i;return -1;}
static int daily_is_row(int i){return strcmp(daily.entries[i].kind,"capture")!=0;}
static int daily_row_count(void){int n=0;for(int i=0;i<daily.count;++i)if(daily_is_row(i))++n;return n;}
static int daily_row_entry(int slot){for(int i=0;i<daily.count;++i)if(daily_is_row(i)&&slot--==0)return i;return -1;}
static int daily_rows_shown(void){int total=daily_row_count()-daily_rows_offset;if(total<=DAILY_ROWS_FIT)return total<0?0:total;return DAILY_ROWS_FIT-1;}
static int daily_rows_more(void){return daily_row_count()-daily_rows_offset>DAILY_ROWS_FIT||daily_rows_offset>0;}
static int daily_row_top(int slot){return DAILY_ROW_TOP+slot*DAILY_ROW_H;}
static int daily_entry_path(int index,char *out,size_t capacity){
    char folder[1024],document[1024],leaf[64];
    if(index<0||index>=daily.count||product_join(folder,sizeof folder,product_root,"daily")||product_join(document,sizeof document,folder,daily.document))return -1;
    snprintf(leaf,sizeof leaf,"%s.ink",daily.entries[index].id);return product_join(out,capacity,document,leaf);
}
static const char *daily_page_title(int index){return !strcmp(daily.entries[index].kind,"capture")?"Today's page":daily.entries[index].title;}

/* ---- Today's sheets ------------------------------------------------------------ */
static int daily_sheet, daily_sheets=1, daily_turn_ready, daily_turner_left, daily_turner_right;
static int daily_sheet_path(int sheet,char *out,size_t capacity){
    if(daily_entry_path(daily_capture_index(),out,capacity))return -1;
    if(!sheet)return 0;
    size_t n=strlen(out);char tail[24];snprintf(tail,sizeof tail,"-%d.ink",sheet+1);
    if(n<4||n-4+strlen(tail)>=capacity)return -1;
    memcpy(out+n-4,tail,strlen(tail)+1);return 0;
}
static int daily_count_sheets(void){char path[1024];int n=1;while(n<99&&!daily_sheet_path(n,path,sizeof path)&&!access(path,F_OK))++n;return n;}
static int daily_on_sheet(void){return daily_selected>=0&&!strcmp(daily.entries[daily_selected].kind,"capture")?daily_sheet:0;}

/* ---- the warm note ---------------------------------------------------------------
 * Written on the device from the day's own entries: no model call, nothing
 * invented, and it is there the instant the app opens, online or not. */
static const char *daily_number_word(int n,int capital){
    static const char *lower[]={"no","one","two","three","four","five","six","seven","eight","nine","ten"};
    static const char *upper[]={"No","One","Two","Three","Four","Five","Six","Seven","Eight","Nine","Ten"};
    static char digits[16];if(n>=0&&n<=10)return capital?upper[n]:lower[n];snprintf(digits,sizeof digits,"%d",n);return digits;
}
static void daily_owner(char *out,size_t capacity){
    out[0]=0;const char *env=getenv("HERMES_OWNER_NAME");char body[80];
    if(env)snprintf(body,sizeof body,"%s",env);
    else{char path[1024];if(live_path(path,sizeof path,"owner.name")||product_file_read(path,body,sizeof body)<0)return;}
    size_t n=strcspn(body,"\r\n");if(n>40)n=40;body[n]=0;
    for(char *p=body;*p;++p)if((unsigned char)*p<32||*p=='\t')*p=' ';
    snprintf(out,capacity,"%.40s",body);
}
static void daily_lower_first(char *s){if(s[0]>='A'&&s[0]<='Z'&&!(s[1]>='A'&&s[1]<='Z'))s[0]=(char)(s[0]+32);}
static int daily_due_today(const DailyEntry *e){return !strncmp(e->when,"Due ",4)&&!strcmp(e->when+4,daily.date);}
static void daily_note(char *out,size_t capacity){
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);
    char name[48],hello[96];daily_owner(name,sizeof name);
    int evening=product_evening();
    const char *part=evening?"Good evening":local.tm_hour<12?"Good morning":"Good afternoon";
    snprintf(hello,sizeof hello,"%s%s%s.",part,name[0]?", ":"",name);
    if(evening){snprintf(out,capacity,"%s Anything to carry into tomorrow? Write it below and tap the star.",hello);return;}
    int meetings=0,due=0,first_due=-1;
    for(int i=0;i<daily.count;++i){if(!strcmp(daily.entries[i].kind,"meeting"))++meetings;if(!strcmp(daily.entries[i].kind,"task")&&daily_due_today(&daily.entries[i])){if(first_due<0)first_due=i;++due;}}
    char due_text[300];due_text[0]=0;
    if(due==1){char title[240];snprintf(title,sizeof title,"%s",daily.entries[first_due].title);daily_lower_first(title);snprintf(due_text,sizeof due_text,"one thing due: %s",title);}
    else if(due>1)snprintf(due_text,sizeof due_text,"%s things due",daily_number_word(due,0));
    if(meetings&&due)snprintf(out,capacity,"%s %s event%s today, and %s.",hello,daily_number_word(meetings,1),meetings==1?"":"s",due_text);
    else if(meetings)snprintf(out,capacity,"%s %s event%s today. I'll keep the rest quiet.",hello,daily_number_word(meetings,1),meetings==1?"":"s");
    else if(due){due_text[0]=(char)(due_text[0]-32);snprintf(out,capacity,"%s %s.",hello,due_text);}
    else snprintf(out,capacity,"%s Nothing on the calendar today. The page is yours.",hello);
}
static void daily_when_short(const DailyEntry *e,char *out,size_t capacity){
    if(!strcmp(e->kind,"meeting")){size_t n=strcspn(e->when," -");if(n>=capacity)n=capacity-1;memcpy(out,e->when,n);out[n]=0;return;}
    if(!strncmp(e->when,"Due ",4)){
        if(!strcmp(e->when+4,daily.date)){snprintf(out,capacity,"Due today");return;}
        if(strcmp(e->when+4,daily.date)<0){snprintf(out,capacity,"Overdue");return;}
        snprintf(out,capacity,"Due %s",strlen(e->when)>=14?e->when+9:e->when+4);return;
    }
    if(!strcmp(e->when,"No deadline")){snprintf(out,capacity,"Anytime");return;}
    snprintf(out,capacity,"%.20s",e->when);
}

static void daily_pretty_date(const char *iso,char *out,size_t capacity){
    struct tm when;memset(&when,0,sizeof when);
    if(strlen(iso)!=10||iso[4]!='-'||iso[7]!='-'){snprintf(out,capacity,"%.40s",iso);return;}
    if(!strcmp(iso,daily.date)){snprintf(out,capacity,"TODAY");return;}
    when.tm_year=atoi(iso)-1900;when.tm_mon=atoi(iso+5)-1;when.tm_mday=atoi(iso+8);when.tm_hour=12;mktime(&when);
    strftime(out,capacity,"%a %d %b",&when);for(char *p=out;*p;++p)*p=(char)toupper((unsigned char)*p);
}
/* ---- drawing helpers ---------------------------------------------------------------- */
static void daily_text_fit(XftFont *f,int x,int y,int max_width,const char *text){
    int n=(int)strlen(text);
    if(hand_width(f,text,n)<=max_width){hand_draw(f,x,y,text,n);return;}
    char line[600];while(n>0){--n;while(n>0&&((unsigned char)text[n]&0xc0)==0x80)--n;
        if(n>=(int)sizeof line-4)continue;memcpy(line,text,(size_t)n);memcpy(line+n,"\xe2\x80\xa6",4);
        if(hand_width(f,line,n+3)<=max_width){hand_draw(f,x,y,line,n+3);return;}}
}
/* A row's label ends at its done box (tasks) or title (meetings). One too wide for that
 * ("Carried from Wednesday", "Carried 12 days") breaks at its last fitting space and
 * continues on a second line instead of running into the box. */
static void daily_when_draw(const char *when,int y,int max_width){
    int n=(int)strlen(when);
    if(hand_width(small,when,n)>max_width)for(int cut=n-1;cut>0;--cut)
        if(when[cut]==' '&&hand_width(small,when,cut)<=max_width){
            hand_draw(small,50,y,when,cut);daily_text_fit(small,50,y+32,max_width,when+cut+1);return;}
    daily_text_fit(small,50,y,max_width,when);
}
static void daily_close_box(void){
    int bx=product_header_close_left(),by=product_header_close_top();
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,3,LineSolid,CapButt,JoinMiter);
    XDrawRectangle(display,backing,gc,bx,by,PRODUCT_HEADER_CLOSE,PRODUCT_HEADER_CLOSE);text_at(bx+22,by+47,"×",0);
}
/* Quiet header: what this is, the close box, and a word only when something is off. */
static void daily_header(const char *kicker){
    product_small_at(50,64,kicker);daily_close_box();
    int right=product_header_close_left()-40;daily_turner_left=daily_turner_right=0;
    if(product_view==PRODUCT_TODAY&&daily_selected>=0&&daily_sheets>1){
        char turner[48];snprintf(turner,sizeof turner,"%s   %d / %d   %s",daily_sheet>0?"\xe2\x80\xb9":" ",daily_sheet+1,daily_sheets,daily_sheet+1<daily_sheets?"\xe2\x80\xba":" ");
        int w=hand_width(font,turner,(int)strlen(turner));daily_turner_left=right-w;daily_turner_right=right;
        text_at(daily_turner_left,70,turner,0);right=daily_turner_left-40;
    }
    if(ask_count&&ask_offline){const char *off="OFFLINE \xc2\xb7 ASKS WAITING";int w=hand_width(small,off,(int)strlen(off));product_small_at(right-w,64,off);}
}
static int daily_page_waiting(void){for(int i=0;i<answer_count;++i)if(answers[i].state=='w')return 1;return 0;}
static void daily_footer(const char *left,int ask_live,const char *right){
    int top=height-145,third=width/3;
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,3,LineSolid,CapButt,JoinMiter);
    XDrawLine(display,backing,gc,0,top,width,top);XDrawLine(display,backing,gc,third,top,third,height);
    if(right&&right[0])XDrawLine(display,backing,gc,third*2,top,third*2,height);
    if(left){int w=hand_width(font,left,(int)strlen(left));text_at(third/2-w/2,top+88,left,0);}
    int on=ask_live&&daily_page_waiting();
    if(on){XFillRectangle(display,backing,gc,third+2,top+2,third-3,height-top-2);star_at(third+third/2,top+72,40,1,white_pixel);}
    else star_at(third+third/2,top+72,40,ask_live,black_pixel);
    if(right&&right[0]){int w=hand_width(font,right,(int)strlen(right));text_at(third*2+third/2-w/2,top+88,right,0);}
}
/* Status is for news, not for routine: "Ink saved" on every stroke is noise. A
 * message shows for ten seconds after it changes, then the page goes quiet. */
static void daily_local_today(char *out,size_t capacity);
static void daily_status(void){
    if(product_view!=PRODUCT_TODAY&&page.count>=MAX_POINTS-(MAX_POINTS/10)){product_small_at(50,height-165,"This page is nearly full. Pen, then Clear, starts a fresh one; this page is kept.");return;}
    /* An older day's list is never presented as today's. */
    char today[16];daily_local_today(today,sizeof today);
    if(product_view==PRODUCT_TODAY&&daily.date[0]&&strcmp(daily.date,today)<0){
        char day[48]="an earlier day",note[140];struct tm when;memset(&when,0,sizeof when);
        when.tm_year=atoi(daily.date)-1900;when.tm_mon=atoi(daily.date+5)-1;when.tm_mday=atoi(daily.date+8);when.tm_hour=12;
        if(mktime(&when)!=(time_t)-1)strftime(day,sizeof day,"%A",&when);
        snprintf(note,sizeof note,"Showing %s's list. Today's will appear here shortly.",day);product_small_at(50,height-165,note);return;
    }
    if(!status[0]||!strncmp(status,"Ink sav",7))return;
    if(strcmp(status,daily_status_seen)){snprintf(daily_status_seen,sizeof daily_status_seen,"%s",status);daily_status_at=seconds();}
    if(seconds()-daily_status_at<10){char shown[110];snprintf(shown,sizeof shown,"%.105s",status);product_small_at(50,height-165,shown);}
}

/* ---- storage ------------------------------------------------------------------------ */
static int daily_history_store(void){
    if(daily_selected<0||daily_selected>=daily.count)return 0;
    size_t path_length=strlen(product_state_file);if(path_length<4||strcmp(product_state_file+path_length-4,".ink"))return -1;
    char base[1024],meta[1050],reply[1050],body[900];if(path_length-4>=sizeof base)return -1;memcpy(base,product_state_file,path_length-4);base[path_length-4]=0;
    if(product_append(meta,sizeof meta,base,".meta")||product_append(reply,sizeof reply,base,".reply"))return -1;
    if(!page.count&&!answer_count){if(unlink(meta)&&errno!=ENOENT)return -1;if(unlink(reply)&&errno!=ENOENT)return -1;return 0;}
    DailyEntry *entry=&daily.entries[daily_selected];int sheet=daily_on_sheet();char title[420];
    if(sheet)snprintf(title,sizeof title,"%s \xc2\xb7 %d",daily_page_title(daily_selected),sheet+1);else snprintf(title,sizeof title,"%.400s",daily_page_title(daily_selected));
    int n=snprintf(body,sizeof body,"daily\n%.410s\n%s\n%s\n%.90s\n",title,daily.date,sheet?"sheet":entry->kind,entry->when[0]?entry->when:"-");
    if(n<0||n>=(int)sizeof body||product_file_write(meta,body,(size_t)n)||product_file_write(reply,entry->context,strlen(entry->context)))return -1;
    return 0;
}
static int daily_save(void){
    if(dirty)store_stroke();
    if(dirty){snprintf(status,sizeof status,"Could not save. Keep this page open.");return -1;}
    if(daily_history_store()){snprintf(status,sizeof status,"Ink is saved, but Pages could not update.");return -1;}
    return 0;
}
static int daily_open_at(int index,int sheet){
    if(index<0||index>=daily.count||daily_save())return -1;
    char folder[1024],path[1024];
    if(product_join(folder,sizeof folder,product_root,"daily")||(mkdir(folder,0700)&&errno!=EEXIST))return -1;
    if(product_join(path,sizeof path,folder,daily.document)||(mkdir(path,0700)&&errno!=EEXIST))return -1;
    if(daily_entry_path(index,folder,sizeof folder))return -1;
    int capture=!strcmp(daily.entries[index].kind,"capture");if(!capture)sheet=0;
    if(sheet>0&&daily_sheet_path(sheet,folder,sizeof folder))return -1;
    /* Loading is transactional: a bad new file cannot replace the current ink. */
    Page *previous=malloc(sizeof page);if(!previous)return -1;*previous=page;
    memset(&page,0,sizeof page);
    if(load_page(folder)){page=*previous;free(previous);snprintf(status,sizeof status,"Page could not be opened. Your current ink is safe.");return -1;}
    free(previous);memcpy(product_state_file,folder,strlen(folder)+1);state_path=product_state_file;ink_undo_valid=0;
    ink_cache_invalidate();daily_selected=index;daily_context=0;slip_kind=SLIP_NONE;if(capture)daily_sheet=sheet;
    DailyEntry *e=&daily.entries[index];
    snprintf(daily_page.ink,sizeof daily_page.ink,"%s",folder);snprintf(daily_page.title,sizeof daily_page.title,"%s",daily_page_title(index));
    snprintf(daily_page.when,sizeof daily_page.when,"%s",e->when);snprintf(daily_page.kind,sizeof daily_page.kind,"%s",!strcmp(e->kind,"capture")?"today":e->kind);
    answers_attach(folder,0);
    product_view=!strcmp(e->kind,"capture")?PRODUCT_TODAY:PRODUCT_NOTEBOOK;product_ink_toolbar_expanded=0;product_ink_menu=0;
    return 0;
}
static int daily_open(int index){return daily_open_at(index,0);}
/* Open sheet `sheet` of Today. daily_sheet changes only once the sheet has loaded, so
 * the page being left is saved under its own sheet number. */
static int daily_open_sheet(int sheet){
    if(daily_open_at(daily_capture_index(),sheet))return -1;
    daily_sheets=daily_count_sheets();return 0;
}
static void daily_enter_today(void){
    /* Today reopens the latest sheet. On failure keep what is open; the status says why. */
    if(daily_capture_index()>=0){daily_open_sheet(daily_count_sheets()-1);return;}
    if(daily_save())return;daily_selected=-1;answer_count=0;answers_path[0]=0;
    product_view=PRODUCT_TODAY;slip_kind=SLIP_NONE;
}
/* The turn: a fresh sheet after the last one, written at once so it is where Today
 * reopens. The full sheet stays exactly as it is, answers and all. */
static int daily_turn(void){
    if(daily_save())return -1;
    int next=daily_count_sheets();char path[1024];
    if(next>=99||daily_sheet_path(next,path,sizeof path)){snprintf(status,sizeof status,"No room for another page today. Pen, then Clear, files this one.");return -1;}
    Page *keep=malloc(sizeof page);if(!keep)return -1;*keep=page;memset(&page,0,sizeof page);
    int bad=save_page(path);page=*keep;free(keep);
    if(bad||daily_open_sheet(next)){unlink(path);snprintf(status,sizeof status,"Could not start a new page. Your ink is safe.");return -1;}
    return 0;
}
/* Is there too little paper left under what is already on this sheet? Needs the
 * answers laid out, so it runs after answers_render. */
static int daily_page_full(void){
    if(page.count>=MAX_POINTS-(MAX_POINTS/10))return 1;
    int bottom=canvas_top;
    for(uint32_t i=0;i<page.count;++i)if(!page.points[i].erase){int y=ink_screen_y(page.points[i].y);if(y>bottom)bottom=y;}
    for(int i=0;i<answer_count;++i)if(answer_box[i].y1>bottom)bottom=answer_box[i].y1;
    return bottom>canvas_bottom-DAILY_TURN_ROOM;
}
/* Clear keeps everything: the page, its prep and Hermes' notes move to Pages first. */
static int daily_clear_page(void){
    if(answers_path[0]&&ask_waiting_on(answers_path)){snprintf(status,sizeof status,"Hermes is still answering on this page. Clear it once the answer arrives.");return -1;}
    if(daily_save())return -1;
    if(!page.count&&!answer_count){snprintf(status,sizeof status,"This page is already blank.");return 0;}
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);char stamp[32],leaf[96],base[1024],ink[1050],meta[1050],reply[1050],side[1050],body[900];
    strftime(stamp,sizeof stamp,"%Y%m%d-%H%M%S",&local);
    const char *id=daily_selected>=0?daily.entries[daily_selected].id:"page";
    snprintf(leaf,sizeof leaf,"%s-%.8s",stamp,id);
    if(product_join(base,sizeof base,product_history_dir,leaf)||product_append(ink,sizeof ink,base,".ink")||product_append(meta,sizeof meta,base,".meta")||product_append(reply,sizeof reply,base,".reply")||product_append(side,sizeof side,base,".hermes"))return -1;
    if(save_page(ink))return -1;
    char date[48];product_display_date(now,date);
    const char *kind=daily_page.kind[0]&&strcmp(daily_page.kind,"today")?daily_page.kind:daily_on_sheet()?"sheet":"capture";
    int n=snprintf(body,sizeof body,"daily\n%.400s\n%s\n%s\n%.90s\n",daily_page.title,date,kind,daily_page.when[0]?daily_page.when:"-");
    if(n<0||n>=(int)sizeof body||product_file_write(meta,body,(size_t)n))return -1;
    if(daily_selected>=0&&daily.entries[daily_selected].context[0])product_file_write(reply,daily.entries[daily_selected].context,strlen(daily.entries[daily_selected].context));
    if(answer_count&&answers_write(side,answers,answer_count))return -1;
    Page *keep=malloc(sizeof page);if(!keep)return -1;*keep=page;
    memset(&page,0,sizeof page);
    if(save_page(state_path)){page=*keep;free(keep);return -1;}
    free(keep);
    if(answers_path[0]&&unlink(answers_path)&&errno!=ENOENT)snprintf(status,sizeof status,"Page cleared; an older note file stayed behind.");
    answer_count=0;reveal_index=-1;ink_cache_invalidate();ink_undo_valid=0;daily_history_store();
    snprintf(status,sizeof status,"A fresh page. The last one is in Pages.");
    return 0;
}

/* ---- surfaces ------------------------------------------------------------------------- */
/* ---- Tasks done from Today ----------------------------------------------------------
 * A task row's box marks it done here at once. The tap is kept in the day's done.list
 * ("<entry> <epoch> <date>"); done-sweep.sh sends taps older than DAILY_DONE_UNDO_S to
 * the VPS, which completes the task in TickTick, and records the answer in done.sent
 * ("<entry> synced|failed|unknown <epoch>", last line wins). The app never writes
 * done.sent and the script never writes done.list, so a tap cannot be lost to a race.
 * Within the undo window a second tap takes it back before anything is sent. */
#define DAILY_DONE_UNDO_S 60
#define DAILY_DONE_BOX_X 236
static int daily_done_file(const char *name,char *out,size_t capacity){
    char folder[1024],document[1024];
    if(product_join(folder,sizeof folder,product_root,"daily")||product_join(document,sizeof document,folder,daily.document))return -1;
    return product_join(out,capacity,document,name);
}
static char daily_done_buffer[16384];
/* When this entry was tapped done (0: not done). */
static double daily_done_at(const char *id){
    char path[1024];if(daily_done_file("done.list",path,sizeof path)||product_file_read(path,daily_done_buffer,sizeof daily_done_buffer)<0)return 0;
    size_t n=strlen(id);double at=0;
    for(char *line=daily_done_buffer;*line;){char *end=strchr(line,'\n');if(!strncmp(line,id,n)&&line[n]==' ')at=atof(line+n+1);if(!end)break;line=end+1;}
    return at;
}
/* The provider's answer for this entry: 's' synced, 'f' failed, 'u' unknown, 0 none yet. */
static char daily_done_sent(const char *id){
    char path[1024];if(daily_done_file("done.sent",path,sizeof path)||product_file_read(path,daily_done_buffer,sizeof daily_done_buffer)<0)return 0;
    size_t n=strlen(id);char state=0;
    for(char *line=daily_done_buffer;*line;){char *end=strchr(line,'\n');if(!strncmp(line,id,n)&&line[n]==' ')state=line[n+1];if(!end)break;line=end+1;}
    return state;
}
static void daily_done_toggle(int index){
    const char *id=daily.entries[index].id;char path[1024];if(daily_done_file("done.list",path,sizeof path))return;
    double at=daily_done_at(id),now=(double)time(NULL);
    if(at>0&&now-at>=DAILY_DONE_UNDO_S){snprintf(status,sizeof status,"Already on its way to TickTick. Undo it there if you need to.");return;}
    char body[16384];size_t used=0;body[0]=0;
    if(product_file_read(path,daily_done_buffer,sizeof daily_done_buffer)>=0){         /* keep every other entry's line */
        size_t n=strlen(id);
        for(char *line=daily_done_buffer;*line;){char *end=strchr(line,'\n');size_t len=end?(size_t)(end-line):strlen(line);
            if(!(len>n&&!strncmp(line,id,n)&&line[n]==' ')&&len&&used+len+2<sizeof body){memcpy(body+used,line,len);used+=len;body[used++]='\n';}
            if(!end)break;line=end+1;}
    }
    if(at<=0)used+=(size_t)snprintf(body+used,sizeof body-used,"%s %.0f %s\n",id,now,daily.date);
    if(product_file_write(path,body,used))snprintf(status,sizeof status,"Could not save that. Try again.");
}
static void daily_done_row(int index,int y){
    const char *id=daily.entries[index].id;double at=daily_done_at(id);
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,3,LineSolid,CapButt,JoinMiter);
    XDrawRectangle(display,backing,gc,DAILY_DONE_BOX_X,y+22,36,36);
    if(at<=0)return;
    XSetLineAttributes(display,gc,5,LineSolid,CapRound,JoinRound);                 /* a drawn tick: no glyph needed */
    XDrawLine(display,backing,gc,DAILY_DONE_BOX_X+7,y+40,DAILY_DONE_BOX_X+16,y+50);XDrawLine(display,backing,gc,DAILY_DONE_BOX_X+16,y+50,DAILY_DONE_BOX_X+31,y+28);
    int w=hand_width(daily_rows_face(),daily.entries[index].title,(int)strlen(daily.entries[index].title));if(w>width-300-140)w=width-300-140;
    XSetLineAttributes(display,gc,3,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,300,y+40,300+w,y+40);
    char sent=daily_done_sent(id);double age=(double)time(NULL)-at;
    const char *note=sent=='s'?"Done \xc2\xb7 in TickTick":sent=='u'?"Done on this Scribe":sent=='f'?"Done \xc2\xb7 TickTick will get it on the next try":
                     age<DAILY_DONE_UNDO_S?"Done \xc2\xb7 tap again to undo":"Done \xc2\xb7 saving to TickTick";
    product_small_at(300,y+92,note);
}
static void daily_turn_cue(void){
    daily_turn_ready=daily_selected>=0&&daily_sheet+1>=daily_sheets&&daily_page_full();
    if(!daily_turn_ready)return;
    const char *next="Next page  \xe2\x80\xba";int w=hand_width(font,next,(int)strlen(next));text_at(width-60-w,height-158,next,0);
}
static void daily_today_render(void){
    char date[48];product_display_date(time(NULL),date);
    if(daily_selected>=0&&daily_sheet>0){
        char kicker[80];snprintf(kicker,sizeof kicker,"%s \xc2\xb7 %d",date,daily_sheet+1);daily_header(kicker);
        XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,2,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,45,DAILY_SHEET_TOP-20,width-45,DAILY_SHEET_TOP-20);
        canvas_top=DAILY_SHEET_TOP;canvas_bottom=height-190;product_draw_page();
        if(!page.count&&!answer_count)product_small_at(70,canvas_top+70,"A fresh page. Keep writing; tap the star when you want me.");
        answers_render();daily_turn_cue();return;
    }
    daily_header(date);
    char note[600];daily_note(note,sizeof note);
    int starts[8],lengths[8],lines=hand_wrap(hermes_hand,note,width-100-70,starts,lengths,4);
    star_at(62,150-hermes_hand->ascent/2,17,1,black_pixel);
    for(int l=0;l<lines;++l)hand_draw(hermes_hand,108,150+l*hermes_hand_line,note+starts[l],lengths[l]);
    int shown=daily_rows_shown();
    for(int slot=0;slot<shown;++slot){
        int index=daily_row_entry(daily_rows_offset+slot);if(index<0)break;DailyEntry *e=&daily.entries[index];int y=daily_row_top(slot);
        char when[48];daily_when_short(e,when,sizeof when);
        daily_when_draw(when,y+48,(!strcmp(e->kind,"task")?DAILY_DONE_BOX_X:300)-58);
        daily_text_fit(daily_rows_face(),300,y+54,width-300-140,e->title);
        if(!strcmp(e->kind,"task"))daily_done_row(index,y);
        char path[1024];if(e->context[0]||(!daily_entry_path(index,path,sizeof path)&&answers_have(path)))star_at(width-80,y+38,16,1,black_pixel);
        XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,45,y+DAILY_ROW_H-40,width-45,y+DAILY_ROW_H-40);
    }
    if(daily_rows_more()){int y=daily_row_top(shown);const char *more=daily_rows_offset+shown<daily_row_count()?"More  \xe2\x80\xba":"Back to the first  \xe2\x80\xba";product_small_at(50,y+48,more);}
    if(!daily.count||daily_row_count()==0)product_small_at(50,DAILY_ROW_TOP+48,"No events or tasks came through for today.");
    if(daily_selected<0)return;
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,2,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,45,DAILY_TODAY_TOP-20,width-45,DAILY_TODAY_TOP-20);
    canvas_top=DAILY_TODAY_TOP;canvas_bottom=height-190;product_draw_page();
    if(!page.count&&!answer_count)product_small_at(70,canvas_top+70,"Write anything here. Tap the star when you want me.");
    answers_render();daily_turn_cue();
}
static void daily_page_render(void){
    char kicker[160],kind[16];snprintf(kind,sizeof kind,"%s",!strcmp(daily_page.kind,"task")?"ONE THING":!strcmp(daily_page.kind,"meeting")?"EVENT":"NOTE");
    char when[96];when[0]=0;
    if(daily_selected>=0&&strcmp(daily_page.kind,"meeting"))daily_when_short(&daily.entries[daily_selected],when,sizeof when);
    else snprintf(when,sizeof when,"%s",strcmp(daily_page.when,"-")?daily_page.when:"");
    snprintf(kicker,sizeof kicker,"%s%s%.90s",kind,when[0]?" \xc2\xb7 ":"",when);for(char *p=kicker;*p;++p)*p=(char)toupper((unsigned char)*p);
    daily_header(kicker);daily_text_fit(heading,50,150,width-100,daily_page.title);
    const char *context=daily_selected>=0?daily.entries[daily_selected].context:"";
    if(context[0]){
        int starts[16],lengths[16],total=hand_wrap(hermes_hand,context,width-100-70-150,starts,lengths,16),lines=total>2?2:total;
        star_at(62,210+hermes_hand->ascent/2-hermes_hand->ascent/2,14,1,black_pixel);
        for(int l=0;l<lines;++l)hand_draw(hermes_hand,108,212+l*(hermes_hand_line-6)+hermes_hand->ascent/2,context+starts[l],lengths[l]);
        product_small_at(width-200,212+(lines-1)*(hermes_hand_line-6)+hermes_hand->ascent/2,total>2?"more  \xe2\x80\xba":"prep  \xe2\x80\xba");
    }
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,45,DAILY_PAGE_TOP-18,width-45,DAILY_PAGE_TOP-18);
    canvas_top=DAILY_PAGE_TOP;canvas_bottom=height-190;product_draw_page();
    if(!page.count&&!answer_count)product_small_at(70,canvas_top+70,"Write here. Tap the star when you want me.");
    answers_render();
}
static void daily_pages_render(void){
    daily_header("PAGES");text_at(50,150,"Your pages",1);product_scan_history();
    if(!product_history_count)product_small_at(50,260,"Pages you write on will appear here, newest first.");
    int y=240,shown=0;
    for(int i=product_history_offset;i<product_history_count&&shown<DAILY_PAGES_PER;++i,++shown){
        ProductHistory *h=&product_history[i];char day[48];daily_pretty_date(h->date,day,sizeof day);product_small_at(50,y+40,day);
        char label[40];snprintf(label,sizeof label,"%s",!strcmp(h->kind,"meeting")?"Event":!strcmp(h->kind,"task")?"One thing":"Note");
        daily_text_fit(daily_rows_face(),50,y+100,width-100-260,h->title);product_small_at(width-250,y+95,label);
        if(answers_have(h->ink))star_at(width-80,y+84,16,1,black_pixel);
        XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,45,y+140,width-45,y+140);y+=DAILY_PAGES_PER>0?220:0;
    }
}
static int daily_archive_is_capture(void){
    if(product_history_selection<0||product_history_selection>=product_history_count)return 0;
    const char *k=product_history[product_history_selection].kind;return !strcmp(k,"capture")||!strcmp(k,"today");
}
static void daily_archive_render(void){
    ProductHistory *entry=&product_history[product_history_selection];
    char kicker[120];snprintf(kicker,sizeof kicker,"SAVED \xc2\xb7 %.60s",entry->date);daily_header(kicker);
    daily_text_fit(heading,50,150,width-100,entry->title);
    int top=daily_archive_is_capture()?DAILY_TODAY_TOP:!strcmp(entry->kind,"sheet")?DAILY_SHEET_TOP:DAILY_PAGE_TOP;
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,45,top-18,width-45,top-18);
    canvas_top=top;canvas_bottom=height-190;product_draw_page();answers_render();
}
static int daily_render(void){
    if(!daily_enabled)return 0;
    if(product_view!=PRODUCT_TODAY&&product_view!=PRODUCT_NOTEBOOK&&product_view!=PRODUCT_HISTORY&&product_view!=PRODUCT_ARCHIVE)return 0;
    if(product_view==PRODUCT_TODAY){daily_today_render();daily_status();daily_footer("Pages",daily_selected>=0,"Pen");}
    else if(product_view==PRODUCT_NOTEBOOK&&daily_selected>=0){daily_page_render();daily_status();daily_footer("Today",1,"Pen");}
    else if(product_view==PRODUCT_HISTORY){daily_pages_render();daily_status();
        daily_footer("Today",1,product_history_count>DAILY_PAGES_PER?(product_history_offset+DAILY_PAGES_PER<product_history_count?"Older":"Newest"):"");}
    else if(product_view==PRODUCT_ARCHIVE&&product_history_selection>=0&&product_history_selection<product_history_count){daily_archive_render();daily_status();daily_footer("Pages",1,"Prep");}
    else {daily_enter_today();daily_today_render();daily_footer("Pages",daily_selected>=0,"Pen");}
    if(product_view==PRODUCT_TODAY||product_view==PRODUCT_NOTEBOOK)product_ink_toolbar_render();
    if(slip_kind){char label[200];
        if(slip_kind==SLIP_ANSWER)snprintf(label,sizeof label,"HERMES \xc2\xb7 %.120s",product_view==PRODUCT_ARCHIVE?product_history[product_history_selection].title:daily_page.title);
        else snprintf(label,sizeof label,"PREP \xc2\xb7 %.120s",product_view==PRODUCT_ARCHIVE?product_history[product_history_selection].title:daily_page.title);
        slip_render(label);}
    return 1;
}

/* The hint on blank paper goes with the first stroke. Redraw into the backing but push
 * only the hint's band (drawn at canvas_top+70), so the panel does not flash. */
static void daily_first_stroke(void){
    if(sleeping)return;
    XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,0,0,width,height);
    if(daily_render())present(0,canvas_top+35,width,55);
}

/* A finger swipe across Today turns its sheets: right-to-left goes forward (and turns a
 * full last sheet), left-to-right goes back. Only a quick, clearly sideways stroke
 * counts, so a palm resting while writing never turns the page. 0 when it is not one. */
/* 100 px: the Scribe reports a natural flick as 1-2 moves over 0.04-0.06 s and only
 * 100-180 px before the release (swipe log, 2026-09-30); taps jitter under 50 px. */
#define DAILY_SWIPE_MIN 100
static int daily_swipe(int dx,int dy,double duration){
    if(product_view!=PRODUCT_TODAY||daily_selected<0||slip_kind||duration>0.8)return 0;
    if(abs(dx)<DAILY_SWIPE_MIN||abs(dx)<2*abs(dy))return 0;
    if(dx>0)return daily_sheet>0?DAILY_SHEET_PREV:DAILY_NOOP;
    return daily_sheet+1<daily_sheets?DAILY_SHEET_NEXT:daily_turn_ready?DAILY_TURN:DAILY_NOOP;
}

/* ---- input ------------------------------------------------------------------------------- */
/* Controls a resting palm lands on while writing: Hermes' answers and page links on the canvas. */
static int daily_palm_guarded(int action){
    return !slip_kind&&((action>=DAILY_ANSWER_0&&action<DAILY_ANSWER_0+ANSWERS_MAX)||
                        (action>=DAILY_PAGE_REF_0&&action<DAILY_TASK_DONE_0));
}
static int daily_button_at(int x,int y){
    if(product_header_close_hit(x,y))return PRODUCT_CLOSE;
    if(y>=height-145){
        int third=x/(width/3);if(third>2)third=2;
        if(product_view==PRODUCT_TODAY){int a[]={PRODUCT_HISTORY_ACTION,daily_selected>=0?DAILY_ASK:DAILY_NOOP,daily_selected>=0?DAILY_PEN:DAILY_NOOP};return a[third];}
        if(product_view==PRODUCT_NOTEBOOK){int a[]={DAILY_TODAY,DAILY_ASK,DAILY_PEN};return a[third];}
        if(product_view==PRODUCT_HISTORY){int a[]={DAILY_TODAY,DAILY_TODAY,product_history_count>DAILY_PAGES_PER?DAILY_PAGES_MORE:DAILY_NOOP};return a[third];}
        if(product_view==PRODUCT_ARCHIVE){int a[]={PRODUCT_BACK,DAILY_ASK_ARCHIVE,DAILY_ARCHIVE_CONTEXT};return a[third];}
        return DAILY_NOOP;
    }
    if(slip_kind){int hit=slip_hit(x,y);
        if(hit>=SLIP_HIT_REF_0)return DAILY_PAGE_REF_0+slip_answer*ANSWER_REFS_MAX+slip_ref_raw[hit-SLIP_HIT_REF_0];
        return hit==SLIP_HIT_NEXT?DAILY_SLIP_NEXT:hit==SLIP_HIT_INSIDE?DAILY_NOOP:DAILY_SLIP_CLOSE;}
    if(product_view==PRODUCT_TODAY&&daily_selected>=0){
        if(daily_turner_right&&y<130&&x>=daily_turner_left-30&&x<daily_turner_right+20)
            return x<(daily_turner_left+daily_turner_right)/2?(daily_sheet>0?DAILY_SHEET_PREV:DAILY_NOOP):(daily_sheet+1<daily_sheets?DAILY_SHEET_NEXT:DAILY_NOOP);
        /* Below the writable canvas and above the footer: drawing never starts here. */
        if(daily_turn_ready&&y>=canvas_bottom-20&&x>=width/2)return DAILY_TURN;
    }
    if(product_view==PRODUCT_TODAY||product_view==PRODUCT_NOTEBOOK){int action=product_ink_button_at(x,y);if(action!=PRODUCT_NONE)return action;}
    if(product_view==PRODUCT_TODAY||product_view==PRODUCT_NOTEBOOK||product_view==PRODUCT_ARCHIVE){
        if(y>=canvas_top){int ref=answer_ref_at(x,y);if(ref>=0)return DAILY_PAGE_REF_0+ref;}
        if(y>=canvas_top){int i=answer_at(x,y);if(i>=0)return answers[i].state=='w'?DAILY_NOOP:DAILY_ANSWER_0+i;}
    }
    if(product_view==PRODUCT_TODAY&&daily_selected>=0&&daily_sheet>0&&y<DAILY_SHEET_TOP-20)return DAILY_NOOP;
    if(product_view==PRODUCT_TODAY&&!(daily_selected>=0&&daily_sheet>0)&&y<DAILY_TODAY_TOP-20){
        int shown=daily_rows_shown();
        for(int slot=0;slot<shown;++slot)if(y>=daily_row_top(slot)&&y<daily_row_top(slot)+DAILY_ROW_H-20){int index=daily_row_entry(daily_rows_offset+slot);
            /* The row's star is its prep: open the page with the prep already on a slip. */
            if(index>=0&&x>=width-180&&daily.entries[index].context[0])return DAILY_OPEN_PREP+index;
            if(index>=0&&!strcmp(daily.entries[index].kind,"task")&&x>=DAILY_DONE_BOX_X-30&&x<DAILY_DONE_BOX_X+60)return DAILY_TASK_DONE_0+index;
            if(index>=0)return DAILY_OPEN+index;}
        if(daily_rows_more()&&y>=daily_row_top(shown)&&y<daily_row_top(shown)+DAILY_ROW_H-20)return DAILY_ROWS_MORE;
        return DAILY_NOOP;
    }
    if(product_view==PRODUCT_NOTEBOOK&&y<DAILY_PAGE_TOP-18)return y>=180&&daily_selected>=0&&daily.entries[daily_selected].context[0]?DAILY_PREP:DAILY_NOOP;
    if(product_view==PRODUCT_HISTORY&&y>=240){int index=product_history_offset+(y-240)/220;if(index<product_history_count&&(y-240)%220<150)return PRODUCT_HISTORY_0+index;return DAILY_NOOP;}
    if(product_view==PRODUCT_ARCHIVE&&y<canvas_top)return DAILY_NOOP;
    return PRODUCT_NONE;
}
static int daily_find_entry_by_path(const char *ink){
    char path[1024];for(int i=0;i<daily.count;++i)if(!daily_entry_path(i,path,sizeof path)&&!strcmp(path,ink))return i;return -1;
}
/* A page from today's list, or one of Today's sheets, opens live. 1 when it was one. */
static int daily_open_live(const char *ink){
    char path[1024];int sheets=daily_capture_index()>=0?daily_count_sheets():0;
    for(int s=0;s<sheets;++s)if(!daily_sheet_path(s,path,sizeof path)&&!strcmp(path,ink)){daily_open_sheet(s);return 1;}
    int live=daily_find_entry_by_path(ink);if(live<0)return 0;
    daily_open(live);return 1;
}
/* Is a page link (label + date, as Pages lists it) the page on screen? Then it is hidden. */
static int answer_ref_is_current(const char *label,const char *date){
    if(product_view==PRODUCT_ARCHIVE&&product_history_selection>=0&&product_history_selection<product_history_count)
        return !strcmp(product_history[product_history_selection].title,label)&&!strcmp(product_history[product_history_selection].date,date);
    if(daily_selected<0||strcmp(daily.date,date))return 0;
    char here[420];int sheet=daily_on_sheet();
    if(sheet)snprintf(here,sizeof here,"%s \xc2\xb7 %d",daily_page_title(daily_selected),sheet+1);
    else snprintf(here,sizeof here,"%s",daily_page_title(daily_selected));
    return !strcmp(here,label);
}
/* A page link opens that page the way Pages would: live if it is today's, else saved. */
static int daily_open_page_ref(int code){
    char label[160],date[16];
    if(!answer_ref_target(code,label,date)||daily_save())return 1;
    product_scan_history();
    for(int k=0;k<product_history_count;++k)
        if(!strcmp(product_history[k].title,label)&&!strcmp(product_history[k].date,date)){slip_kind=SLIP_NONE;return daily_activate(PRODUCT_HISTORY_0+k);}
    snprintf(status,sizeof status,"That page is not on this Scribe any more.");product_repaint();return 1;
}
static int daily_activate(int action){
    if(action==DAILY_NOOP)return 1;
    if(action==PRODUCT_CLOSE){daily_save();return 0;}                 /* the header X: save, then quit */
    if(action==DAILY_ARCHIVE_TODAY||action==DAILY_SAVE)action=DAILY_TODAY;
    if(action==DAILY_TODAY){
        if(product_view==PRODUCT_ARCHIVE){daily_selected=-1;}
        slip_kind=SLIP_NONE;daily_enter_today();product_repaint();return 1;
    }
    if(action==PRODUCT_INK_CLEAR_PAGE){product_ink_menu=0;daily_clear_page();product_repaint();return 1;}
    if(action==DAILY_PEN){product_ink_toolbar_expanded=!product_ink_toolbar_expanded;product_ink_menu=0;slip_kind=SLIP_NONE;product_repaint();return 1;}
    if(action==DAILY_ASK){
        if(daily_selected<0){product_repaint();return 1;}
        slip_kind=SLIP_NONE;product_ink_toolbar_expanded=0;product_ink_menu=0;
        ask_now(daily_page.kind,daily_page.when,daily_page.title);product_repaint();return 1;
    }
    if(action>=DAILY_OPEN&&action<DAILY_OPEN+DAILY_MAX){
        if(daily_open(action-DAILY_OPEN)&&!status[0])snprintf(status,sizeof status,"Could not open this page. Your ink is safe.");
        product_repaint();return 1;
    }
    if(action==DAILY_SHEET_PREV||action==DAILY_SHEET_NEXT){
        int to=daily_sheet+(action==DAILY_SHEET_NEXT?1:-1);
        if(to>=0&&to<daily_sheets){slip_kind=SLIP_NONE;daily_open_sheet(to);}
        product_repaint();return 1;
    }
    if(action==DAILY_TURN){slip_kind=SLIP_NONE;daily_turn();product_repaint();return 1;}
    if(action>=DAILY_PAGE_REF_0&&action<DAILY_PAGE_REF_0+ANSWERS_MAX*ANSWER_REFS_MAX)return daily_open_page_ref(action-DAILY_PAGE_REF_0);
    if(action>=DAILY_TASK_DONE_0&&action<DAILY_TASK_DONE_0+DAILY_MAX){daily_done_toggle(action-DAILY_TASK_DONE_0);product_repaint();return 1;}
    if(action>=DAILY_OPEN_PREP&&action<DAILY_OPEN_PREP+DAILY_MAX){
        int index=action-DAILY_OPEN_PREP;
        if(daily_open(index)){if(!status[0])snprintf(status,sizeof status,"Could not open this page. Your ink is safe.");}
        else slip_open_context(daily_page.title,daily.entries[index].context);
        product_repaint();return 1;
    }
    if(action==DAILY_ROWS_MORE){daily_rows_offset+=daily_rows_shown();if(daily_rows_offset>=daily_row_count())daily_rows_offset=0;product_repaint();return 1;}
    if(action==DAILY_PREP&&daily_selected>=0){slip_open_context(daily_page.title,daily.entries[daily_selected].context);product_repaint();return 1;}
    if(action>=DAILY_ANSWER_0&&action<DAILY_ANSWER_0+ANSWERS_MAX){slip_open_answer(action-DAILY_ANSWER_0);product_ink_toolbar_expanded=0;product_repaint();return 1;}
    if(action==DAILY_SLIP_CLOSE){slip_kind=SLIP_NONE;product_repaint();return 1;}
    if(action==DAILY_SLIP_NEXT){if(slip_page+1<slip_pages)++slip_page;else slip_kind=SLIP_NONE;product_repaint();return 1;}
    if(action==PRODUCT_HISTORY_ACTION){
        if(daily_save())return 1;
        slip_kind=SLIP_NONE;product_ink_toolbar_expanded=0;product_ink_menu=0;product_history_offset=0;product_view=PRODUCT_HISTORY;product_repaint();return 1;
    }
    if(action==DAILY_PAGES_MORE){product_history_offset=product_history_offset+DAILY_PAGES_PER<product_history_count?product_history_offset+DAILY_PAGES_PER:0;product_repaint();return 1;}
    if(action>=PRODUCT_HISTORY_0&&action<PRODUCT_HISTORY_0+PRODUCT_HISTORY_MAX){
        if(daily_save())return 1;
        int index=action-PRODUCT_HISTORY_0;if(index>=product_history_count)return 1;
        /* A page from today's list opens live, exactly as its row on Today would. */
        if(daily_open_live(product_history[index].ink)){product_repaint();return 1;}
        daily_selected=-1;
        if(product_open_history(index)){snprintf(status,sizeof status,"This page could not be opened. Nothing was changed.");product_repaint();return 1;}
        /* state_path follows what is on screen, so nothing can ever save a saved page
         * over a live one. */
        snprintf(product_state_file,sizeof product_state_file,"%s",product_history[index].ink);state_path=product_state_file;
        answers_attach(product_history[index].ink,1);slip_kind=SLIP_NONE;product_repaint();return 1;
    }
    if(action==PRODUCT_BACK&&product_view==PRODUCT_ARCHIVE){slip_kind=SLIP_NONE;answer_count=0;answers_path[0]=0;product_view=PRODUCT_HISTORY;product_repaint();return 1;}
    if(action==DAILY_ASK_ARCHIVE){
        slip_kind=SLIP_NONE;
        if(!(product_history_selection>=0&&product_history_selection<product_history_count&&daily_open_live(product_history[product_history_selection].ink)))daily_enter_today();
        product_repaint();return 1;
    }
    if(action==DAILY_ARCHIVE_CONTEXT&&product_view==PRODUCT_ARCHIVE){slip_open_context(product_history[product_history_selection].title,live_body);product_repaint();return 1;}
    return 0;
}
/* ---- Today stays current -------------------------------------------------------------
 * The Scribe sleeps with the app open, so "launch" is not "morning". The launcher keeps
 * the bundle file (HERMES_DAILY_FILE, today.wire) fresh; every DAILY_REFRESH_S the app
 * looks at it and reloads Today in place when it changed: a new day after a short
 * pause in writing (writing on yesterday's page is the worse surprise), a same-day
 * update (a moved meeting, a new task) only while Today is on screen and after a longer
 * pause, so it never swaps a page from under the pen. Page, answers and taps are saved
 * first; ink files are keyed by stable ids, so a reload never strands anything. */
#define DAILY_REFRESH_S 30
#define DAILY_NEW_DAY_IDLE_S 10
#define DAILY_UPDATE_IDLE_S 45
static time_t daily_bundle_mtime;
static off_t daily_bundle_size;
static double daily_refresh_next;
static void daily_local_today(char *out,size_t capacity){time_t now=time(NULL);struct tm local;localtime_r(&now,&local);strftime(out,capacity,"%Y-%m-%d",&local);}
/* The DATE line of a bundle file, or -1. */
static int daily_bundle_date(const char *path,char *out){
    char head[1024];int n=product_file_read(path,head,sizeof head);if(n<=0)return -1;
    const char *at=strstr(head,"\nDATE\t");if(!at||(int)(at-head)+16>n)return -1;
    memcpy(out,at+6,10);out[10]=0;return out[4]=='-'&&out[7]=='-'?0:-1;
}
static void daily_refresh_tick(void){
    double now=seconds();if(now<daily_refresh_next)return;daily_refresh_next=now+DAILY_REFRESH_S;
    const char *path=getenv("HERMES_DAILY_FILE");struct stat st;if(!path||stat(path,&st))return;
    if(!daily_bundle_mtime){daily_bundle_mtime=st.st_mtime;daily_bundle_size=st.st_size;return;}   /* the launch copy */
    if(st.st_mtime==daily_bundle_mtime&&st.st_size==daily_bundle_size)return;
    char date[16];if(daily_bundle_date(path,date)||strcmp(date,daily.date)<0)return;       /* never go back a day */
    int new_day=strcmp(date,daily.date)!=0;
    if(now-last_input_at<(new_day?DAILY_NEW_DAY_IDLE_S:DAILY_UPDATE_IDLE_S))return;         /* mid-thought: next tick */
    if(!new_day&&(product_view!=PRODUCT_TODAY||slip_kind||product_ink_toolbar_expanded))return;
    if(daily_save())return;
    int was=daily_selected;daily_selected=-1;                                                /* daily_parse's guard */
    if(product_load_daily_wire(path)){
        daily_selected=was;daily_bundle_mtime=st.st_mtime;daily_bundle_size=st.st_size;
        snprintf(status,sizeof status,"Today's update could not be opened. Your ink is safe.");return;
    }
    daily_bundle_mtime=st.st_mtime;daily_bundle_size=st.st_size;
    daily_rows_offset=0;slip_kind=SLIP_NONE;product_ink_toolbar_expanded=0;daily_enter_today();ink_cache_invalidate();product_repaint();
    fprintf(stderr,"today: reloaded %s (%s)\n",date,new_day?"new day":"update");
}
static void daily_tick(void){
    ask_tick();
    daily_refresh_tick();
    if(!slip_kind&&(product_view==PRODUCT_TODAY||product_view==PRODUCT_NOTEBOOK))answers_animate();
}
#endif
