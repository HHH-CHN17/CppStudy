/*******************************************************************************
* @author       LP
* @version:     1.0
* @date         25-1-28
* @description:
*******************************************************************************/
#ifndef CLOUDCONFERENCE_PROTOCOL_H
#define CLOUDCONFERENCE_PROTOCOL_H

#include <unordered_map>
#include "Public.h"
#include "MsgQueue.h"

#define MAXSIZE 1000
#define MB (1024*1024)

//消息类型和图片格式的定义
//一般有用的就是创建会议，加入，退出，关闭会议这些，其他信息做转发
enum MSG_TYPE
{
    IMG_SEND = 0,
    IMG_RECV,
    AUDIO_SEND,
    AUDIO_RECV,
    TEXT_SEND,
    TEXT_RECV,
    CREATE_MEETING,
    EXIT_MEETING,
    JOIN_MEETING,
    CLOSE_CAMERA,

    CREATE_MEETING_RESPONSE = 20,
    PARTNER_EXIT = 21,
    PARTNER_JOIN = 22,
    JOIN_MEETING_RESPONSE = 23,
    PARTNER_JOIN2 = 24

};

//图片格式定义
enum Image_Format {
    Format_Invalid,
    Format_Mono,
    Format_MonoLSB,
    Format_Indexed8,
    Format_RGB32,
    Format_ARGB32,
    Format_ARGB32_Premultiplied,
    Format_RGB16,
    Format_ARGB8565_Premultiplied,
    Format_RGB666,
    Format_ARGB6666_Premultiplied,
    Format_RGB555,
    Format_ARGB8555_Premultiplied,
    Format_RGB888,
    Format_RGB444,
    Format_ARGB4444_Premultiplied,
    Format_RGBX8888,
    Format_RGBA8888,
    Format_RGBA8888_Premultiplied,
    Format_BGR30,
    Format_A2BGR30_Premultiplied,
    Format_RGB30,
    Format_A2RGB30_Premultiplied,
    Format_Alpha8,
    Format_Grayscale8,
    Format_RGBX64,
    Format_RGBA64,
    Format_RGBA64_Premultiplied,
    Format_Grayscale16,
    Format_BGR888,
    NImageFormats
};

//和客户端交互的消息定义，包括了消息的状态，消息，消息队列的结构体
enum STATUS
{
    CLOSE = 0,
    ON = 1,
};

// 待测试
// 用于网络通信（服务端->客户端）
struct MSG
{
    char *ptr;          // 待传输消息
    int len;            // 消息长度
    int targetfd;       // 客户端fd（sender）
    MSG_TYPE msgType;   // 消息类型
    uint32_t ip;        // 客户端IP（sender）
    Image_Format format;

    MSG()
    {

    }
    MSG(MSG_TYPE msg_type, char *msg, int length, int fd)
    {
        ptr = msg;
        len = length;
        targetfd = fd;
        msgType = msg_type;
    }
};

struct SendQueue
{
private:
    lock_free_queue<MSG, MAXSIZE> lfq_;
    using smart_ptr = lock_free_queue<MSG, MAXSIZE>::smart_ptr;

public:
    SendQueue() = default;
    SendQueue(const SendQueue&) = delete;
    SendQueue(SendQueue&&) = delete;
    SendQueue& operator=(const SendQueue&) = delete;
    SendQueue& operator=(SendQueue&&) = delete;

    void push_msg(MSG msg)
    {
        std::cout << "pushing" << std::endl;
        lfq_.push(msg);
        std::cout << "pushing end" << std::endl;
    }

    MSG pop_msg()
    {
        smart_ptr sp = std::move(lfq_.pop());

        MSG msg = *sp;
        sp.release();
        return msg;
    }
    void clear()
    {
        while(!lfq_.empty())
        {
            lfq_.pop();
        }
    }
    bool isempty()
    {
        return lfq_.empty();
    }
};

#endif //CLOUDCONFERENCE_PROTOCOL_H
