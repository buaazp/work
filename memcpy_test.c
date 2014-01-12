/**
 * @file memcpy_test.c
<<<<<<< HEAD
 * @brief memcpy vs vmsplice test
=======
 * @brief memcpy vs vmsplice test.
>>>>>>> 43a1e396d9459fac49cc0732904deadfb276dea6
 * @author 招牌疯子 zp@buaa.us
 * @version 1.0.0
 * @date 2014-01-11
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

#define MAX 1024
#define BUF_SIZE 4096
#define PIPE_BUF 4096
#define SHMSIZ 1073741824

pid_t pid;
int in_fd;
int pipefd[2]; 
int shmid;
pthread_mutex_t lock;
struct stat statbuf;

void cal_md5(const char *buff, const int len, char *md5);
void sendfile(int mode);
void getfile(int mode);

int main(int argc,char **argv)
{
    if(argc!=3) 
    {
        perror("Usage: copy [filename] [copy_mode]\n mode: 1. memcpy 2. splice\n note: file size should be less than 512M.\n");
        return 0;
    }

    int mode = atoi(argv[2]);

    if((in_fd = open(argv[1],O_RDONLY))<0) { 
        perror("open in_fd faild");
        return 0;
    }
   
    fstat(in_fd,&statbuf);
    
    int len = statbuf.st_size;
    if(len >= SHMSIZ)
    {
        perror("file size");
        exit(EXIT_FAILURE);
    }

    shmid = shmget((key_t)1234, SHMSIZ, 0666 | IPC_CREAT);
    if(shmid == -1)
    {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_init(&lock,NULL);
    pthread_mutex_lock(&lock);

    pipe(pipefd);

    pid = fork();
    switch(pid)
    {
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
        case 0:
            getfile(mode);
            break;
        default:
        {
            printf("child_pid: %d\n", pid);
            sendfile(mode);
        }
    }

    if(pid > 0)
    {
        int status;
        pid_t ret;
        ret = wait(&status);
        if(ret <0)
        {
            perror("wait error");
            exit(EXIT_FAILURE);
        }

        //printf("ret = %d pid = %d\n", ret, pid);
        
        if (WIFEXITED(status))
        {
            printf("child exited normal exit status=%d\n", WEXITSTATUS(status));
            printf("-------------------------------------------\n\n");
        }
        else if (WIFSIGNALED(status))
            printf("child exited abnormal signal number=%d\n", WTERMSIG(status));
        else if (WIFSTOPPED(status))
            printf("child stoped signal number=%d\n", WSTOPSIG(status));
        if(shmctl(shmid, IPC_RMID, 0) == -1)
        {
            perror("shmctl(IPC_RMID) failed");
            exit(EXIT_FAILURE);
        }
    }

    pthread_mutex_destroy(&lock);
    close(pipefd[0]);
    close(pipefd[1]);
    close(in_fd);
    return 0;
}

void cal_md5(const char *buff, const int len, char *md5)
{
    printf("Begin to Caculate MD5...\n");
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
    printf("md5: %s\n", md5sum);
}

void sendfile(int mode)
{
    int len=statbuf.st_size;
    int bytes;
    void *usrmem;
    usrmem = malloc(SHMSIZ);
    if(usrmem == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    char buffer[BUF_SIZE];
    void *head = usrmem;
    while((bytes = read(in_fd,buffer,sizeof(buffer))) >0)
    {
        memcpy(head, buffer, bytes);
        head += bytes;
    }
    char md5[33];
    cal_md5(usrmem, len, md5);
    //printf("pid: [%d] out bytes=%d\n", pid, bytes);

    void *shared_memory = shmat(shmid, (void *)0, 0);
    if(shared_memory == (void *)-1)
    {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    //printf("Memory attached at %X\n", (int)shared_memory);

    clock_t begin, end;
    begin = clock();
    //printf("mode[%d] sendfile() begin at: %f\n", mode, (double)begin/CLOCKS_PER_SEC);

    if(mode == 1)
    {
        void *shm = shared_memory;
        void *src = usrmem;

        int size=BUF_SIZE,total=0;
        while(total < len)
        {
            size = len - total > BUF_SIZE ? BUF_SIZE : len-total;
            memcpy(shm,src,size);
            shm += size;
            src += size;
            total += size;
            //printf("mode[%d] sendfile() total=%d size=%d\n", mode, total, size);
        }
        pthread_mutex_unlock(&lock);
        //cal_md5(shared_memory, len, md5);
    }

    else if(mode == 2)
    {
        char *p = usrmem;
        size_t send;
        size_t left = len;
        long ret;
        struct iovec iov;
        long nr_segs = 1;
        int flags = 0x1;

        while (left > 0)
        {
            send = left > PIPE_BUF ? PIPE_BUF : left;
            iov.iov_base = p;
            iov.iov_len = send;

            ret = vmsplice(pipefd[1], &iov, nr_segs, flags);
            if (ret == -1)
            {
                perror("vmsplice failed");
                return;
            }

            left -= ret;
            p += ret;
            //printf("mode[%d] sendfile() left=%d ret=%d\n", mode, left, ret);
        }
    }

    end = clock();
    //printf("mode[%d] sendfile() end at: %f\n", mode, (double)end/CLOCKS_PER_SEC);
    printf("mode[%d] sendfile() CPU time: %fs\n", mode, (double)(end - begin)/CLOCKS_PER_SEC);

    if(shmdt(shared_memory) == -1)
    {
        perror("sendfile(): shmdt failed");
        exit(EXIT_FAILURE);
    }

    if(usrmem != NULL)
        free(usrmem);
}

void getfile(int mode)
{
    int len=statbuf.st_size;
    char md5[33];
    int bytes;
    void *usrmem;
    usrmem = malloc(SHMSIZ);
    if(usrmem == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    void *shared_memory = shmat(shmid, (void *)0, 0);
    if(shared_memory == (void *)-1)
    {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    //printf("Memory attached at %X\n", (int)shared_memory);

    clock_t begin, end;
    begin = clock();
    //printf("mode[%d] getfile() begin at: %f\n", mode, (double)begin/CLOCKS_PER_SEC);

    if(mode == 1)
    {
        void *shm = shared_memory;
        void *dst = usrmem;
       
        pthread_mutex_lock(&lock);
        int size=BUF_SIZE,total=0;
        while(total < len)
        {
            size = len - total > BUF_SIZE ? BUF_SIZE : len-total;
            memcpy(dst, shm, size);
            shm += size;
            dst += size;
            total += size;
            //printf("mode[%d] getfile() total=%d size=%d\n", mode, total, size);
        }
        pthread_mutex_unlock(&lock);
    }

    else if(mode == 2)
    {
        char *p = usrmem;
        size_t get;
        size_t left = len;
        long ret;
        struct iovec iov;
        long nr_segs = 1;
        int flags = 0x1;

        while (left > 0)
        {
            get = left > PIPE_BUF ? PIPE_BUF : left;
            iov.iov_base = p;
            iov.iov_len = get;

            ret = vmsplice(pipefd[0], &iov, nr_segs, flags);
            if (ret == -1)
            {
                perror("vmsplice failed");
                return;
            }

            left -= ret;
            p += ret;
            //printf("mode[%d] getfile() left=%d ret=%d\n", mode, left, ret);
        }
    }

    end = clock();
    //printf("mode[%d] getfile() end at: %f\n", mode, (double)end/CLOCKS_PER_SEC);
    printf("mode[%d] getfile() CPU time: %fs\n", mode, (double)(end - begin)/CLOCKS_PER_SEC);

    cal_md5(usrmem, len, md5);

    if(shmdt(shared_memory) == -1)
    {
        perror("getfile(): shmdt failed");
        exit(EXIT_FAILURE);
    }

    if(usrmem != NULL)
        free(usrmem);
}

