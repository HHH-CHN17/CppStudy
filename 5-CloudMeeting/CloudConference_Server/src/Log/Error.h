/*******************************************************************************
* @author:      LP
* @version:     1.0
* @date:        25-2-3
* @description:
*******************************************************************************/

#ifndef CLOUDCONFERENCE_ERROR_H
#define CLOUDCONFERENCE_ERROR_H


#include <cerrno>
#include "Public.h"
#include <cstdarg>

//错误信息反馈模块，此模块简化了，采用的是标准输入输出的方式
static void err_doit(int errnoflag, int error, const char *fmt, va_list ap);

/*
 * fatal error unrelated to a system call
 * print a message and terminate *
*/

void err_quit(const char * fmt, ...);

void err_msg(const char *fmt, ...);


#endif //CLOUDCONFERENCE_ERROR_H
