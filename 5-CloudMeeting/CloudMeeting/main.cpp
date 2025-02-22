#include <QApplication>
#include "widget.h"
#include "screen.h"
#include <QTextCodec>
int main(int argc, char* argv[])
{
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF8"));
    QApplication app(argc, argv);
    //获取主窗口的宽，高，便于后续视频的定位，分析
    //与widget的关系为：依赖关系，两者之间可以独立运行，有参数上的交互
    Screen::init();

    Widget w;
    w.show();
    return app.exec();
}
