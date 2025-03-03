/*******************************************************************************
* @author       LP
* @version:     1.0
* @date         25-1-28
* @description: 主进程业务
*******************************************************************************/

#ifndef CLOUDCONFERENCE_USERFORWARD_H
#define CLOUDCONFERENCE_USERFORWARD_H

#include "Public.h"
#include "Protocol.h"

//用户处理模块，用户传来一个消息，我们在该模块进行接受，查找该用户属于哪个房间，然后将信息转发至那个房间去
//extern socklen_t addrlen;
//extern int ListenFd;
//extern int nprocesses;
//extern RoomGuard *room;


class RoomGuard{
public:
    typedef struct
    {
        pid_t child_pid;    //process id，也是具体的房间号
        //int child_pipefd;   //主进程与子进程的通信套接口，即socketfd[0]
        int child_status;   //0：当前进程空闲；1：当前进程已被分配房间
        int total;          //当前房间人数
    }Process;

    std::atomic_int     n_avail_;           //可用房间数
    int                 n_room_num_;        // 总房间数
    std::mutex          room_mtx_;        //保证线程安全
    std::unordered_map<int, Process>    umap_room_;    //<child_pipefd, Process>。用于管理房间，与子进程通信，child_pipefd即socketfd[0]
    std::mutex listen_mtx_;

public:
    explicit RoomGuard (int n);

    void accept_client(int epollfd, int listenfd);

//  从客户端的connfd中读数据，并将相关消息和connfd下发给子进程，此函数由主进程的子线程执行，注意在下发成功后主进程会关闭connfd，之后的数据收发由子进程进行
    void forward_to_child(int client_fd);

//  往connfd（客户端）回传数据，使用的是自定义网络协议，ip不用写，因为ip表示的是发送者ip
    void reply_to_sender(int client_fd, MSG msg);

//  处理子进程传来的数据
    void process_msg_from_child(int sockfd);
};

//namespace UF{
//    std::mutex mtx;
//
//    void accept_and_forward_to_child(int i);
//
////  从客户端的connfd中读数据，并将相关消息和connfd下发给子进程，此函数由主进程的子线程执行，注意在下发成功后主进程会关闭connfd，之后的数据收发由子进程进行
//    void parse_and_forward(int client_fd);
//
////  往connfd（客户端）回传数据，使用的是自定义网络协议，ip不用写，因为ip表示的是发送者ip
//    void reply_to_sender(int fd, MSG msg);
//
////  处理子进程传来的数据
//    void process_msg_from_child(std::shared_ptr<RoomGuard> sp_room_guard, int sockfd);
//}



#endif //CLOUDCONFERENCE_USERFORWARD_H
