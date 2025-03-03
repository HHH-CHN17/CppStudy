#include "RoomBusiness.h"
#include "ProcessPool.h"

//SendQueue send_queue; //save data
//RB::UserPool* user_pool = new RB::UserPool();
//STATUS volatile roomstatus = ON;    // 当前房间的状态

Room::Room()
{
    umapFdToIp_.clear();
    ownerfd_ = 0;
    //FD_ZERO(&fdset_);
    epollfd_ = -1;
    nHeadCount_ = 0;
    roomstatus = ON;
}

void Room::clear_room()
{
    std::lock_guard<std::mutex> lg{mtx_};
    roomstatus = CLOSE;
    for (auto& ele : umapFdToIp_)
    {
        ele.second = CLOSE;
    }
    //memset(status, 0, sizeof(status));
    nHeadCount_ = 0;
    ownerfd_ = 0;
    //FD_ZERO(&fdset_);
    epollfd_ = -1;
    umapFdToIp_.clear();
    //umapClientStatus_.clear();
    send_queue.clear();
}

void Room::fd_close(int fd, int pipefd)
{

    if(ownerfd_ == fd) // room close
    {
        //room close
        clear_room();
        printf("clear room\n");
        //write to father process
        char cmd = 'E';
        if(write_all(pipefd, &cmd, 1) < 1)
        {
            err_msg("write_all error");
        }
    }
    else
    {
        uint32_t ip;
        //delete fd from UserPool

        {
            std::lock_guard<std::mutex> lg{mtx_};
            ip = umapFdToIp_[fd];
            processpool::removedfd(epollfd_, fd);
            umapFdToIp_.erase(fd);
            nHeadCount_--;
        }

        char cmd = 'Q';
        if(write_all(pipefd, &cmd, 1) < 1)
        {
            err_msg("write error");
        }

        // msg ipv4

        MSG msg;
        memset(&msg, 0, sizeof(MSG));
        msg.msgType = PARTNER_EXIT;
        msg.targetfd = -1;
        msg.ip = ip; // network order
        Close(fd);
        send_queue.push_msg(msg);
    }
}

void Room::accept_from_parent(int ipc_fd, int epollfd)
{
    printf("child executing\n");
    int client_fd = -1;
    int bytes;
    char ch;
    /*
        此时fd为进程间通信fd
        c为传输的信息
        tfd为客户端fd
    */
    if((bytes = ipc_read(ipc_fd, &ch, 1, &client_fd)) <= 0)
    {
        err_quit("ipc_read error");
    }
    if(client_fd < 0)
    {
        printf("ch = %c\n", ch);
        err_quit("no descriptor from ipc_read");
    }
    //add to epoll
    printf("ch = %c\n", ch);
    if(ch == 'C') // create
    {
        {
            std::lock_guard<std::mutex> lg{mtx_};
            epollfd_ = epollfd;
            processpool::addfd(epollfd_, client_fd);

            ownerfd_ = client_fd;
            umapFdToIp_[client_fd] = getpeerip(client_fd);
            nHeadCount_++;
            roomstatus = ON; // set on
        }

        MSG msg;
        msg.msgType = CREATE_MEETING_RESPONSE;
        msg.targetfd = client_fd;
        int roomNo = htonl(getpid());
        msg.ptr = (char *) malloc(sizeof(int));
        memcpy(msg.ptr, &roomNo, sizeof(int));
        msg.len = sizeof(int);
        send_queue.push_msg(msg);
        printf("create meeting success\n");
    }
    else if(ch == 'J') // join
    {
        std::unique_lock<std::mutex> ul{mtx_};
        if(roomstatus == CLOSE) // meeting close (owner close)
        {
            processpool::removedfd(epollfd_, client_fd);
            ul.unlock();
            return;
        }
        else
        {
            processpool::addfd(epollfd_, client_fd);

            nHeadCount_++;
            umapFdToIp_[client_fd] = getpeerip(client_fd);
            ul.unlock();

            //向其他用户广播有client_fd加入
            MSG msg;
            memset(&msg, 0, sizeof(MSG));
            msg.msgType = PARTNER_JOIN;
            msg.ptr = nullptr;
            msg.len = 0;
            msg.targetfd = client_fd;
            msg.ip = umapFdToIp_[client_fd];
            send_queue.push_msg(msg);

            //向client_fd发送当前会议所有参会者
            MSG msg1;
            memset(&msg1, 0, sizeof(MSG));
            msg1.msgType = PARTNER_JOIN2;
            msg1.targetfd = client_fd;
            int size = nHeadCount_ * sizeof(uint32_t);

            msg1.ptr = (char *)malloc(size);
            int pos = 0;

            for (auto& ele : umapFdToIp_)
            {
                if(ele.first != client_fd)
                {
                    uint32_t ip = ele.second;
                    memcpy(msg1.ptr + pos, &ip, sizeof(uint32_t));
                    pos += sizeof(uint32_t);
                    msg1.len += sizeof(uint32_t);
                }
            }
            send_queue.push_msg(msg1);// 按理说本行代码应该放在333行后面，因为msg1的作用是向tfd发送当前会议所有的参会者

            printf("join meeting: %d\n", msg.ip);
        }
    }
}

void Room::msg_forward()
{
    //char * sendbuf = (char *)malloc(4 * MB);
    char* sendbuf = new char[4 * MB];
    /*
     * $_msgType_ip_size_data_#
    */

    while(true)
    {
        while (send_queue.isempty())
        {
            //std::cout << "send queue empty\n";
            //this_thread::yield();
            this_thread::sleep_for(50ms);
        }

        memset(sendbuf, 0, 4 * MB);
        MSG msg = send_queue.pop_msg();
        int len = 0;
        if (msg.ptr == nullptr)
            continue;
        printf("get msg: %s", msg.ptr);

        sendbuf[len++] = '$';
        short type = htons((short)msg.msgType);
        memcpy(sendbuf + len, &type, sizeof(short)); //msgtype
        len+=2;

        if(msg.msgType == CREATE_MEETING_RESPONSE || msg.msgType == PARTNER_JOIN2)
        {
            len += 4;
        }
        else if(msg.msgType == TEXT_RECV || msg.msgType == PARTNER_EXIT || msg.msgType == PARTNER_JOIN || msg.msgType == IMG_RECV || msg.msgType == AUDIO_RECV || msg.msgType == CLOSE_CAMERA)
        {
            memcpy(sendbuf + len, &msg.ip, sizeof(uint32_t));
            len+=4;
        }

        int msglen = htonl(msg.len);
        memcpy(sendbuf + len, &msglen, sizeof(int));
        len += 4;
        memcpy(sendbuf + len, msg.ptr, msg.len);
        len += msg.len;
        sendbuf[len++] = '#';

        {
            std::lock_guard<std::mutex> lg{mtx_};
            if(msg.msgType == CREATE_MEETING_RESPONSE)
            {
                //send buf to target
                if(write_all(msg.targetfd, sendbuf, len) < 0)
                {
                    err_msg("write_all error");
                }
            }
            else if(msg.msgType == PARTNER_EXIT || msg.msgType == IMG_RECV || msg.msgType == AUDIO_RECV || msg.msgType == TEXT_RECV || msg.msgType == CLOSE_CAMERA)
            {
                for (auto& ele : umapFdToIp_)
                {
                    if (msg.targetfd != ele.first)
                    {
                        if(write_all(ele.first, sendbuf, len) < 0)
                        {
                            err_msg("write_all error");
                        }
                    }
                }
            }
            else if(msg.msgType == PARTNER_JOIN)
            {
                for (auto& ele : umapFdToIp_)
                {
                    if (msg.targetfd != ele.first)
                    {
                        if(write_all(ele.first, sendbuf, len) < 0)
                        {
                            err_msg("write_all error");
                        }
                    }
                }
            }
            else if(msg.msgType == PARTNER_JOIN2)
            {
                auto it = umapFdToIp_.find(msg.targetfd);
                if (it == umapFdToIp_.end())
                {
                    cout << "user didn't exist" << endl;
                }
                else
                {
                    if(write_all(it->first, sendbuf, len) < 0)
                    {
                        err_msg("write_all error");
                    }
                }
            }
        }

        //free
        if(msg.ptr)
        {
            free(msg.ptr);
            msg.ptr = nullptr;
        }
    }
    free(sendbuf);
}

void Room::parse_and_forward_to_client(int client_fd, int ipc_fd){
    char head[15] = {0};
    int ret = read_all(client_fd, head, 11); // head size = 11
    //有人退出会议
    if(ret <= 0)
    {
        printf("peer close\n");
        fd_close(client_fd, ipc_fd);
    }
    else if(ret == 11)                                      // 解析消息头部信息,并接收具体消息做转发
    {
        if(head[0] == '$')
        {
            //solve datatype
            MSG_TYPE msgtype;
            memcpy(&msgtype, head + 1, 2);
            msgtype = (MSG_TYPE)ntohs(msgtype);

            MSG msg;
            memset(&msg, 0, sizeof(MSG));
            msg.targetfd = client_fd;
            memcpy(&msg.ip, head + 3, 4);
            int msglen;
            memcpy(&msglen, head + 7, 4);
            msg.len = ntohl(msglen);

            if(msgtype == IMG_SEND || msgtype == AUDIO_SEND || msgtype == TEXT_SEND)            // 根据客户端传来的消息做消息转发，转发给当前房间的其他客户端
            {
                msg.msgType = (msgtype == IMG_SEND) ? IMG_RECV : ((msgtype == AUDIO_SEND)? AUDIO_RECV : TEXT_RECV);
                msg.ptr = (char *)malloc(msg.len);
                msg.ip = umapFdToIp_[client_fd];
                if((ret = read_all(client_fd, msg.ptr, msg.len)) < msg.len)
                {
                    err_msg("3 msg format error");
                }
                else
                {
                    char tail;
                    read_all(client_fd, &tail, 1);
                    if(tail != '#')
                    {
                        err_msg("4 msg format error");
                    }
                    else
                    {
                        send_queue.push_msg(msg);
                    }
                }
            }
            else if(msgtype == CLOSE_CAMERA)
            {
                char tail;
                read_all(client_fd, &tail, 1);
                if(tail == '#' && msg.len == 0)
                {
                    msg.msgType = CLOSE_CAMERA;
                    send_queue.push_msg(msg);
                }
                else
                {
                    err_msg("camera data error ");
                }
            }
        }
        else
        {
            err_msg("1 msg format error");
        }
    }
    else
    {
        err_msg("2 msg format error");
    }

    //msg_forward();
}