/*******************************************************************************
* @author:      LP
* @version:     1.0
* @date:        25-1-28
* @description:
*******************************************************************************/

#include "UserForward.h"
#include "ProcessPool.h"

RoomGuard::RoomGuard (int n) : n_avail_(0)
{
    n_avail_ = n;
    n_room_num_ = n;
    //pptr = (Process *)Calloc(n, sizeof(Process));
    umap_room_.reserve(n);
}

void RoomGuard::accept_client(int epollfd, int listenfd)
{
    int client_fd;
    struct sockaddr_in clnt_addr{};
    socklen_t clnt_addr_size;
    char buf[MAXSOCKADDR];

    clnt_addr_size = sizeof(clnt_addr);

    {
        std::lock_guard<std::mutex> lg{listen_mtx_};
        client_fd = Accept(listenfd, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
    }

    //constexpr int on = true;
    // 可将 Time-wait 状态下的套接字端口号重新分配给新的套接字。
    //Setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    // 实时性较高的系统应当禁用Nagle算法
    //Setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

    printf("connection from %s\n", Sock_ntop(buf, MAXSOCKADDR, (struct sockaddr*)&clnt_addr, clnt_addr_size));

    processpool::addfd(epollfd, client_fd);
    //parse_and_forward(client_fd); // process user
}

// 从客户端的client_fd中读数据，并将相关消息和client_fd下发给子进程，此函数由主进程的子线程执行，注意在下发成功后主进程会关闭client_fd，之后的数据收发由子进程进行
void RoomGuard::forward_to_child(int client_fd)
{
    char head[15]  = {0};
    //read head
    while(1)
    {
        /*
            消息头只有11字节，包括'$'+MSG_TYPE+IP+len
            while循环的消息处理分两步：
            1. 从client_fd中读11字节的消息头，并解析消息头，处理CREATE_MEETING和JOIN_MEETING
            2. 从client_fd中读datasize字节的具体消息
        */
        ssize_t ret = read_all(client_fd, head, 11);
        if(ret <= 0)
        {
            close(client_fd); //close
            printf("%d close\n", client_fd);
            return;
        }
        else if(ret < 11)
        {
            printf("data len too short\n");
        }
        else if(head[0] != '$')
        {
            printf("data format error\n");
        }
        else
        {
            //解析MSG_TYPE
            MSG_TYPE msgtype;
            memcpy(&msgtype, head + 1, 2);
            msgtype = (MSG_TYPE)ntohs(msgtype);

            //解析IP
            uint32_t ip;
            memcpy(&ip, head + 3, 4);
            ip = ntohl(ip);

            //解析数据长度
            uint32_t datasize;
            memcpy(&datasize, head + 7, 4);
            datasize = ntohl(datasize);

            // 读具体的消息数据
            if(msgtype == CREATE_MEETING)
            {
                char tail;
                read_all(client_fd, &tail, 1);
                //read data from client
                if(datasize == 0 && tail == '#')
                {
                    char *c = (char *)&ip;
                    printf("create meeting  ip: %d.%d.%d.%d\n", (u_char)c[3], (u_char)c[2], (u_char)c[1], (u_char)c[0]);

                    //无空闲房间
                    if(n_avail_ <= 0)
                    {
                        printf("no more room\n");
                        MSG msg;
                        memset(&msg, 0, sizeof(msg));
                        msg.msgType = CREATE_MEETING_RESPONSE;
                        int roomNo = 0;
                        msg.ptr = (char *) malloc(sizeof(int));
                        memcpy(msg.ptr, &roomNo, sizeof(int));
                        msg.len = sizeof(roomNo);
                        reply_to_sender(client_fd, msg);                     //通知客户端无空闲房间
                    }
                    else
                    {
                        {
                            std::lock_guard<std::mutex> lg{room_mtx_};

                            // 寻找第一个未被占领的空闲房间
                            auto it = umap_room_.begin();
                            while (it != umap_room_.end())
                            {
                                if(it->second.child_status == 0) break;
                            }
                            if(it == umap_room_.end())
                            {
                                printf("no more room\n");
                                MSG msg;
                                memset(&msg, 0, sizeof(msg));
                                msg.msgType = CREATE_MEETING_RESPONSE;
                                int roomNo = 0;
                                msg.ptr = (char *) malloc(sizeof(int));
                                memcpy(msg.ptr, &roomNo, sizeof(int));
                                msg.len = sizeof(roomNo);
                                reply_to_sender(client_fd, msg);
                            }
                            else                                // 有空闲房间
                            {
                                char cmd = 'C';// create
                                //ssize_t ret = write(client_fd, &cmd, 1);
                                printf("send msg to %d\n", it->first);
                                if(ipc_write(it->first, &cmd, 1, client_fd) < 0)   //通知子进程创建房间
                                {
                                    err_msg("write fd error");
                                }
                                else
                                {
                                    close(client_fd);
                                    printf("room %d empty\n", it->second.child_pid);
                                    it->second.child_status = 1;     // 占领空闲进程
                                    --n_avail_;
                                    it->second.total++;
                                    return;
                                }
                            }
                        }
                    }
                }
                else
                {
                    printf("CREATE_MEETING data format error\n");
                }
            }
            else if(msgtype == JOIN_MEETING)
            {
                //read msgsize，正常情况下，此时的msgsize=4
                uint32_t msgsize, roomno;
                memcpy(&msgsize, head + 7, 4);
                msgsize = ntohl(msgsize);
                //read data + #
                int r = static_cast<int>(read_all(client_fd, head, msgsize + 1));                 // 从connfd中读具体数据，存于head数组中
                if(r < msgsize + 1)
                {
                    printf("data too short\n");
                }
                else
                {
                    if(head[msgsize] == '#')
                    {
                        memcpy(&roomno, head, msgsize);                     // 获取房间号（msgtype为JOIN时，msgdata为房间号）
                        roomno = ntohl(roomno);
                        //printf("room : %d\n", roomno);
                        //find room no
                        bool ok = false;
                        auto it = umap_room_.begin();
                        while (it != umap_room_.end())
                        {
                            if(it->second.child_pid == roomno && it->second.child_status == 1)
                            {
                                ok = true; //找到房间，ok=true
                                break;
                            }
                        }

                        MSG msg;
                        memset(&msg, 0, sizeof(msg));
                        msg.msgType = JOIN_MEETING_RESPONSE;
                        msg.len = sizeof(uint32_t);
                        if(ok)
                        {
                            {
                                std::lock_guard<std::mutex> lg{room_mtx_};

                                if(it->second.total >= 1024)                 // 房间人满
                                {
                                    msg.ptr = (char *)malloc(msg.len);
                                    uint32_t full = -1;
                                    memcpy(msg.ptr, &full, sizeof(uint32_t));
                                    reply_to_sender(client_fd, msg);
                                }
                                else                                            // 进入房间成功
                                {
                                    char cmd = 'J'; // join
                                    //printf("i  =  %d\n", i);
                                    if(ipc_write(it->first, &cmd, 1, client_fd) < 0)   //往子进程传入加入会议的消息，并在成功后修改主进程的进程池数据
                                    {
                                        err_msg("write fd:");
                                    }
                                    else
                                    {

                                        msg.ptr = (char *)malloc(msg.len);
                                        memcpy(msg.ptr, &roomno, sizeof(uint32_t));
                                        reply_to_sender(client_fd, msg);
                                        it->second.total++;// add 1
                                        close(client_fd);
                                        return;
                                    }
                                }
                            }
                        }
                        else                                                // 找不到该房间
                        {
                            msg.ptr = (char *)malloc(msg.len);
                            uint32_t fail = 0;
                            memcpy(msg.ptr, &fail, sizeof(uint32_t));
                            reply_to_sender(client_fd, msg);
                        }
                    }
                    else
                    {
                        printf("format error\n");
                    }
                }
            }
            else
            {
                printf("data format error\n");
            }
        }
    }
}

// 往connfd（客户端）回传数据，使用的是自定义网络协议，ip不用写，因为ip表示的是发送者ip
void RoomGuard::reply_to_sender(int client_fd, MSG msg)
{
    char *buf = (char *) malloc(100);
    memset(buf, 0, 100);
    int bytestowrite = 0;
    buf[bytestowrite++] = '$';

    uint16_t type = msg.msgType;
    type = htons(type);
    memcpy(buf + bytestowrite, &type, sizeof(uint16_t));

    bytestowrite += 2;//skip type

    bytestowrite += 4; //skip ip

    uint32_t size = msg.len;
    size = htonl(size);
    memcpy(buf + bytestowrite, &size, sizeof(uint32_t));
    bytestowrite += 4;

    memcpy(buf + bytestowrite, msg.ptr, msg.len);
    bytestowrite += msg.len;

    buf[bytestowrite++] = '#';

    // 往客户端fd回传数据
    if(write_all(client_fd, buf, bytestowrite) < bytestowrite)
    {
        printf("write fail\n");
    }

    if(msg.ptr)
    {
        free(msg.ptr);
        msg.ptr = nullptr;
    }
    free(buf);
}

void RoomGuard::process_msg_from_child(int sockfd)
{
    auto it = umap_room_.find(sockfd);
    if (it == umap_room_.end()){
        std::cout << "child sockfd: [" << sockfd << "] isn't exist!" << std::endl;
        return;
    }
    char rc;
    int  n= static_cast<int>(read_all(it->first, &rc, 1));
    if(n <= 0)
    {
        err_quit("child %d terminated unexpectedly", it->first);
    }
    printf("c = %c\n", rc);
    if(rc == 'E') // room empty
    {
        std::lock_guard<std::mutex> lg{room_mtx_};
        it->second.child_status = 0;
        n_avail_++;
        printf("room %d is now free\n", umap_room_[sockfd].child_pid);
    }
    else if(rc == 'Q') // partner quit
    {
        std::lock_guard<std::mutex> lg{room_mtx_};
        it->second.total--;
    }
    else // trash data
    {
        err_msg("read from %d error", it->first);
        return;
    }
}
