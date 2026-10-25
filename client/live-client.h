/* Native live-mode networking: child-process HTTP, durable request identity.
 * Included after the drawing helpers. Never call Xlib from the child. */
#include <sys/wait.h>
static const char *live_dir="/mnt/us/hermes-native-live";
static char live_job[33], live_stage[32], live_body[60000];
static char live_command_text[8];
static uint32_t live_cutoff;
static pid_t live_child;
static int live_accepted, live_terminal, live_command_mode, live_page, live_lines, live_pages=1;
static unsigned line_start[3000],line_length[3000];
static double live_next, live_started, live_status_at;
static void repaint(void);

static int live_path(char *out,size_t size,const char *name) {
    return snprintf(out,size,"%s/%s",live_dir,name)>=(int)size?-1:0;
}
static int live_write(const char *name,const char *data,size_t length) {
    char path[1024],tmp[1030];if(live_path(path,sizeof path,name))return -1;
    snprintf(tmp,sizeof tmp,"%s.tmp",path);
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd<0)return -1;
    size_t done=0;while(done<length){ssize_t n=write(fd,data+done,length-done);if(n<=0){close(fd);return -1;}done+=(size_t)n;}
    int bad=fsync(fd);if(close(fd))bad=1;if(bad||rename(tmp,path))return -1;
    fd=open(live_dir,O_RDONLY|O_DIRECTORY);if(fd<0)return -1;bad=fsync(fd);close(fd);return bad;
}
static int live_read(const char *name,char *out,size_t capacity) {
    char path[1024];if(live_path(path,sizeof path,name))return -1;
    FILE *f=fopen(path,"rb");if(!f)return -1;
    size_t n=fread(out,1,capacity-1,f);int bad=ferror(f)||fgetc(f)!=EOF;fclose(f);
    if(bad)return -1;out[n]=0;return (int)n;
}
static int live_id(char id[33]) {
    unsigned char bytes[16];int fd=open("/dev/urandom",O_RDONLY);if(fd<0)return -1;
    ssize_t n=read(fd,bytes,sizeof bytes);close(fd);if(n!=sizeof bytes)return -1;
    const char *hex="0123456789abcdef";for(int i=0;i<16;++i){id[i*2]=hex[bytes[i]>>4];id[i*2+1]=hex[bytes[i]&15];}id[32]=0;return 0;
}
static int valid_job(const char *id) {
    if(strlen(id)!=32)return 0;
    for(int i=0;i<32;++i)if(!((id[i]>='a'&&id[i]<='f')||(id[i]>='0'&&id[i]<='9')))return 0;
    return 1;
}
static void live_wrap(void) {
    unsigned len=(unsigned)strlen(live_body),pos=0;live_lines=0;
    while(pos<len&&live_lines<3000) {
        unsigned start=pos,end=pos,last_space=pos;
        while(end<len&&live_body[end]!='\n') {
            unsigned next=end+1;while(next<len&&((unsigned char)live_body[next]&0xc0)==0x80)++next;
            XGlyphInfo ext;XftTextExtentsUtf8(display,font,(FcChar8 *)live_body+start,next-start,&ext);
            if(ext.xOff>width-110&&end>start) {if(last_space>start)end=last_space;break;}
            if(live_body[end]==' ')last_space=end;
            end=next;
        }
        line_start[live_lines]=start;line_length[live_lines]=end-start;++live_lines;
        pos=end;if(pos<len&&(live_body[pos]=='\n'||live_body[pos]==' '))++pos;
    }
    int per=(height-(canvas_bottom+165)-220)/48;if(per<1)per=1;
    live_pages=(live_lines+per-1)/per;if(live_pages<1)live_pages=1;
    if(live_page>=live_pages)live_page=live_pages-1;
}
static void live_render(void) {
    int y=canvas_bottom+100;
    text_at(50,y,"HERMES",0);
    if(!live_body[0]) {
        text_at(50,y+80,live_job[0]?"Your note is being processed. You can keep writing.":"Write a question above, then tap Send.",0);
        text_at(50,y+140,"Only this app's note is sent when you tap Send.",0);
    } else {
        int per=(height-(canvas_bottom+165)-220)/48;if(per<1)per=1;
        for(int i=live_page*per;i<live_lines&&i<(live_page+1)*per;++i) {
            char line[1024];unsigned n=line_length[i];if(n>=sizeof line)n=sizeof line-1;
            memcpy(line,live_body+line_start[i],n);line[n]=0;
            text_at(50,y+65+(i-live_page*per)*48,line,0);
        }
    }
    XSetForeground(display,gc,black_pixel);XSetLineAttributes(display,gc,2,LineSolid,CapButt,JoinMiter);
    XDrawRectangle(display,backing,gc,40,height-190,300,90);text_at(65,height-132,"Earlier",0);
    XDrawRectangle(display,backing,gc,width-340,height-190,300,90);text_at(width-310,height-132,"More",0);
    char position[64];snprintf(position,sizeof position,"Answer %d / %d",live_page+1,live_pages);text_at(width/2-110,height-132,position,0);
}
/* The page's context, set by the app layer right before Send (product_send_context).
 * These ride along as headers so Hermes is told which notebook the note came from,
 * what the page is titled, what the writer is asking for, and how he reacted to the day's
 * cards -- instead of receiving bare handwriting and a bucket name. */
static char live_ctx_topic[400],live_ctx_notebook[160],live_ctx_intent[24]="ask",live_ctx_reactions[1100];
static int live_spawn(void) {
    if(live_child||!live_job[0]||live_terminal)return 0;
    char script[1024];if(live_path(script,sizeof script,"ink-worker.sh"))return -1;
    pid_t child=fork();if(child<0)return -1;
    if(child==0) {
        close(ConnectionNumber(display));
        const char *mode=live_accepted?"get":live_command_mode?"command":"submit";
        const char *context=live_command_mode?live_command_text:live_ctx_topic;
        execl("/bin/sh","sh",script,mode,live_job,product_space_slug(),
              context,live_ctx_notebook,live_ctx_intent,live_ctx_reactions,(char *)NULL);
        _exit(127);
    }
    live_child=child;return 0;
}
static int live_submit(void) {
    if(live_job[0]) {
        if(!live_terminal){live_next=seconds();return live_spawn();}
        char pending_path[1024];live_path(pending_path,sizeof pending_path,"pending");unlink(pending_path);
        live_job[0]=0;live_terminal=0;live_accepted=0;live_command_mode=0;live_command_text[0]=0;live_body[0]=0;
    }
    if(!page.count||page.count==page.sent)return 0;
    char id[33],request[1024],pending[96];
    if(live_id(id)||live_path(request,sizeof request,"request.ink")||save_page_from(request,page.sent))return -1;
    int n=snprintf(pending,sizeof pending,"%s\n%u\n",id,page.count);
    if(live_write("pending",pending,(size_t)n))return -1;
    memcpy(live_job,id,33);live_cutoff=page.count;live_started=seconds();live_terminal=0;live_accepted=0;live_command_mode=0;live_command_text[0]=0;
    live_body[0]=0;live_page=0;live_wrap();strcpy(live_stage,"connecting");
    if(live_spawn())return -1;return 1;
}
static int live_submit_command(const char *command) {
    if(strcmp(command,"CONFIRM")&&strcmp(command,"CANCEL"))return -1;
    if(live_job[0]) {
        if(!live_terminal)return -1;
        char pending_path[1024];live_path(pending_path,sizeof pending_path,"pending");unlink(pending_path);
        live_job[0]=0;live_terminal=0;live_accepted=0;live_command_mode=0;live_command_text[0]=0;live_body[0]=0;
    }
    char id[33];if(live_id(id))return -1;
    snprintf(live_command_text,sizeof live_command_text,"%s",command);
    live_command_mode=1;memcpy(live_job,id,33);live_cutoff=page.count;live_started=seconds();live_terminal=0;live_accepted=0;
    live_body[0]=0;live_page=0;live_wrap();strcpy(live_stage,"connecting");
    if(live_spawn())return -1;return 1;
}
static int live_acknowledge_page(void) {
    /* A fixed command (Confirm/Cancel) is not a page submission. If the user
     * wrote more ink after the PREPARE reply, leave that ink unsent so a later
     * Send can still transmit it. Only a normal page job advances the watermark. */
    if(live_command_mode)return 0;
    uint32_t old=page.sent;page.sent=live_cutoff>page.count?page.count:live_cutoff;
    if(save_page(state_path)){page.sent=old;return -1;}
    return 0;
}
static void live_status(void) {
    const char *phase=!strcmp(live_stage,"reading")?"Reading handwriting":
        !strcmp(live_stage,"thinking")?"Hermes is preparing an answer":
        !strcmp(live_stage,"queued")?"Note received - queued":"Connecting securely";
    snprintf(status,sizeof status,"%s - %.0f seconds",phase,seconds()-live_started);
    if(!live_mode)status_only();
}
static void live_tick(void) {
    if(!live_job[0])return;
    double now=seconds();
    if(live_child) {
        int code;pid_t done=waitpid(live_child,&code,WNOHANG);
        if(done==live_child) {
            live_child=0;live_next=now+2;
            if(!WIFEXITED(code)||WEXITSTATUS(code)!=0) {
                /* Name the cause. Every failure used to read "Connection interrupted",
                 * which hid whether the worker had no key (3), could not reach the tunnel
                 * (7), was told the service is busy (9), was rejected (22), found no
                 * request file (26), timed out (28) or blew the reply cap (63). */
                int why=WIFEXITED(code)?WEXITSTATUS(code):-1;
                if(why==9)strcpy(status,"Hermes is busy right now. Ink saved. Tap Send again in a minute.");
                else snprintf(status,sizeof status,"Connection interrupted (worker %d). Ink saved. Tap Send to reconnect.",why);
                status_only();
                live_next=now+15;live_status_at=now+14;
            } else {
                static char wire_buffer[62000];int size=live_read("worker.wire",wire_buffer,sizeof wire_buffer);
                char *newline=size>0?strchr(wire_buffer,'\n'):NULL;
                if(!newline||newline-wire_buffer>=31) {strcpy(status,"Invalid server response. Ink saved.");status_only();live_next=now+15;return;}
                *newline=0;
                if(strcmp(wire_buffer,"queued")&&strcmp(wire_buffer,"reading")&&strcmp(wire_buffer,"thinking")&&strcmp(wire_buffer,"done")&&strcmp(wire_buffer,"error")&&strcmp(wire_buffer,"uncertain"))return;
                live_accepted=1;size_t stage_size=(size_t)(newline-wire_buffer);memcpy(live_stage,wire_buffer,stage_size);live_stage[stage_size]=0;
                if(strlen(newline+1)>=sizeof live_body){strcpy(status,"Reply too large. Ink saved.");status_only();return;}
                if(strcmp(live_body,newline+1)){strcpy(live_body,newline+1);live_page=0;live_wrap();repaint();}
                if(!strcmp(live_stage,"done")) {
                    *newline='\n';
                    if(live_write("last.wire",wire_buffer,(size_t)size)) {strcpy(status,"Could not save reply. Keeping request for recovery.");status_only();return;}
                    /* An erase while this answer was in flight compacted the page, so the cutoff taken at submit can now exceed the ink left; load_page() rejects sent>count, which would lock him out of his own notebook. */
                    if(live_acknowledge_page()){strcpy(status,"Could not save note. Keeping request for recovery.");status_only();return;}
                    product_save_reply_current();
                    char path[1024];live_path(path,sizeof path,"pending");unlink(path);live_job[0]=0;live_accepted=0;live_command_mode=0;live_command_text[0]=0;
                    strcpy(status,"Answer ready below. Tap New note for your next question.");repaint();
                } else if(!strcmp(live_stage,"error")||!strcmp(live_stage,"uncertain")) {
                    live_terminal=1;product_save_reply_current();strcpy(status,"Request stopped - details below. Your ink is saved. Tap Send to try again.");repaint();
                }
            }
        }
    }
    if(live_job[0]&&!live_terminal) {
        if(!live_child&&now>=live_next)live_spawn();
        if(now>=live_status_at){live_status();live_status_at=now+2;}
    }
}
static void live_abandon_terminal(void) {
    if(!live_job[0]||!live_terminal)return;
    char path[1024];live_path(path,sizeof path,"pending");unlink(path);
    live_job[0]=0;live_terminal=0;live_accepted=0;live_command_mode=0;live_command_text[0]=0;
}
static int live_new_note(void) {
    if(live_job[0]&&!live_terminal){strcpy(status,"Wait for this answer before starting a new note.");return -1;}
    char id[33],archive[100],path[1024];
    if(page.count) {
        if(live_id(id))return -1;snprintf(archive,sizeof archive,"saved-note-%s.ink",id);
        if(live_path(path,sizeof path,archive)||save_page(path))return -1;
    }
    uint32_t count=page.count,sent=page.sent;page.count=0;page.sent=0;
    if(save_page(state_path)){page.count=count;page.sent=sent;return -1;}
    live_path(path,sizeof path,"pending");unlink(path);live_job[0]=0;live_terminal=0;live_command_mode=0;live_command_text[0]=0;erasing=0;
    strcpy(status,"New note ready. Previous ink kept privately on this Scribe.");return 0;
}
static int live_init(void) {
    static char buffer[62000];
    int n=live_read("last.wire",buffer,sizeof buffer);
    if(n>0&&!strncmp(buffer,"done\n",5)&&strlen(buffer+5)<sizeof live_body)strcpy(live_body,buffer+5);
    char pending[100];n=live_read("pending",pending,sizeof pending);
    if(n>0) {
        /* A pending request is only worth reopening while it still describes THIS page.
         * When it does not (the page was finished, cleared or rewritten), the old guard
         * returned -1 and the app REFUSED TO START -- so one interrupted job at 19:18
         * left the whole app unopenable until the file was removed by hand. Never resend
         * it, but never lock him out either: set it aside and say so. */
        char path[1024];
        char *nl=strchr(pending,'\n');
        int usable=0;
        if(nl) {
            *nl=0;
            char *end;unsigned long count=strtoul(nl+1,&end,10);
            usable=valid_job(pending)&&(*end=='\n'||*end==0)&&count<=page.count;
            if(usable) {
                strcpy(live_job,pending);live_cutoff=(uint32_t)count;strcpy(live_stage,"connecting");live_started=seconds();
            }
        }
        if(!usable) {
            if(!live_path(path,sizeof path,"pending"))unlink(path);
            snprintf(status,sizeof status,"The last request was not confirmed and has been set aside. Your ink is safe - Send again when ready.");
        }
    }
    if(!live_job[0]&&!status[0])strcpy(status,"Write a question. Tap Send when you are ready.");
    else if(live_job[0])strcpy(status,"Reconnecting to your saved request...");
    return 0;
}
