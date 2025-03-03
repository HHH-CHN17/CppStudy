/*******************************************************************************
* @author:      LP
* @version:     1.0
* @date:        25-2-1
* @description: 单例进程池类
*******************************************************************************/

#ifndef CLOUDCONFERENCE_PROCESSPOOL_H
#define CLOUDCONFERENCE_PROCESSPOOL_H

#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <cstdio>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <cstdlib>
#include <csignal>
#include <sys/signalfd.h>
#include "Protocol.h"
#include "RoomBusiness.h"
#include "UserForward.h"
#include "SingletonBase.h"
#include "ThreadPool.h"

using namespace std;

class processpool : public Singleton_Lazy_Base<processpool>
{
    friend class Singleton_Lazy_Base<processpool>;
private:
    static void Destory(processpool* p_sgl) {
        delete p_sgl;
    }

    explicit processpool(int listenfd, int process_number = 2, int thread_num_per_proc = 5);
    ~processpool(){std::cout << "delete correctly" << std::endl;}
public:
    // 不想要默认构造但是设为delete后单例中的GetInstance()函数显然无法通过编译，可以通过在函数体中抛异常解决。
    processpool(){ throw std::bad_exception(); };
    processpool(const processpool&) = delete;
    processpool(processpool&&) = delete;
    processpool& operator=(const processpool&) = delete;

    // 启动线程池
    void run();

    /* 将文件描述符设置成非阻塞,调用非阻塞I/O跟阻塞I/O的差别为调用之后立即返回，
    * 返回后，CPU的时间片可以用来处理其他事务，此时性能是提升的。
    * 但是非阻塞I/O的问题是：由于完整的I/O没有完成，立即返回的并不是业务层期望的数据，
    * 而仅仅是当前调用的状态。为了获取完整的数据，应用程序需要重复调用I/O操作来确认是否完成。
    * 这种重复调用判断操作是否完成的技术叫做轮询。
    */
    static int setnonblocking(int fd)
    {
        // fcntl函数可以改变文件描述符的性质
        int old_option = fcntl(fd, F_GETFL);
        int new_option = old_option | O_NONBLOCK;
        fcntl(fd, F_SETFL, new_option);
        return old_option;
    }

    // 将文件描述符上的读事件注册到epollfd标识的epoll内核事件表上
    static void addfd(int epollfd, int fd)
    {
        epoll_event event{};
        event.data.fd = fd;
        // 设置边沿触发和读事件
        event.events = EPOLLIN | EPOLLET;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
        setnonblocking(fd);
    }

    // 从epollfd标识的epoll内核事件表中删除fd上所有的注册事件
    static void removedfd(int epollfd, int fd)
    {
        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
        close(fd);
    }

private:
    // 初始化epoll以及信号处理
    void init_sig_and_epoll();

    void run_parent();

    void run_child();

    void init_parent();

    void init_child();

    void sig_handler_in_parent(int sockfd);

    void sig_handler_in_child(int sockfd);

private:
    // 进程池允许的最大进程数量
    static constexpr int MAX_PROCESS_NUMBER = 16;
    // 每个子进程最多能处理的事件数
    static constexpr int USER_PER_PROCESS = 65536;
    // epoll能处理的事件数
    static constexpr int MAX_EVENT_NUMBER = 10000;
    // 用于处理信号的fd
    int sig_fd_;
    // 进程池中的进程总数
    int process_number_;
    // 每个进程都有一个epoll内核事件表，用m_epollfd标识
    int epollfd_;
    // 监听socket
    int listenfd_;
    // 子进程通过m_stop来决定是否停止运行
    int stop_;
    // 房间管理（主进程使用）
    std::shared_ptr<RoomGuard> sp_room_guard_;
    // 房间（子进程使用）
    std::shared_ptr<Room> sp_room_;
    // 子进程与父进程通信接口，父进程中该值为-1，也可以用于区分父子进程
    int ipc_fd_;
    // 线程池，每个进程都有一份
    ThreadPool<1000> thread_pool_;
    // 每个进程的线程数量
    int thread_qty_;
    // 进程标志：false表示子进程，true表示父进程
    bool is_parent_;
};





#endif //CLOUDCONFERENCE_PROCESSPOOL_H
