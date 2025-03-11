### Qt project 文件（1）

- Qt 有pro 文件来组织项目 .pro文件可以通过 qtcreator 自动添加生成, 同时也可以通过命令行的方式手动生成.
- `qmake -project` 把某个文件夹下所有的文件添加到 pro下作为整个工程的一部分. 主要是 .h .cpp .qrc .ui 文件 .
- qmake 会根据当前的 .pro 文件生成 Makefile. 这个 Makefile描述了如何编译链接整个程序的文件.
- make 找Makefile文件去编译程序. 在 windows 上, 由于使用的 linux gcc g++ 移植到windows 的mingw 编译器 . 只能去使用 `mingw32-make`. 或者说自己去改下 mingw32-make 这个文件 . 使用 `make clean` 清除上次编译的内容. 如果你的项目比较大编译需要花很多时间，那么你可以使用`make -j8` 编译。

### Qt project 文件（2）

- 每次修改了pro文件最好都去做一次`qmake`.

- `QT += core gui widgets` 表示各种Qt的模块.除了使用QT += widgets 如果你使用Qt去构建你的项目，某些模块默认加入了,还可以使用QT -= widgets去掉这些模块.

- `CONFIG += console c++17 debug warn_off exceptions no_exceptions stl no_stl`

- `TEMPLATE = app lib subdirs`关键字用于指定项目的类型

- `TARGET = CCQt` 表示可执行程序的名字

- `INCLUDEPATH += .` 表示包含的头文件的路径 一个点表示当前文件夹.

- `LIBS += .` 包含的库文件的路径

- `INCLUDEPATH += $$PWD/libmath/include` 包含头文件 $$PWD表示当前文件夹目录.

- `LIBS += -L$$PWD/libmath/lib -lccmath` -L表示文件夹 -l表示文件夹下的具体的库的名字.

### Qt GUI 文件

- .ui 文件是基于 xml 的界面文件. 计算机行业很多框架都是基于 xml, 比如安卓 .

- .ui 我们最终是需要通过 qt 编译成 c++ 代码的 .

- Qt xml 文件什么时候转化为一个 UI 类的 ?qmake 生成了 debug release 文件夹 , Makefile, Makefile.debug, Makefile.release. 但是没有生成 ui_xxx 类 . **执行 make/build 的时候，生成了 ui_xxx 类**. 这个时候，我们的 Qt 在成员初始化列表里面，才能 new ui. 然后执行 ui->setup()

### Qt Creator 和 VS 创建的qt程序之间的差异

先看下图：

- Qt Creator：

  - 头文件：

    ![image-20250310220233867](./assets/image-20250310220233867.png)

  - 源文件：

    ![image-20250310220304352](./assets/image-20250310220304352.png)

- VS：

  - 头文件：

    ![image-20250310220432447](./assets/image-20250310220432447.png)

  - 源文件：

    ![image-20250310220529354](./assets/image-20250310220529354.png)

总结：

1. 首先需要注意，两个编译器`#include "ui_mainwindow.h"`的位置不同，Qt Creator是放在了.cpp文件中，而VS是放在了.h文件中。
2. 这就导致了在创建 ui 对象时，对于VS来说，可以创建一个完整的栈对象，而对于Qt Creator来说，只能先进行前置申明，然后创建一个对应的ui指针。