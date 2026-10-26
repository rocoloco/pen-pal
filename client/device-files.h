/* The Scribe-side workspace: where the app keeps its files, and small durable-file helpers.
 * Included after the drawing helpers. */
#include <sys/wait.h>
static const char *live_dir="/mnt/us/hermes-native-live";
static char live_body[60000];       /* the note kept with an archived page, shown on its prep slip */
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
