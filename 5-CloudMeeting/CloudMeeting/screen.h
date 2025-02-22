#ifndef SCREEN_H
#define SCREEN_H

//屏幕工具模块，检测屏幕的宽，高以及其他设置
class Screen
{
public:
    static int width;
    static int height;

    static void init();
};

#endif // SCREEN_H
