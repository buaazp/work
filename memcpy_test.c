/**
 * @file copy_test.c
 * @brief Test three copy methods between processes.
 * @author 招牌疯子 zp@buaa.us
 * @version 1.0.0
 * @date 2014-01-06
 */


#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<unistd.h>
#include<time.h>
#include<sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/uio.h>

#define MAX 1024
#define BUF_SIZE 4096
#define SHMSIZ 1073741824

pid_t pid;
int in_fd;
int pipefd[2]; 
int shmid;
struct stat statbuf;

void sendfile(int mode);
void getfile(int mode);

int main(int argc,char **argv)
{
    if(argc!=3) 
    {
        perror("Usage: copy [filename] [copy_mode]\n mode: 1. pipe 2. memcpy 3. splice\n");
        return 1;
    }

    int mode = atoi(argv[2]);

    if((in_fd = open(argv[1],O_RDONLY))<0) { 
        perror("open in_fd faild");
        return 1;
    }
   
    fstat(in_fd,&statbuf);
    
    int len = statbuf.st_size;
    if(len > SHMSIZ)
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

    close(pipefd[0]);
    close(pipefd[1]);
    close(in_fd);
    return 0;
}

void sendfile(int mode)
{
    int len=statbuf.st_size;
    int bytes;
    void *usrmem;
    usrmem = malloc(SHMSIZ);
    if(usrmem < 0)
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
    //printf("pid: [%d] out bytes=%d\n", pid, bytes);
    struct iovec iov;
    iov.iov_base = usrmem;
    iov.iov_len = len;

    void *shared_memory = shmat(shmid, (void *)0, 0);
    if(shared_memory == (void *)-1)
    {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    //printf("Memory attached at %X\n", (int)shared_memory);

    clock_t begin, end;
    begin = clock();
    printf("mode[%d] sendfile() begin at: %f\n", mode, (double)begin/CLOCKS_PER_SEC);

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
            //printf("total_write=%d size=%d\n", total, size);
            printf("mode[%d] sendfile() total=%d size=%d\n", mode, total, size);
        }
    }

    else if(mode == 2)
    {
        long nr_segs = 1;
        int flags = 0x1;

        while(len > 0)
        {
            if((bytes = vmsplice(pipefd[1], &iov, nr_segs, flags)) < 0)
            //if((bytes=splice(in_fd,NULL,pipefd[1],NULL,len,0x1))<0)
            {
                perror("vmsplice pipefd faild");
                return;
            }
            else
                len -= bytes;
            printf("mode[%d] sendfile() len=%d bytes=%d\n", mode, len, bytes);
        }
    }

    end = clock();
    printf("mode[%d] sendfile() end at: %f\n", mode, (double)end/CLOCKS_PER_SEC);
    printf("mode[%d] sendfile() CPU time: %fs\n", mode, (double)(end - begin)/CLOCKS_PER_SEC);

    if(shmdt(shared_memory) == -1)
    {
        perror("sendfile(): shmdt failed");
        exit(EXIT_FAILURE);
    }

    return;
}

void getfile(int mode)
{
    int len=statbuf.st_size;
    int bytes;
    void *usrmem;
    usrmem = malloc(SHMSIZ);
    if(usrmem < 0)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    struct iovec iov;
    iov.iov_base = usrmem;
    iov.iov_len = len;

    void *shared_memory = shmat(shmid, (void *)0, 0);
    if(shared_memory == (void *)-1)
    {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    //printf("Memory attached at %X\n", (int)shared_memory);

    clock_t begin, end;
    begin = clock();
    printf("mode[%d] getfile() begin at: %f\n", mode, (double)begin/CLOCKS_PER_SEC);

    if(mode == 1)
    {
        void *shm = shared_memory;
        void *dst = usrmem;
       
        int size=BUF_SIZE,total=0;
        while(total < len)
        {
            size = len - total > BUF_SIZE ? BUF_SIZE : len-total;
            memcpy(dst, shm, size);
            shm += size;
            dst += size;
            total += size;
            printf("mode[%d] getfile() total=%d size=%d\n", mode, total, size);
        }
    }

    else if(mode == 2)
    {
        long nr_segs = 1;
        int flags = 0x1;
        while(len > 0)
        {
            if((bytes = vmsplice(pipefd[0], &iov, nr_segs, flags)) < 0)
            //if((bytes=splice(pipefd[0],NULL,out_fd3,NULL,len,0x1))<0)
            {
                perror("splice out_fd3 faild");
                return;
            }
            else
                len -= bytes;
            printf("mode[%d] getfile() len=%d bytes=%d\n", mode, len, bytes);
        }
    }

    end = clock();
    printf("mode[%d] getfile() end at: %f\n", mode, (double)end/CLOCKS_PER_SEC);
    printf("mode[%d] getfile() CPU time: %fs\n", mode, (double)(end - begin)/CLOCKS_PER_SEC);

    if(shmdt(shared_memory) == -1)
    {
        perror("getfile(): shmdt failed");
        exit(EXIT_FAILURE);
    }
    return;
}

