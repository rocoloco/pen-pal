#ifndef HERMES_PRODUCT_UI_H
#define HERMES_PRODUCT_UI_H

#include <dirent.h>
#include <ctype.h>
#include "daily-model.h"

#define PRODUCT_SPACE_COUNT 4
#define PRODUCT_HISTORY_MAX 80
#define PRODUCT_BRIEF_MAX DAILY_WIRE_MAX

enum ProductView { PRODUCT_TODAY, PRODUCT_NOTEBOOK, PRODUCT_FOCUS, PRODUCT_HISTORY, PRODUCT_ARCHIVE };
enum ProductAction {
    PRODUCT_NONE=-1, PRODUCT_SPACES=1, PRODUCT_WRITE, PRODUCT_HISTORY_ACTION,
    PRODUCT_UNDO, PRODUCT_SEND, PRODUCT_NEW_PAGE, PRODUCT_CLOSE, PRODUCT_TODAY_ACTION,
    PRODUCT_EXIT_FOCUS, PRODUCT_MORE, PRODUCT_BACK, PRODUCT_POPUP_CLOSE,
    PRODUCT_SPACE_WORK=30, PRODUCT_SPACE_IDEAS, PRODUCT_SPACE_HOME, PRODUCT_SPACE_INBOX,
    PRODUCT_PRIORITY_0=50, PRODUCT_PRIORITY_1, PRODUCT_PRIORITY_2,
    PRODUCT_HISTORY_0=100,
    /* Today-card chips. One card, three verbs: start it, not today, let it go. */
    PRODUCT_START_0=60, PRODUCT_START_1, PRODUCT_START_2,
    PRODUCT_SNOOZE_0=70, PRODUCT_SNOOZE_1, PRODUCT_SNOOZE_2,
    PRODUCT_DROP_0=80, PRODUCT_DROP_1, PRODUCT_DROP_2,
    PRODUCT_CLOSE_DAY=90,
    /* The verb the loop was missing: finish. Files the page, closes the card. */
    PRODUCT_DONE=95,
    /* Appears only when Hermes has a locally recorded PREPARE action. */
    PRODUCT_CONFIRM_ACTION=96,
    /* Clear page: lives in the pen rail, files the page first, never discards ink. */
    PRODUCT_INK_CLEAR_PAGE=98,
    PRODUCT_INK_TOGGLE=200, PRODUCT_INK_COLLAPSE, PRODUCT_INK_PEN,
    PRODUCT_INK_HIGHLIGHTER, PRODUCT_INK_ERASER, PRODUCT_INK_UNDO,
    PRODUCT_INK_PEN_STYLE_0=220, PRODUCT_INK_PEN_STYLE_1, PRODUCT_INK_PEN_STYLE_2, PRODUCT_INK_PEN_STYLE_3,
    PRODUCT_INK_PEN_WIDTH_0=230, PRODUCT_INK_PEN_WIDTH_1, PRODUCT_INK_PEN_WIDTH_2, PRODUCT_INK_PEN_WIDTH_3, PRODUCT_INK_PEN_WIDTH_4,
    PRODUCT_INK_HIGHLIGHT_WIDTH_0=240, PRODUCT_INK_HIGHLIGHT_WIDTH_1, PRODUCT_INK_HIGHLIGHT_WIDTH_2, PRODUCT_INK_HIGHLIGHT_WIDTH_3,
    PRODUCT_INK_ERASER_WIDTH_0=250, PRODUCT_INK_ERASER_WIDTH_1, PRODUCT_INK_ERASER_WIDTH_2, PRODUCT_INK_ERASER_WIDTH_3
};

typedef struct { const char *slug, *label; } ProductSpace;
static const ProductSpace product_spaces[PRODUCT_SPACE_COUNT]={
    {"work","WORK"},{"projects","PROJECTS"},{"home","HOME"},{"inbox","INBOX"}
};
typedef struct { char id[128],space[24],title[100],next[180],age[24]; } ProductPriority;
typedef struct { char ink[1024],reply[1024],title[100],date[48],space[24],kind[16],when_text[96]; time_t when; } ProductHistory;

static enum ProductView product_view=PRODUCT_TODAY;
static int product_space=1, product_popup, product_focus_index, product_history_selection, product_history_offset;
static int product_ink_toolbar_expanded, product_ink_menu;
static int product_priority_y[3];
static char product_root[1024],product_history_dir[1024],product_state_file[1024],product_reply_file[1024];
static ProductHistory product_history[PRODUCT_HISTORY_MAX];
static int product_history_count;
static ProductPriority product_priorities[3];
static int product_priority_count;
static char product_greeting[180]="Good morning. Hermes is preparing your day.";
static char product_waiting[3][180],product_new[3][180];
static int product_waiting_count,product_new_count;
/* Day context for the top of Today: the streak, how long since the last note, and
 * how many loops are still open. All three come from the brief; when the server does
 * not send them (older wire format) the line is simply omitted. */
static char product_day_number[16],product_since[120];
static int product_loops;
/* "Closed: <card>" or "Filed to History", shown on the Today page right after DONE --
 * finishing something has to be visible, or the loop feels like it did nothing. */
static char product_closed_note[160];
/* "What moved / what's stuck / waiting / tomorrow" prompts shown on the closing page,
 * and the topic stamped on the current page so Send has something to name. */
static char product_close_day_topic[120]="CLOSE THE DAY";
static char product_empty_guide[180]="WHAT MOVED \xc2\xb7 WHAT IS STUCK \xc2\xb7 WAITING ON \xc2\xb7 TOMORROW ONE THING";
static char product_topic[120];
static char product_brief_day[16],product_brief_stage[20]="connecting";
static pid_t product_brief_child;
static double product_brief_next;
static int product_connection_ready;

static void product_repaint(void);
static void product_url_encode(const char *in,char *out,size_t capacity);
static int product_archive_current(void);
static int product_load_brief_wire(const char *path,int cache);
static const char *product_space_slug(void){return product_spaces[product_space].slug;}
static const char *product_label_for_slug(const char *slug){for(int i=0;i<PRODUCT_SPACE_COUNT;++i)if(!strcmp(slug,product_spaces[i].slug))return product_spaces[i].label;return "INBOX";}
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
/* ---- Today reactions, the topic stamp, and the closing page -------------------
 * "Not today", "let it go" and "done" are kept on the device and carried to Hermes
 * with the next Send and with the next brief request. The brief is written
 * server-side, so a reaction that never leaves the device would simply come back as
 * the same card tomorrow. New clients write action<TAB>date<TAB>id<TAB>title;
 * older title-only lines remain readable, so the page can count what was closed
 * TODAY without guessing. */
static char product_reactions[1600];
static int product_reactions_loaded;
static void product_today_stamp(char out[16]){
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);strftime(out,16,"%Y-%m-%d",&local);
}
static void product_reactions_load(void){
    char path[1024];product_reactions[0]=0;product_reactions_loaded=1;
    if(!product_join(path,sizeof path,product_root,"today.react"))product_file_read(path,product_reactions,sizeof product_reactions);
}
static int product_reactions_has(const char *action,const char *id,const char *title){
    char prefix[64];if(!product_reactions_loaded)product_reactions_load();
    snprintf(prefix,sizeof prefix,"%s\t",action);
    size_t plen=strlen(prefix);
    for(const char *line=product_reactions;line&&*line;){
        const char *next=strchr(line,'\n');size_t len=next?(size_t)(next-line):strlen(line);
        if(len>plen&&!strncmp(line,prefix,plen)){
            char copy[420];size_t n=len<sizeof copy-1?len:sizeof copy-1;memcpy(copy,line,n);copy[n]=0;
            char *fields[4]={0};char *part=strtok(copy,"\t");int count=0;
            while(part&&count<4){fields[count++]=part;part=strtok(NULL,"\t");}
            if(count>=4&&fields[2]&&fields[2][0]){
                /* New-format records are identity-based. Do not let a second
                 * item with the same title inherit this reaction -- but KEEP
                 * SCANNING. Returning here made the first record of an action
                 * shadow every later one, so only the first card he ever
                 * dropped could be hidden and every other tap did nothing. */
                if(id&&id[0]&&!strcmp(fields[2],id))return 1;
            }
            else{
                /* Legacy records have action/date/title, and new records also retain
                 * the title so a migration never makes an old card reappear. */
                const char *record_title=count>=4?fields[3]:count>=3?fields[2]:"";
                if(title&&title[0]&&!strcmp(record_title,title))return 1;
            }
        }
        line=next?next+1:NULL;
    }
    return 0;
}
static int product_reactions_count_today(const char *action){
    char prefix[64],day[16];if(!product_reactions_loaded)product_reactions_load();
    product_today_stamp(day);snprintf(prefix,sizeof prefix,"%s\t%s\t",action,day);
    int count=0;size_t plen=strlen(prefix);
    for(const char *line=product_reactions;line&&*line;line=strchr(line,'\n')?strchr(line,'\n')+1:NULL)
        if(!strncmp(line,prefix,plen))++count;
    return count;
}
static void product_reactions_add(const char *action,const char *id,const char *title){
    char path[1024],line[420],day[16];product_reactions_load();product_today_stamp(day);
    if(product_reactions_has(action,id,title))return;   /* already recorded: never grow the file with duplicates */
    snprintf(line,sizeof line,"%s\t%s\t%s\t%s\n",action,day,id?id:"",title?title:"");
    if(strlen(product_reactions)+strlen(line)<sizeof product_reactions-1)strcat(product_reactions,line);
    if(!product_join(path,sizeof path,product_root,"today.react"))product_file_write(path,product_reactions,strlen(product_reactions));
}
/* A card is off Today if he deferred, dropped or finished it. */
static int product_card_hidden(const ProductPriority *priority){
    return product_reactions_has("snooze",priority->id,priority->title)||product_reactions_has("drop",priority->id,priority->title)||product_reactions_has("done",priority->id,priority->title);
}
/* Does this page's topic name one of today's cards? Compared by WORDS, not by prefix:
 * the brief is regenerated each day and the wording drifts, so "Clarify garbled work
 * note" and "Clarify the garbled work Note" are the same task to a person, and so are
 * "Call accountant" and "Call the accountant". */
static int product_alnum(unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z');}
static unsigned char product_lower(unsigned char c){return (c>='A'&&c<='Z')?(unsigned char)(c+32):c;}
static int product_has_word(const char *haystack,const char *word,size_t length){
	for(const char *p=haystack;*p;){
		while(*p&&!product_alnum((unsigned char)*p))++p;
		const char *start=p;while(*p&&product_alnum((unsigned char)*p))++p;
		size_t n=(size_t)(p-start);
		if(n>=length&&n-length<=1){    /* allow a trailing plural */
			int same=1;
			for(size_t i=0;i<length;++i)if(product_lower((unsigned char)start[i])!=product_lower((unsigned char)word[i])){same=0;break;}
			if(same)return 1;
		}
	}
	return 0;
}
static int product_task_matches(const char *a,const char *b){
    const char *shorter=strlen(a)<=strlen(b)?a:b,*longer=strlen(a)<=strlen(b)?b:a;
    int words=0;
    for(const char *p=shorter;*p;){
        while(*p&&!product_alnum((unsigned char)*p))++p;
        const char *start=p;while(*p&&product_alnum((unsigned char)*p))++p;
        if(p==start)break;
        size_t n=(size_t)(p-start);
        if(n<3)continue;                            /* "a", "of", "to" carry no signal */
        if(!product_has_word(longer,start,n))return 0;
        ++words;
    }
    return words>=2;                                /* two shared real words is a match */
}
static int product_topic_card(void){
    if(!product_topic[0])return -1;
    for(int i=0;i<product_priority_count;++i)if(product_task_matches(product_topic,product_priorities[i].title))return i;
    return -1;
}
/* Notes for one task must never bleed into another. There is one page per notebook, so
 * opening a DIFFERENT card on a page that already holds ink would otherwise mix two
 * tasks under one heading -- which is how his first session ended up with the same
 * answer written three times. File the old page instead of discarding it. */
static int product_prepare_task_page(const char *title){
    if(!page.count||product_task_matches(product_topic,title))return 0;
    if(product_archive_current()||page.count)return -1;      /* it sets its own status */
    snprintf(status,sizeof status,"The previous page is in Review. This page is for: %.60s",title);
    return 0;
}
/* The topic line stamped on the current page: what this page is about. Send uses it
 * to name the note, and the closing page uses it to preload the ritual's questions. */
static int product_topic_path(char *out,size_t capacity){
    char leaf[64];snprintf(leaf,sizeof leaf,"%s.topic",product_space_slug());
    return product_join(out,capacity,product_root,leaf);
}
static void product_topic_load(void){
    char path[1024];product_topic[0]=0;
    if(product_topic_path(path,sizeof path))return;
    if(product_file_read(path,product_topic,sizeof product_topic)<0){product_topic[0]=0;return;}
    char *newline=strchr(product_topic,'\n');if(newline)*newline=0;
}
static void product_topic_set(const char *text){
    char path[1024];snprintf(product_topic,sizeof product_topic,"%s",text);
    if(product_topic_path(path,sizeof path))return;
    product_file_write(path,product_topic,strlen(product_topic));
}
static void product_topic_clear(void){
    char path[1024];product_topic[0]=0;
    if(!product_topic_path(path,sizeof path))unlink(path);
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
static int product_copy(const char *from,const char *to){
    int in=open(from,O_RDONLY);if(in<0)return -1;int out=open(to,O_WRONLY|O_CREAT|O_EXCL,0600);if(out<0){close(in);return -1;}
    char buffer[8192];int bad=0;for(;;){ssize_t n=read(in,buffer,sizeof buffer);if(n<0){bad=1;break;}if(!n)break;ssize_t done=0;while(done<n){ssize_t w=write(out,buffer+done,(size_t)(n-done));if(w<=0){bad=1;break;}done+=w;}if(bad)break;}
    if(fsync(out))bad=1;close(in);if(close(out))bad=1;if(bad){unlink(to);return -1;}return 0;
}
static int product_set_paths(void){
    char leaf[64];int n=snprintf(leaf,sizeof leaf,"%s.ink",product_space_slug());if(n<0||(size_t)n>=sizeof leaf||product_join(product_state_file,sizeof product_state_file,product_root,leaf))return -1;
    n=snprintf(leaf,sizeof leaf,"%s.reply",product_space_slug());if(n<0||(size_t)n>=sizeof leaf||product_join(product_reply_file,sizeof product_reply_file,product_root,leaf))return -1;
    state_path=product_state_file;return 0;
}
static void product_today_date(void){
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);strftime(product_brief_day,sizeof product_brief_day,"%Y-%m-%d",&local);
}
static void product_display_date(time_t when,char out[48]){
    struct tm local;localtime_r(&when,&local);strftime(out,48,"%A · %B %d",&local);
    for(char *p=out;*p;++p)*p=(char)toupper((unsigned char)*p);
}
static int product_prepare_storage(void){
    if(product_join(product_root,sizeof product_root,live_dir,getenv("HERMES_DAILY_FILE")?"daily-workspace":"workspace-v2"))return -1;
    if(product_join(product_history_dir,sizeof product_history_dir,product_root,"history"))return -1;
    if(mkdir(product_root,0700)&&errno!=EEXIST)return -1;
    if(mkdir(product_history_dir,0700)&&errno!=EEXIST)return -1;
    char selected[1024];if(product_join(selected,sizeof selected,product_root,"selected"))return -1;
    char value[40];if(product_file_read(selected,value,sizeof value)>0){for(int i=0;i<PRODUCT_SPACE_COUNT;++i)if(!strncmp(value,product_spaces[i].slug,strlen(product_spaces[i].slug)))product_space=i;}
    char marker[1024];if(product_join(marker,sizeof marker,product_root,"imported-v1"))return -1;
    if(!getenv("HERMES_DAILY_FILE")&&access(marker,F_OK)){
        char old[1024],ink[1024],meta[1024],reply[1024];
        if(product_join(old,sizeof old,live_dir,"page.ink"))return -1;
        time_t now=time(NULL);char id[32];struct tm local;localtime_r(&now,&local);strftime(id,sizeof id,"%Y%m%d-%H%M%S",&local);
        char leaf[96];int leaf_n=snprintf(leaf,sizeof leaf,"%s-inbox-imported.ink",id);if(leaf_n<0||(size_t)leaf_n>=sizeof leaf||product_join(ink,sizeof ink,product_history_dir,leaf))return -1;
        if(!access(old,R_OK)){
            if(product_copy(old,ink))return -1;
            leaf_n=snprintf(leaf,sizeof leaf,"%s-inbox-imported.meta",id);if(leaf_n<0||(size_t)leaf_n>=sizeof leaf||product_join(meta,sizeof meta,product_history_dir,leaf))return -1;
            char date[48],body[256];product_display_date(now,date);int n=snprintf(body,sizeof body,"inbox\nImported Hermes Ink page\n%s\n",date);
            if(product_file_write(meta,body,(size_t)n))return -1;
            if(product_join(old,sizeof old,live_dir,"last.wire"))return -1;leaf_n=snprintf(leaf,sizeof leaf,"%s-inbox-imported.reply",id);if(leaf_n<0||(size_t)leaf_n>=sizeof leaf||product_join(reply,sizeof reply,product_history_dir,leaf))return -1;
            if(!access(old,R_OK))product_copy(old,reply);
        }
        if(product_file_write(marker,"HERMES_WORKSPACE_V2\n",20))return -1;
    }
    if(getenv("HERMES_DAILY_FILE"))product_space=3;
    if(product_set_paths())return -1;product_load_ink_settings();product_today_date();
    if(getenv("HERMES_DAILY_FILE")&&(product_load_brief_wire(getenv("HERMES_DAILY_FILE"),0)||!daily_enabled))return -1;
    if(daily_enabled)snprintf(status,sizeof status,"Ink saves on this Scribe.");
    /* Today is the front door on every fresh launch. The selected notebook remains
     * available behind Write; reopening the last canvas would turn the command center
     * back into a notebook app. */
    product_view=PRODUCT_TODAY;product_popup=0;return 0;
}
static void product_save_selected(void){
    char path[1024];if(product_join(path,sizeof path,product_root,"selected"))return;
    product_file_write(path,product_space_slug(),strlen(product_space_slug()));
}
static void product_load_reply(void){
    live_body[0]=0;product_file_read(product_reply_file,live_body,sizeof live_body);live_page=0;live_wrap();
}
static int product_needs_resume(void){
    return strstr(product_since,"days since")!=NULL;
}
static int product_reply_needs_confirmation(void){
    const char *answer=live_body[0]?strstr(live_body,"HERMES:\n"):NULL;
    return answer&&strstr(answer+8,"PREPARE:")!=NULL;
}
static void product_save_reply_current(void){
    if(live_body[0])product_file_write(product_reply_file,live_body,strlen(live_body));
}
static int product_switch_space(int next){
    if(next<0||next>=PRODUCT_SPACE_COUNT)return -1;
    if(live_job[0]&&!live_terminal){snprintf(status,sizeof status,"Hermes is finishing this page. Switching will be ready shortly.");return -1;}
    live_abandon_terminal();
    if(dirty)store_stroke();product_save_reply_current();
    product_space=next;if(product_set_paths())return -1;memset(&page,0,sizeof page);ink_cache_invalidate();
    if(load_page(state_path)){snprintf(status,sizeof status,"This notebook could not be opened. Other pages are safe.");return -1;}
    product_load_reply();product_save_selected();product_topic_load();product_day_closed_load();product_view=PRODUCT_NOTEBOOK;product_popup=0;product_ink_toolbar_expanded=0;product_ink_menu=0;
    snprintf(status,sizeof status,"%s notebook",product_spaces[product_space].label);return 0;
}
static const char *product_answer(void){
    char *answer=strstr(live_body,"HERMES:\n");return answer?answer+8:live_body;
}
static void product_title(char out[100]){
    if(daily_enabled&&daily_selected>=0){snprintf(out,100,"%.99s",daily.entries[daily_selected].title);return;}
    snprintf(out,100,"%s note",product_spaces[product_space].label);
    char *read=strstr(live_body,"I read:\n");if(!read)return;read+=8;
    size_t n=strcspn(read,"\r\n");if(n>80)n=80;if(n){memcpy(out,read,n);out[n]=0;}
}
static int product_archive_current(void){
    if(live_job[0]&&!live_terminal){snprintf(status,sizeof status,"Wait for Hermes to finish before starting a new page.");return -1;}
    if(dirty)store_stroke();if(!page.count){snprintf(status,sizeof status,"This page is already blank.");return 0;}
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);char id[32],leaf[80],base[1024],ink[1024],meta[1024],reply[1024];
    strftime(id,sizeof id,"%Y%m%d-%H%M%S",&local);int leaf_n=snprintf(leaf,sizeof leaf,"%s-%s",id,product_space_slug());if(leaf_n<0||(size_t)leaf_n>=sizeof leaf||product_join(base,sizeof base,product_history_dir,leaf))return -1;
    if(product_append(ink,sizeof ink,base,".ink")||save_page(ink))return -1;
    char title[100],date[48],body[320];product_title(title);product_display_date(now,date);int n=snprintf(body,sizeof body,"%s\n%s\n%s\n",product_space_slug(),title,date);
    if(product_append(meta,sizeof meta,base,".meta")||product_file_write(meta,body,(size_t)n))return -1;
    if(live_body[0]){if(product_append(reply,sizeof reply,base,".reply")||product_file_write(reply,live_body,strlen(live_body)))return -1;}
    live_abandon_terminal();memset(&page,0,sizeof page);if(save_page(state_path)){load_page(ink);return -1;}
    live_body[0]=0;live_page=0;unlink(product_reply_file);snprintf(status,sizeof status,"New page ready. The previous page is in Review.");return 0;
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
    product_history_count=0;if(daily_enabled)product_scan_daily_history();DIR *dir=opendir(product_history_dir);if(!dir){qsort(product_history,(size_t)product_history_count,sizeof product_history[0],product_history_compare);return;}struct dirent *entry;
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
    if(index<0||index>=product_history_count||(live_job[0]&&!live_terminal))return -1;if(dirty)store_stroke();product_save_reply_current();live_abandon_terminal();
    if(load_page(product_history[index].ink))return -1;ink_cache_invalidate();ink_undo_valid=0;
    live_body[0]=0;product_file_read(product_history[index].reply,live_body,sizeof live_body);live_page=0;live_wrap();product_history_selection=index;daily_history_context=0;daily_history_context_page=0;product_view=PRODUCT_ARCHIVE;return 0;
}
static void product_restore_current(void){
    memset(&page,0,sizeof page);load_page(state_path);product_load_reply();product_view=PRODUCT_HISTORY;
}

static void product_parse_brief(char *body){
    product_priority_count=product_waiting_count=product_new_count=0;
    product_day_number[0]=0;product_since[0]=0;product_loops=0;
    char *save=NULL;for(char *line=strtok_r(body,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
        char *type=strtok(line,"\t"),*a=strtok(NULL,"\t"),*b=strtok(NULL,"\t"),*c=strtok(NULL,"\t"),*d=strtok(NULL,"\t"),*e=strtok(NULL,"\t");if(!type||!a)continue;
        if(!strcmp(type,"GREETING"))snprintf(product_greeting,sizeof product_greeting,"%s",a);
        else if(!strcmp(type,"DAY"))snprintf(product_day_number,sizeof product_day_number,"%s",a);
        else if(!strcmp(type,"SINCE"))snprintf(product_since,sizeof product_since,"%s",a);
        else if(!strcmp(type,"LOOPS"))product_loops=atoi(a);
        else if(!strcmp(type,"ITEM")&&b&&c&&product_priority_count<3){
            ProductPriority *p=&product_priorities[product_priority_count++];memset(p,0,sizeof *p);snprintf(p->space,sizeof p->space,"%s",a);
            if(e){snprintf(p->id,sizeof p->id,"%s",b);snprintf(p->title,sizeof p->title,"%s",c);snprintf(p->next,sizeof p->next,"%s",d);snprintf(p->age,sizeof p->age,"%s",!strcmp(e,"-")?"":e);}
            else {snprintf(p->title,sizeof p->title,"%s",b);snprintf(p->next,sizeof p->next,"%s",c);snprintf(p->age,sizeof p->age,"%s",d?d:"");}
        }
        else if(!strcmp(type,"WAIT")&&product_waiting_count<3)snprintf(product_waiting[product_waiting_count++],180,"%s",a);
        else if(!strcmp(type,"NEW")&&product_new_count<3)snprintf(product_new[product_new_count++],180,"%s",a);
    }
}
static int product_load_brief_wire(const char *path,int cache){
    char wire[PRODUCT_BRIEF_MAX];int n=product_file_read(path,wire,sizeof wire);if(n<0||memchr(wire,0,(size_t)n))return -1;char *newline=strchr(wire,'\n');if(!newline)return -1;*newline=0;
    size_t stage_length=strlen(wire);if(stage_length>=sizeof product_brief_stage)return -1;memcpy(product_brief_stage,wire,stage_length+1);
    if(!strcmp(wire,"ready")){
        if(!strncmp(newline+1,"FORMAT\tHINKDAILY1\n",18)){if(daily_parse(newline+1))return -1;}
        else {if(daily_enabled)return -1;product_parse_brief(newline+1);}
        product_connection_ready=1;
    }
    (void)cache;
    return 0;
}
static void product_save_brief_cache(const char *wire,size_t length){char path[1024];if(!product_join(path,sizeof path,product_root,"brief-cache.wire"))product_file_write(path,wire,length);}
static int product_brief_spawn(const char *mode){
    if(product_brief_child)return 0;char script[1024];if(product_join(script,sizeof script,live_dir,"brief-worker.sh"))return -1;
    /* His reactions to today's cards ride with the brief request too: the brief is
     * written server-side, so without them a card he already closed comes back
     * tomorrow as if nothing had happened. */
    static char reactions[1400];product_reactions_load();product_url_encode(product_reactions,reactions,sizeof reactions);
    pid_t child=fork();if(child<0)return -1;
    if(child==0){close(ConnectionNumber(display));execl("/bin/sh","sh",script,mode,product_brief_day,reactions,(char *)NULL);_exit(127);}product_brief_child=child;return 0;
}
static void product_brief_start(void){
    const char *native_file=getenv("HERMES_DAILY_FILE");
    if(native_file){if(product_load_brief_wire(native_file,0))snprintf(status,sizeof status,"Daily bundle could not be opened. Existing ink is safe.");return;}
    char cache[1024];if(!product_join(cache,sizeof cache,product_root,"brief-cache.wire"))product_load_brief_wire(cache,1);snprintf(product_brief_stage,sizeof product_brief_stage,"connecting");product_brief_spawn("submit");product_brief_next=seconds()+3;
}
/* product_day_closed_today is defined further down (with the closing-ritual state); the
 * brief tick needs it earlier, so declare it here. Missing this cost a build. */
static int product_day_closed_today(void);
static void product_brief_tick(void){
    if(getenv("HERMES_DAILY_FILE"))return;
    double now=seconds();
    char today[16];product_today_stamp(today);
    if(strcmp(today,product_brief_day)){
        snprintf(product_brief_day,sizeof product_brief_day,"%s",today);
        product_priority_count=0;product_waiting_count=product_new_count=0;
        snprintf(product_greeting,sizeof product_greeting,"Hermes is preparing today.");
        product_connection_ready=0;snprintf(product_brief_stage,sizeof product_brief_stage,"connecting");
        if(!product_brief_child){product_brief_spawn("submit");product_brief_next=now+3;}
        product_repaint();return;
    }
    /* The day's closure lives in a file and can change while we run: a backfill, another
     * surface, or his own tap elsewhere. It used to be read only when a notebook was
     * opened, so a marker written after startup never took effect and the CLOSE THE DAY
     * band stayed up on a day he had already closed. Re-read on a slow tick, and repaint
     * only when the answer actually changes. */
    {static double day_check;if(now-day_check>15){day_check=now;int was=product_day_closed_today();product_day_closed_load();if(was!=product_day_closed_today())product_repaint();}}if(product_brief_child){int code;pid_t done=waitpid(product_brief_child,&code,WNOHANG);if(done==product_brief_child){product_brief_child=0;if(WIFEXITED(code)&&WEXITSTATUS(code)==0){char wire_path[1024],wire[PRODUCT_BRIEF_MAX];if(product_join(wire_path,sizeof wire_path,live_dir,"brief.wire"))return;int n=product_file_read(wire_path,wire,sizeof wire);if(n>0){product_load_brief_wire(wire_path,0);if(!strncmp(wire,"ready\n",6))product_save_brief_cache(wire,(size_t)n);product_repaint();}product_brief_next=strcmp(product_brief_stage,"ready")?now+2:now+300;}else{snprintf(product_brief_stage,sizeof product_brief_stage,"offline");product_brief_next=now+15;product_repaint();}}}
    if(!product_brief_child&&strcmp(product_brief_stage,"ready")&&now>=product_brief_next)product_brief_spawn("get");
    /* A ready brief is not the end of the story: the server rewrites the day's brief when
     * he closes things, so re-fetch it periodically instead of showing a stale page until
     * the next restart. */
    else if(!product_brief_child&&!strcmp(product_brief_stage,"ready")&&now>=product_brief_next){
        product_brief_spawn("get");product_brief_next=now+300;
    }
}

static void product_small_at(int x,int y,const char *s){XftDrawStringUtf8(textdraw,&black,small,x,y,(const FcChar8 *)s,(int)strlen(s));}
static int product_wrap_at(int x,int y,int max_width,const char *text,int max_lines,int line_height,int use_small){
    const char *p=text;int lines=0;while(*p&&lines<max_lines){while(*p==' '||*p=='\n')++p;if(!*p)break;const char *end=p,*fit=p,*space=p;
        while(*end&&*end!='\n'){const char *next=end+1;while(*next&&((unsigned char)*next&0xc0)==0x80)++next;XGlyphInfo ext;XftFont *f=use_small?small:font;XftTextExtentsUtf8(display,f,(const FcChar8 *)p,(int)(next-p),&ext);if(ext.xOff>max_width)break;if(*end==' ')space=end;fit=next;end=next;}
        if(*end&&*end!='\n'&&space>p)fit=space;size_t n=(size_t)(fit-p);char line[512];if(n>=sizeof line)n=sizeof line-1;memcpy(line,p,n);line[n]=0;if(use_small)product_small_at(x,y+lines*line_height,line);else text_at(x,y+lines*line_height,line,0);++lines;p=fit;while(*p==' ')++p;if(*p=='\n')++p;
    }return lines;
}
static void product_rule(int y){XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,1,LineSolid,CapButt,JoinMiter);XDrawLine(display,backing,gc,45,y,width-45,y);}
/* The app-level close button, top-right of the header. It is the SECOND X in the
 * UI and it does a different job from the popup's: this one closes the app, the
 * popup's closes the space selector. Both are always live, so neither has to be
 * "sometimes a menu, sometimes the app". Same rule as the popup button: the drawn
 * box and the tap target come from these helpers, never from a second set of
 * numbers. Restricting the target to the box (instead of the whole right third of
 * the header) also stops a tap near the corner from exiting the app by accident. */
#define PRODUCT_HEADER_CLOSE 66
static int product_header_close_left(void){return width-34-PRODUCT_HEADER_CLOSE;}
static int product_header_close_top(void){return 24;}
static int product_header_close_hit(int x,int y){
    int left=product_header_close_left(),top=product_header_close_top();
    return x>=left-14&&x<left+PRODUCT_HEADER_CLOSE+14&&y>=top-14&&y<top+PRODUCT_HEADER_CLOSE+14;
}
static void product_header(const char *title,const char *subtitle){
    product_small_at(50,58,"HERMES");const char *connection=product_connection_ready?"READY":!strcmp(product_brief_stage,"offline")?"OFFLINE · SAVED":"CONNECTING";product_small_at(width-520,58,connection);
    text_at(50,135,title,1);product_small_at(50,190,subtitle);
    int bx=product_header_close_left(),by=product_header_close_top();
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,3,LineSolid,CapButt,JoinMiter);
    XDrawRectangle(display,backing,gc,bx,by,PRODUCT_HEADER_CLOSE,PRODUCT_HEADER_CLOSE);
    text_at(bx+22,by+47,"×",0);
    product_rule(225);
}
static void product_toolbar(const char *a,const char *b,const char *c){
    int top=height-145;product_rule(top);int third=width/3;XDrawLine(display,backing,gc,third,top,third,height);XDrawLine(display,backing,gc,third*2,top,third*2,height);
    text_at(third/2-55,top+88,a,0);text_at(third+third/2-55,top+88,b,0);text_at(third*2+third/2-55,top+88,c,0);
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
    if(product_popup)return 0;
    if(product_view==PRODUCT_NOTEBOOK||product_view==PRODUCT_FOCUS)return !(daily_enabled&&daily_selected<0);
    return daily_enabled&&product_view==PRODUCT_TODAY&&daily_selected>=0;
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
    /* Clear page lives here, with the pens, rather than next to Send: it is the one
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
static int product_answer_line_count(const char *text,int max_width,int draw,int x,int y,int line_height,int first,int limit){
    const char *p=text;int lines=0;while(*p){while(*p==' '||*p=='\n')++p;if(!*p)break;const char *end=p,*fit=p,*space=p;
        while(*end&&*end!='\n'){const char *next=end+1;while(*next&&((unsigned char)*next&0xc0)==0x80)++next;XGlyphInfo ext;XftTextExtentsUtf8(display,font,(const FcChar8 *)p,(int)(next-p),&ext);if(ext.xOff>max_width)break;if(*end==' ')space=end;fit=next;end=next;}
        if(*end&&*end!='\n'&&space>p)fit=space;if(draw&&lines>=first&&lines<first+limit){size_t n=(size_t)(fit-p);char line[512];if(n>=sizeof line)n=sizeof line-1;memcpy(line,p,n);line[n]=0;text_at(x,y+(lines-first)*line_height,line,0);}++lines;p=fit;while(*p==' ')++p;if(*p=='\n')++p;
    }return lines;
}
static void product_render_answer(int y,int per){
    const char *answer=product_answer();int total=product_answer_line_count(answer,width-100,0,0,0,0,0,0);live_pages=(total+per-1)/per;if(live_pages<1)live_pages=1;if(live_page>=live_pages)live_page=live_pages-1;
    product_answer_line_count(answer,width-100,1,50,y,42,live_page*per,per);if(live_pages>1){char label[64];snprintf(label,sizeof label,"RESPONSE %d OF %d · TAP FOR MORE →",live_page+1,live_pages);product_small_at(width-560,height-220,label);}
}
static void product_status_at_bottom(void){
    /* A full page is a dead end, so warn while there is still room to act rather
     * than letting drawing simply stop. */
    if(page.count>=MAX_POINTS-(MAX_POINTS/10)){
        char warn[130];
        snprintf(warn,sizeof warn,"PAGE NEARLY FULL (%u of %d). Open REVIEW > NEW PAGE to keep writing.",
                 (unsigned)page.count,MAX_POINTS);
        product_small_at(50,height-165,warn);
        return;
    }
    if(status[0]){char shown[110];snprintf(shown,sizeof shown,"%.105s",status);product_small_at(50,height-165,shown);}
}
/* The launcher watchdog writes crash.marker when it had to relaunch the app.
 * Show it once, then clear it, so a crash is visible instead of silent. */
static void product_crash_recovery_notice(void){
    char path[1024],body[256];
    if(product_join(path,sizeof path,live_dir,"crash.marker"))return;
    if(product_file_read(path,body,sizeof body)<=0)return;
    unlink(path);
    char *nl=strchr(body,'\n');if(nl)*nl=0;
    if(body[0])snprintf(status,sizeof status,"%.150s",body);
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
/* What the writer is asking for. Closing the day is a debrief; everything else is a
 * question until they say otherwise. */
static const char *product_send_intent(void){
    if(product_topic[0]&&!strcmp(product_topic,product_close_day_topic))return "debrief";
    return "ask";
}
/* Fill the context that rides along with the ink. Called on Send, so it always
 * reflects the page as it is NOW rather than as it was when the app started. */
static void product_send_context(void){
    product_url_encode(product_topic,live_ctx_topic,sizeof live_ctx_topic);
    product_url_encode(product_spaces[product_space].label,live_ctx_notebook,sizeof live_ctx_notebook);
    snprintf(live_ctx_intent,sizeof live_ctx_intent,"%s",product_send_intent());
    product_url_encode(product_reactions,live_ctx_reactions,sizeof live_ctx_reactions);
}
/* ---- The day can be closed ----------------------------------------------------
 * The closing ritual used to leave no trace: the CLOSE THE DAY band stayed on the page
 * until midnight even though he had just written the debrief, which reads as the app
 * ignoring him. Finishing the ritual (Send, or DONE on the CLOSE THE DAY page) marks the
 * day closed; the band gives way to the one line he will want in the morning, and the
 * brief is re-requested so anything he closed leaves WAITING the same evening. */
static char product_day_closed[16];
static int product_day_closed_today(void){
    char today[16];product_today_stamp(today);
    return product_day_closed[0]!=0&&!strcmp(product_day_closed,today);
}
static void product_day_closed_load(void){
    char path[1024];product_day_closed[0]=0;
    if(product_join(path,sizeof path,product_root,"day.closed"))return;
    if(product_file_read(path,product_day_closed,sizeof product_day_closed)<0){product_day_closed[0]=0;return;}
    char *newline=strchr(product_day_closed,'\n');if(newline)*newline=0;
}
static void product_close_the_day(void){
    char path[1024],today[16];
    if(product_day_closed_today())return;
    product_today_stamp(today);
    snprintf(product_day_closed,sizeof product_day_closed,"%s",today);
    if(!product_join(path,sizeof path,product_root,"day.closed"))
        product_file_write(path,product_day_closed,strlen(product_day_closed));
    product_brief_start();      /* his reactions changed: the day's brief is re-requested */
}
/* Tomorrow's one thing, lifted straight out of last night's debrief. Recognition beats
 * recall: he should not have to remember what he promised. The closing page is usually
 * finished and archived, so look in the live reply first and then in History, newest
 * first -- the promise must survive the page being filed. */
static int product_tomorrow_in(const char *path,char *out,size_t capacity){
    char body[4096];out[0]=0;
    if(product_file_read(path,body,sizeof body)<=0)return 0;
    for(char *p=body;*p;){
        char *newline=strchr(p,'\n');size_t length=newline?(size_t)(newline-p):strlen(p);
        if(length>10&&!strncmp(p,"TOMORROW",8)){
            const char *text=p+8;
            while(text<p+length&&(*text==':'||*text==' '||*text=='\t'))++text;
            size_t n=(size_t)(p+length-text);
            if(n>=capacity)n=capacity-1;
            memcpy(out,text,n);out[n]=0;
            return 1;
        }
        if(!newline)break;
        p=newline+1;
    }
    return 0;
}
static void product_tomorrow_line(char *out,size_t capacity){
    char path[1024],candidate[1024],best[80];out[0]=0;
    if(!product_join(path,sizeof path,product_root,"inbox.reply")&&product_tomorrow_in(path,out,capacity))return;
    if(product_history_dir[0]){
        DIR *dir=opendir(product_history_dir);
        if(!dir)return;
        best[0]=0;
        struct dirent *entry;
        while((entry=readdir(dir))){
            size_t n=strlen(entry->d_name);
            if(n<12||strcmp(entry->d_name+n-12,"-inbox.reply"))continue;
            if(strcmp(entry->d_name,best)>0)snprintf(best,sizeof best,"%s",entry->d_name);
        }
        closedir(dir);
        if(best[0]&&!product_join(candidate,sizeof candidate,product_history_dir,best))
            product_tomorrow_in(candidate,out,capacity);
    }
}
/* ---- Today ---------------------------------------------------------------
 * The page is meant to be reacted to, not read: every card carries three verbs
 * (start it, not today, let it go) and after 19:00 the closing ritual is the
 * loudest thing on the page. Drawn box and tap target always come from the same
 * numbers, the same rule the two X buttons follow. */
#define PRODUCT_CHIP_W 250
#define PRODUCT_CHIP_H 68
#define PRODUCT_CHIP_GAP 26
static const char *product_chip_labels[3]={"START","NOT TODAY","LET IT GO"};
static int product_text_width(const char *s,int use_small){
    XGlyphInfo ext;XftTextExtentsUtf8(display,use_small?small:font,(const FcChar8 *)s,(int)strlen(s),&ext);return ext.xOff;
}
static int product_chip_left(int index){return 155+index*(PRODUCT_CHIP_W+PRODUCT_CHIP_GAP);}
static int product_visible_card(int i){
    int slot=0;for(int k=0;k<i;++k)if(!product_card_hidden(&product_priorities[k]))++slot;return slot;
}
static int product_visible_cards(void){
    int count=0;for(int i=0;i<product_priority_count;++i)if(!product_card_hidden(&product_priorities[i]))++count;return count;
}
static int product_today_card_y(int slot){return 545+slot*300;}
static int product_today_chip_y(int slot){return product_today_card_y(slot)+152;}
static int product_evening(void){
    /* Evening is time-dependent, so the probes and the self-test pin it with
     * PRODUCT_EVENING instead of waiting for 19:00. */
    const char *forced=getenv("PRODUCT_EVENING");
    if(forced)return atoi(forced)!=0;
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);return local.tm_hour>=19;
}
static int product_close_day_top(void){return height-435;}
static int product_close_day_hit(int x,int y){
    if(!product_evening()||product_day_closed_today())return 0;
    int top=product_close_day_top();
    return y>=top&&y<top+110&&x>=45&&x<=width-45;
}
static int product_chip_at(int x,int y,int *card,int *chip){
    for(int i=0;i<product_priority_count;++i){
        if(product_card_hidden(&product_priorities[i]))continue;
        int top=product_today_chip_y(product_visible_card(i));
        if(y<top||y>=top+PRODUCT_CHIP_H)continue;
        for(int c=0;c<3;++c){int left=product_chip_left(c);if(x>=left&&x<left+PRODUCT_CHIP_W){*card=i;*chip=c;return 1;}}
    }
    return 0;
}
static void product_chip_draw(int index,int y,int inverse){
    int left=product_chip_left(index);
    if(inverse){XSetForeground(display,gc,black_pixel);XFillRectangle(display,backing,gc,left,y,PRODUCT_CHIP_W,PRODUCT_CHIP_H);}
    else{XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,left,y,PRODUCT_CHIP_W,PRODUCT_CHIP_H);
        XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,2,LineSolid,CapButt,JoinMiter);XDrawRectangle(display,backing,gc,left,y,PRODUCT_CHIP_W-1,PRODUCT_CHIP_H-1);}
    int w=product_text_width(product_chip_labels[index],1);
    product_ink_text_at(left+(PRODUCT_CHIP_W-w)/2,y+45,product_chip_labels[index],0,inverse);
}
static void product_today_render(void){
    char subtitle[64];time_t now=time(NULL);product_display_date(now,subtitle);product_header("TODAY",subtitle);
    int y=300;
    {
        char line[220];line[0]=0;
        if(product_day_number[0])snprintf(line+strlen(line),sizeof line-strlen(line),"DAY %s",product_day_number);
        if(product_since[0])snprintf(line+strlen(line),sizeof line-strlen(line),"%s%s",line[0]?" \xc2\xb7 ":"",product_since);
        if(product_loops)snprintf(line+strlen(line),sizeof line-strlen(line),"%s%d OPEN LOOP%s",line[0]?" \xc2\xb7 ":"",product_loops,product_loops==1?"":"S");
        int closed=product_reactions_count_today("done");
        if(closed)snprintf(line+strlen(line),sizeof line-strlen(line),"%s%d CLOSED TODAY",line[0]?" \xc2\xb7 ":"",closed);
        if(line[0]){product_small_at(50,y,line);y+=56;}
    }
    /* Finishing something is the reward; say so, briefly, right where he landed. */
    if(product_closed_note[0]){product_small_at(50,y,product_closed_note);y+=52;}
    y+=product_wrap_at(50,y+14,width-100,product_greeting,2,42,0)*42+34;
    /* Only promise "three things" when there are things to show. A READY brief with
     * nothing in it means exactly that: do not leave a heading over nothing, and do
     * not claim Hermes is still preparing when it has already answered. */
    if(product_visible_cards()>0){product_small_at(50,y,"THREE THINGS THAT MATTER");y+=52;}
    else if(strcmp(product_brief_stage,"ready")){text_at(50,y,"Hermes is preparing today...",0);y+=80;}
    for(int i=0;i<product_priority_count;++i){
        if(product_card_hidden(&product_priorities[i]))continue;
        int slot=product_visible_card(i),top=product_today_card_y(slot);
        char number[8];number[0]=(char)('1'+(i<9?i:8));number[1]=0;
        text_at(55,top+40,number,1);text_at(150,top+40,product_priorities[i].title,0);
        if(product_priorities[i].age[0]){int w=product_text_width(product_priorities[i].age,1);product_small_at(width-60-w,top+40,product_priorities[i].age);}
        product_wrap_at(150,top+88,width-260,product_priorities[i].next,2,34,1);
        product_priority_y[i]=top;
        for(int c=0;c<3;++c)product_chip_draw(c,product_today_chip_y(slot),c==0);
        if(slot<2)product_rule(top+272);
    }
    int bottom=545+product_visible_cards()*300+10;
    /* Today is a launchpad, not a second inbox. Keep the secondary bands bounded
     * so three priority cards can never push text into the footer; Review still
     * exposes the complete waiting list and the full new-items list. */
    int waiting_limit=product_waiting_count<2?product_waiting_count:2;
    if(waiting_limit){product_rule(bottom);bottom+=50;product_small_at(50,bottom,"WAITING ON");bottom+=45;
        for(int i=0;i<waiting_limit;++i)bottom+=product_wrap_at(50,bottom,width-100,product_waiting[i],2,34,1)*34+18;}
    int new_limit=product_waiting_count?1:(product_new_count<2?product_new_count:2);
    if(new_limit){product_rule(bottom);bottom+=50;product_small_at(50,bottom,"NEW FROM HERMES");bottom+=45;
        for(int i=0;i<new_limit;++i)bottom+=product_wrap_at(50,bottom,width-100,product_new[i],2,34,1)*34+18;}
    int box=height-520;product_rule(box);
    if(product_evening()&&!product_day_closed_today()){
        int top=product_close_day_top();
        XSetForeground(display,gc,black_pixel);XFillRectangle(display,backing,gc,45,top,width-90,110);
        int w=product_text_width("CLOSE THE DAY",0);product_ink_text_at(width/2-w/2,top+52,"CLOSE THE DAY",1,1);
        const char *hint="60 seconds: what moved, what is stuck, tomorrow";
        int h=product_text_width(hint,1);product_ink_text_at(width/2-h/2,top+90,hint,0,1);
    }else if(product_day_closed_today()){
        char tomorrow[220];product_tomorrow_line(tomorrow,sizeof tomorrow);
        product_small_at(50,box+55,"DAY CLOSED");
        if(tomorrow[0])product_wrap_at(50,box+115,width-100,tomorrow,2,38,1);
        else product_small_at(50,box+115,"Nothing promised for tomorrow.");
    }else if(product_needs_resume()){
        product_small_at(50,box+55,"RESUME");text_at(50,box+125,"Start with one small thing.",0);
        product_small_at(50,box+180,"Tap Write. Hermes will keep everything else quiet.");
    }else{
        product_small_at(50,box+55,"CAPTURE");text_at(50,box+125,"Write anything without deciding where it belongs.",0);
        product_small_at(50,box+180,"Tap Write. Hermes will keep it in Inbox until you move it.");
    }
    product_status_at_bottom();
    product_toolbar("Go to","Write","Review");
}
static void product_notebook_render(void){
    char subtitle[80];time_t now=time(NULL);char date[48];product_display_date(now,date);
    /* The topic line is the page's subject: what it is about, and what Send names it
     * as. Without it every page is "current page" and Hermes has to guess. */
    if(product_topic[0])snprintf(subtitle,sizeof subtitle,"%.72s",product_topic);
    else snprintf(subtitle,sizeof subtitle,"CURRENT PAGE · %s",date);
    product_header(product_spaces[product_space].label,subtitle);
    canvas_top=270;canvas_bottom=live_body[0]?1450:height-190;product_draw_page();
    if(!page.count){
        if(product_topic[0]&&!strcmp(product_topic,product_close_day_topic))product_small_at(70,canvas_top+65,product_empty_guide);
        else product_small_at(70,canvas_top+65,"WRITE NATURALLY. TAP SEND WHEN YOU WANT HERMES.");
    }
    if(live_body[0]){product_rule(1510);product_small_at(50,1565,"HERMES");product_render_answer(1625,12);}
    else if(live_job[0]){product_small_at(50,height-280,"HERMES IS READING · YOU CAN KEEP WRITING");}
    /* When the page came from a Today card, one line tells him what DONE will do, so
     * finishing never has to be guessed at. */
    if(product_reply_needs_confirmation())product_small_at(50,height-230,"Tap CONFIRM to do only this. Write CANCEL to stop.");
    else if(live_body[0]&&product_topic_card()>=0)product_small_at(50,height-230,"Finished? DONE files this page and closes the card.");
    product_status_at_bottom();
    product_toolbar("Review","Done","Send");
    product_ink_toolbar_render();
}
static void product_focus_render(void){
    ProductPriority *p=product_priority_count?&product_priorities[product_focus_index]:NULL;product_header("FOCUS",p?product_label_for_slug(p->space):"TODAY");
    product_small_at(50,305,"ONE THING THAT MATTERS NOW");text_at(50,385,p?p->title:"Decide what deserves focus",1);product_rule(455);
    if(live_body[0]){
        product_small_at(50,520,"HERMES");
        product_render_answer(575,3);
        product_rule(760);
    } else {
        product_small_at(50,520,"NEXT");
        product_wrap_at(190,520,width-240,p?p->next:"Write down the next useful action.",3,42,0);
    }
    canvas_top=live_body[0]?830:700;canvas_bottom=1930;product_draw_page();if(!page.count)product_small_at(60,canvas_top+60,"YOUR THINKING GOES HERE");product_rule(1990);
    if(product_reply_needs_confirmation()){
        product_small_at(50,2045,"CONFIRMATION");
        product_wrap_at(50,2095,width-100,"Tap CONFIRM to do only this, or write CANCEL to stop.",2,38,1);
    }else{
        product_small_at(50,2045,"WHEN YOU SEND THIS");
        product_wrap_at(50,2095,width-100,"Hermes will turn your thinking into a decision and identify the unresolved assumption.",2,38,1);
    }
    product_status_at_bottom();product_toolbar("Exit","Done","Send");
    product_ink_toolbar_render();
}
static const char *product_history_label(const char *slug){return daily_enabled?"NOTEBOOK":product_label_for_slug(slug);}
static void product_history_render(void){
    product_header(daily_enabled?"FIND":"REVIEW",daily_enabled?"RECENT PAGES":"WHAT TO REMEMBER");product_scan_history();int start=product_history_offset,y=315;
    if(product_waiting_count){
        product_small_at(50,y,"WAITING");y+=45;
        for(int i=0;i<product_waiting_count&&i<2;++i)y+=product_wrap_at(50,y,width-100,product_waiting[i],2,34,1)*34+18;
        product_rule(y+18);y+=73;
    }
    if(product_needs_resume()){
        product_small_at(50,y,"RESUME");
        product_wrap_at(50,y+50,width-100,"Start with one small thing. Nothing else needs catching up first.",2,34,1);
        y+=145;product_rule(y);y+=55;
    }
    if(!product_history_count)text_at(50,y,daily_enabled?"Your saved pages will appear here.":"Your previous pages will appear here.",0);
    int shown=0;
    for(int i=start;i<product_history_count&&shown<8&&y+100<height-170;++i,++shown){
        product_small_at(50,y,product_history[i].date);text_at(50,y+48,product_history[i].title,0);char label[80];snprintf(label,sizeof label,"%s  ›",product_history_label(product_history[i].space));product_small_at(width-300,y+48,label);product_rule(y+95);y+=150;
    }
    product_toolbar("Today",daily_enabled?"New note":"New page",product_history_count>8?(product_history_offset+8<product_history_count?"Earlier":"Newest"):"Close");
}
static void product_archive_render(void){
    ProductHistory *h=&product_history[product_history_selection];product_header(h->title,h->date);canvas_top=270;canvas_bottom=live_body[0]?1500:height-190;product_draw_page();
    if(live_body[0]){product_rule(1550);product_small_at(50,1605,"HERMES");product_render_answer(1665,11);}product_toolbar("Back","Continue","Close");
}
/* Popup geometry, defined once and used by BOTH the renderer and the hit test: a
 * drawn button whose tap target is computed separately is free to drift away from
 * its own rectangle, and nothing on screen would show it. */
#define PRODUCT_POPUP_LEFT 170
#define PRODUCT_POPUP_RIGHT (width-170)
#define PRODUCT_POPUP_TOP 325
#define PRODUCT_POPUP_ROW 205
#define PRODUCT_POPUP_CLOSE 66
static int product_popup_close_left(void){return PRODUCT_POPUP_RIGHT-PRODUCT_POPUP_CLOSE-34;}
static int product_popup_close_top(void){return PRODUCT_POPUP_TOP+18;}
/* Tap target is a little larger than the box: a finger is not a pen. */
static int product_popup_close_hit(int x,int y){
    int left=product_popup_close_left(),top=product_popup_close_top();
    return x>=left-14&&x<left+PRODUCT_POPUP_CLOSE+14&&y>=top-14&&y<top+PRODUCT_POPUP_CLOSE+14;
}
static void product_popup_render(void){
    int left=PRODUCT_POPUP_LEFT,right=PRODUCT_POPUP_RIGHT,top=PRODUCT_POPUP_TOP,row=PRODUCT_POPUP_ROW,rows=PRODUCT_SPACE_COUNT+1;
    XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,left,top,right-left,row*rows+120);XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,3,LineSolid,CapButt,JoinMiter);XDrawRectangle(display,backing,gc,left,top,right-left,row*rows+120);product_small_at(left+40,top+55,"GO TO");
    /* Row 0 is Today. The day view used to be reachable only from the toolbar on
     * itself, so the moment you navigated away the way back was gone. */
    text_at(left+40,top+160,"TODAY",0);product_small_at(left+40,top+210,"YOUR DAY AND ITS OPEN LOOPS");
    XDrawLine(display,backing,gc,left+35,top+100+row-15,right-35,top+100+row-15);
    for(int i=0;i<PRODUCT_SPACE_COUNT;++i){int y=top+100+(i+1)*row;text_at(left+40,y+60,product_spaces[i].label,0);const char *detail=i==0?"WORK AND DECISIONS":i==1?"BETS AND EXPERIMENTS":i==2?"FAMILY AND HOUSEHOLD":"UNSORTED CAPTURES";product_small_at(left+40,y+110,detail);if(i<PRODUCT_SPACE_COUNT-1)XDrawLine(display,backing,gc,left+35,y+row-15,right-35,y+row-15);}
    /* A visible way OUT. Until now the popup could only be dismissed by tapping
     * the dimmed area around it -- which worked, but nothing on screen said so,
     * so the menu read as a dead end. */
    int bx=product_popup_close_left(),by=product_popup_close_top();
    XDrawRectangle(display,backing,gc,bx,by,PRODUCT_POPUP_CLOSE,PRODUCT_POPUP_CLOSE);
    text_at(bx+22,by+47,"×",0);
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
    product_header(product_spaces[product_space].label,"SLEEPING");
    product_rule(mid-210);
    product_center_at(mid-60,"HERMES INK",0);
    product_center_at(mid+60,"ASLEEP",1);
    product_center_at(mid+180,"PRESS POWER TO WAKE",0);
    product_rule(mid+290);
    product_center_at(mid+370,product_spaces[product_space].label,0);
    product_center_at(mid+420,date,0);
    present(0,0,width,height);
}
static void product_repaint(void){
    /* While asleep the sleep screen owns the panel -- but only once we have decided
     * to paint it: until then we must not repaint the app over the system's own
     * lock screen. */
    if(sleeping){if(sleep_painted)product_sleep_render();return;}
    XSetForeground(display,gc,white_pixel);XFillRectangle(display,backing,gc,0,0,width,height);XSetForeground(display,gc,black_pixel);
    if(daily_render()){present(0,0,width,height);return;}
    if(product_view==PRODUCT_TODAY)product_today_render();else if(product_view==PRODUCT_NOTEBOOK)product_notebook_render();else if(product_view==PRODUCT_FOCUS)product_focus_render();else if(product_view==PRODUCT_HISTORY)product_history_render();else product_archive_render();
    if(product_popup)product_popup_render();present(0,0,width,height);
}
static int product_can_draw_at(int x,int y){return !(daily_enabled&&(daily_context||slip_kind))&&product_ink_view()&&!product_ink_region_at(x,y)&&x>=45&&x<=width-45&&y>=canvas_top+20&&y<=canvas_bottom-20;}
static int product_button_at(int x,int y){
    if(daily_enabled&&product_view!=PRODUCT_FOCUS)return daily_button_at(x,y);
    /* The header's X is the app-level close, and it stays live even while the
     * popup is open. The user asked for TWO Xs with two jobs -- one for the space
     * selector, one for the application itself -- not one that changes meaning
     * depending on what happens to be on screen. */
    if(product_header_close_hit(x,y))return PRODUCT_CLOSE;
    if(product_popup){
        if(product_popup_close_hit(x,y))return PRODUCT_POPUP_CLOSE;
        int top=PRODUCT_POPUP_TOP+100,rows=PRODUCT_SPACE_COUNT+1;
        if(x>=PRODUCT_POPUP_LEFT&&x<=PRODUCT_POPUP_RIGHT&&y>=top&&y<top+PRODUCT_POPUP_ROW*rows){
            int index=(y-top)/PRODUCT_POPUP_ROW;
            return index==0?PRODUCT_TODAY_ACTION:PRODUCT_SPACE_WORK+(index-1);
        }
        return PRODUCT_SPACES;
    }
    if(!product_popup&&(product_view==PRODUCT_NOTEBOOK||product_view==PRODUCT_FOCUS)){int ink_action=product_ink_button_at(x,y);if(ink_action!=PRODUCT_NONE)return ink_action;}
    if(y<225)return PRODUCT_SPACES;
    if(product_view==PRODUCT_TODAY){
        /* Chips first: they sit under the card, and a card body tap still opens
         * Focus, so the two targets must not overlap. */
        int card,chip;
        if(product_chip_at(x,y,&card,&chip))return (chip==0?PRODUCT_START_0:chip==1?PRODUCT_SNOOZE_0:PRODUCT_DROP_0)+card;
        if(product_close_day_hit(x,y))return PRODUCT_CLOSE_DAY;
        for(int i=0;i<product_priority_count;++i){if(product_card_hidden(&product_priorities[i]))continue;int top=product_today_card_y(product_visible_card(i));if(y>=top&&y<top+150)return PRODUCT_PRIORITY_0+i;}
    }
    if(product_view==PRODUCT_NOTEBOOK&&live_body[0]&&y>=1510&&y<height-145)return PRODUCT_MORE;
    if(product_view==PRODUCT_FOCUS&&live_body[0]&&live_pages>1&&y>=520&&y<760)return PRODUCT_MORE;
    if(product_view==PRODUCT_HISTORY&&y>=315&&y<315+8*150){int index=product_history_offset+(y-315)/150;if(index<product_history_count)return PRODUCT_HISTORY_0+index;}
    if(y<height-145)return PRODUCT_NONE;int third=x/(width/3);if(third>2)third=2;
    if(product_view==PRODUCT_TODAY){int a[]={PRODUCT_SPACES,PRODUCT_WRITE,PRODUCT_HISTORY_ACTION};return a[third];}
    if(product_view==PRODUCT_NOTEBOOK){int a[]={PRODUCT_HISTORY_ACTION,product_reply_needs_confirmation()?PRODUCT_CONFIRM_ACTION:PRODUCT_DONE,PRODUCT_SEND};return a[third];}
    if(product_view==PRODUCT_FOCUS){int a[]={PRODUCT_EXIT_FOCUS,product_reply_needs_confirmation()?PRODUCT_CONFIRM_ACTION:PRODUCT_DONE,PRODUCT_SEND};return a[third];}
    if(product_view==PRODUCT_HISTORY){int a[]={PRODUCT_TODAY_ACTION,PRODUCT_NEW_PAGE,product_history_count>8?PRODUCT_MORE:PRODUCT_CLOSE};return a[third];}
    {int a[]={PRODUCT_BACK,PRODUCT_WRITE,PRODUCT_CLOSE};return a[third];}
}
static void product_activate(int action){
    if(daily_enabled&&daily_activate(action))return;
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
    if(action>=PRODUCT_HISTORY_0&&action<PRODUCT_HISTORY_0+PRODUCT_HISTORY_MAX){product_open_history(action-PRODUCT_HISTORY_0);product_repaint();return;}
    if(action>=PRODUCT_SPACE_WORK&&action<PRODUCT_SPACE_WORK+PRODUCT_SPACE_COUNT){product_switch_space(action-PRODUCT_SPACE_WORK);product_repaint();return;}
    if(action>=PRODUCT_START_0&&action<=PRODUCT_DROP_2){
        int family=action<PRODUCT_SNOOZE_0?PRODUCT_START_0:action<PRODUCT_DROP_0?PRODUCT_SNOOZE_0:PRODUCT_DROP_0,card=action-family;
        if(card>=product_priority_count){product_popup=0;product_repaint();return;}
        ProductPriority *priority=&product_priorities[card];
        if(family==PRODUCT_START_0){
            /* Open that space's notebook with the card as the page topic, so the page
             * arrives with a subject instead of as a blank page. Ink left over from a
             * different task is filed first rather than mixed into this one. */
            for(int i=0;i<PRODUCT_SPACE_COUNT;++i)if(!strcmp(priority->space,product_spaces[i].slug))product_switch_space(i);
            if(product_prepare_task_page(priority->title)){product_repaint();return;}
            product_topic_set(priority->title);
            product_focus_index=card;product_view=PRODUCT_NOTEBOOK;product_popup=0;product_repaint();return;
        }
        /* "Not today" or "let it go": hide it here, and tell Hermes now rather than
         * at the next restart. The brief is written server-side, so a reaction that
         * never leaves the device would just come back as the same card. */
        product_reactions_add(family==PRODUCT_SNOOZE_0?"snooze":"drop",priority->id,priority->title);
        snprintf(status,sizeof status,"%s: %.60s",family==PRODUCT_SNOOZE_0?"Not today":"Let go",priority->title);
        product_brief_start();
        product_repaint();return;
    }
    if(action==PRODUCT_CLOSE_DAY){
        /* The closing ritual: five prompts, one page, kept in the inbox. Hermes
         * answers with tomorrow's shape -- that answer is the point of closing. */
        for(int i=0;i<PRODUCT_SPACE_COUNT;++i)if(!strcmp(product_spaces[i].slug,"inbox"))product_switch_space(i);
        product_topic_set(product_close_day_topic);
        product_view=PRODUCT_NOTEBOOK;product_popup=0;product_repaint();return;
    }
    if(action>=PRODUCT_PRIORITY_0&&action<=PRODUCT_PRIORITY_2){product_focus_index=action-PRODUCT_PRIORITY_0;for(int i=0;i<PRODUCT_SPACE_COUNT;++i)if(!strcmp(product_priorities[product_focus_index].space,product_spaces[i].slug))product_switch_space(i);if(product_prepare_task_page(product_priorities[product_focus_index].title)){product_repaint();return;}product_topic_set(product_priorities[product_focus_index].title);product_view=PRODUCT_FOCUS;product_popup=0;product_repaint();return;}
    if(action==PRODUCT_DONE){
        /* FINISH: the verb this loop was missing. The page is filed into History
         * (nothing is thrown away), the canvas is left clear, the Today card this
         * page was opened from closes, and Today comes back showing that it closed. */
        if(live_job[0]&&!live_terminal){snprintf(status,sizeof status,"Hermes is still reading this page. Try again in a moment.");product_repaint();return;}
        int card=product_topic_card();
        int was_ritual=product_topic[0]&&!strcmp(product_topic,product_close_day_topic);
        if(product_archive_current()||page.count){product_repaint();return;}   /* it already explains why */
        if(was_ritual)product_close_the_day();
        char closed[100];closed[0]=0;
        if(card>=0){ProductPriority closed_priority=product_priorities[card];snprintf(closed,sizeof closed,"%s",closed_priority.title);product_reactions_add("done",closed_priority.id,closed_priority.title);product_brief_start();}
        product_topic_clear();
        snprintf(product_closed_note,sizeof product_closed_note,closed[0]?"Closed: %.90s":"Filed to Review",closed);
        snprintf(status,sizeof status,"%s",product_closed_note);
        product_view=PRODUCT_TODAY;product_popup=0;product_ink_toolbar_expanded=0;product_ink_menu=0;
        panel_refresh();product_repaint();return;
    }
    if(action==PRODUCT_INK_CLEAR_PAGE){
        /* Empty this page, but keep the ink: it is filed to History first. Buried in the
         * pen rail on purpose -- it is the only action that empties a page, so it should
         * never be a neighbour of Send. */
        if(product_archive_current()||page.count){product_repaint();return;}
        product_topic_clear();ink_cache_invalidate();panel_refresh();
        snprintf(status,sizeof status,"Page filed to Review. The canvas is clear.");
        product_view=PRODUCT_NOTEBOOK;product_popup=0;product_ink_menu=0;product_repaint();return;
    }
    if(action==PRODUCT_CONFIRM_ACTION){
        int result=product_reply_needs_confirmation()?live_submit_command("CONFIRM"):-1;
        snprintf(status,sizeof status,"%s",result>0?"Confirmation sent to Hermes.":"The prepared action is unchanged. Try again when ready.");
        product_repaint();return;
    }
    if(action==PRODUCT_SPACES){product_ink_toolbar_expanded=0;product_ink_menu=0;product_popup=!product_popup;product_repaint();return;}
    if(action==PRODUCT_POPUP_CLOSE){product_popup=0;product_ink_menu=0;product_repaint();return;}
    if(action==PRODUCT_TODAY_ACTION||action==PRODUCT_EXIT_FOCUS){product_view=PRODUCT_TODAY;product_ink_toolbar_expanded=0;product_ink_menu=0;product_popup=0;product_repaint();return;}
    if(action==PRODUCT_WRITE){if(product_view==PRODUCT_TODAY)product_switch_space(3);else if(product_view==PRODUCT_ARCHIVE){product_restore_current();product_view=PRODUCT_NOTEBOOK;}else product_view=PRODUCT_NOTEBOOK;product_repaint();return;}
    if(action==PRODUCT_HISTORY_ACTION){if(dirty)store_stroke();product_ink_toolbar_expanded=0;product_ink_menu=0;product_view=PRODUCT_HISTORY;product_history_offset=0;product_repaint();return;}
    if(action==PRODUCT_BACK){product_restore_current();product_repaint();return;}
    if(action==PRODUCT_UNDO){uint32_t sent=page.sent;undo_stroke();page.sent=sent;dirty=1;store_stroke();product_repaint();return;}
    if(action==PRODUCT_SEND){if(dirty)store_stroke();product_send_context();if(!strcmp(live_ctx_intent,"debrief"))product_close_the_day();int result=dirty?-1:live_submit();snprintf(status,sizeof status,"%s",result<0?"Could not start request. Ink is safe.":live_job[0]?"Hermes is reading your page...":"Write something before Send.");product_repaint();return;}
    if(action==PRODUCT_MORE){if(product_view==PRODUCT_HISTORY){product_history_offset=product_history_offset+8<product_history_count?product_history_offset+8:0;}else if(live_pages>1)live_page=(live_page+1)%live_pages;product_repaint();return;}
    if(action==PRODUCT_NEW_PAGE){if(!product_archive_current()){ink_cache_invalidate();product_topic_clear();product_view=PRODUCT_NOTEBOOK;product_scan_history();}product_repaint();return;}
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
