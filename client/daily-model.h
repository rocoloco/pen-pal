#ifndef HERMES_DAILY_MODEL_H
#define HERMES_DAILY_MODEL_H

#define DAILY_MAX 128
#define DAILY_WIRE_MAX 262144
typedef struct { char kind[16], id[37], title[512], when[96], context[8192]; } DailyEntry;
typedef struct { char document[37], date[16], zone[64]; int count; DailyEntry entries[DAILY_MAX]; } DailyBundle;
static DailyBundle daily, daily_candidate;
static int daily_enabled, daily_selected=-1, daily_offset, daily_context;
static int daily_history_context, daily_history_context_page;
static int slip_kind;          /* a slip (see page-answers.h) is covering the page */
static int daily_render(void);
static int daily_button_at(int x,int y);
static int daily_activate(int action);

static int daily_uuid(const char *s){
    if(!s||strlen(s)!=36)return 0;
    for(int i=0;i<36;++i)if(i==8||i==13||i==18||i==23){if(s[i]!='-')return 0;}
        else if(!strchr("0123456789abcdef",s[i]))return 0;
    return 1;
}
static int daily_field(char *out,size_t capacity,const char *in){
    if(!in||strlen(in)>=capacity||strchr(in,'\r'))return -1;
    memcpy(out,in,strlen(in)+1);return 0;
}
static int daily_parse(char *body){
    memset(&daily_candidate,0,sizeof daily_candidate);
    int format=0;char *saved=NULL;
    for(char *line=strtok_r(body,"\n",&saved);line;line=strtok_r(NULL,"\n",&saved)){
        char *cursor=NULL,*type=strtok_r(line,"\t",&cursor),*value=strtok_r(NULL,"\t",&cursor);
        if(!type||!value)return -1;
        if(!strcmp(type,"FORMAT")){if(format++||strcmp(value,"HINKDAILY1"))return -1;}
        else if(!strcmp(type,"DOCUMENT")){if(daily_candidate.document[0]||!daily_uuid(value)||daily_field(daily_candidate.document,sizeof daily_candidate.document,value))return -1;}
        else if(!strcmp(type,"DATE")){if(daily_candidate.date[0]||strlen(value)!=10||value[4]!='-'||value[7]!='-'||daily_field(daily_candidate.date,sizeof daily_candidate.date,value))return -1;}
        else if(!strcmp(type,"ZONE")){if(daily_candidate.zone[0]||daily_field(daily_candidate.zone,sizeof daily_candidate.zone,value))return -1;}
        else if(!strcmp(type,"ENTRY")){
            if(daily_candidate.count>=DAILY_MAX||!(strcmp(value,"meeting")==0||strcmp(value,"task")==0||strcmp(value,"capture")==0||strcmp(value,"reference")==0))return -1;
            DailyEntry *entry=&daily_candidate.entries[daily_candidate.count];
            if(daily_field(entry->kind,sizeof entry->kind,value)||daily_field(entry->id,sizeof entry->id,strtok_r(NULL,"\t",&cursor))||!daily_uuid(entry->id)||
               daily_field(entry->title,sizeof entry->title,strtok_r(NULL,"\t",&cursor))||daily_field(entry->when,sizeof entry->when,strtok_r(NULL,"\t",&cursor))||
               daily_field(entry->context,sizeof entry->context,strtok_r(NULL,"\t",&cursor)))return -1;
            for(int i=0;i<daily_candidate.count;++i)if(!strcmp(entry->id,daily_candidate.entries[i].id))return -1;
            if(!strcmp(entry->context,"-"))entry->context[0]=0;
            ++daily_candidate.count;
        }else return -1;
        if(strtok_r(NULL,"\t",&cursor))return -1;
    }
    if(format!=1||!daily_candidate.document[0]||!daily_candidate.date[0]||!daily_candidate.zone[0]||!daily_candidate.count)return -1;
    /* A refresh never swaps context underneath an open ink page. Reload from Today. */
    if(daily_enabled&&daily_selected>=0)return -1;
    daily=daily_candidate;daily_enabled=1;daily_offset=0;return 0;
}
#endif
