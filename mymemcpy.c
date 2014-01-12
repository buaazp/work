/**
 * @file mymemcpy.c
 * @brief use vmsplice to mem copy.
 * @author 招牌疯子 zp@buaa.us
 * @version 1.0.0
 * @date 2014-01-12
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/uio.h>
#include "zmd5.h"

#define BUF_SIZE 4096
#define PIPE_BUF 4096

int in_fd;
int pipefd[2]; 
struct stat statbuf;

void cal_md5(const char *buff, const int len, char *md5);
void *mymemcpy(void *dest, const void *src, size_t n);

int main(int argc,char **argv)
{
    if(argc!=2) 
    {
        perror("Usage: mymemcpy [filename] \n");
        return 0;
    }

    if((in_fd = open(argv[1],O_RDONLY))<0) { 
        perror("open in_fd faild");
        return 0;
    }
   
    fstat(in_fd,&statbuf);
    int len = statbuf.st_size;
    int bytes;
    void *srcmem, *destmem1, *destmem2;
    srcmem = malloc(len);
    if(srcmem == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memset(srcmem, 0, len);
    destmem1 = malloc(len);
    if(destmem1 == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memset(destmem1, 0, len);
    destmem2 = malloc(len);
    if(destmem2 == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memset(destmem2, 0, len);
    char buffer[BUF_SIZE];
    void *head = srcmem;
    while((bytes = read(in_fd,buffer,sizeof(buffer))) >0)
    {
        memcpy(head, buffer, bytes);
        head += bytes;
    }
    char md5[33];
    printf("\n=======================\nstep1: before memory copy:\n\n");
    printf("    [srcmem]");
    cal_md5(srcmem, len, md5);
    printf("    [destmem1]");
    cal_md5(destmem1, len, md5);
    printf("    [destmem2]");
    cal_md5(destmem2, len, md5);

    printf("\n=======================\nstep2: start memcpy() copy...\n\n");
    clock_t begin, end;

    begin = clock();
    memcpy(destmem1, srcmem, len);
    end = clock();

    printf("    memcpy() CPU time: %fs\n", (double)(end - begin)/CLOCKS_PER_SEC);


    printf("\n=======================\nstep3: start mymemcpy() copy...\n\n");
    begin = clock();

    mymemcpy(destmem2, srcmem, len);

    end = clock();
    printf("    mymemcpy() CPU time: %fs\n", (double)(end - begin)/CLOCKS_PER_SEC);

    printf("\n=======================\nstep4: after memory copy:\n\n");
    printf("    [destmem1]");
    cal_md5(destmem1, len, md5);
    printf("    [destmem2]");
    cal_md5(destmem2, len, md5);

    printf("\n=======================\nstep5: copy test finished.\n\n");
    free(srcmem);
    free(destmem1);
    free(destmem2);
    close(in_fd);
    return 0;
}

void *mymemcpy(void *dest, const void *src, size_t n)
{
    void *p = src, *q = dest;
    size_t send;
    size_t left = n;
    long ret;
    struct iovec iov[2];
    long nr_segs = 1;
    int flags = 0x1;

    pipe(pipefd);

    while (left > 0)
    {
        send = left > PIPE_BUF ? PIPE_BUF : left;
        iov[0].iov_base = p;
        iov[0].iov_len = send;

        ret = vmsplice(pipefd[1], &iov[0], nr_segs, flags);
        if (ret == -1)
        {
            perror("vmsplice failed");
            return NULL;
        }
        p += ret;

        iov[1].iov_base = q;
        iov[1].iov_len = send;
        ret = vmsplice(pipefd[0], &iov[1], nr_segs, flags);
        if (ret == -1)
        {
            perror("vmsplice failed");
            return NULL;
        }
        q += ret;

        left -= ret;
        //printf("mode[%d] sendfile() left=%d ret=%d\n", mode, left, ret);
    }

    close(pipefd[0]);
    close(pipefd[1]);
    return dest;
}

void cal_md5(const char *buff, const int len, char *md5)
{
    md5_state_t mdctx;
    md5_byte_t md_value[16];
    char md5sum[33];
    int i;
    int h, l;
    md5_init(&mdctx);
    md5_append(&mdctx, (const unsigned char*)(buff), len);
    md5_finish(&mdctx, md_value);

    for(i=0; i<16; ++i)
    {
        h = md_value[i] & 0xf0;
        h >>= 4;
        l = md_value[i] & 0x0f;
        md5sum[i * 2] = (char)((h >= 0x0 && h <= 0x9) ? (h + 0x30) : (h + 0x57));
        md5sum[i * 2 + 1] = (char)((l  >= 0x0 && l <= 0x9) ? (l + 0x30) : (l + 0x57));
    }
    md5sum[32] = '\0';
    strcpy(md5, md5sum);
    printf(" md5: %s\n", md5sum);
}

