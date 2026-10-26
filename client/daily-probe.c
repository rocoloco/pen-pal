/* Exercise the real native renderer, ink persistence and "the page answers" under
 * Xvfb, against a fake ink-worker.sh that plays the server (online, thinking,
 * answered, offline, rejected). Synthetic content only.
 * Usage: daily-probe <synthetic daily.wire> <screenshot directory> <strokes directory> */
#define main hermes_app_main
#include "hermes-native.c"
#undef main
#include <assert.h>

static const char *shots,*strokes_dir;
static void capture(const char *name){
    char path[1024];assert(!product_join(path,sizeof path,shots,name));
    XSync(display,False);XImage *im=XGetImage(display,backing,0,0,width,height,AllPlanes,ZPixmap);assert(im);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",width,height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        unsigned long p=XGetPixel(im,x,y);unsigned char rgb[]={(p>>16)&255,(p>>8)&255,p&255};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(!fclose(f));XDestroyImage(im);
}
/* Write a pre-generated line of handwriting (screen pixels) onto the open canvas. */
static void write_ink(const char *name){
    char path[1024],leaf[80];snprintf(leaf,sizeof leaf,"%s.txt",name);assert(!product_join(path,sizeof path,strokes_dir,leaf));
    FILE *f=fopen(path,"r");assert(f);int x,y,start,any=0;
    while(fscanf(f,"%d %d %d",&x,&y,&start)==3){
        if(x<45||x>width-45||y<canvas_top+25||y>canvas_bottom-25)continue;
        int nx=(int)((long)(x-45)*65535/(width-90)),ny=(int)((long)(y-canvas_top-25)*65535/(canvas_bottom-canvas_top-50));
        assert(!append_point(nx,ny,start||!any,0));any=1;
    }
    fclose(f);assert(any);dirty=1;store_stroke();assert(!dirty);ink_cache_invalidate();product_repaint();
}
static void put(const char *name,const char *body){char path[1024];assert(!product_join(path,sizeof path,live_dir,name));assert(!product_file_write(path,body,strlen(body)));}
static void drop(const char *name){char path[1024];assert(!product_join(path,sizeof path,live_dir,name));unlink(path);}
static int has(const char *name){char path[1024];assert(!product_join(path,sizeof path,live_dir,name));return !access(path,F_OK);}
static int pump(int (*done)(void),double limit){
    double end=seconds()+limit;
    while(seconds()<end){daily_tick();if(done())return 1;{struct timespec nap={0,20000000};nanosleep(&nap,NULL);}}
    return 0;
}
static int accepted(void){return ask_accepted;}
static int settled(void){return !ask_count&&reveal_index<0;}
static int offline(void){return ask_offline;}
static int calls_contain(const char *needle){char path[1024],body[8192];assert(!product_join(path,sizeof path,live_dir,"worker.calls"));return product_file_read(path,body,sizeof body)>0&&strstr(body,needle)!=NULL;}
static int row_slot(int entry){for(int s=0;s<daily_row_count();++s)if(daily_row_entry(s)==entry)return s;return -1;}
static void ask_and_answer(const char *reply,const char *shot_thinking){
    put("reply.txt",reply);drop("reply.txt");            /* thinking first */
    product_activate(DAILY_ASK);assert(ask_count==1&&answer_count>0&&answers[answer_count-1].state=='w');
    assert(page.sent==page.count);
    char name[80];snprintf(name,sizeof name,"ask-%s.ink",ask_queue[0].id);assert(has(name)&&has("asks.queue"));
    assert(pump(accepted,10));product_repaint();
    if(shot_thinking){dots_phase=2;product_repaint();capture(shot_thinking);}
    put("reply.txt",reply);ask_next=0;assert(pump(settled,20));
    assert(answers[answer_count-1].state=='d');assert(!has(name)&&!has("asks.queue"));
    product_repaint();
}

int main(int argc,char **argv){
    assert(argc==4);shots=argv[2];strokes_dir=argv[3];display=XOpenDisplay(NULL);assert(display);
    int screen=DefaultScreen(display);width=1860;height=2480;
    white_pixel=WhitePixel(display,screen);black_pixel=BlackPixel(display,screen);gray_pixel=black_pixel;pencil_pixel=black_pixel;
    window=XCreateSimpleWindow(display,RootWindow(display,screen),0,0,width,height,0,black_pixel,white_pixel);
    gc=XCreateGC(display,window,0,NULL);backing=XCreatePixmap(display,window,width,height,DefaultDepth(display,screen));
    textdraw=XftDrawCreate(display,backing,DefaultVisual(display,screen),DefaultColormap(display,screen));
    font=XftFontOpenName(display,screen,"sans:pixelsize=34");heading=XftFontOpenName(display,screen,"sans:bold:pixelsize=54");small=XftFontOpenName(display,screen,"sans:pixelsize=27");
    assert(font&&heading&&small&&textdraw);
    assert(XftColorAllocName(display,DefaultVisual(display,screen),DefaultColormap(display,screen),"black",&black));
    assert(XftColorAllocName(display,DefaultVisual(display,screen),DefaultColormap(display,screen),"white",&white_text));
    char root[]="/tmp/hermes-daily-XXXXXX";assert(mkdtemp(root));live_dir=root;
    /* The fake server. It records every call so the probe can check what was sent. */
    put("ink-worker.sh",
        "#!/bin/sh\nB=$(dirname \"$0\")\n[ -e \"$B/offline\" ] && exit 7\n"
        "printf '%s %s %s %s %s\\n' \"$1\" \"$2\" \"$4\" \"$5\" \"$(wc -c < \"$B/request.ink\")\" >> \"$B/worker.calls\"\n"
        "if [ \"$1\" = submit ]; then\n  if [ -e \"$B/reject\" ]; then printf 400 > \"$B/worker.status\"; exit 22; fi\n"
        "  printf 202 > \"$B/worker.status\"; printf 'queued\\n' > \"$B/worker.wire\"; exit 0\nfi\n"
        "if [ -e \"$B/reply.txt\" ]; then { printf 'done\\nI read:\\n'; cat \"$B/heard.txt\" 2>/dev/null; printf '\\n\\nHERMES:\\n'; cat \"$B/reply.txt\"; } > \"$B/worker.wire\"\n"
        "else printf 'thinking\\n' > \"$B/worker.wire\"; fi\nexit 0\n");
    assert(!setenv("HERMES_DAILY_FILE",argv[1],1));assert(!product_prepare_storage());
    assert(daily_enabled&&daily.count>=4&&strstr(product_root,"daily-workspace"));
    char original[37];strcpy(original,daily.document);
    char invalid[]="FORMAT\tHINKDAILY1\nDOCUMENT\t../../outside\n";assert(daily_parse(invalid));assert(!strcmp(original,daily.document));
    hermes_hand_open(screen);ask_queue_load();assert(ask_count==0);
    {   /* The hand is the requested file, not whatever fontconfig falls back to. */
        FcChar8 *file=NULL;const char *want=getenv("HERMES_REPLY_FONT");
        assert(want&&FcPatternGetString(hermes_hand->pattern,FC_FILE,0,&file)==FcResultMatch&&!strcmp((const char *)file,want));
    }

    /* ---- Today: a warm note, the day, and blank paper that is already a page ---- */
    daily_enter_today();product_repaint();
    int capture_index=daily_capture_index();assert(capture_index>=0&&daily_selected==capture_index&&product_view==PRODUCT_TODAY);
    assert(canvas_top==DAILY_TODAY_TOP&&canvas_bottom==height-190);
    assert(product_can_draw_at(55,1600)&&product_can_draw_at(width-55,1600)&&!product_can_draw_at(100,600));
    char note[600];daily_note(note,sizeof note);assert(strstr(note,getenv("HERMES_OWNER_NAME")));
    capture("01-today.ppm");
    int garden=-1,club=-1,bike=-1;for(int i=0;i<daily.count;++i){if(strstr(daily.entries[i].title,"Garden"))garden=i;if(strstr(daily.entries[i].title,"Book club"))club=i;if(strstr(daily.entries[i].title,"bicycle"))bike=i;}
    assert(garden>=0&&club>=0&&bike>=0);
    assert(product_button_at(400,daily_row_top(row_slot(garden))+60)==DAILY_OPEN+garden);
    assert(product_button_at(width/2,height-70)==DAILY_ASK&&product_button_at(100,height-70)==PRODUCT_HISTORY_ACTION&&product_button_at(width-100,height-70)==DAILY_PEN);

    /* Nothing new written: the star says so and sends nothing. */
    product_activate(DAILY_ASK);assert(ask_count==0&&!has("worker.calls"));
    write_ink("today-1");write_ink("today-2");
    put("heard.txt","call mum re: Sunday lunch\nwhich day works for the bike repair?");
    ask_and_answer("HEARD: You want a day for the bike repair.\nThursday after 17:00 is clear, and the repair is due today, so I'd ring the shop this morning.\nNEXT: call before noon.","02-today-thinking.ppm");
    assert(calls_contain("submit")&&calls_contain("Today%27s%20page")&&calls_contain("page%7Ctoday%7CCapture"));
    assert(strncmp(answers[0].answer,"HEARD",5)&&strstr(answers[0].answer,"Next: call before noon."));
    capture("03-today-answered.ppm");
    /* A second ask sends only what was written after the first. */
    {uint32_t before=page.sent;write_ink("today-3");assert(page.count>before);
     put("reject","x");product_activate(DAILY_ASK);ask_next=0;assert(pump(settled,10));drop("reject");
     assert(answer_count==2&&answers[1].state=='e'&&page.sent==page.count);}

    /* ---- A meeting page: prep above, full-width ink, the answer below ---- */
    product_activate(DAILY_OPEN+garden);assert(product_view==PRODUCT_NOTEBOOK&&daily_selected==garden);product_repaint();
    assert(canvas_top==DAILY_PAGE_TOP&&canvas_bottom==height-190);
    assert(product_can_draw_at(55,800)&&product_can_draw_at(width-55,800));
    assert(!product_can_draw_at(44,800)&&!product_can_draw_at(100,300));
    char garden_path[1024];strcpy(garden_path,state_path);
    snprintf(daily.entries[garden].context,sizeof daily.entries[garden].context,
             "Last time: raised beds on the balcony, a 200 euro budget, and Alex offered spare seedlings. Bring the balcony measurements. Alex prefers mornings; Saturdays are usually free.");
    product_repaint();capture("04-meeting.ppm");
    write_ink("garden-1");write_ink("garden-2");write_ink("garden-3");
    put("heard.txt","3 beds, 1.2 m each\ncompost delivery Sat?\nwhat soil mix for tomatos?");
    ask_and_answer("I read \"tomatos\" as tomatoes. Equal parts compost and topsoil, with a handful of perlite in each bed; give the tomatoes the sunniest one.\nNEXT: ask Alex whether Saturday before 11 works for the delivery.",NULL);
    capture("05-meeting-answered.ppm");
    assert(calls_contain("Garden%20plans%20with%20Alex")&&calls_contain("page%7Cmeeting%7C"));
    /* The answer belongs to the page: leave, come back, it is still there. */
    product_activate(DAILY_TODAY);assert(product_view==PRODUCT_TODAY);product_activate(DAILY_OPEN+garden);
    assert(!strcmp(state_path,garden_path)&&answer_count==1&&answers[0].state=='d'&&strstr(answers[0].heard,"tomatos"));
    product_repaint();
    assert(product_button_at(300,answer_box[0].y0+10)==DAILY_ANSWER_0);product_activate(DAILY_ANSWER_0);
    assert(slip_kind==SLIP_ANSWER&&!product_can_draw_at(500,1800));capture("06-slip-answer.ppm");
    product_activate(DAILY_SLIP_CLOSE);assert(!slip_kind);
    assert(product_button_at(400,230)==DAILY_PREP);product_activate(DAILY_PREP);assert(slip_kind==SLIP_CONTEXT);capture("07-slip-prep.ppm");
    assert(product_button_at(width/2,1000)==DAILY_NOOP);product_activate(DAILY_SLIP_CLOSE);
    product_activate(DAILY_PEN);assert(product_ink_toolbar_expanded);capture("08-pen.ppm");product_activate(DAILY_PEN);assert(!product_ink_toolbar_expanded);

    /* ---- Offline: the ask waits, says so, survives a restart, and lands later ---- */
    product_activate(DAILY_OPEN+club);assert(daily_selected==club);product_repaint();
    write_ink("club-1");put("offline","x");
    product_activate(DAILY_ASK);assert(ask_count==1);ask_next=0;assert(pump(offline,10));product_repaint();
    capture("09-offline.ppm");
    char club_side[1024];strcpy(club_side,answers_path);
    ask_count=0;ask_queue_load();assert(ask_count==1&&!strcmp(ask_queue[0].sidecar,club_side));   /* restart */
    product_activate(DAILY_TODAY);assert(product_view==PRODUCT_TODAY);
    drop("offline");put("heard.txt","3 questions for chapter 12?");
    put("reply.txt","Why does Ada keep the letter after she knows what it says?\nWho is the narrator protecting, and from whom?\nWas the ending earned, or only surprising?");
    ask_next=0;assert(pump(settled,20));assert(!ask_offline);
    {static Answer check[ANSWERS_MAX];int n=0;assert(!answers_read(club_side,check,&n));assert(n==1&&check[0].state=='d'&&strstr(check[0].answer,"Ada"));}
    product_activate(DAILY_OPEN+club);assert(answer_count==1&&answers[0].state=='d');product_repaint();capture("10-club-answered.ppm");

    /* ---- A long answer continues on a slip; it never pushes the ink ---- */
    product_activate(DAILY_OPEN+bike);assert(daily_selected==bike);product_repaint();write_ink("bike-1");
    put("heard.txt","what should I ask the bike shop?");
    ask_and_answer("Ask these, in this order:\n1. Can they look at it this week, and what would a quote cost?\n2. Is the rear brake squeal the pads or the rotor?\n3. Do they replace the chain while it is in, and is it worn?\n4. Can they true the rear wheel at the same time?\n5. How long will it take, and can you drop it before work?\n6. Do they keep the old parts so you can see what was replaced?\n7. Is there a warranty on the work?\n8. What would they skip if the budget is tight?\nNext: call them before noon; the repair is due today.",NULL);
    assert(answer_box[0].more);capture("11-long-answer.ppm");

    /* ---- Pages, a cleared page, and a saved page with its notes ---- */
    product_activate(PRODUCT_HISTORY_ACTION);assert(product_view==PRODUCT_HISTORY&&product_history_count>=4);capture("12-pages.ppm");
    int garden_row=-1;for(int i=0;i<product_history_count;++i)if(!strcmp(product_history[i].ink,garden_path))garden_row=i;assert(garden_row>=0);
    product_activate(PRODUCT_HISTORY_0+garden_row);assert(product_view==PRODUCT_NOTEBOOK&&daily_selected==garden);   /* today's pages open live */
    product_activate(PRODUCT_INK_CLEAR_PAGE);assert(!page.count&&!answer_count&&!strcmp(state_path,garden_path));
    product_activate(PRODUCT_HISTORY_ACTION);int saved=-1;
    for(int i=0;i<product_history_count;++i)if(!strcmp(product_history[i].kind,"meeting")&&strstr(product_history[i].ink,"/history/"))saved=i;
    assert(saved>=0);product_activate(PRODUCT_HISTORY_0+saved);
    assert(product_view==PRODUCT_ARCHIVE&&page.count>0&&answer_count==1&&answers_readonly&&!strcmp(state_path,product_history[saved].ink));
    product_repaint();assert(canvas_top==DAILY_PAGE_TOP);capture("13-saved.ppm");
    product_activate(DAILY_ASK_ARCHIVE);assert(product_view==PRODUCT_TODAY||product_view==PRODUCT_NOTEBOOK);

    /* ---- Today turns like a notebook: a full sheet offers the next, which is all paper ---- */
    product_activate(DAILY_TODAY);assert(product_view==PRODUCT_TODAY&&daily_sheet==0&&daily_sheets==1);product_repaint();
    assert(daily_turn_ready&&!daily_turner_right);capture("14-today-full.ppm");
    assert(product_button_at(width-150,height-160)==DAILY_TURN&&product_button_at(100,height-160)!=DAILY_TURN);
    char sheet1[1024];strcpy(sheet1,state_path);uint32_t sheet1_count=page.count;int sheet1_answers=answer_count;
    product_activate(DAILY_TURN);assert(daily_sheet==1&&daily_sheets==2&&!page.count&&!answer_count&&strcmp(state_path,sheet1)&&!access(state_path,F_OK));
    product_repaint();assert(canvas_top==DAILY_SHEET_TOP&&!daily_turn_ready&&product_can_draw_at(100,400)&&product_button_at(400,600)==PRODUCT_NONE);
    capture("15-sheet-2.ppm");
    write_ink("bike-1");put("heard.txt","what should I ask the bike shop?");
    ask_and_answer("Whether they can fit it in this week, and what it will cost before they start.",NULL);
    assert(answer_count==1&&answers[0].state=='d'&&calls_contain("page%7Ctoday%7CCapture"));capture("16-sheet-2-answered.ppm");
    /* A quick sideways finger swipe turns sheets; slow, short or diagonal ones do not. */
    assert(daily_swipe(400,20,0.3)==DAILY_SHEET_PREV&&daily_swipe(-400,20,0.3)==DAILY_NOOP);
    assert(!daily_swipe(400,300,0.3)&&!daily_swipe(60,0,0.2)&&!daily_swipe(400,0,1.5));
    assert(daily_swipe(138,19,0.04)==DAILY_SHEET_PREV);      /* a real flick from the Scribe's log */
    char sheet2[1024];strcpy(sheet2,state_path);
    assert(daily_turner_right&&product_button_at(daily_turner_left+5,70)==DAILY_SHEET_PREV&&product_button_at(daily_turner_right-5,70)==DAILY_NOOP);
    product_activate(DAILY_SHEET_PREV);assert(daily_sheet==0&&!strcmp(state_path,sheet1)&&page.count==sheet1_count&&answer_count==sheet1_answers);
    product_repaint();assert(canvas_top==DAILY_TODAY_TOP&&!daily_turn_ready);capture("17-back-to-sheet-1.ppm");
    assert(daily_swipe(-400,0,0.3)==DAILY_SHEET_NEXT&&daily_swipe(400,0,0.3)==DAILY_NOOP);
    /* A row's star opens its page with the prep already showing; a plain row just opens. */
    {int plain=-1;for(int i=0;i<daily.count;++i)if(daily_is_row(i)&&!daily.entries[i].context[0])plain=i;assert(plain>=0&&daily.entries[garden].context[0]);
     int star_y=daily_row_top(row_slot(garden))+40;
     assert(product_button_at(width-80,star_y)==DAILY_OPEN_PREP+garden&&product_button_at(400,star_y)==DAILY_OPEN+garden);
     assert(product_button_at(width-80,daily_row_top(row_slot(plain))+40)==DAILY_OPEN+plain);
     product_activate(DAILY_OPEN_PREP+garden);assert(product_view==PRODUCT_NOTEBOOK&&daily_selected==garden&&slip_kind==SLIP_CONTEXT);
     product_repaint();capture("18-star-prep.ppm");product_activate(DAILY_SLIP_CLOSE);
     product_activate(DAILY_TODAY);product_activate(DAILY_SHEET_PREV);assert(daily_sheet==0);}
    /* ---- A task is marked done from its row; undo inside the window; the sync answer shows ---- */
    {   assert(!strcmp(daily.entries[bike].kind,"task"));product_repaint();
        int by=daily_row_top(row_slot(bike))+40;
        assert(product_button_at(DAILY_DONE_BOX_X+10,by)==DAILY_TASK_DONE_0+bike);
        assert(product_button_at(600,by)==DAILY_OPEN+bike);                 /* the title still opens the page */
        product_activate(DAILY_TASK_DONE_0+bike);assert(daily_done_at(daily.entries[bike].id)>0);
        product_repaint();capture("21-task-done.ppm");
        product_activate(DAILY_TASK_DONE_0+bike);assert(daily_done_at(daily.entries[bike].id)==0);   /* undone */
        product_activate(DAILY_TASK_DONE_0+bike);
        char sent[1024],line[200];assert(!daily_done_file("done.sent",sent,sizeof sent));
        snprintf(line,sizeof line,"%s synced 1\n",daily.entries[bike].id);assert(!product_file_write(sent,line,strlen(line)));
        assert(daily_done_sent(daily.entries[bike].id)=='s');product_repaint();capture("22-task-synced.ppm");
    }
    assert(product_button_at(daily_turner_right-5,70)==DAILY_SHEET_NEXT);
    /* Today reopens the latest sheet, from a meeting and after a restart. */
    product_activate(DAILY_OPEN+garden);product_activate(DAILY_TODAY);assert(daily_sheet==1&&!strcmp(state_path,sheet2));
    daily_sheet=0;daily_sheets=1;daily_enter_today();assert(daily_sheet==1&&daily_sheets==2&&answer_count==1);
    /* Pages lists both sheets under their own names; the later one opens live, on its sheet. */
    product_activate(DAILY_SHEET_PREV);assert(daily_sheet==0);
    product_activate(PRODUCT_HISTORY_ACTION);{int s1=-1,s2=-1;
     for(int i=0;i<product_history_count;++i){if(!strcmp(product_history[i].ink,sheet1))s1=i;if(!strcmp(product_history[i].ink,sheet2))s2=i;}
     assert(s1>=0&&s2>=0&&!strcmp(product_history[s1].title,"Today's page")&&!strcmp(product_history[s2].kind,"sheet")&&strstr(product_history[s2].title," \xc2\xb7 2"));
     product_activate(PRODUCT_HISTORY_0+s2);}
    assert(product_view==PRODUCT_TODAY&&daily_sheet==1&&!strcmp(state_path,sheet2)&&answer_count==1);
    /* ---- An answer links back to the page it drew on; a link to this page is hidden ---- */
    {   char *a=answers[0].answer;size_t n=strlen(a);
        snprintf(a+n,ANSWER_TEXT-n,"\n\xe2\x86\x97 Today's page \xc2\xb7 %s\n\xe2\x86\x97 Today's page \xc2\xb7 2 \xc2\xb7 %s",daily.date,daily.date);
        assert(!answers_write(answers_path,answers,answer_count));   /* as a delivered answer is */
        product_repaint();assert(answer_box[0].refs==1);
        int ry=answer_ref_top(0,0)+20;assert(product_button_at(300,ry)==DAILY_PAGE_REF_0);
        assert(product_button_at(300,answer_box[0].y0+10)==DAILY_ANSWER_0);          /* the words still open the slip */
        capture("19-page-link.ppm");
        /* Read on a slip (as long answers are), the answer keeps its link above the footer. */
        product_activate(DAILY_ANSWER_0);assert(slip_kind==SLIP_ANSWER&&!strstr(slip_text,"\xe2\x86\x97"));
        product_repaint();assert(slip_refs==1);
        assert(product_button_at(300,slip_ref_top(0)+20)==DAILY_PAGE_REF_0);capture("20-slip-link.ppm");
        product_activate(DAILY_PAGE_REF_0);assert(!slip_kind);
        assert(product_view==PRODUCT_TODAY&&daily_sheet==0&&!strcmp(state_path,sheet1));   /* opened live, on sheet 1 */
        product_activate(DAILY_SHEET_NEXT);assert(daily_sheet==1);product_repaint();       /* and the page's own row */
        product_activate(DAILY_PAGE_REF_0);assert(daily_sheet==0&&!strcmp(state_path,sheet1));
    }
    product_activate(DAILY_SHEET_NEXT);assert(daily_sheet==1);

    /* ---- Safety: corrupt files, legacy lanes, and the erase watermark ---- */
    product_activate(DAILY_OPEN+club);char club_path[1024];strcpy(club_path,state_path);uint32_t club_count=page.count;
    int other=-1;for(int i=0;i<daily.count;++i)if(daily_is_row(i)&&i!=club&&i!=garden&&i!=bike)other=i;assert(other>=0);
    char damaged[1024];assert(!daily_entry_path(other,damaged,sizeof damaged));assert(!product_file_write(damaged,"bad",3));
    assert(daily_open(other));assert(page.count==club_count&&!strcmp(club_path,state_path));assert(!unlink(damaged));
    {   /* Erasing asked-about ink moves the watermark down by what was erased. */
        memset(&page,0,sizeof page);canvas_top=DAILY_PAGE_TOP;canvas_bottom=height-190;
        for(int i=0;i<3;++i)assert(!append_point(5000+i*300,5000,i==0,0));
        for(int i=0;i<3;++i)assert(!append_point(40000+i*300,30000,i==0,0));
        page.sent=page.count;
        for(int i=0;i<4;++i)assert(!append_point(20000+i*300,50000,i==0,0));
        assert(!append_point(5300,5000,1,1));assert(!append_point(5400,5000,0,1));
        assert(!ink_erase_snapshot());ink_compact_from(ink_undo_page);
        assert(page.count==7&&page.sent==3);
    }
    /* ---- Today stays current: a same-day update waits for Today; a new day waits for a pause ---- */
    {   static char wire[65536],next[65536];char bundle[1024],old_date[16],new_date[16];
        int n=product_file_read(argv[1],wire,sizeof wire);assert(n>0&&n<60000);
        assert(!product_join(bundle,sizeof bundle,root,"bundle.wire"));assert(!product_file_write(bundle,wire,(size_t)n));
        assert(!setenv("HERMES_DAILY_FILE",bundle,1));
        daily_bundle_mtime=0;daily_refresh_next=0;daily_tick();assert(daily_bundle_mtime);        /* the launch copy */
        int rows=daily.count;snprintf(old_date,sizeof old_date,"%s",daily.date);
        /* A new task arrives mid-day. On a meeting page nothing moves; back on Today it appears. */
        n=snprintf(next,sizeof next,"%sENTRY\ttask\t11111111-2222-5333-8444-555555555555\tA task added in TickTick\tDue %s\t-\n",wire,old_date);
        assert(!product_file_write(bundle,next,(size_t)n));daily_bundle_size=0;                    /* same second: force "changed" */
        product_activate(DAILY_OPEN+garden);last_input_at=seconds()-120;daily_refresh_next=0;daily_tick();
        assert(product_view==PRODUCT_NOTEBOOK&&daily.count==rows);
        product_activate(DAILY_TODAY);daily_refresh_next=0;daily_tick();
        assert(product_view==PRODUCT_TODAY&&daily.count==rows+1&&!strcmp(daily.date,old_date));
        /* The next day lands while he is writing: it waits for a pause, then Today is the new day. */
        struct tm day;memset(&day,0,sizeof day);day.tm_year=atoi(old_date)-1900;day.tm_mon=atoi(old_date+5)-1;day.tm_mday=atoi(old_date+8)+1;day.tm_hour=12;
        mktime(&day);strftime(new_date,sizeof new_date,"%Y-%m-%d",&day);
        char *date_line=strstr(next,"\nDATE\t");assert(date_line);memcpy(date_line+6,new_date,10);
        char *doc_line=strstr(next,"\nDOCUMENT\t");assert(doc_line);doc_line[10]=doc_line[10]=='a'?'b':'a';   /* a new day's document */
        assert(!product_file_write(bundle,next,strlen(next)));daily_bundle_size=0;
        last_input_at=seconds();daily_refresh_next=0;daily_tick();assert(!strcmp(daily.date,old_date));
        last_input_at=seconds()-60;daily_refresh_next=0;daily_tick();
        assert(!strcmp(daily.date,new_date)&&product_view==PRODUCT_TODAY&&daily_selected==daily_capture_index()&&daily_sheet==0);
        product_repaint();capture("23-new-day.ppm");
    }
    XCloseDisplay(display);
    puts("Native daily check passed: warm Today with its own page, full-width pages, ask snapshots only new ink, answers land on their own page, "
         "offline asks queue and survive restart, long answers continue on a slip, prep slip, pen rail, Pages, clear keeps notes, "
         "Today turns to a fresh sheet and back, saved pages read-only, corrupt-file recovery, legacy isolation, erase watermark.");
    return 0;
}
