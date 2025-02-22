/*******************************************************************************
* @author:      LP
* @version:     1.0
* @date:        25-2-3
* @description:
*******************************************************************************/

#include "Error.h"

//错误信息反馈模块，此模块简化了，采用的是标准输入输出的方式
static void err_doit(int errnoflag, int error, const char *fmt, va_list ap)
{
    char buf[MAXLINE];
    vsnprintf(buf, MAXLINE - 1, fmt, ap);

    if(errnoflag)
    {
        snprintf(buf + strlen(buf), MAXLINE - 1 - strlen(buf), ": %s", strerror(error));
    }
    strcat(buf, "\n");
    fflush(stdout);
    fputs(buf, stderr);
    fflush(NULL);
}

/*
 * fatal error unrelated to a system call
 * print a message and terminate *
*/

void err_quit(const char * fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    err_doit(1, errno, fmt, ap);
    va_end(ap);
    exit(1);
}

void err_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    err_doit(1, errno, fmt, ap);
    va_end(ap);
}
