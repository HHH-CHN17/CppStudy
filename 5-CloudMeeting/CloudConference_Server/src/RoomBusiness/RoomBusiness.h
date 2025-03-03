/*******************************************************************************
* @author       LP
* @version:     1.0
* @date         25-1-28
* @description: 房间具体的业务处理（子进程业务）
*******************************************************************************/

#ifndef CLOUDCONFERENCE_ROOMBUSINESS_H
#define CLOUDCONFERENCE_ROOMBUSINESS_H

#include <map>
#include <unordered_map>
#include <mutex>
#include <thread>
#include "Public.h"
#include "Protocol.h"
#include "ThreadPool.h"
#include "Error.h"
//#include "WrapUnix.h"
#include "src/PublicDef/NetWork/NetWork.h"

// fdToIp抽空改成无锁哈希表
#define SENDTHREADSIZE 5

class Room{
public:
    //fd_set fdset_;                                          // 当前房间分配的fdset，用于初始化客户端的fd们，并接收fd中待读取数据，也就是说一个房间对应一个select，使用子进程的主线程进行数据接收，子线程们发送
    int epollfd_;
    mutable std::mutex mtx_;
    int ownerfd_;                                           // 创建者的connfd（创建者也是一个客户端）
    int nHeadCount_;                                        // 当前房间的总人数
    std::unordered_map<int, uint32_t> umapFdToIp_;               // key为客户端fd，value为客户端IP

    SendQueue send_queue; //save data
    //UserPool* user_pool = new UserPool();
    STATUS volatile roomstatus = ON;    // 当前房间的状态

    Room();

    void clear_room();

    //file description close
    void fd_close(int fd, int pipefd);

    // ipc_fd：主进程与子进程通信的套接口socket[1]
    // accept fd from father
    void accept_from_parent(int ipc_fd, int epollfd);

    void msg_forward();

    void parse_and_forward_to_client(int client_fd, int ipc_fd);
};



#endif //CLOUDCONFERENCE_ROOMBUSINESS_H
