# 内存拷贝memcpy()与vmsplice()性能对比

[@招牌疯子](http://weibo.com/buaazp)

## 综述

在上一篇文章[《进程间大数据拷贝方法调研》](http://blog.buaa.us/?p=186)中介绍和对比了三种A进程读取文件然后拷贝给B进程的方法，测试结果显示在涉及到内存与磁盘间的数据传输时，splice方法由于避免了内核缓冲区与用户缓冲区之间的多次数据拷贝，表现最好。但是由于这种对比限定在包含I/O读写，且进程不能对数据进行修改的特殊情景中，毕竟在实际情况下不太常见，理论意义大于实际意义。

那本文要探讨的情景，在实际编程过程中就十分常见了：

**A进程的内存中有一大块数据，要传递给B进程。**

为了解决这种情景下的数据传输问题，本文将要对比的两种方法为：

1. 申请共享内存，A进程将数据memcpy进去，B进程再memcpy出来。
2. 申请一对管道，A进程将数据vmsplice到管道写端，B进程从读端再vmsplice出来放入自己的用户空间。

实际应用中我们大多使用第一种方法，因为感觉上memcpy已经足够快了，那么它们到底谁更快呢，我们还是要用测试数据来说话。

## 测试方法

### memcpy方法

对于memcpy方法，统计的是从A进程用户空间拷贝数据到共享内存所消耗的时间和从共享内存拷贝数据到B进程用户空间的时间。

> 可能有同学会问，为啥不直接将变量定义到共享内存中，这样A修改了之后B直接使用即可，无需拷贝。
> 
> 这是因为我们期望的状态是A进程有一份自己的数据，B进程也有一份自己的数据，它们是互不干扰的，在大部分时间里是可以保持数据不一致这个状态的，只有需要的时候才进行拷贝。

代码如下：

*sendfile()*

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

usrmem是用户空间中的内存，在sendfile之前已经灌入了数据。并且在A进程向共享内存拷贝过程中上锁，以阻塞B进程对共享内存的读操作，结束之后解锁。

*getfile()*

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
    
读数据的时候也上锁，防止过程中被写入数据造成污染，结束之后解锁。

### vmsplice方法

vmsplice是splice函数族中的一个，它用于映射用户空间到内核空间（载体是管道），使得用户进程可以直接操作内核缓冲区的数据。

splice函数族包括：
 
> long splice(int fd_in, off_t *off_in,int fd_out, off_t *off_out,size_t len,unsignedint flags);
> 
> long tee(int fd_in,int fd_out,size_t len,unsignedint flags);
> 
> long vmsplice(int fd,conststruct iovec *iov, unsignedlong nr_segs,unsignedint flags);
> 
> ssize_t sendfile(int out_fd,int in_fd, off_t *offset,size_tcount);

可以看到这一系列函数把操作对象为用户空间、内核空间、文件流fd的映射操作全覆盖了，具体各个函数的用法可以查看man文档。

*sendfile()*

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

vmsplice想要操作用户空间依赖于struct iovec对象，它有两个成员，iov_base是指向用户空间的指针，iov_len是用户空间的大小，由于管道有大小限制（Linux下默认4096），因此在做vmsplice的时候需要不断更新struct iovec的内容。

*getfile()*

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

getfile的时候只需要把第一个参数设为管道的读端，因为vmsplice实际上是一个映射过程，与拷贝不同，它不关心两个参数谁是source谁是destination。

### 对比方法

对比的是上述四个过程所消耗的CPU时间，为了检验数据拷贝是否完成，在拷贝开始前和结束后对A、B进程的用户空间计算MD5值。

测试对象原始数据是一个1G大小的文件，每种方法各测试20次，统计sendfile过程和getfile过程的时间，两者相加是整个数据拷贝的时间，计算平均值进行对比。

> 为什么不实用K-BEST的方法？
> 
> 因为测试数据出来之后发现所有数据都在合理范围之内，但最小值明显小于平均值，从实际意义出发，不具备代表性。

## 测试结果

经过统计的计算结果如下：

![性能对比测试结果](http://ww4.sinaimg.cn/large/599be90djw1ecfualjxwcj20wg0khagc.jpg)

在sendfile过程中，memcpy比vmsplice要快；而getfile过程中vmsplice却比memcpy快。

总时间上memcpy小幅优于vmsplice，大约仅有2.6%的领先。

### 结论

数据上看memcpy好于vmsplice，但前提是内存够用，在我们的测试中即使是1G的数据依然直接全部放置于共享内存中，这在实际编程中是不允许的，肯定要分段进行拷贝，必然会增加锁消耗；而vmsplice之所以慢就是受限于管道的大小，数据实际上是分段传递的，因此在内存占用上，vmsplice会比memcpy少1/3。

因此我的结论是，**在纯内存拷贝这个业务场景下，memcpy与vmsplice基本等效**，并没有谁可以明显由于对手。

splice函数族更加适用的场景应该是涉及到磁盘数据拷贝的情景中，如果你是犹豫需不需要用vmsplice方法替换已有的memcpy代码的话，我的建议是不需要，memcpy确实已经足够快了。

> 如需转载，请注明出处，谢谢！

### 测试代码和原始数据

memcpy vs vmsplice [测试代码](https://github.com/buaazp/work/blob/master/memcpy_test.c)

测试结果原始数据，ret_mem_1和ret_mem_2文件 [github地址](https://github.com/buaazp/work)