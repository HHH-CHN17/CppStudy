/*******************************************************************************
* @author:      LP
* @version:     1.0
* @date:        25-1-31
* @description:
*******************************************************************************/

#ifndef CLOUDCONFERENCE_TEST1_H
#define CLOUDCONFERENCE_TEST1_H

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>

using namespace std;

/* 描述一个子进程的类，m_pid是目标子进程的PID，m_pipefd是父进程和子进程通信用的管道 */
class process
{
public:
    process() : m_pid(-1) {}
public:
    pid_t m_pid;
    int m_pipefd[2];
};

/* 进程池类，将它定义成模板类是为了代码复用，其模板参数是处理逻辑任务的类 */
template <typename T>
class processpool
{
private:
    /* 将构造函数定义为私有的，因此我们只能通过后面的create静态函数来创建processpool实例 */
    processpool(int listenfd, int process_number = 8);
public:
    /* 单例模式，保证程序最多创建一个processpool实例，这是程序正确处理信号的必要条件 */
    static processpool<T>* create(int listenfd, int process_number = 8)
    {
        if (!m_instance)
        {
            m_instance = new processpool<T>(listenfd, process_number);
        }
        return m_instance;
    }
    ~processpool()
    {
        delete []m_sub_process;
    }
    /* 启动线程池 */
    void run();

private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    /* 进程池允许的最大进程数量 */
    static const int MAX_PROCESS_NUMBER = 16;
    /* 每个子进程最多能处理的事件数 */
    static const int USER_PER_PROCESS = 65536;
    /* epoll能处理的事件数 */
    static const int MAX_EVENT_NUMBER = 10000;
    /* 进程池中的进程总数 */
    int m_process_number;
    /* 子进程在池中的序号，从0开始 */
    int m_idx;
    /* 每个进程都有一个epoll内核事件表，用m_epollfd标识 */
    int m_epollfd;
    /* 监听socket */
    int m_listenfd;
    /* 子进程通过m_stop来决定是否停止运行*/
    int m_stop;
    /* 保存所有子进程的描述信息 */
    process* m_sub_process;
    /* 进程池静态实例 */
    static processpool<T>* m_instance;
};

/* 用于处理信号的管道，以实现统一事件源，后面称之为信号管道 */
static int sig_pipefd[2];

/* 将文件描述符设置成非阻塞,调用非阻塞I/O跟阻塞I/O的差别为调用之后立即返回，
 * 返回后，CPU的时间片可以用来处理其他事务，此时性能是提升的。
 * 但是非阻塞I/O的问题是：由于完整的I/O没有完成，立即返回的并不是业务层期望的数据，
 * 而仅仅是当前调用的状态。为了获取完整的数据，应用程序需要重复调用I/O操作来确认是否完成。
 * 这种重复调用判断操作是否完成的技术叫做轮询。 */
static int setnonblocking(int fd)
{
    /* fcntl函数可以改变文件描述符的性质 */
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* 将文件描述符上的读事件注册到epollfd标识的epoll内核事件表上 */
static void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    /* 设置边沿触发和读事件 */
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* 从epollfd标识的epoll内核事件表中删除fd上所有的注册事件 */
static void removedfd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/*信号处理函数*/
static void sig_handler(int sig)
{
    /* 保存原来的errno，在函数最后恢复，以保证函数的可重入性，errno是记录系统的最后一次错误代码 */
    int save_errno = errno;
    int msg = sig;
    /* 将信号写入管道，以通知主循环 */
    send(sig_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

/* 设置信号的处理函数 */
static void addsig(int sig, void(handler)(int), bool restart = true)
{
    /* sigaction函数的功能是检查或修改与指定信号相关联的处理动作 */
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    /* sa_handler此参数和signal()的参数handler相同，代表新的信号处理函数 */
    sa.sa_handler = handler;
    if (restart)
    {
        /* SA_RESTART重新调用被该系统终止的系统调用 */
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

template<typename T>
processpool<T>* processpool<T>::m_instance = NULL;

/* 进程池的构造函数 */
template<typename T>
processpool<T>::processpool(int listenfd, int process_number)
        : m_listenfd(listenfd), m_process_number(process_number), m_idx(-1), m_stop(false)
{
    assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));

    /* 创建process_number个子进程，并建立它们与父进程之间的管道 */
    m_sub_process = new process[process_number];
    assert(m_sub_process);
    for (int i = 0; i < process_number; ++i)
    {
        /* socketpair函数能够创建一对套接字进行进程间通信，每一个套接字既可以读也可以写，
         m_sub_process[i].m_pipefd是指每一个子进程与父进程通信的两根管道 */
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
        assert(0 == ret);
        /* 通过fork函数创建线程池中的子进程 */
        m_sub_process[i].m_pid = fork();
        assert(m_sub_process[i].m_pid >= 0);
        if (m_sub_process[i].m_pid > 0)
        {
            /* 父进程只管从管道里面写，所以关闭父进程的读文件描述符 */
            close(m_sub_process[i].m_pipefd[1]);
            continue;
        }
        else
        {
            /* 子进程只管从管道里面读，所以关闭子进程的写文件描述符 */
            close(m_sub_process[i].m_pipefd[0]);
            m_idx = i;
            break;
        }
    }
}

/*统一事件源*/
template<typename T>
void processpool<T>::setup_sig_pipe()
{
    /*创建epoll事件监听表和信号管道*/
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    /* 使用socketpair函数创建管道，注册sig_pipefd[0]上的可读事件 */
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);
    setnonblocking(sig_pipefd[1]);
    addfd(m_epollfd, sig_pipefd[0]);

    /*设置信号处理函数*/
    addsig(SIGCHLD, sig_handler);   /* 子进程状态发生变化（退出或者暂停 */
    addsig(SIGTERM, sig_handler);   /* 终止进程，kill命令默认发射的信号就是SIGTERM */
    addsig(SIGINT, sig_handler);    /* 键盘输入以中断进程 */
    addsig(SIGPIPE, SIG_IGN);       /* 往读端被关闭的管道或者socket连接中写数据 */
}

/*父进程在m_idx值为-1，子进程在m_idx值大于等于0，我们据此判断接下来要运行的是父进程代码还是子进程代码*/
template<typename T>
void processpool<T>::run()
{
    if (m_idx != -1)
    {
        run_child();
        return;
    }
    run_parent();
}

template<typename T>
void processpool<T>::run_child()
{
    /* 设置统一事件源，创建两个进程间通信的管道，利用epoll监听管道的可读事件从而处理信号 */
    setup_sig_pipe();

    /* 每个子进程都要通过其在进程池中的序号值m_idx找到与父进程通信的管道 */
    int pipefd = m_sub_process[m_idx].m_pipefd[1];
    /* 子进程需要监听管道文件描述符pipefd，因为父进程将通过它来通知子进程accept新连接 */
    addfd(m_epollfd, pipefd);

    epoll_event events[MAX_EVENT_NUMBER];
    T* users = new T[USER_PER_PROCESS];
    assert(users);
    /* number是就绪的文件描述符的个数 */
    int number = 0;
    int ret = -1;

    while (!m_stop)
    {
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            cout << "epoll failure\n" << endl;
            break;
        }

        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            if ((sockfd == pipefd) && (events[i].events & EPOLLIN))
            {
                int client = 0;
                /*从父、子进程之间的管道读取数据，并将结果保存在变量client中。
                 * 如果读取成功，则表示有新客户连接到来*/
                ret = recv(sockfd, (char*)&client, sizeof(client), 0);
                if (((ret < 0) && (errno != EAGAIN)) || ret == 0)
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addr_length = sizeof(client_addr_length);
                    int connfd = accept(m_listenfd, (struct sockaddr*)&client_address,
                                        &client_addr_length);
                    if (connfd < 0)
                    {
                        cout << "errno is：" << errno << endl;
                        continue;
                    }
                    /* 将客户端文件描述符上的可读事件加入内核事件表 */
                    addfd(m_epollfd, connfd);
                    /*模板类T必须实现init方法，以初始化一个客户连接，我们直接使用connfd来
                      索引逻辑处理对象（T类型的对象），以提高程序效率*/
                    users[connfd].init(m_epollfd, connfd, client_address);
                }
            }
                /*下面处理子进程接受到的信号*/
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if (ret <= 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
                                {
                                    continue;
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                m_stop = true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
                /*如果其他可读数据，那么必然是客户请求到来，调用逻辑处理对象的process方法来处理之*/
            else if (events[i].events * EPOLLIN)
            {
                users[sockfd].process();
            }
            else
            {
                continue;
            }
        }
    }
    delete []users;
    users = NULL;
    close(pipefd);
    //close(m_listenfd);
    /*我们将这句话注释掉，是为了提醒我们：应该由m_listenfd创建者来关闭这个文件描述符，
    即所谓的对象（比如一个文件描述符，或者另一段堆内存）由哪个函数创建，就应该由那个函数销毁*/
    close(m_epollfd);
}

template<typename T>
void processpool<T>::run_parent()
{
    setup_sig_pipe();

    /*父进程监听m_listenfd*/
    addfd(m_epollfd, m_listenfd);
    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while (!m_stop)
    {
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            cout << "epoll failure" << endl;
            break;
        }
        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == m_listenfd)
            {
                /*如果有新连接到来，就采用Round Robin方式将其分配给一个子进程处理*/
                int i = sub_process_counter;
                do
                {
                    if (m_sub_process[i].m_pid != -1)
                    {
                        break;
                    }
                    i = (i + 1) % m_process_number;
                }
                while (i != sub_process_counter);

                if (m_sub_process[i].m_pid == -1)
                {
                    m_stop = true;
                    break;
                }
                sub_process_counter = (i + 1) % m_process_number;
                send(m_sub_process[i].m_pipefd[0], (char*)&new_conn, sizeof(new_conn), 0);
                cout << "send request to child " << i << endl;
            }
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if (ret < 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
                                {
                                    for (int i = 0; i < m_process_number; ++i)
                                    {
                                        /*如果进程池中第i个子进程退出了，则主进程关闭相应的通信管道，
                                          并设置相应的m_pid为-1，以标记该子进程已经退出*/
                                        if (m_sub_process[i].m_pid == pid)
                                        {
                                            cout << "child " << i << "join" << endl;
                                            close(m_sub_process[i].m_pipefd[0]);
                                            m_sub_process[i].m_pid = -1;
                                        }
                                    }
                                }
                                /*如果所有子进程都已经退出了，则父进程也退出*/
                                m_stop = true;
                                for (int i = 0; i < m_process_number; ++i)
                                {
                                    if (m_sub_process[i].m_pid != -1)
                                    {
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                /*如果父进程接受到终止信号，那么就杀死所有子进程，并等待它们全部结束。
                                  当然，通知子进程结束更好的方法是向父、子进程之间的通信管道发送特殊数据*/
                                cout << "kill all the child now" << endl;
                                for (int i = 0; i < m_process_number; ++i)
                                {
                                    int pid = m_sub_process[i].m_pid;
                                    if (pid != -1)
                                    {
                                        kill(pid, SIGTERM);
                                    }
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                continue;
            }
        }
    }
    //close(m_listenfd); /*由创建者关闭这个文件描述符*/
    close(m_epollfd);
}

#endif //CLOUDCONFERENCE_TEST1_H
