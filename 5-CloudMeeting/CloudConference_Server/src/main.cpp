#include "prefix.h"

//usage: ./app [host] <port #> <#threads> <#processes>

int main(int argc, char **argv)
{
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