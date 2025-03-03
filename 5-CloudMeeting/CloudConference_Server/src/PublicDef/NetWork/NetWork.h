//
// Created by lipei on 25-1-28.
//

#ifndef CLOUDCONFERENCE_NETWORK_H
#define CLOUDCONFERENCE_NETWORK_H

#include "Public.h"
#include "Error.h"

//网络模块
uint32_t getpeerip(int fd);

/*
    从fd中读size个char并存入buf中，返回实际读到的字节数
    大于0：成功读取的数据长度（Byte）；
    等于0：建议重试；
    等于-1：异常发生，
*/
template<typename T>
ssize_t	read_all(int fd, T* ptr, ssize_t size)
{
    ssize_t lefttoread = size, hasread = 0;
    while(lefttoread > 0)
    {
        hasread = read(fd, ptr, lefttoread);
        if(hasread<0)
        {
            if(errno == EINTR || errno == EWOULDBLOCK)
            {
                //printf("read finish\n");
                err_msg("read error: %s", strerror(errno));
                hasread = 0;
                break;
            }
            else
            {
                return -1;
            }
        }
        else if(hasread == 0) //eof
        {
            break;
        }
        lefttoread -= hasread;
        ptr += hasread;
    }
    return size - lefttoread;
}


ssize_t write_all(int fd, const void * buf, size_t n);

const char *Sock_ntop(char * str, int size ,const sockaddr * sa, socklen_t salen);

void Setsockopt(int fd, int level, int optname, const void * optval, socklen_t optlen);

void Close(int fd);

void Listen(int fd, int backlog);

/*
    ->返回建立并绑定好的监听套接口（ListenFd）
    host：主机名或IP地址
    service：服务名或端口号
    addrlen：是一种和int类似的数据类型，长度为32bits（32/64位），表示服务器地址长度
    相关知识：https://blog.csdn.net/weixin_45437521/article/details/109037537
*/
int Tcp_listen(const char * host, const char * service, socklen_t * addrlen);

int Accept(int listenfd, SA * addr, socklen_t *addrlen);

void Socketpair(int family, int type, int protocol, int * sockfd);

ssize_t ipc_write(int fd, void *ptr, size_t nbytes, int sendfd);

/*
    函数功能：子进程调用recvmsg()将数据从fd中读取出来，并解析
    ->返回实际读取到的字节数
    fd：子进程与主进程的通信接口，即socket[1]
    ptr：传出参数，表示接收到的数据
    nbytes：需要接收的数据长度
    recvfd：后端与客户端的socket通信套接口，即connfd
*/
ssize_t ipc_read(int fd, void *ptr, size_t nbytes, int *recvfd);

#endif //CLOUDCONFERENCE_NETWORK_H
