#include "NetWork.h"

uint32_t getpeerip(int fd)
{
    sockaddr_in peeraddr{};
    socklen_t addrlen;
    if(getpeername(fd, (sockaddr *)&peeraddr, &addrlen) < 0)
    {
        err_msg("getpeername error");
        return -1;
    }
    return peeraddr.sin_addr.s_addr;
}

ssize_t write_all(int fd, const void * buf, size_t n)
{
    ssize_t lefttowrite = n, haswrite = 0;
    char *ptr = (char *)buf;
    while(lefttowrite > 0)
    {
        if((haswrite = write(fd, ptr, lefttowrite)) < 0)
        {
            if(haswrite < 0 && errno == EINTR)
            {
                haswrite = 0;
            }
            else
            {
                return -1; //error
            }
        }
        lefttowrite -= haswrite;
        ptr += haswrite;
    }
    return n;
}

const char *Sock_ntop(char * str, int size ,const sockaddr * sa, socklen_t salen)
{
    switch (sa->sa_family)
    {
        case AF_INET:
        {
            struct sockaddr_in *sin = (struct sockaddr_in *) sa;
            if(inet_ntop(AF_INET, &sin->sin_addr, str, size) == NULL)
            {
                err_msg("inet_ntop error");
                return NULL;
            }
            if(ntohs(sin->sin_port) > 0)
            {
                snprintf(str + strlen(str), size  -  strlen(str), ":%d", ntohs(sin->sin_port));
            }
            return str;
        }
        case AF_INET6:
        {
            struct sockaddr_in6 *sin = (struct sockaddr_in6 *) sa;
            if(inet_ntop(AF_INET6, &sin->sin6_addr, str, size) == NULL)
            {
                err_msg("inet_ntop error");
                return NULL;
            }
            if(ntohs(sin->sin6_port) > 0)
            {
                snprintf(str + strlen(str), size -  strlen(str), ":%d", ntohs(sin->sin6_port));
            }
            return str;
        }
        default:
            return "server error";
    }
    return NULL;
}


void Setsockopt(int fd, int level, int optname, const void * optval, socklen_t optlen)
{
    if(setsockopt(fd, level, optname, optval, optlen) < 0)
    {
        err_msg("setsockopt error");
    }
}

void Close(int fd)
{
    if(close(fd) < 0)
    {
        err_msg("Close error");
    }
}

void Listen(int fd, int backlog)
{
    if(listen(fd, backlog) < 0)
    {
        err_quit("listen error");
    }
}

int Tcp_listen(const char * host, const char * service, socklen_t * addrlen)
{
    int listenfd, n;

    /*
        struct sockaddr {
            sa_family_t     sin_family;     //地址族
            char            sa_data[14];    //14字节，包含套接字中的目标地址和端口信息
        };
        struct sockaddr_in{
            sa_family_t     sin_family;     //地址族
            uint16_t        sin_port;       //16位TCP/UDP端口号，必须为网络字节序
            struct in_addr  sin_addr;       //32位IP地址，必须为网络字节序
            char            sin_zero[8];    //不使用
        };//该结构体和上面那个都是占16字节，两者可以互相转化
        struct in_addr {
            in_addr_t s_addr;
        };

        struct addrinfo
        {
            int ai_flags;       // 自定义标志位，用于控制getaddrinfo()函数的行为，可以取以下值：
                                    - AI_PASSIVE: 表示要解析服务器地址。如果设置了此标志，则host参数可以为NULL，表示将返回所有可用的本地地址。
                                    - I_CANONNAME: 表示要返回规范名称。如果设置了此标志，则ai_canonname成员将指向规范名称。
                                    - AI_NUMERICHOST: 表示host参数必须是一个数字地址字符串。如果设置了此标志，则ai_canonname成员将指向一个空字符串。
                                    - AI_V4MAPPED: 表示如果ai_family设置为AF_UNSPEC，则返回的地址列表中将包含 IPv4 地址和 IPv6 地址。
                                    - AI_ALL: 表示返回所有可用的地址，包括 IPv4 地址和 IPv6 地址。
            int ai_family;      // 协议族信息，如PF_INET和PF_INET6
            int ai_socktype;    // 套接字类型，如SOCK_STREAM和SOCK_DGRAM
            int ai_protocol;    // 协议名称，如IPPROTO_TCP和IPPROTO_UDP，一般来说，前两个参数已经可以决定所采取的协议，该参数置0也可以
            socklen_t ai_addrlen;       // IP地址长度
            struct sockaddr *ai_addr;   // IP地址
            char *ai_canonname;         // 提供主机名或服务名的规范名称，即主机名或服务名的完全限定域名，可用于将主机名或服务名转换为 IP 地址
            struct addrinfo *ai_next;   // 链表的next指针
        };
    */
    struct addrinfo hints, *res, *ressave;
    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE; //设置了AI_PASSIVE标志，但没有指定主机名，那么return ipv6和ipv4通配地址
    hints.ai_family = AF_UNSPEC; //返回的是适用于指定主机名和服务名且适合任意协议族的地址
    hints.ai_socktype = SOCK_STREAM;

    char addr[MAXSOCKADDR];

    /*
        getaddrinfo函数根据给定的主机名和服务名，返回一个struct addrinfo结构链表，每个struct addrinfo结构都包含一个互联网地址。getaddrinfo函数将gethostbyname和getservbyname函数提供的功能组合到一个接口中，但与后一个函数不同，getaddrinfo是可重入的，可支持IPv4、IPv6。
        可以导致返回多个addrinfo结构的情形有以下2个：
            1. 如果与hostname参数关联的地址有多个，那么适用于所请求地址簇的每个地址都返回一个对应的结构。
            2. 如果service参数指定的服务支持多个套接口类型，那么每个套接口类型都可能返回一个对应的结构，具体取决于hints结构的ai_socktype成员。
        我们必须先分配一个hints结构，把它清零后填写需要的字段，再调用getaddrinfo然后遍历一个链表逐个尝试每个返回地址。
        getaddrinfo解决了把主机名和服务名转换成套接口地址结构的问题。

        int getaddrinfo( const char *hostname, const char *service, const struct addrinfo *hints, struct addrinfo **result );
        ->成功返回0，res存放返回addrinfo结构链表的指针；失败返回非0
        nodename:节点名可以是主机名，也可以是IP地址。（IPV4的10进点分，或是IPV6的16进制）
        servname:包含十进制数的端口号或服务名如（ftp,http）
        hints: 参数可以是一个空指针，也可以是一个指向某个addrinfo结构的指针，调用者在这个结构中填入关于期望返回的信息类型的暗示。hints参数中，调用者可以设置的字段有：ai_flags、ai_family、ai_socktype、ai_protocol。
        res:存放返回addrinfo结构链表的指针

        参考资料：https://blog.csdn.net/drdairen/article/details/52794043
                https://blog.csdn.net/wkd_007/article/details/136374403
    */
    if((n = getaddrinfo(host, service, &hints, &res)) > 0)                      //获取由主机名和服务名转换成套接口地址结构
    {
        err_quit("tcp listen error for %s %s: %s", host, service, gai_strerror(n));
    }
    ressave = res;
    do
    {
        /*
            通过上面那些步骤，我们已经获取了由主机名和服务名转换成的套接字地址结构，存储在res中

            int socket(int domain, int type, int protocol);
            ->成功时返回文件描述符，失败时返回-1
            domain：套接字中使用的协议族（Protocol Family）信息
            type：套接字数据传输类型信息
            protocol：计算机间通信中使用的协议族信息
        */
        listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);  //创建套接字
        if(listenfd < 0)
        {
            continue; //error try again
        }

        /*
            可将 Time-wait 状态下的套接字端口号重新分配给新的套接字。
            Setsockopt必须在bind之前进行调用
        */
        constexpr int on = true;
        Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        /*
            int bind(int sockfd, struct sockaddr* myaddr, socklen_t addrlen);
            ->成功返回0，失败返回-1
            sockfd：要分配的地址信息（IP地址和端口号）的套接字文件描述符
            myaddr：存有地址信息的结构体变量地址值
            addrlen：第二个参数的长度
        */
        if(bind(listenfd, res->ai_addr, res->ai_addrlen) == 0)                  //向套接字分配网络地址
        {
            printf("server address: %s\n", Sock_ntop(addr, MAXSOCKADDR, res->ai_addr, res->ai_addrlen));
            break; //success，此处可以看到，即使有多个网络接口，我们最终也只会监听第一个成功绑定的listenfd
        }
        Close(listenfd);
    }while((res = res->ai_next) != NULL);// 刚刚说到res可能存在多个结果，所以需要遍历链表
    freeaddrinfo(ressave); //free

    if(res == NULL)
    {
        err_quit("tcp listen error for %s: %s", host, service);
    }

    /*
        int listen(int sock, int backlog);
        ->成功时返回0，失败时返回-1
        sock：希望进入等待连接请求状态的套接字文件描述符，传递的描述符套接字参数成为服务器端套接字(监听套接字)。
        backlog：连接请求等待队列(Queue)的长度，若为5，则队列长度为5，表示最多使5个连接请求进入队列。

        好好理解第二个参数：未决连接，监听套接字队列。最开始是：已完成和未完成之和最大值。现在是：最大已完成连接数。
        对于一个调用listen()函数进行监听的套接字，操作系统回个这个套接字维护两个队列。
        a、未完成连接的队列（SYN_RCVD）：
        当客户端发送TCP连接三次握手的第一次给服务器的时候，
        服务器就会在未完成队列中创建一个跟这个syn包对应的一项。
        可以看成是半连接，这个连接的状态从listen变成syn_rcvd状态，
        同时返回给客户端返回第二次握手。
        这个时候服务器在等待完成第三次握手

        b、已完成连接的队列（ESTABLISHED）
        当第三次握手完成了，这个连接就变成了ESTABLISHED状态。
        每个已经完成三次握手的客户端都放在这个队列中作为一项。

        listen函数不会阻塞，它只是相当于把socket的属性更改为被动连接，可以接收其他进程的连接，它只是设置好socket的属性之后就会返回。监听的过程实质由操作系统完成。
    */
    Listen(listenfd, LISTENQ);                                                  //listen是非阻塞的

    if(addrlen)
    {
        *addrlen = res->ai_addrlen;
    }

    return listenfd;
}

int Accept(int listenfd, SA * addr, socklen_t *addrlen)
{
    int n;
    for(;;)
    {
        /*
            int accept(int sock, struct sockaddr* addr, socklen_t* addrlen);
            ->成功时返回创建的套接字文件描述符，失败时返回-1
            sock：服务器套接字的文件描述符（即listenfd）
            addr：保存发起连接请求的客户端地址信息的变量地址值，调用该函数后向，addr指向的内存空间填充客户端的地址信息
            addrlen：第二个参数addr结构体的长度，调用该函数后，向该变量填入客户端地址长度

            功能简介：该函数阻塞：从处于established 状态的队列中取出完成的连接（多线程下该队列为临界区资源），当队列中没有完成连接时候，会形成阻塞，直到取出队列中已完成连接的用户连接为止（Accept默认会阻塞进程，直到有一个客户连接建立后返回）。
        */
        if((n = accept(listenfd, addr, addrlen)) < 0)
        {
            if(errno == EINTR)
                continue;
            else
                err_quit("accept error");
        }
        else
        {
            return n;
        }
    }
}
void Socketpair(int family, int type, int protocol, int* sockfd)
{

    /*
        该函数与socket差不多，但是socketpair更简单，一次调用可以创建两个无名的、相互连接的套接字，且不需要额外的步骤即可进行进程间通信
        PS："无名的"表示没有与特定的网络地址或端口相关联，通常用于进程间通信 (IPC)

        int socketpair(int domain, int type, int protocol, int sv[2]);
        ->成功返回0，创建好的套接字为sv[0]与sv[1]；失败返回-1，错误码存于error
        domin: 即协议域，又称为协议族（family）。常用的协议族有，AF_INET、AF_INET6。与socket的参数一样
        type: 指定socket类型。常用的socket类型有，SOCK_STREAM、SOCK_DGRAM。与socket的参数一样
        protocol: 指定协议。常用的协议有，IPPROTO_TCP、IPPTOTO_UDP。与socket的参数一样
        sv: 存储了两个套接字的文件描述符，这两个套接字彼此连接，可以通过 recvmsg() 和 sendmsg() 函数进行通信。

        基本用法：
        1. 这对套接字可以用于全双工通信，每一个套接字既可以读也可以写。例如，可以往sv[0]中写，从sv[1]中读；或者从sv[1]中写，从sv[0]中读；
        2. 如果往一个套接字(如sv[0])中写入后，再从该套接字读时会阻塞，只能在另一个套接字中(sv[1])上读成功；
        3. 读、写操作可以位于同一个进程，也可以分别位于不同的进程，如父子进程。如果是父子进程时，一般会功能分离，一个进程用来读，一个用来写。因为文件描述副sv[0]和sv[1]是进程共享的，所以读的进程要关闭写描述符, 反之，写的进程关闭读描述符。
    */
    if(socketpair(family, type, protocol, sockfd) < 0)
    {
        err_quit("socketpair error");
    }
}

/*
    函数功能：主进程将数据包装进msghdr中，调用sendmsg()向子进程发送数据
    ->；返回成功传入子进程数据的字节数
    fd：主进程与子进程的通信套接口，即socket[0]
    ptr：待传输的数据
    nbytes：待传输数据的长度
    sendfd：后端与客户端的socket通信套接口，即connfd
*/
ssize_t ipc_write(int fd, void *ptr, size_t nbytes, int sendfd)
{
    struct msghdr msg;
    struct iovec iov[1];

    union{
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    }control_un;
    struct cmsghdr *cmptr;

    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    cmptr = CMSG_FIRSTHDR(&msg);
    cmptr->cmsg_len = CMSG_LEN(sizeof(int));
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(cmptr)) = sendfd;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    return (sendmsg(fd, &msg, 0));
}

/*
    函数功能：子进程调用recvmsg()将数据从fd中读取出来，并解析
    ->返回实际读取到的字节数
    fd：子进程与主进程的通信接口，即socket[1]
    ptr：传出参数，表示接收到的数据
    nbytes：需要接收的数据长度
    recvfd：后端与客户端的socket通信套接口，即connfd
*/
ssize_t ipc_read(int fd, void *ptr, size_t nbytes, int *recvfd)
{
    struct msghdr msg;
    struct iovec iov[1];
    ssize_t n;

    union {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    }control_un;

    struct cmsghdr *cmptr;
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    //-------------------------
    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    if((n  = recvmsg(fd, &msg, MSG_WAITALL)) < 0)
    {
        return n;
    }

    if((cmptr = CMSG_FIRSTHDR(&msg)) != NULL && cmptr->cmsg_len == CMSG_LEN(sizeof(int)))
    {
        if(cmptr->cmsg_level != SOL_SOCKET)
        {
            err_msg("control level != SOL_SOCKET");
        }
        if(cmptr->cmsg_type != SCM_RIGHTS)
        {
            err_msg("control type != SCM_RIGHTS");
        }
        *recvfd = *((int *) CMSG_DATA(cmptr));
    }
    else
    {
        *recvfd = -1; // descroptor was not passed
    }

    return n;
}