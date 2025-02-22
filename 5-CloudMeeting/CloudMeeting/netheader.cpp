#include "netheader.h"
#include "logqueue.h"
#include <QDebug>
#include <time.h>

QUEUE_DATA<MESG> queue_send; //�ı�����Ƶ���Ͷ���
QUEUE_DATA<MESG> queue_recv; //���ն���
QUEUE_DATA<MESG> audio_recv; //��Ƶ���ն���

LogQueue *logqueue = nullptr;

void log_print(const char *filename, const char *funcname, int line, const char *fmt, ...)
{
    Log *log = (Log *) malloc(sizeof (Log));
    if(log == nullptr)
    {
        qDebug() << "malloc log fail";
    }
    else
    {
        memset(log, 0, sizeof (Log));
        log->ptr = (char *) malloc(1 * KB);
        if(log->ptr == nullptr)
        {
            free(log);
            qDebug() << "malloc log.ptr fail";
            return;
        }
        else
        {
            memset(log->ptr, 0, 1 * KB);
            time_t t = time(NULL);
            int pos = 0;
            // 参考资料：https://www.runoob.com/cprogramming/c-function-strftime.html
            int m = strftime(log->ptr + pos, KB - 2 - pos, "%F %H:%M:%S ", localtime(&t));
			pos += m;

            // 参考资料：https://www.runoob.com/cprogramming/c-function-snprintf.html
            // https://c.biancheng.net/c/snprintf.html 类似printf，但还是有点不同的呢
            m = snprintf(log->ptr + pos, KB - 2 - pos, "%s:%s::%d>>>", filename, funcname, line);
			pos += m;

            // 参考资料：https://zhuanlan.zhihu.com/p/365756463
			va_list ap;
			va_start(ap, fmt);
            // 参考资料：https://blog.csdn.net/ATOOHOO/article/details/88947408
            m = _vsnprintf(log->ptr + pos, KB - 2 - pos, fmt, ap);
			pos += m;
			va_end(ap);
            strcat_s(log->ptr+pos, KB-pos, "\n");
            log->len = strlen(log->ptr);
			logqueue->pushLog(log);
        }
    }
}
