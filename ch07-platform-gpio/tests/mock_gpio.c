// SPDX-License-Identifier: GPL-2.0-only
/* Host-only UAPI mock. Compile with link wrappers; never linked into target app. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "lab_gpio_uapi.h"
ssize_t __real_read(int, void *, size_t);
ssize_t __real_write(int, const void *, size_t);
static int device_index(int fd) {
    struct stat a,b; char path[256];
    if(fstat(fd,&a)!=0) return -1;
    for(int i=0;i<8;i++) {
        snprintf(path,sizeof(path),"%s/dev%d",getenv("MOCK_DIR"),i);
        if(stat(path,&b)==0 && a.st_ino==b.st_ino && a.st_dev==b.st_dev) return i;
    }
    return -1;
}
static int value(int channel,const char *name, int fallback) {
    char path[256]; int n=fallback; FILE *f;
    snprintf(path,sizeof(path),"%s/%s%d",getenv("MOCK_DIR"),name,channel);
    f=fopen(path,"r"); if(f) { if(fscanf(f,"%d",&n)!=1) n=fallback; fclose(f); }
    return n;
}
static void trace(int channel,const char *kind,int v) {
    char path[256]; struct timespec ts; FILE *f;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    snprintf(path,sizeof(path),"%s/trace",getenv("MOCK_DIR"));
    f=fopen(path,"a");
    if(f) { fprintf(f,"%.6f %ld %d %s %d\n",ts.tv_sec+ts.tv_nsec/1e9,(long)getpid(),channel,kind,v); fclose(f); }
}
void __wrap_syslog(int priority,const char *fmt,...) {
    va_list ap; char msg[512],path[256]; FILE *f; (void)priority;
    va_start(ap,fmt); vsnprintf(msg,sizeof(msg),fmt,ap); va_end(ap);
    snprintf(path,sizeof(path),"%s/errors",getenv("MOCK_DIR"));
    f=fopen(path,"a"); if(f) { fprintf(f,"%s\n",msg); fclose(f); }
}
void __wrap___syslog_chk(int priority,int flag,const char *fmt,...) {
    va_list ap; char msg[512]; (void)flag;
    va_start(ap,fmt); vsnprintf(msg,sizeof(msg),fmt,ap); va_end(ap);
    __wrap_syslog(priority,"%s",msg);
}
int __wrap_ioctl(int fd, unsigned long cmd,...) {
    va_list ap; void *arg; int led=0,channel=device_index(fd);
    if(channel<0) { errno=ENOTTY; return -1; }
    if(value(channel,"unbind",0)) { trace(channel,"UNBIND",0); errno=ENODEV; return -1; }
    if(cmd==LAB_GPIO_IOC_OFF) { if(value(channel,"fail_off",0)) { trace(channel,"OFF_FAIL",0); errno=EIO; return -1; } if(pwrite(fd,&led,sizeof(led),0)!=sizeof(led)) abort(); trace(channel,"OFF",0); return 0; }
    if(cmd!=LAB_GPIO_IOC_GET && cmd!=LAB_GPIO_IOC_SET) { errno=ENOTTY; return -1; }
    va_start(ap,cmd); arg=va_arg(ap,void *); va_end(ap);
    if(cmd==LAB_GPIO_IOC_GET) {
        struct lab_gpio_state *s=arg;
        if(!s) { errno=EFAULT; return -1; }
        if(value(channel,"fail_get",0)) { trace(channel,"GET_FAIL",0); errno=EIO; return -1; }
        if(pread(fd,&led,sizeof(led),0)!=sizeof(led)) abort();
        s->abi_version=LAB_GPIO_ABI_VERSION; s->led=(unsigned)led;
        s->button=(unsigned)value(channel,"button",0); s->reserved=0;
        trace(channel,"GET",(int)s->button); return 0;
    }
    if(cmd==LAB_GPIO_IOC_SET) {
        struct lab_gpio_led *s=arg;
        if(!s) { errno=EFAULT; return -1; }
        if(s->abi_version!=LAB_GPIO_ABI_VERSION || s->led>1 || s->reserved[0] || s->reserved[1]) { errno=EINVAL; return -1; }
        if(value(channel,"fail_set",0)) { trace(channel,"SET_FAIL",(int)s->led); errno=EIO; return -1; }
        led=(int)s->led; if(pwrite(fd,&led,sizeof(led),0)!=sizeof(led)) abort(); trace(channel,"SET",led); return 0;
    }
    errno=ENOTTY; return -1;
}
ssize_t __wrap_read(int fd,void *buffer,size_t count) {
    int led,channel=device_index(fd); off_t pos; char s[2];
    if(channel<0) return __real_read(fd,buffer,count);
    if(value(channel,"unbind",0)) { errno=ENODEV; return -1; }
    if(!count) return 0;
    pos=lseek(fd,0,SEEK_CUR); if(pos>=2) return 0;
    if(pread(fd,&led,sizeof(led),0)!=sizeof(led)) abort();
    s[0]=led?'1':'0'; s[1]='\n'; if(count>(size_t)(2-pos)) count=(size_t)(2-pos);
    memcpy(buffer,s+pos,count); lseek(fd,(off_t)count,SEEK_CUR); return (ssize_t)count;
}
ssize_t __wrap_write(int fd,const void *buffer,size_t count) {
    int led,channel=device_index(fd); const char *s=buffer;
    if(channel<0) return __real_write(fd,buffer,count);
    if(value(channel,"unbind",0)) { errno=ENODEV; return -1; }
    if(!count) return 0;
    if(count!=1 || (*s!='0' && *s!='1')) { errno=EINVAL; return -1; }
    led=*s-'0'; if(pwrite(fd,&led,sizeof(led),0)!=sizeof(led)) abort(); trace(channel,"WRITE",led); return 1;
}
