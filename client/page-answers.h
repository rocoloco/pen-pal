#ifndef HERMES_PAGE_ANSWERS_H
#define HERMES_PAGE_ANSWERS_H
/* ---- The page answers --------------------------------------------------------
 * One gesture: write, tap the star, and Hermes answers on the same page, under
 * what was asked, in its own hand. Three rules shape everything here:
 *
 *  1. Ink is never moved, hidden or sent unasked. An ask snapshots exactly the ink
 *     written since the previous ask (page.sent .. page.count) into its own file
 *     BEFORE any network work, then advances page.sent and saves the page.
 *  2. An answer belongs to its page, not to the screen. Replies live in a sidecar
 *     (<page>.hermes) next to the page's .ink, so a reply that arrives while he is
 *     on another page, or after a restart, still lands where it was asked.
 *  3. Offline is a normal state. Asks wait in a small durable queue (asks.queue)
 *     and are retried with a gentle backoff; the page says so in Hermes' voice.
 *
 * The reply is text in a handwriting face (Caveat, OFL, shipped beside the app as
 * fonts/Caveat-Regular.ttf), so it is always clearly Hermes' and never mistaken for
 * his own strokes. It is revealed a line at a time, black only, so each step is a
 * small A2-safe update rather than a full-screen refresh. */

#include <strings.h>
#include <fontconfig/fcfreetype.h>
#define ANSWERS_MAX 16
#define ANSWER_TEXT 4096
#define ANSWER_HEARD 1024
#define ASK_QUEUE_MAX 8
#define HAND_PX_DEFAULT 56
#define ANSWER_INLINE_LINES 8

typedef struct { char id[33]; uint16_t top,bottom; char state; char answer[ANSWER_TEXT]; char heard[ANSWER_HEARD]; } Answer;
/* Page links: the server may end an answer with lines "\n↗ <page title> · <YYYY-MM-DD>"
 * naming archived pages it drew on. They are drawn as tappable rows under the answer
 * (never in Hermes' hand) and open that page; a link to the page on screen is hidden. */
#define ANSWER_REFS_MAX 2
#define ANSWER_REF_H 60
#define ANSWER_REF_MARK "\n\xe2\x86\x97 "
typedef struct { int y0,y1,lines,stub,more,refs,ref[ANSWER_REFS_MAX]; } AnswerBox;
static void daily_pretty_date(const char *iso,char *out,size_t capacity);
static int answer_ref_is_current(const char *label,const char *date);
typedef struct { char id[33],sidecar[1024],kind[16],when[96],title[200]; uint16_t top,bottom; } Ask;

static Answer answers[ANSWERS_MAX], answers_scratch[ANSWERS_MAX];
static AnswerBox answer_box[ANSWERS_MAX];
static int answer_count, answers_scratch_count, answers_readonly;
static char answers_path[1024];
static Ask ask_queue[ASK_QUEUE_MAX];
static int ask_count, ask_accepted, ask_offline, ask_fail_streak;
static pid_t ask_child;
static double ask_next, ask_started, ask_backoff;
static char ask_stage[16];
static XftFont *hermes_hand;
static int hermes_hand_line=58;
static int reveal_index=-1, reveal_shown;
static double reveal_next, dots_next;
static int dots_phase;
/* The slip: one overlay for anything longer than the page has room for. */
enum { SLIP_NONE, SLIP_CONTEXT, SLIP_ANSWER };
static int slip_answer, slip_page, slip_pages=1;

/* ---- the hand ------------------------------------------------------------------ */
static void hermes_hand_open(int screen){
    const char *px_env=getenv("HERMES_REPLY_FONT_PX");double px=px_env?atof(px_env):HAND_PX_DEFAULT;if(px<24||px>120)px=HAND_PX_DEFAULT;
    char shipped[1024];const char *tries[2]={getenv("HERMES_REPLY_FONT"),NULL};
    if(!live_path(shipped,sizeof shipped,"fonts/Caveat-Regular.ttf"))tries[1]=shipped;
    /* Open the file itself: describe it with FreeType and open exactly that. Matching by
     * name or path is not reliable -- the Scribe's fontconfig 2.8 answers a file= request
     * with Amazon Ember. */
    for(int i=0;i<2&&!hermes_hand;++i)if(tries[i]&&!access(tries[i],R_OK)){
        int faces=0;FcPattern *face=FcFreeTypeQuery((const FcChar8 *)tries[i],0,NULL,&faces);if(!face)continue;
        FcPatternDel(face,FC_PIXEL_SIZE);FcPatternAddDouble(face,FC_PIXEL_SIZE,px);XftDefaultSubstitute(display,screen,face);
        hermes_hand=XftFontOpenPattern(display,face);
        if(!hermes_hand){FcPatternDestroy(face);continue;}
        fprintf(stderr,"hermes hand: %s at %.0fpx\n",tries[i],px);
    }
    /* No font file yet: an italic serif still reads as "someone else's voice". */
    if(!hermes_hand)hermes_hand=XftFontOpenName(display,screen,"serif:italic:pixelsize=42");
    if(!hermes_hand)hermes_hand=font;
    hermes_hand_line=hermes_hand->ascent+hermes_hand->descent+8;
    if(hermes_hand_line<40)hermes_hand_line=40;
}
static int hand_width(XftFont *f,const char *s,int n){XGlyphInfo ext;XftTextExtentsUtf8(display,f,(const FcChar8 *)s,n,&ext);return ext.xOff;}
static void hand_draw(XftFont *f,int x,int y,const char *s,int n){XftDrawStringUtf8(textdraw,&black,f,x,y,(const FcChar8 *)s,n);}
/* Word-wrap into (start,length) pairs; returns the number of lines. */
static int hand_wrap(XftFont *f,const char *text,int max_width,int *starts,int *lengths,int max_lines){
    int count=0;const char *p=text;
    while(*p&&count<max_lines){
        if(*p=='\n'){starts[count]=(int)(p-text);lengths[count]=0;++count;++p;continue;}
        const char *end=p,*fit=p,*space=NULL;
        while(*end&&*end!='\n'){
            const char *next=end+1;while(*next&&((unsigned char)*next&0xc0)==0x80)++next;
            if(hand_width(f,p,(int)(next-p))>max_width&&fit>p)break;
            if(*end==' ')space=end;fit=next;end=next;
        }
        if(*end&&*end!='\n'&&space&&space>p)fit=space;
        starts[count]=(int)(p-text);lengths[count]=(int)(fit-p);++count;
        p=fit;while(*p==' ')++p;if(*p=='\n')++p;
    }
    return count;
}
/* The star is drawn, never typed: the Scribe's system fonts may not carry U+2726. */
static void star_at(int cx,int cy,int r,int filled,unsigned long color){
    /* Unit directions for -90..225 degrees in 45 degree steps, x1000. */
    static const int ux[8]={0,707,1000,707,0,-707,-1000,-707},uy[8]={-1000,-707,0,707,1000,707,0,-707};
    XPoint pts[9];
    for(int i=0;i<8;++i){int d=(i%2)?r*30/100:r;pts[i].x=(short)(cx+d*ux[i]/1000);pts[i].y=(short)(cy+d*uy[i]/1000);}
    pts[8]=pts[0];XSetForeground(display,gc,color);
    if(filled)XFillPolygon(display,backing,gc,pts,8,Nonconvex,CoordModeOrigin);
    else {XSetLineAttributes(display,gc,3,LineSolid,CapRound,JoinRound);XDrawLines(display,backing,gc,pts,9,CoordModeOrigin);}
}

/* ---- the sidecar ------------------------------------------------------------- */
static int answers_sidecar_for(const char *ink,char *out,size_t capacity){
    size_t n=strlen(ink);if(n<5||strcmp(ink+n-4,".ink")||n-4+8>=capacity)return -1;
    memcpy(out,ink,n-4);memcpy(out+n-4,".hermes",8);return 0;
}
static char answers_file_buffer[ANSWERS_MAX*(ANSWER_TEXT+ANSWER_HEARD+160)+64];
static int answers_read(const char *path,Answer *list,int *count){
    *count=0;int n=product_file_read(path,answers_file_buffer,sizeof answers_file_buffer);
    if(n<0)return errno==ENOENT?0:-1;
    if(strncmp(answers_file_buffer,"HERMESX1\n",9))return -1;
    const char *p=answers_file_buffer+9,*end=answers_file_buffer+n;
    while(p<end&&*count<ANSWERS_MAX){
        char id[33];unsigned top,bottom,alen,hlen;char state;int used=0;
        if(sscanf(p,"X\t%32[0-9a-f]\t%u\t%u\t%c\t%u\t%u\n%n",id,&top,&bottom,&state,&alen,&hlen,&used)!=6||!used)return -1;
        p+=used;if(strlen(id)!=32||top>65535||bottom>65535||!strchr("wde",state)||alen>=ANSWER_TEXT||hlen>=ANSWER_HEARD||p+alen+hlen+1>end)return -1;
        Answer *a=&list[*count];memset(a,0,sizeof *a);memcpy(a->id,id,33);a->top=(uint16_t)top;a->bottom=(uint16_t)bottom;a->state=state;
        memcpy(a->answer,p,alen);a->answer[alen]=0;p+=alen;memcpy(a->heard,p,hlen);a->heard[hlen]=0;p+=hlen;
        if(*p!='\n')return -1;++p;++*count;
    }
    return 0;
}
static int answers_write(const char *path,const Answer *list,int count){
    if(!count){if(unlink(path)&&errno!=ENOENT)return -1;return 0;}
    size_t at=0;memcpy(answers_file_buffer,"HERMESX1\n",9);at=9;
    for(int i=0;i<count;++i){
        const Answer *a=&list[i];size_t alen=strlen(a->answer),hlen=strlen(a->heard);
        int n=snprintf(answers_file_buffer+at,sizeof answers_file_buffer-at,"X\t%s\t%u\t%u\t%c\t%u\t%u\n",a->id,(unsigned)a->top,(unsigned)a->bottom,a->state,(unsigned)alen,(unsigned)hlen);
        if(n<0||at+(size_t)n+alen+hlen+1>=sizeof answers_file_buffer)return -1;
        at+=(size_t)n;memcpy(answers_file_buffer+at,a->answer,alen);at+=alen;memcpy(answers_file_buffer+at,a->heard,hlen);at+=hlen;answers_file_buffer[at++]='\n';
    }
    return product_file_write(path,answers_file_buffer,at);
}
static void answers_attach(const char *ink,int readonly){
    answer_count=0;answers_path[0]=0;answers_readonly=readonly;reveal_index=-1;
    if(answers_sidecar_for(ink,answers_path,sizeof answers_path)){answers_path[0]=0;return;}
    if(answers_read(answers_path,answers,&answer_count)){answer_count=0;snprintf(status,sizeof status,"Hermes' notes on this page could not be read. Your ink is safe.");}
}
static int answers_have(const char *ink){
    char side[1024];struct stat st;return !answers_sidecar_for(ink,side,sizeof side)&&!stat(side,&st)&&st.st_size>9;
}

/* ---- the queue --------------------------------------------------------------- */
static void ask_clean(char *s){for(;*s;++s)if(*s=='\t'||*s=='\n'||*s=='\r')*s=' ';}
static int ask_queue_save(void){
    static char body[ASK_QUEUE_MAX*1500];size_t at=0;
    for(int i=0;i<ask_count;++i){const Ask *a=&ask_queue[i];
        int n=snprintf(body+at,sizeof body-at,"%s\t%s\t%u\t%u\t%s\t%s\t%s\n",a->id,a->sidecar,(unsigned)a->top,(unsigned)a->bottom,a->kind,a->when[0]?a->when:"-",a->title[0]?a->title:"-");
        if(n<0||at+(size_t)n>=sizeof body)return -1;at+=(size_t)n;}
    char path[1024];if(live_path(path,sizeof path,"asks.queue"))return -1;
    if(!ask_count){if(unlink(path)&&errno!=ENOENT)return -1;return 0;}
    return product_file_write(path,body,at);
}
static void ask_queue_load(void){
    static char body[ASK_QUEUE_MAX*1500+2];char path[1024];ask_count=0;
    if(live_path(path,sizeof path,"asks.queue")||product_file_read(path,body,sizeof body)<0)return;
    char *saved=NULL;
    for(char *line=strtok_r(body,"\n",&saved);line&&ask_count<ASK_QUEUE_MAX;line=strtok_r(NULL,"\n",&saved)){
        char *c=NULL,*f[7];int k=0;for(char *t=strtok_r(line,"\t",&c);t&&k<7;t=strtok_r(NULL,"\t",&c))f[k++]=t;
        if(k!=7||!valid_job(f[0])||strlen(f[1])>=1024)continue;
        Ask *a=&ask_queue[ask_count];memset(a,0,sizeof *a);memcpy(a->id,f[0],33);snprintf(a->sidecar,sizeof a->sidecar,"%s",f[1]);
        a->top=(uint16_t)strtoul(f[2],NULL,10);a->bottom=(uint16_t)strtoul(f[3],NULL,10);
        snprintf(a->kind,sizeof a->kind,"%s",f[4]);snprintf(a->when,sizeof a->when,"%s",strcmp(f[5],"-")?f[5]:"");snprintf(a->title,sizeof a->title,"%s",strcmp(f[6],"-")?f[6]:"");
        char ask_file[80];snprintf(ask_file,sizeof ask_file,"ask-%s.ink",a->id);char ask_path[1024];
        if(live_path(ask_path,sizeof ask_path,ask_file)||access(ask_path,R_OK))continue;   /* snapshot gone: nothing to send */
        ++ask_count;
    }
}
static int ask_waiting_on(const char *sidecar){for(int i=0;i<ask_count;++i)if(!strcmp(ask_queue[i].sidecar,sidecar))return 1;return 0;}
static int ask_copy_request(const char *id){
    char name[80],from[1024],to[1024];snprintf(name,sizeof name,"ask-%s.ink",id);
    if(live_path(from,sizeof from,name)||live_path(to,sizeof to,"request.ink"))return -1;
    FILE *f=fopen(from,"rb");if(!f)return -1;
    static char data[20+MAX_POINTS*6+64];size_t n=fread(data,1,sizeof data,f);int bad=ferror(f)||!feof(f);fclose(f);
    if(bad||!n)return -1;return live_write("request.ink",data,n);
}
static int ask_spawn(void){
    if(ask_child||!ask_count)return 0;
    Ask *a=&ask_queue[0];
    if(!ask_accepted&&ask_copy_request(a->id))return -1;
    char script[1024],topic[400],notebook[400],raw[200];if(live_path(script,sizeof script,"ink-worker.sh"))return -1;
    product_url_encode(a->title,topic,sizeof topic);
    snprintf(raw,sizeof raw,"page|%s|%s",a->kind,a->when);product_url_encode(raw,notebook,sizeof notebook);
    pid_t child=fork();if(child<0)return -1;
    if(child==0){
        close(ConnectionNumber(display));
        execl("/bin/sh","sh",script,ask_accepted?"get":"submit",a->id,"inbox",topic,notebook,"ask","",(char *)NULL);
        _exit(127);
    }
    if(!ask_started)ask_started=seconds();
    ask_child=child;return 0;
}
/* "HEARD:" and "NEXT:" are labels for a screen; on paper they read like a form. */
static void answer_tidy(char *text){
    char out[ANSWER_TEXT];size_t at=0;const char *p=text;
    while(*p&&at+1<sizeof out){
        const char *line=p;while(*p&&*p!='\n')++p;size_t n=(size_t)(p-line);
        if(n>=6&&!strncasecmp(line,"HEARD:",6)){line+=6;n-=6;while(n&&*line==' '){++line;--n;}}
        else if(n>=9&&!strncasecmp(line,"QUESTION:",9)){line+=9;n-=9;while(n&&*line==' '){++line;--n;}}
        else if(n>=5&&!strncasecmp(line,"NEXT:",5)){if(at+6<sizeof out){memcpy(out+at,"Next: ",6);at+=6;}line+=5;n-=5;while(n&&*line==' '){++line;--n;}}
        if(at+n+1>=sizeof out)n=sizeof out-at-2;
        memcpy(out+at,line,n);at+=n;if(*p=='\n'){out[at++]='\n';++p;}
    }
    while(at&&(out[at-1]=='\n'||out[at-1]==' '))--at;out[at]=0;memcpy(text,out,at+1);
}
static void answers_layout(void);
static void answer_render_one(int i);
/* Finish the head ask: write its answer into its page's sidecar and move on. */
static void ask_finish(char state,const char *text,const char *heard){
    if(!ask_count)return;
    Ask done=ask_queue[0];int current=answers_path[0]&&!strcmp(done.sidecar,answers_path);
    Answer *list=current?answers:answers_scratch;int *count=current?&answer_count:&answers_scratch_count;
    if(!current&&answers_read(done.sidecar,answers_scratch,&answers_scratch_count))answers_scratch_count=0;
    int at=-1;for(int i=0;i<*count;++i)if(!strcmp(list[i].id,done.id))at=i;
    if(at<0&&*count<ANSWERS_MAX){at=(*count)++;memset(&list[at],0,sizeof list[at]);memcpy(list[at].id,done.id,33);list[at].top=done.top;list[at].bottom=done.bottom;}
    if(at>=0){
        list[at].state=state;snprintf(list[at].answer,ANSWER_TEXT,"%s",text);snprintf(list[at].heard,ANSWER_HEARD,"%s",heard);
        if(state=='d')answer_tidy(list[at].answer);
        if(answers_write(done.sidecar,list,*count)){snprintf(status,sizeof status,"Could not save Hermes' answer. It will be asked again.");ask_next=seconds()+30;return;}
    }
    memmove(ask_queue,ask_queue+1,sizeof ask_queue[0]*(size_t)(ask_count-1));--ask_count;
    if(ask_queue_save())snprintf(status,sizeof status,"Could not update the question queue. Your ink is safe.");
    char name[80],path[1024];snprintf(name,sizeof name,"ask-%s.ink",done.id);if(!live_path(path,sizeof path,name))unlink(path);
    ask_accepted=0;ask_stage[0]=0;ask_started=0;ask_backoff=0;ask_fail_streak=0;ask_next=seconds();
    if(current&&at>=0){reveal_index=at;reveal_shown=0;reveal_next=seconds()+0.15;product_repaint();}
}
static int ask_http_status(void){char body[8];return live_read("worker.status",body,sizeof body)>0?atoi(body):0;}
static void ask_tick(void){
    if(!ask_count)return;
    double now=seconds();
    if(ask_child){
        int code;pid_t done=waitpid(ask_child,&code,WNOHANG);if(done!=ask_child)return;
        ask_child=0;int why=WIFEXITED(code)?WEXITSTATUS(code):-1;
        if(why!=0){
            int http=why==22&&!ask_accepted?ask_http_status():0;
            ++ask_fail_streak;
            /* A request the service refuses outright will be refused again: say so once
             * and let the rest of the queue go. Everything else is weather. */
            if((http>=400&&http<500&&http!=408&&http!=429)||(why==22&&ask_accepted&&ask_fail_streak>=4)){
                ask_finish('e',"I couldn't take this page just now. Your ink is safe here; tap the star to ask again.","");return;}
            int was=ask_offline;ask_offline=why!=9&&http!=429;
            ask_backoff=ask_backoff<5?5:ask_backoff*2;if(ask_backoff>60)ask_backoff=60;ask_next=now+ask_backoff;
            if(was!=ask_offline)product_repaint();
            return;
        }
        static char wire_buffer[62000];int size=live_read("worker.wire",wire_buffer,sizeof wire_buffer);
        char *newline=size>0?strchr(wire_buffer,'\n'):NULL;
        if(!newline||newline-wire_buffer>=15){ask_next=now+10;return;}
        *newline=0;const char *body=newline+1;
        int was=ask_offline;ask_offline=0;ask_fail_streak=0;ask_backoff=0;ask_accepted=1;snprintf(ask_stage,sizeof ask_stage,"%.15s",wire_buffer);
        if(!strcmp(wire_buffer,"done")){
            const char *answer=strstr(body,"HERMES:\n"),*heard=strstr(body,"I read:\n");
            char heard_text[ANSWER_HEARD];heard_text[0]=0;
            if(heard){heard+=8;const char *stop=answer?answer:heard+strlen(heard);size_t n=(size_t)(stop-heard);if(n>=sizeof heard_text)n=sizeof heard_text-1;memcpy(heard_text,heard,n);heard_text[n]=0;
                while(n&&(heard_text[n-1]=='\n'||heard_text[n-1]==' '))heard_text[--n]=0;}
            ask_finish('d',answer?answer+8:body,heard_text);return;
        }
        if(!strcmp(wire_buffer,"error")||!strcmp(wire_buffer,"uncertain")){ask_finish('e',body[0]?body:"I couldn't finish that one. Your ink is safe; tap the star to ask again.","");return;}
        ask_next=now+2;if(was)product_repaint();
        return;
    }
    if(now>=ask_next&&ask_spawn()){ask_next=now+15;}
}
/* The star. Snapshot the new ink, then queue it. */
static int ask_now(const char *kind,const char *when,const char *title){
    if(answers_readonly||!answers_path[0])return -1;
    if(dirty)store_stroke();
    if(dirty){snprintf(status,sizeof status,"Your last stroke could not be saved, so nothing was sent.");return -1;}
    if(page.count<=page.sent){snprintf(status,sizeof status,"Write something first, then tap the star.");return 0;}
    if(ask_count>=ASK_QUEUE_MAX){snprintf(status,sizeof status,"A few questions are already waiting. I'll get to them first.");return 0;}
    if(answer_count>=ANSWERS_MAX){snprintf(status,sizeof status,"This page is full of answers. Pen, then Clear, gives you a fresh one.");return 0;}
    uint32_t top=65535,bottom=0;int any=0;
    for(uint32_t i=page.sent;i<page.count;++i){const Point *p=&page.points[i];if(p->erase)continue;any=1;if(p->y<top)top=p->y;if(p->y>bottom)bottom=p->y;}
    if(!any){snprintf(status,sizeof status,"Write something first, then tap the star.");return 0;}
    Ask a;memset(&a,0,sizeof a);if(live_id(a.id))return -1;
    snprintf(a.sidecar,sizeof a.sidecar,"%.1000s",answers_path);snprintf(a.kind,sizeof a.kind,"%.15s",kind);snprintf(a.when,sizeof a.when,"%.95s",when);snprintf(a.title,sizeof a.title,"%.199s",title);
    ask_clean(a.kind);ask_clean(a.when);ask_clean(a.title);a.top=(uint16_t)top;a.bottom=(uint16_t)bottom;
    char name[80],path[1024];snprintf(name,sizeof name,"ask-%s.ink",a.id);
    if(live_path(path,sizeof path,name)||save_page_from(path,page.sent)){snprintf(status,sizeof status,"Could not prepare the question. Your ink is safe.");return -1;}
    ask_queue[ask_count++]=a;
    if(ask_queue_save()){--ask_count;unlink(path);snprintf(status,sizeof status,"Could not queue the question. Your ink is safe.");return -1;}
    Answer *slot=&answers[answer_count++];memset(slot,0,sizeof *slot);memcpy(slot->id,a.id,33);slot->top=a.top;slot->bottom=a.bottom;slot->state='w';
    if(answers_write(answers_path,answers,answer_count))snprintf(status,sizeof status,"Asked. (The page's note list could not be saved; the answer will still arrive.)");
    uint32_t old=page.sent;page.sent=page.count;
    if(save_page(state_path)){page.sent=old;}
    if(ask_count==1){ask_next=seconds();ask_started=0;}
    dots_phase=0;dots_next=seconds()+0.9;
    return 1;
}

/* ---- layout and drawing ---------------------------------------------------------- */
static int answers_left(void){return 108;}
static int answers_width(void){return width-45-answers_left()-40;}
static int answer_head_waiting(int i){return ask_count&&answers_path[0]&&!strcmp(ask_queue[0].sidecar,answers_path)&&!strcmp(ask_queue[0].id,answers[i].id);}
static const char *answer_placeholder(int i){
    if(ask_offline)return "I'll answer when we're back online. Your page is saved.";
    if(!answer_head_waiting(i))return "Next in line.";
    return NULL;   /* thinking: drawn as dots */
}
/* Split an answer into its body (NUL-terminated copy) and its page-link lines. */
static int answer_split(const char *text,char *body,size_t capacity,char refs[][200]){
    const char *mark=strstr(text,ANSWER_REF_MARK);
    size_t n=mark?(size_t)(mark-text):strlen(text);if(n>=capacity)n=capacity-1;
    memcpy(body,text,n);body[n]=0;
    int count=0;
    while(mark&&count<ANSWER_REFS_MAX){
        const char *s=mark+strlen(ANSWER_REF_MARK),*e=strchr(s,'\n');size_t m=e?(size_t)(e-s):strlen(s);if(m>=200)m=199;
        memcpy(refs[count],s,m);refs[count][m]=0;++count;mark=e?strstr(e,ANSWER_REF_MARK):NULL;
    }
    return count;
}
/* "<label> · <YYYY-MM-DD>" -> label and date; 0 when the line is not a page link. */
static int answer_ref_parse(const char *ref,char *label,char *date){
    const char *dot=NULL;for(const char *p=strstr(ref," \xc2\xb7 ");p;p=strstr(p+1," \xc2\xb7 "))dot=p;
    if(!dot||dot==ref||strlen(dot+4)!=10)return 0;
    size_t n=(size_t)(dot-ref);if(n>=160)n=159;memcpy(label,ref,n);label[n]=0;memcpy(date,dot+4,11);return 1;
}
/* The row drawn for a link: "<label> · <pretty date>  ›". 0 when not a page link. */
static int answer_ref_row(const char *ref,char *row,size_t capacity){
    char label[160],date[16],day[48];
    if(!answer_ref_parse(ref,label,date))return 0;
    daily_pretty_date(date,day,sizeof day);snprintf(row,capacity,"%.150s \xc2\xb7 %s  \xe2\x80\xba",label,day);return 1;
}
static char answer_body_buffer[ANSWER_TEXT];
static void answers_layout(void){
    int prev=0,lh=hermes_hand_line,starts[64],lengths[64];
    for(int i=0;i<answer_count;++i){
        AnswerBox *b=&answer_box[i];memset(b,0,sizeof *b);
        int y=ink_screen_y(answers[i].bottom)+26;if(prev&&y<prev+18)y=prev+18;
        int lines=1,refs=0;char ref_text[ANSWER_REFS_MAX][200];
        if(answers[i].state!='w'){
            refs=answer_split(answers[i].answer,answer_body_buffer,sizeof answer_body_buffer,ref_text);
            lines=hand_wrap(hermes_hand,answer_body_buffer,answers_width(),starts,lengths,64);
        }
        else if(answer_placeholder(i))lines=hand_wrap(hermes_hand,answer_placeholder(i),answers_width(),starts,lengths,64);
        if(lines<1)lines=1;
        if(lines>ANSWER_INLINE_LINES){b->more=1;lines=ANSWER_INLINE_LINES;}
        int limit=canvas_bottom-12;
        if(y+lines*lh>limit){
            b->more=1;int fit=(limit-y)/lh;
            if(fit>=2)lines=fit;
            else{b->stub=1;lines=1;if(y+lh>limit)y=limit-lh;}
        }
        b->y0=y;b->lines=lines;b->y1=y+lines*lh+8;
        if(!b->stub)for(int r=0;r<refs;++r){          /* links that fit and are not this page */
            char label[160],date[16];
            if(!answer_ref_parse(ref_text[r],label,date)||answer_ref_is_current(label,date))continue;
            if(b->y1+ANSWER_REF_H>limit)break;
            b->ref[b->refs++]=r;b->y1+=ANSWER_REF_H;
        }
        prev=b->y1;
    }
}
static int answer_ref_top(int i,int slot){return answer_box[i].y1-(answer_box[i].refs-slot)*ANSWER_REF_H;}
/* A link's code is i*ANSWER_REFS_MAX + r: answer i, its r-th link line. The page's rows
 * and the slip's rows share it, so one handler opens either. */
static int answer_ref_at(int x,int y){
    if(x<20||x>width-45)return -1;
    for(int i=0;i<answer_count;++i)for(int s=0;s<answer_box[i].refs;++s)
        if(y>=answer_ref_top(i,s)&&y<answer_ref_top(i,s)+ANSWER_REF_H)return i*ANSWER_REFS_MAX+answer_box[i].ref[s];
    return -1;
}
/* The label and date of link `code`. */
static int answer_ref_target(int code,char *label,char *date){
    int i=code/ANSWER_REFS_MAX,r=code%ANSWER_REFS_MAX;char refs[ANSWER_REFS_MAX][200];
    if(i<0||i>=answer_count)return 0;
    int n=answer_split(answers[i].answer,answer_body_buffer,sizeof answer_body_buffer,refs);
    return r<n&&answer_ref_parse(refs[r],label,date);
}
static void answer_bracket(int i){
    int y0=ink_screen_y(answers[i].top)-14,y1=ink_screen_y(answers[i].bottom)+14;
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,5,LineSolid,CapButt,JoinMiter);
    XDrawLine(display,backing,gc,30,y0,30,y1);XDrawLine(display,backing,gc,30,y0,40,y0);XDrawLine(display,backing,gc,30,y1,40,y1);
}
static void answer_dots(int i,int phase){
    AnswerBox *b=&answer_box[i];int y=b->y0+hermes_hand_line/2;
    XSetForeground(display,gc,black_pixel);
    for(int d=0;d<=phase%3;++d)XFillArc(display,backing,gc,answers_left()+d*38,y-8,16,16,0,360*64);
}
static void answer_render_one(int i){
    AnswerBox *b=&answer_box[i];Answer *a=&answers[i];int lh=hermes_hand_line;
    int filled=a->state!='w';
    star_at(62,b->y0+lh/2-4,17,filled,black_pixel);
    if(b->lines>1||a->state!='w'){XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,2,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,62,b->y0+lh/2+22,62,b->y1-10);}
    if(a->state=='w'){
        if(!answers_readonly)answer_bracket(i);
        const char *text=answer_placeholder(i);
        if(!text){answer_dots(i,dots_phase);return;}
        hand_draw(hermes_hand,answers_left(),b->y0+hermes_hand->ascent,text,(int)strlen(text));return;
    }
    if(b->stub){const char *stub="My answer is on a slip. Tap to read it.";hand_draw(hermes_hand,answers_left(),b->y0+hermes_hand->ascent,stub,(int)strlen(stub));return;}
    char refs[ANSWER_REFS_MAX][200];answer_split(a->answer,answer_body_buffer,sizeof answer_body_buffer,refs);
    const char *body=answer_body_buffer;
    int starts[64],lengths[64],total=hand_wrap(hermes_hand,body,answers_width(),starts,lengths,64);
    int text_lines=b->more?b->lines-1:b->lines;          /* when it continues, the last line says so */
    if(text_lines<1)text_lines=1;if(text_lines>total)text_lines=total;
    int revealing=i==reveal_index,visible=revealing?reveal_shown:text_lines;if(visible>text_lines)visible=text_lines;
    for(int l=0;l<visible;++l)hand_draw(hermes_hand,answers_left(),b->y0+l*lh+hermes_hand->ascent,body+starts[l],lengths[l]);
    if(b->more&&(!revealing||reveal_shown>text_lines))product_small_at(answers_left(),b->y0+text_lines*lh+hermes_hand->ascent-6,"continues on a slip  \xe2\x80\xba");
    if(revealing&&reveal_shown<=text_lines)return;       /* links appear once the words have */
    for(int s=0;s<b->refs;++s){
        char row[240];if(answer_ref_row(refs[b->ref[s]],row,sizeof row))product_small_at(answers_left(),answer_ref_top(i,s)+40,row);
    }
}
static void answers_render(void){
    answers_layout();
    for(int i=0;i<answer_count;++i)answer_render_one(i);
}
static int answer_at(int x,int y){
    if(x<20||x>width-45)return -1;
    for(int i=0;i<answer_count;++i)if(y>=answer_box[i].y0-10&&y<answer_box[i].y1)return i;
    return -1;
}
/* One step of the reveal or of the thinking dots: a small rect, never the screen. */
static void answers_animate(void){
    double now=seconds();
    if(reveal_index>=0&&reveal_index<answer_count&&now>=reveal_next){
        AnswerBox *b=&answer_box[reveal_index];int text_lines=b->more?b->lines-1:b->lines;if(text_lines<1)text_lines=1;
        ++reveal_shown;int y=b->y0+(reveal_shown-1)*hermes_hand_line;
        answer_render_one(reveal_index);present(0,y-6,width,hermes_hand_line+16);
        reveal_next=now+0.18;
        if(reveal_shown>text_lines||(!b->more&&reveal_shown>=text_lines)){int finished=reveal_index;reveal_index=-1;answer_render_one(finished);present(0,b->y0-6,width,b->y1-b->y0+16);}
    }
    if(now>=dots_next&&!ask_offline){
        dots_next=now+0.9;
        for(int i=0;i<answer_count;++i)if(answers[i].state=='w'&&answer_head_waiting(i)){
            AnswerBox *b=&answer_box[i];++dots_phase;
            int x=answers_left()-10,y=b->y0,w=3*38+30,h=hermes_hand_line;
            if(ink_cache_ready)XCopyArea(display,ink_cache,backing,gc,x,y,w,h,x,y);
            else{XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,x,y,w,h);}
            answer_dots(i,dots_phase);present(x,y,w,h);
        }
    }
}

/* ---- the slip ------------------------------------------------------------------- */
static int slip_top(void){return height*38/100;}
static int slip_bottom(void){return height-165;}
static int slip_close_left(void){return width-45-34-66;}
static char slip_text[ANSWER_TEXT+ANSWER_HEARD+64],slip_heard[ANSWER_HEARD];
typedef struct { XftFont *f; const char *text; int start,length,height,rule; } SlipRow;
static XftFont *slip_font(void){return slip_kind==SLIP_ANSWER?hermes_hand:font;}
static int slip_line_height(void){return slip_kind==SLIP_ANSWER?hermes_hand_line:48;}
static int slip_refs, slip_ref_raw[ANSWER_REFS_MAX];
static int slip_ref_top(int s){return slip_bottom()-95-(slip_refs-s)*ANSWER_REF_H;}
static void slip_open_answer(int i){
    if(i<0||i>=answer_count||answers[i].state=='w')return;
    slip_kind=SLIP_ANSWER;slip_answer=i;slip_page=0;
    char refs[ANSWER_REFS_MAX][200];answer_split(answers[i].answer,answer_body_buffer,sizeof answer_body_buffer,refs);
    snprintf(slip_text,sizeof slip_text,"%s",answer_body_buffer);snprintf(slip_heard,sizeof slip_heard,"%s",answers[i].heard);
}
static void slip_open_context(const char *title,const char *context){
    slip_kind=SLIP_CONTEXT;slip_page=0;slip_heard[0]=0;
    const char *text=context&&context[0]?context:"Nothing to prepare for this one yet. The page is yours.";(void)title;
    size_t n=strlen(text);if(n>=sizeof slip_text)n=sizeof slip_text-1;memcpy(slip_text,text,n);slip_text[n]=0;
}
static void slip_render(const char *label){
    if(!slip_kind)return;
    int top=slip_top(),bottom=slip_bottom(),left=45,right=width-45;
    XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,left,top,right-left,bottom-top);
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,5,LineSolid,CapButt,JoinMiter);XDrawRectangle(display,backing,gc,left,top,right-left,bottom-top);
    star_at(left+48,top+52,16,1,black_pixel);product_small_at(left+80,top+62,label);
    int bx=slip_close_left(),by=top+24;XSetLineAttributes(display,gc,3,LineSolid,CapButt,JoinMiter);XDrawRectangle(display,backing,gc,bx,by,66,66);text_at(bx+22,by+47,"×",0);
    /* One list of lines, each with its own face: Hermes' words in the hand, and what
     * the page was read as in plain type, so the two can never be confused. */
    static SlipRow rows[520];
    int count=0,starts[400],lengths[400],inner=right-left-110;
    XftFont *f=slip_font();int n=hand_wrap(f,slip_text,inner,starts,lengths,400);
    for(int l=0;l<n&&count<520;++l)rows[count++]=(SlipRow){f,slip_text,starts[l],lengths[l],slip_line_height(),0};
    if(slip_heard[0]&&count<518){
        rows[count++]=(SlipRow){small,"",0,0,50,1};
        rows[count++]=(SlipRow){small,"WHAT I READ ON YOUR PAGE",0,24,48,0};
        n=hand_wrap(font,slip_heard,inner,starts,lengths,400);
        for(int l=0;l<n&&count<520;++l)rows[count++]=(SlipRow){font,slip_heard,starts[l],lengths[l],46,0};
    }
    /* An answer read on a slip keeps its page links: they sit above the footer. */
    slip_refs=0;
    if(slip_kind==SLIP_ANSWER&&slip_answer>=0&&slip_answer<answer_count){
        char refs[ANSWER_REFS_MAX][200],ref_label[160],ref_date[16];
        int n=answer_split(answers[slip_answer].answer,answer_body_buffer,sizeof answer_body_buffer,refs);
        for(int r=0;r<n;++r)if(answer_ref_parse(refs[r],ref_label,ref_date)&&!answer_ref_is_current(ref_label,ref_date))slip_ref_raw[slip_refs++]=r;
    }
    int area=bottom-95-(top+120)-slip_refs*ANSWER_REF_H,page_index=0,used=0,first[64];first[0]=0;
    for(int r=0;r<count;++r){if(used+rows[r].height>area&&used>0){if(page_index<63)first[++page_index]=r;used=0;}used+=rows[r].height;}
    slip_pages=page_index+1;if(slip_page>=slip_pages)slip_page=0;
    int y=top+120,end=slip_page+1<slip_pages?first[slip_page+1]:count;
    for(int r=first[slip_page];r<end;++r){
        if(rows[r].rule){XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,left+55,y+25,right-55,y+25);}
        else if(rows[r].length)hand_draw(rows[r].f,left+55,y+rows[r].f->ascent,rows[r].text+rows[r].start,rows[r].length);
        y+=rows[r].height;
    }
    for(int s=0;s<slip_refs;++s){                         /* the answer's page links, on every slip page */
        char refs[ANSWER_REFS_MAX][200],row[240];
        answer_split(answers[slip_answer].answer,answer_body_buffer,sizeof answer_body_buffer,refs);
        if(answer_ref_row(refs[slip_ref_raw[s]],row,sizeof row))product_small_at(left+55,slip_ref_top(s)+40,row);
    }
    product_rule(bottom-95);
    char position[48];snprintf(position,sizeof position,"%d / %d",slip_page+1,slip_pages);product_small_at(left+50,bottom-38,position);
    const char *next=slip_page+1<slip_pages?"Next  ›":"Close";int w=hand_width(font,next,(int)strlen(next));text_at(right-60-w,bottom-32,next,0);
}
enum { SLIP_HIT_NONE, SLIP_HIT_CLOSE, SLIP_HIT_NEXT, SLIP_HIT_INSIDE, SLIP_HIT_REF_0 };
static int slip_hit(int x,int y){
    if(!slip_kind)return SLIP_HIT_NONE;
    int top=slip_top(),bottom=slip_bottom();
    if(x>=slip_close_left()-14&&x<slip_close_left()+80&&y>=top+10&&y<top+104)return SLIP_HIT_CLOSE;
    for(int s=0;s<slip_refs;++s)if(x>=45&&x<width-45&&y>=slip_ref_top(s)&&y<slip_ref_top(s)+ANSWER_REF_H)return SLIP_HIT_REF_0+s;
    if(y>=bottom-95&&y<bottom&&x>=width/2)return SLIP_HIT_NEXT;
    if(y>=top&&y<bottom&&x>=45&&x<width-45)return SLIP_HIT_INSIDE;
    return SLIP_HIT_CLOSE;           /* a tap outside the slip puts it away */
}
#endif
