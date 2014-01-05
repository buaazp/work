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

#define MAX 1024
#define BUF_SIZE 4096
#define SHMSIZ 2147483648

pid_t pid;
int in_fd,out_fd1,out_fd2,out_fd3;
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

    char outfile1[MAX],outfile2[MAX],outfile3[MAX];

    strcpy(outfile1,argv[1]);
    strcat(outfile1,".pipe");
    strcpy(outfile2,argv[1]);
    strcat(outfile2,".memcpy");
    strcpy(outfile3,argv[1]);
    strcat(outfile3,".splice");

    int mode = atoi(argv[2]);

    if((in_fd = open(argv[1],O_RDONLY))<0) { 
        perror("open in_fd faild");
        return 1;
    }
    switch(mode)
    {
        case 1:
            if((out_fd1 = open(outfile1,O_WRONLY|O_CREAT|O_TRUNC,0777))<0) {
                perror("open out_fd1 faild");
                return 1;
            }
            break;
        case 2:
            if((out_fd2 = open(outfile2,O_RDWR|O_CREAT|O_TRUNC,0777))<0){//mmap要读写
                perror("open out_fd2 faild");
                return 1;
            }
            break;
        case 3:
            if((out_fd3 = open(outfile3,O_WRONLY|O_CREAT|O_TRUNC,0777))<0) {
                perror("open out_fd3 faild");
                return 1;
            }
            break;
        default:
            //nothing to do
            printf("Wooop!\n");
    }
    
    fstat(in_fd,&statbuf);

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
            exit(1);
        case 0:
            getfile(mode);
            break;
        default:
            printf("child_pid: %d\n", pid);
            sendfile(mode);
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
    close(out_fd1);
    close(out_fd2);
    close(out_fd3);
    return 0;
}

void sendfile(int mode)
{
    lseek(in_fd,0,SEEK_SET);
    int len=statbuf.st_size;
    int bytes;

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
        char buffer[BUF_SIZE];
        while((bytes = read(in_fd,buffer,sizeof(buffer))) >0)
        {
            if(write(pipefd[1],buffer,bytes) != bytes)
            {
                perror("write pipe errno");
                exit(1);
            }
        }
        //printf("pid: [%d] out bytes=%d\n", pid, bytes);
    }

    else if(mode == 2)
    {
        //如果是lseek创建空洞，就要write一个空字节进去
        void *src = mmap(NULL, len, PROT_READ, MAP_SHARED, in_fd, 0);
        if(src==MAP_FAILED) {
            perror("mmap map src faild");
            return;
        }
        
        void *shm = shared_memory;

        int size=BUF_SIZE,total=0;
        while(total < len)
        {
            size = len - total > BUF_SIZE ? BUF_SIZE : len-total;
            memcpy(shm,src,size);
            shm += size;
            src += size;
            total += size;
            //printf("total_write=%d size=%d\n", total, size);
        }

        munmap(src, len);
    }

    else if(mode == 3)
    {
        while(len > 0)
        {
            if((bytes=splice(in_fd,NULL,pipefd[1],NULL,len,0x1))<0)
            {
                perror("splice in_fd faild");
                return;
            }
            else
                len -= bytes;
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
        char buffer[BUF_SIZE];
        while(len > 0)
        {
            if((bytes = read(pipefd[0],buffer,sizeof(buffer))) < 0)
            {
                perror("read pipefd error");
                exit(1);
            }
            if((write(out_fd1,buffer,bytes)) != bytes)
            {
                perror("write out_fd1 error");
                exit(1);
            }
            else
                len -= bytes;
        }
        //printf("pid: [%d] len=%d out_bytes=%d\n", pid, len, bytes);
    }
    
    else if(mode == 2)
    {
        if(ftruncate(out_fd2, len) < 0) {
            perror("ftruncate faild");
            return;
        }
        void *dst = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, out_fd2, 0);
        if(dst==MAP_FAILED) {
            perror("mmap map dst faild");
            return;
        }

        void *shm = shared_memory;
       
        int size=BUF_SIZE,total=0;
        while(total < len)
        {
            size = len - total > BUF_SIZE ? BUF_SIZE : len-total;
            memcpy(dst, shm, size);
            shm += size;
            dst += size;
            total += size;
            //printf("total_read=%d size=%d\n", total, size);
        }

        munmap(dst, len);
    }

    else if(mode == 3)
    {
        while(len > 0)
        {
            if((bytes=splice(pipefd[0],NULL,out_fd3,NULL,len,0x1))<0)
            {
                perror("splice out_fd3 faild");
                return;
            }
            else
                len -= bytes;
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

