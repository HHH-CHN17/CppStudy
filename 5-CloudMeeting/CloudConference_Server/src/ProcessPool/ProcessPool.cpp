/*******************************************************************************
* @author:      LP
* @version:     1.0
* @date:        25-2-1
* @description:
*******************************************************************************/

#include "ProcessPool.h"

int processpool::sig_fd_ = -1;

/* 进程池的构造函数 */
processpool::processpool(int listenfd, int process_number, int thread_num_per_proc)
        : m_listenfd(listenfd), m_process_number(process_number)
        , ipc_fd_(-1), m_stop(false), sp_room_guard_(), sp_room_()
        , thread_pool_()
{
    assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));

    /* 创建process_number个子进程，并建立它们与父进程之间的管道 */
    //m_sub_process = new process[process_number];
    //assert(m_sub_process);

    // 房间初始化，由于房间模块是为进程模块服务的，所以要先初始化房间，我们把房间模块和进程模块放在一起理解比较好
    // 注意：房间模块可以理解成一个单例类，仅主进程持有，该模块通过成员 Process *pptr 数组管理着所有的子进程（也可以理解成所有房间）
    sp_room_guard_ = make_shared<RoomGuard>(process_number);
    assert(sp_room_guard_);

    for (int i = 0; i < process_number; ++i)
    {
        /* socketpair函数能够创建一对套接字进行进程间通信，每一个套接字既可以读也可以写，
         m_sub_process[i].m_pipefd是指每一个子进程与父进程通信的两根管道 */
        //int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
        //assert(0 == ret);

        int ipc_fd[2];
        // 实现父子进程全双工通信, 根据代码逻辑，父进程与每一个子进程之间都会创建一个sockfd数组进行通信
        Socketpair(AF_LOCAL, SOCK_STREAM, 0, ipc_fd);
        pid_t pid;

        if((pid = fork()) > 0)  // 父进程
        {
            thread_pool_.set_and_start(thread_num_per_proc);
            Close(ipc_fd[1]);                       //父进程使用sockfd[0]与子进程进行通信，
            RoomGuard::Process proc;
            proc.child_pid = pid;
            //proc.child_pipefd = ipc_fd[0];
            proc.child_status = 0;
            proc.total = 0;
            sp_room_guard_->umap_room_.insert(make_pair(ipc_fd[0], proc));
            ipc_fd_ = -1;
            sp_room_.reset();
            cout << "parent init: " << getpid() << "\n";
            continue;
        }
        else
        {
            thread_pool_.set_and_start(thread_num_per_proc);
            // 子进程
            Close(listenfd); // 子进程不建立对listenfd的监听
            Close(ipc_fd[0]);
            ipc_fd_ = ipc_fd[1];
            sp_room_guard_.reset();
            cout << "child init: " << getpid() << "\n";
            break;
        }
    }
}

/*统一事件源*/
void processpool::setup_sig_pipe()
{
    /*创建epoll事件监听表和信号管道*/
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGPIPE);

    /* 阻塞信号以使得它们不被默认的处理试方式处理 */

    int ret = sigprocmask(SIG_BLOCK, &mask, nullptr);
    assert(ret != -1);

    sig_fd_ = signalfd(-1, &mask, SFD_NONBLOCK);
    assert(sig_fd_ != -1);

    //setnonblocking(sig_fd_);
    addfd(m_epollfd, sig_fd_);
}

/*父进程在ipc_fd_值为-1，子进程在ipc_fd_值大于等于0，我们据此判断接下来要运行的是父进程代码还是子进程代码*/
void processpool::run()
{
    if (ipc_fd_ != -1)
    {
        run_child();
        return;
    }
    run_parent();
}

void processpool::child_init()
{
    for (int i = 0; i < SENDTHREADSIZE; i++)
    {
        thread_pool_.submit(&Room::msg_forward, sp_room_.get());
    }
}

void processpool::run_child()
{

    /* 设置统一事件源，创建两个进程间通信的管道，利用epoll监听管道的可读事件从而处理信号 */
    setup_sig_pipe();

    //err_msg("child: %d running\n", getpid());

    /* 子进程需要监听管道文件描述符pipefd，因为父进程将通过它来通知子进程accept新连接 */
    addfd(m_epollfd, ipc_fd_);

    child_init();

    epoll_event events[MAX_EVENT_NUMBER];
    /* number是就绪的文件描述符的个数 */
    int number = 0;
    //int ret = -1;

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

            if ((sockfd == ipc_fd_) && (events[i].events & EPOLLIN))
            {
                int client = 0;
                /*从父、子进程之间的管道读取数据，并将结果保存在变量client中。
                 * 如果读取成功，则表示有新客户连接到来*/
                //thread_pool_.submit(&Room::accept_from_parent, sp_room_.get(), sockfd, m_epollfd);
                sp_room_->accept_from_parent(sockfd, m_epollfd);
            }
                /*下面处理子进程接受到的信号*///???????????????????????????????????
            else if ((sockfd == sig_fd_) && (events[i].events & EPOLLIN))//???
            {
                struct signalfd_siginfo fdsi{};

                int sig;
                char signals[1024];
                //ret = recv(sig_fd_, signals, sizeof(signals), 0);
                //ssize_t ret = read(sockfd, &fdsi, sizeof(struct signalfd_siginfo));
                ssize_t ret = read_all(sockfd, &fdsi, sizeof(struct signalfd_siginfo));
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
                /*如果其他可读数据，那么必然是客户请求到来*/
            else if (events[i].events * EPOLLIN)
            {
                cout << "parse_and_forward_to_client" << endl;
                //thread_pool_.submit(&Room::parse_and_forward_to_client, sp_room_.get(), sockfd, ipc_fd_);
                sp_room_->parse_and_forward_to_client(sockfd, ipc_fd_);
            }
            else
            {
                continue;
            }
        }
    }
    close(ipc_fd_);
    //close(m_listenfd);
    /*我们将这句话注释掉，是为了提醒我们：应该由m_listenfd创建者来关闭这个文件描述符，
    即所谓的对象（比如一个文件描述符，或者另一段堆内存）由哪个函数创建，就应该由那个函数销毁*/
    close(m_epollfd);
}

void processpool::run_parent()
{

    setup_sig_pipe();

    //err_msg("parent: %d running\n", getpid());

    /*父进程不用epoll监听m_listenfd，而是使用多线程*/
    addfd(m_epollfd, m_listenfd);

    epoll_event events[MAX_EVENT_NUMBER];
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
                sp_room_guard_->accept_and_forward_to_child(sockfd);
            }
            else if ((sockfd == sig_fd_) && (events[i].events & EPOLLIN))
            {
                struct signalfd_siginfo fdsi{};

                int sig;
                char signals[1024];
                //ret = read_all(sig_fd_, signals, sizeof(signals));
                ret = recv(sig_fd_, signals, sizeof(signals), 0);
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
                                    /*如果进程池中pid子进程退出了，则主进程关闭相应的通信管道，
                                            并移除该房间信息*/
                                    std::lock_guard<std::mutex> lg{sp_room_guard_->room_mtx_};
                                    for (auto& ele : sp_room_guard_->umap_room_)
                                    {
                                        if (ele.second.child_pid == pid)
                                        {
                                            cout << "child " << pid << " join\n";
                                            close(ele.first);
                                            sp_room_guard_->umap_room_.erase(ele.first);
                                            break;
                                        }
                                    }
                                }
                                /*如果所有子进程都已经退出了，则父进程也退出*/
                                if (sp_room_guard_->umap_room_.empty())
                                    m_stop = true;
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                /*如果父进程接受到终止信号，那么就杀死所有子进程，并等待它们全部结束。
                                  当然，通知子进程结束更好的方法是向父、子进程之间的通信管道发送特殊数据*/
                                cout << "kill all the child now" << endl;
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
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
                /*如果其他可读数据，那么必然是子进程的消息*/
            else if (events[i].events * EPOLLIN)
            {
                sp_room_guard_->process_msg_from_child(sockfd);
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