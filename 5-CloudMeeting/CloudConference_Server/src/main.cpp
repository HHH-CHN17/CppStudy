#include "prefix.h"
#include "Protocol.h"

//usage: ./app [host] <port #> <#threads> <#processes>

int main(int argc, char **argv)
{
    //test();

    // SendQueue send_queue;
    //
    // auto func1 = [&]()
    // {
    //     MSG msg;
    //     msg.msgType = CREATE_MEETING_RESPONSE;
    //     msg.targetfd = 8;
    //     uint32_t roomNo = getpid();htonl(getpid());
    //     msg.ptr = new char(sizeof(uint32_t));
    //     memcpy(msg.ptr, &roomNo, sizeof(uint32_t));
    //     msg.len = sizeof(uint32_t);
    //     send_queue.push_msg(msg);
    //     cout << roomNo << endl;
    // };
    //
    // auto func2 = [&]()
    // {
    //     while (send_queue.isempty())
    //     {
    //         this_thread::yield();
    //     }
    //
    //     MSG msg1 = send_queue.pop_msg();
    //     int len = 0;
    //     if (msg1.len == -1)
    //         cout << "error" << endl;
    //     //printf("get msg: %d\n",*msg.ptr);
    //
    //     uint32_t ip;
    //     memcpy(&ip, msg1.ptr, msg1.len);
    //     cout << ip << endl;
    // };
    // thread t2(func2);
    // thread t1(func1);
    // t1.join();
    // t2.join();
    //
    //
    // return 0;

    int listenfd;
    socklen_t addrlen;

    // 网络初始化，通过调用Tcp_listen()进入等待客户端连接状态（阻塞）
    if(argc == 4)
    {
        listenfd = Tcp_listen(argv[1], nullptr, &addrlen);
    }
    else if(argc == 5)
    {
        listenfd = Tcp_listen(argv[1], argv[2], &addrlen);
    }
    else
    {
        err_quit("usage: ./app [host] <port #> <#threads> <#processes>");
    }

    int nthreads = atoi(argv[argc - 2]);    //处理客户端连接的线程数
    int nprocesses = atoi(argv[argc-1]);        //开启的房间数
    cout << "nprocesses: " << nprocesses << "\n";
    cout << "nthreads: " << nthreads << "\n";

    processpool::InitInstance(listenfd, nprocesses, nthreads);
    processpool::GetInstance().run();

    cout << "execution end" << endl;
    return 0;
}