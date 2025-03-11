/*******************************************************************************
* @author:      LP
* @version:     1.0
* @date:        25-2-1
* @description:
*******************************************************************************/

#include "ProcessPool.h"

/* 进程池的构造函数 */
processpool::processpool(int listenfd, int process_number, int thread_num_per_proc)
        : sig_fd_(-1), process_number_(process_number), epollfd_(-1), listenfd_(listenfd)
        , stop_(false), ipc_fd_(-1)
        , thread_pool_(), is_parent_()
{
    assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));

    for (int i = 0; i < process_number; ++i)
    {

        int ipc_fd[2];
        // 父子进程全双工通信
        Socketpair(AF_LOCAL, SOCK_STREAM, 0, ipc_fd);
        setnonblocking(ipc_fd[0]);
        setnonblocking(ipc_fd[1]);

        pid_t pid = fork();
        if(pid > 0)  // 父进程
        {
            Close(ipc_fd[1]);                       //父进程使用sockfd[0]与子进程进行通信

            thread_qty_ = thread_num_per_proc;
            //thread_pool_.set_and_start(thread_num_per_proc);

            sp_room_guard_ = make_shared<RoomGuard>(process_number);
            assert(sp_room_guard_.get());
            RoomGuard::Process proc;
            proc.child_pid = pid;
            proc.child_status = 0;
            proc.total = 0;
            sp_room_guard_->umap_room_.insert(make_pair(ipc_fd[0], proc));

            epollfd_ = -1;
            is_parent_ = true;
            cout << "parent: " << getpid() << " ipc_fd: " << ipc_fd[0]
                << " | child: " << pid << " ipc_fd: " << ipc_fd[1]<< endl;
        }
        else                    // 子进程
        {
            Close(listenfd);
            Close(ipc_fd[0]);

            thread_qty_ = thread_num_per_proc;
            //thread_pool_.set_and_start(thread_num_per_proc * 2 + SENDTHREADSIZE);
            sp_room_ = make_shared<Room>();
            assert(sp_room_.get());

            ipc_fd_ = ipc_fd[1];
            epollfd_ = -1;
            is_parent_ = false;
            break;
        }
    }
}

void processpool::init_sig_and_epoll()
{
    // 在初始化阶段设置 SIGCHLD 的行为
    struct sigaction sa{};
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;  // 子进程停止/恢复时不向父进程发送信号 | 系统调用被信号中断不返回错误，而是重启系统调用
    sigemptyset(&sa.sa_mask);

    // 注意：这里不需要设置 sa_handler，因为我们用 signalfd 处理信号
    if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // 创建epoll
    epollfd_ = epoll_create(5);
    assert(epollfd_ != -1);

    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGPIPE);

    // 阻塞信号以使得它们不被默认的处理试方式处理
    int ret = sigprocmask(SIG_BLOCK, &mask, nullptr);
    assert(ret != -1);

    sig_fd_ = signalfd(-1, &mask, SFD_NONBLOCK);
    assert(sig_fd_ != -1);

    addfd(epollfd_, sig_fd_);
}

void processpool::run()
{
    if (!is_parent_)
    {
        run_child();
        return;
    }
    run_parent();
}

void processpool::init_child()
{
    thread_pool_.set_and_start(thread_qty_ * 2 + SENDTHREADSIZE);// 线程池需要在进程正式执行的时候才启动：当设置了两个子进程的时候，执行顺序是 创建主进程==>复制主进程来创建子进程==>再复制主进程来创建子进程，在创建第二个子进程的时候，主进程的线程池中的已经启动了，所以此时第二个子进程的线程池无法运行

    stop_ = false;
    for (int i = 0; i < SENDTHREADSIZE; i++)
    {
        thread_pool_.submit(&Room::msg_forward, sp_room_.get());
    }
    addfd(epollfd_, ipc_fd_);
}

void processpool::init_parent()
{
    thread_pool_.set_and_start(thread_qty_);

    stop_ = false;
    for (auto& ele : sp_room_guard_->umap_room_)
    {
        addfd(epollfd_, ele.first);
    }
    addfd(epollfd_, listenfd_);
}

void processpool::run_child()
{
    init_sig_and_epoll();
    init_child();

    epoll_event events[MAX_EVENT_NUMBER];
    // count是就绪的文件描述符的个数
    int count = 0;

    while (!stop_)
    {
        count = epoll_wait(epollfd_, events, MAX_EVENT_NUMBER, -1);
        if ((count < 0) && (errno != EINTR))
        {
            cout << "epoll failure\n" << endl;
            break;
        }

        for (int i = 0; i < count; ++i)
        {
            int sockfd = events[i].data.fd;
            if ((sockfd == ipc_fd_) && (events[i].events & EPOLLIN))        // 收到父进程消息
            {
                // thread_pool_.submit(&Room::accept_from_parent,
                //     sp_room_.get(), sockfd, epollfd_);
                sp_room_->accept_from_parent(sockfd, epollfd_);
            }
            else if ((sockfd == sig_fd_) && (events[i].events & EPOLLIN))   // 收到信号
            {
                printf("%d recv signal", getpid());
                // 信号处理应该在主进程进行：1. 使用线程池时无法及时处理收到的信号。2. 每个线程都有自己的信号队列，很麻烦
                sig_handler_in_child(sockfd);
            }
            else if (events[i].events * EPOLLIN)                            // 收到客户端消息
            {
                cout << "parse_and_forward_to_client" << endl;
                // thread_pool_.submit(&Room::parse_and_forward_to_client,
                //     sp_room_.get(), sockfd, ipc_fd_);
                sp_room_->parse_and_forward_to_client(sockfd, ipc_fd_);
            }
            else
            {
                printf("%d error sockfd: %d",getpid(), sockfd);
                continue;
            }
        }
    }
    close(ipc_fd_);
    close(epollfd_);
}

void processpool::run_parent()
{
    init_sig_and_epoll();
    init_parent();

    epoll_event events[MAX_EVENT_NUMBER];
    int number = 0;

    while (!stop_)
    {
        number = epoll_wait(epollfd_, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            cout << "epoll failure" << endl;
            break;
        }
        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd_)                                                // 收到新连接
            {
                // thread_pool_.submit(&RoomGuard::accept_client,
                //     sp_room_guard_.get(),epollfd_, sockfd);
                sp_room_guard_->accept_client(epollfd_, sockfd);
            }
            else if ((sockfd == sig_fd_) && (events[i].events & EPOLLIN))           // 收到信号
            {
                // 信号处理应该在主进程进行：1. 使用线程池时无法及时处理收到的信号。2. 每个线程都有自己的信号队列，很麻烦
                sig_handler_in_parent(sockfd);
            }
            else if ((sp_room_guard_->umap_room_.find(sockfd) != sp_room_guard_->umap_room_.end()))       // 收到子进程消息
            {
                // thread_pool_.submit(&RoomGuard::process_msg_from_child,
                //     sp_room_guard_.get(), sockfd);
                sp_room_guard_->process_msg_from_child(sockfd);
            }
            else if (events[i].events * EPOLLIN)                                    // 收到客户端消息（转发给对应子进程）
            {
                // thread_pool_.submit(&RoomGuard::forward_to_child,
                //     sp_room_guard_.get(), sockfd);

                //线程池需要加信号控制！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！
                //printf("%d recv from client: %d\n",getpid(), sockfd);
                sp_room_guard_->forward_to_child(sockfd);
            }
            else
            {
                printf("%d error sockfd: %d",getpid(), sockfd);
                continue;
            }
        }
    }
    //close(m_listenfd); /*由创建者关闭这个文件描述符*/
    close(epollfd_);
}

void processpool::sig_handler_in_parent(int sockfd)
{
    ssize_t bytes = -1;
    struct signalfd_siginfo fdsi[5]{};
    bytes = read_all(sockfd, fdsi, sizeof(signalfd_siginfo) * 5);

    if (bytes > 0)
    {
        int count = bytes / sizeof(struct signalfd_siginfo);
        for (int i = 0; i < count; ++i)
        {
            switch (fdsi[i].ssi_signo)
            {
                case SIGCHLD:
                {
                    printf("%d recv SIGCHLD ", getpid());
                    if( !(fdsi[i].ssi_code == CLD_EXITED ||   // 正常退出
                        fdsi[i].ssi_code == CLD_KILLED ||   // 被信号杀死
                        fdsi[i].ssi_code == CLD_DUMPED))
                        err_msg("%d: recv SIGCHLD unexpectedly\n", getpid());
                    pid_t pid;
                    int stat;
                    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
                    {
                        /*如果进程池中pid子进程退出了，则主进程关闭相应的通信管道，
                                并移除该房间信息*/
                        std::lock_guard<std::mutex> lg{sp_room_guard_->room_mtx_};
                        for (auto& ele : sp_room_guard_->umap_room_)
                        {
                            if (ele.second.child_pid == pid)
                            {
                                printf("child %d join\n", pid);
                                close(ele.first);
                                sp_room_guard_->umap_room_.erase(ele.first);
                                break;
                            }
                        }
                    }
                    /*如果所有子进程都已经退出了，则父进程也退出*/
                    if (sp_room_guard_->umap_room_.empty())
                        stop_ = true;
                    //break;
                }
                case SIGTERM:
                case SIGINT:
                {
                    /*如果父进程接受到终止信号，那么就杀死所有子进程，并等待它们全部结束。
                      当然，通知子进程结束更好的方法是向父、子进程之间的通信管道发送特殊数据*/
                    cout << "kill all the child now\n";
                    std::lock_guard<std::mutex> lg{sp_room_guard_->room_mtx_};
                    for (auto& ele : sp_room_guard_->umap_room_)
                    {
                        int pid = ele.second.child_pid;
                        if (pid != -1)
                        {
                            kill(pid, SIGTERM);
                        }
                    }
                    break;
                }
                case SIGPIPE:
                {
                    printf("%d: sigpipe!\n", getpid());
                }
                default:
                {
                    break;
                }
            }
        }
    }
}

void processpool::sig_handler_in_child(int sockfd)
{
    struct signalfd_siginfo fdsi[5]{};
    ssize_t bytes = read_all(sockfd, fdsi, sizeof(struct signalfd_siginfo) * 5);
    if (bytes > 0)
    {
        int count = bytes / sizeof(struct signalfd_siginfo);
        for (int i = 0; i < count; ++i)
        {
            switch (fdsi[i].ssi_signo )
            {
            case SIGCHLD:
                {
                    printf("%d recv SIGCHLD ", getpid());
                    if( !(fdsi[i].ssi_code == CLD_EXITED ||   // 正常退出
                        fdsi[i].ssi_code == CLD_KILLED ||   // 被信号杀死
                        fdsi[i].ssi_code == CLD_DUMPED))
                        err_msg("%d: recv SIGCHLD unexpectedly\n", getpid());
                    err_msg("%d: recv SIGCHLD\n", getpid());
                    //cout << getpid() << ": recv SIGCHLD\n";
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
                    printf("%d terminate!", getpid());
                    stop_ = true;
                    break;
                }
            case SIGPIPE:
                {
                    printf("%d: sigpipe!\n", getpid());
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