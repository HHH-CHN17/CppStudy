#include "widget.h"
#include "ui_widget.h"
#include "screen.h"
#include <QCamera>
#include <QCameraViewfinder>
#include <QCameraImageCapture>
#include <QDebug>
#include <QPainter>
#include "myvideosurface.h"
#include "sendimg.h"
#include <QRegExp>
#include <QRegExpValidator>
#include <QMessageBox>
#include <QScrollBar>
#include <QHostAddress>
#include <QTextCodec>
#include "logqueue.h"
#include <QDateTime>
#include <QCompleter>
#include <QStringListModel>
#include <QSound>
QRect  Widget::pos = QRect(-1, -1, -1, -1);
//注意：日志队列为全局变量，extern表示该变量/函数定义在其他文件里
//在widget的构造函数里初始化
extern LogQueue *logqueue;

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{


    //开启日志线程，该线程的实现方式是重写run函数
    //是的，首先启动日志
    logqueue = new LogQueue();
    logqueue->start();

    qDebug() << "main: " <<QThread::currentThread();
    qRegisterMetaType<MSG_TYPE>();
    
    //写日志，思路源于sh80
    WRITE_LOG("-------------------------Application Start---------------------------");
    WRITE_LOG("main UI thread id: 0x%p", QThread::currentThreadId());
    //ui界面
    _createmeet = false;
    _openCamera = false;
    _joinmeet = false;
    Widget::pos = QRect(0.1 * Screen::width, 0.1 * Screen::height, 0.8 * Screen::width, 0.8 * Screen::height);

    ui->setupUi(this);
    //这是设置翻译，sh80简化版
    ui->openAudio->setText(QString(OPENAUDIO).toUtf8());
    ui->openVedio->setText(QString(OPENVIDEO).toUtf8());

    this->setGeometry(Widget::pos);
    this->setMinimumSize(QSize(Widget::pos.width() * 0.7, Widget::pos.height() * 0.7));
    this->setMaximumSize(QSize(Widget::pos.width(), Widget::pos.height()));

    //这些东西开局还没进入会议室之前，肯定得关掉的
    ui->exitmeetBtn->setDisabled(true);
    ui->joinmeetBtn->setDisabled(true);
    ui->createmeetBtn->setDisabled(true);
    ui->openAudio->setDisabled(true);
    ui->openVedio->setDisabled(true);
    ui->sendmsg->setDisabled(true);
    mainip = 0; //副屏幕上选定的用户ip

    //-------------------局部线程（注意线程创建顺序）----------------------------
    //创建传输视频帧线程，不同于日志线程，用的是movetothread的方式启动线程
    _sendImg = new SendImg();
    _imgThread = new QThread();
    _sendImg->moveToThread(_imgThread); //新起线程接受视频帧
    _sendImg->start();
    //_imgThread->start();
    //处理每一帧数据

    //--------------------------------------------------
	//音频
	_ainput = new AudioInput();
	_ainputThread = new QThread();
	_ainput->moveToThread(_ainputThread);
    _ainput->start();
	_ainputThread->start(); //获取音频，发送

	_aoutput = new AudioOutput();
	_aoutput->start(); //播放

	connect(this, SIGNAL(startAudio()), _ainput, SLOT(startCollect()));
	connect(this, SIGNAL(stopAudio()), _ainput, SLOT(stopCollect()));
	connect(_ainput, SIGNAL(audioinputerror(QString)), this, SLOT(audioError(QString)));
	connect(_aoutput, SIGNAL(audiooutputerror(QString)), this, SLOT(audioError(QString)));
	connect(_aoutput, SIGNAL(speaker(QString)), this, SLOT(speaks(QString)));

    //创建网络通信线程，用于数据处理（局部线程）
    _mytcpSocket = new MyTcpSocket(); // 底层线程专管发送
    connect(_mytcpSocket, SIGNAL(sendTextOver()), this, SLOT(textSend()));
    //connect(_mytcpSocket, SIGNAL(socketerror(QAbstractSocket::SocketError)), this, SLOT(mytcperror(QAbstractSocket::SocketError)));

    //----------------------------------------------------------
    
    //文本传输(局部线程)
    _sendText = new SendText();
    _textThread = new QThread();
    _sendText->moveToThread(_textThread);
    _textThread->start(); // 加入线程
    _sendText->start(); // 发送

    connect(this, SIGNAL(PushText(MSG_TYPE,QString)), _sendText, SLOT(push_Text(MSG_TYPE,QString)));
    //-----------------------------------------------------------

    //配置摄像头
    _camera = new QCamera(this);
    //摄像头出错处理
    connect(_camera, SIGNAL(error(QCamera::Error)), this, SLOT(cameraError(QCamera::Error)));
    _imagecapture = new QCameraImageCapture(_camera);
    _myvideosurface = new MyVideoSurface(this);

    //摄像头配置完成后，通过信号与槽将摄像头连接至之前创建的视频传输线程_sendImg

    connect(_myvideosurface, SIGNAL(frameAvailable(QVideoFrame)), this, SLOT(cameraImageCapture(QVideoFrame)));
    connect(this, SIGNAL(pushImg(QImage)), _sendImg, SLOT(ImageCapture(QImage)));


    //监听_imgThread退出信号
    connect(_imgThread, SIGNAL(finished()), _sendImg, SLOT(clearImgQueue()));


    //------------------启动接收数据线程-------------------------
    _recvThread = new RecvSolve();
    //关于信号与槽第五个参数详解：
    //https://blog.csdn.net/u012999461/article/details/128222189
    //https://blog.csdn.net/z34568/article/details/129490041
    //此处使用阻塞连接模式，希望可以保证线程同步
    connect(_recvThread, SIGNAL(datarecv(MESG*)), this, SLOT(datasolve(MESG*)), Qt::BlockingQueuedConnection);
    _recvThread->start();

    //预览窗口重定向在MyVideoSurface
    _camera->setViewfinder(_myvideosurface);
    _camera->setCaptureMode(QCamera::CaptureStillImage);//？

    //设置滚动条
    ui->scrollArea->verticalScrollBar()->setStyleSheet("QScrollBar:vertical { width:8px; background:rgba(0,0,0,0%); margin:0px,0px,0px,0px; padding-top:9px; padding-bottom:9px; } QScrollBar::handle:vertical { width:8px; background:rgba(0,0,0,25%); border-radius:4px; min-height:20; } QScrollBar::handle:vertical:hover { width:8px; background:rgba(0,0,0,50%); border-radius:4px; min-height:20; } QScrollBar::add-line:vertical { height:9px;width:8px; border-image:url(:/images/a/3.png); subcontrol-position:bottom; } QScrollBar::sub-line:vertical { height:9px;width:8px; border-image:url(:/images/a/1.png); subcontrol-position:top; } QScrollBar::add-line:vertical:hover { height:9px;width:8px; border-image:url(:/images/a/4.png); subcontrol-position:bottom; } QScrollBar::sub-line:vertical:hover { height:9px;width:8px; border-image:url(:/images/a/2.png); subcontrol-position:top; } QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:rgba(0,0,0,10%); border-radius:4px; }");
    ui->listWidget->setStyleSheet("QScrollBar:vertical { width:8px; background:rgba(0,0,0,0%); margin:0px,0px,0px,0px; padding-top:9px; padding-bottom:9px; } QScrollBar::handle:vertical { width:8px; background:rgba(0,0,0,25%); border-radius:4px; min-height:20; } QScrollBar::handle:vertical:hover { width:8px; background:rgba(0,0,0,50%); border-radius:4px; min-height:20; } QScrollBar::add-line:vertical { height:9px;width:8px; border-image:url(:/images/a/3.png); subcontrol-position:bottom; } QScrollBar::sub-line:vertical { height:9px;width:8px; border-image:url(:/images/a/1.png); subcontrol-position:top; } QScrollBar::add-line:vertical:hover { height:9px;width:8px; border-image:url(:/images/a/4.png); subcontrol-position:bottom; } QScrollBar::sub-line:vertical:hover { height:9px;width:8px; border-image:url(:/images/a/2.png); subcontrol-position:top; } QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:rgba(0,0,0,10%); border-radius:4px; }");

    QFont te_font = this->font();
    te_font.setFamily("MicrosoftYaHei");
    te_font.setPointSize(12);

    ui->listWidget->setFont(te_font);

    ui->tabWidget->setCurrentIndex(1);
    ui->tabWidget->setCurrentIndex(0);
}


void Widget::cameraImageCapture(QVideoFrame frame)
{
    //qDebug() << QThread::currentThreadId() << this;
    //这个和消息发送模块的步骤基本一样
	//处理消息发送线程的一共有三层：ui线程，sendimg线程，tcpsocket线程，这三个线程的关系类似于mvc架构
	//ui线程：发送pushImg信号，调用sendimg中的ImageCapture，将消息转发
	//sendimg线程：ImageCapture函数把当前视频帧进行压缩，存入imgqueue视频帧队列，run函数循环读取imgqueue，不为空时说明有新消息进入，此时pop_front，构造符合tcp_socket线程的消息结构体，将该结构体加入net_thread的消息队列queue_send（net_thread主要实现的就是消息队列，此处的作用是建立起了send_text线程和tcpsocket线程发送信息的桥梁）
    //tcpsocket线程：run函数循环pop出queue_send中的数据，如果有，则调用senddata按照正常流程发送（注意解决字节序，粘包拆包问题）
    //这里需要注意的是：视频发送和消息发送两个线程的数据最终都是统一push进queue_send队列，而不是直接write，为了防止某个线程过长时间占用网络资源导致另一个线程一直阻塞
    if(frame.isValid() && frame.isReadable())
    {
        QImage videoImg = QImage(frame.bits(), frame.width(), frame.height(), QVideoFrame::imageFormatFromPixelFormat(frame.pixelFormat()));
        //此处做一个坐标系转换
        QTransform matrix;
        matrix.rotate(180.0);
        //矩阵转换
        QImage img =  videoImg.transformed(matrix, Qt::FastTransformation).scaled(ui->mainshow_label->size());

        //肯定是房间内有人才发给服务器
        if(partner.size() > 1)
        {
            //捕获到的视频帧交给sendimg处理
			emit pushImg(img);
        }

        if(_mytcpSocket->getlocalip() == mainip)
        {
            ui->mainshow_label->setPixmap(QPixmap::fromImage(img).scaled(ui->mainshow_label->size()));
        }
        //自己看自己
        Partner *p = partner[_mytcpSocket->getlocalip()];
        if(p) p->setpic(img);

        //qDebug()<< "format: " <<  videoImg.format() << "size: " << videoImg.size() << "byteSIze: "<< videoImg.sizeInBytes();
    }
    //释放内存
    frame.unmap();
}

Widget::~Widget()
{
    //终止底层发送与接收线程

    if(_mytcpSocket->isRunning())
    {
        _mytcpSocket->stopImmediately();
        _mytcpSocket->wait();
    }

    //终止接收处理线程
    if(_recvThread->isRunning())
    {
        _recvThread->stopImmediately();
        _recvThread->wait();
    }

    if(_imgThread->isRunning())
    {
        _imgThread->quit();
        _imgThread->wait();
    }

    if(_sendImg->isRunning())
    {
        _sendImg->stopImmediately();
        _sendImg->wait();
    }

    if(_textThread->isRunning())
    {
        _textThread->quit();
        _textThread->wait();
    }

    if(_sendText->isRunning())
    {
        _sendText->stopImmediately();
        _sendText->wait();
    }
    
    if (_ainputThread->isRunning())
    {
        _ainputThread->quit();
        _ainputThread->wait();
    }

    if (_aoutput->isRunning())
    {
        _aoutput->stopImmediately();
        _aoutput->wait();
    }
    WRITE_LOG("-------------------Application End-----------------");

    //关闭日志
    if(logqueue->isRunning())
    {
        logqueue->stopImmediately();
        logqueue->wait();
    }

    delete ui;
}

void Widget::on_createmeetBtn_clicked()
{
    if(false == _createmeet)
    {
        ui->createmeetBtn->setDisabled(true);
        ui->openAudio->setDisabled(true);
        ui->openVedio->setDisabled(true);
        ui->exitmeetBtn->setDisabled(true);
        emit PushText(CREATE_MEETING); //将 “创建会议"加入到发送队列
        WRITE_LOG("create meeting");
    }
}

void Widget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    /*
     * 触发事件(3条， 一般使用第二条进行触发)
     * 1. 窗口部件第一次显示时，系统会自动产生一个绘图事件。从而强制绘制这个窗口部件，主窗口起来会绘制一次
     * 2. 当重新调整窗口部件的大小时，系统也会产生一个绘制事件--QWidget::update()或者QWidget::repaint()
     * 3. 当窗口部件被其它窗口部件遮挡，然后又再次显示出来时，就会对那些隐藏的区域产生一个绘制事件
    */
}


//退出会议（1，加入的会议， 2，自己创建的会议）
void Widget::on_exitmeetBtn_clicked()
{
    if(_camera->status() == QCamera::ActiveStatus)
    {
        _camera->stop();
    }

    ui->createmeetBtn->setDisabled(true);
    ui->exitmeetBtn->setDisabled(true);
    _createmeet = false;
    _joinmeet = false;
    //-----------------------------------------
    //清空partner
    clearPartner();
    // 关闭套接字

    //关闭socket
    _mytcpSocket->disconnectFromHost();
    _mytcpSocket->wait();

    ui->outlog->setText(tr("已退出会议"));

    ui->connServer->setDisabled(false);
    ui->groupBox_2->setTitle(QString("主屏幕"));
//    ui->groupBox->setTitle(QString("副屏幕"));
    //清除聊天记录
    while(ui->listWidget->count() > 0)
    {
        QListWidgetItem *item = ui->listWidget->takeItem(0);
        ChatMessage *chat = (ChatMessage *) ui->listWidget->itemWidget(item);
        delete item;
        delete chat;
    }
    iplist.clear();
    ui->plainTextEdit->setCompleter(iplist);


    WRITE_LOG("exit meeting");

    QMessageBox::warning(this, "Information", "退出会议" , QMessageBox::Yes, QMessageBox::Yes);

    //-----------------------------------------
}

void Widget::on_openVedio_clicked()
{
    if(_camera->status() == QCamera::ActiveStatus)
    {
        _camera->stop();
        WRITE_LOG("close camera");
        if(_camera->error() == QCamera::NoError)
        {
            _imgThread->quit();
            _imgThread->wait();
            ui->openVedio->setText("开启摄像头");
			emit PushText(CLOSE_CAMERA);
        }
        closeImg(_mytcpSocket->getlocalip());
    }
    else
    {
        _camera->start(); //开启摄像头
        WRITE_LOG("open camera");
        if(_camera->error() == QCamera::NoError)
        {
            _imgThread->start();
            ui->openVedio->setText("关闭摄像头");
        }
    }
}


void Widget::on_openAudio_clicked()
{
    if (!_createmeet && !_joinmeet) return;
    if (ui->openAudio->text().toUtf8() == QString(OPENAUDIO).toUtf8())
    {
        emit startAudio();
        ui->openAudio->setText(QString(CLOSEAUDIO).toUtf8());
    }
    else if(ui->openAudio->text().toUtf8() == QString(CLOSEAUDIO).toUtf8())
    {
        emit stopAudio();
        ui->openAudio->setText(QString(OPENAUDIO).toUtf8());
    }
}

void Widget::closeImg(quint32 ip)
{
    if (!partner.contains(ip))
    {
        qDebug() << "close img error";
        return;
    }
    Partner * p = partner[ip];
    p->setpic(QImage(":/myImage/1.jpg"));

    if(mainip == ip)
    {
        ui->mainshow_label->setPixmap(QPixmap::fromImage(QImage(":/myImage/1.jpg").scaled(ui->mainshow_label->size())));
    }
}

void Widget::on_connServer_clicked()
{
    QString ip = ui->ip->text(), port = ui->port->text();
    ui->outlog->setText("正在连接到" + ip + ":" + port);
    repaint();

    QRegExp ipreg("((2{2}[0-3]|2[01][0-9]|1[0-9]{2}|0?[1-9][0-9]|0{0,2}[1-9])\\.)((25[0-5]|2[0-4][0-9]|[01]?[0-9]{0,2})\\.){2}(25[0-5]|2[0-4][0-9]|[01]?[0-9]{1,2})");

    QRegExp portreg("^([0-9]|[1-9]\\d|[1-9]\\d{2}|[1-9]\\d{3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])$");
    QRegExpValidator ipvalidate(ipreg), portvalidate(portreg);
    int pos = 0;
    if(ipvalidate.validate(ip, pos) != QValidator::Acceptable)
    {
        QMessageBox::warning(this, "Input Error", "Ip Error", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }
    if(portvalidate.validate(port, pos) != QValidator::Acceptable)
    {
        QMessageBox::warning(this, "Input Error", "Port Error", QMessageBox::Yes, QMessageBox::Yes);
        return;
    }

    if(_mytcpSocket ->connectToServer(ip, port, QIODevice::ReadWrite))
    {
        ui->outlog->setText("成功连接到" + ip + ":" + port);
        ui->openAudio->setDisabled(true);
        ui->openVedio->setDisabled(true);
        ui->createmeetBtn->setDisabled(false);
        ui->exitmeetBtn->setDisabled(true);
		ui->joinmeetBtn->setDisabled(false);
		WRITE_LOG("succeeed connecting to %s:%s", ip.toStdString().c_str(), port.toStdString().c_str());
        QMessageBox::warning(this, "Connection success", "成功连接服务器" , QMessageBox::Yes, QMessageBox::Yes);
        ui->sendmsg->setDisabled(false);
        ui->connServer->setDisabled(true);
    }
    else
    {
        ui->outlog->setText("连接失败,请重新连接...");
        WRITE_LOG("failed to connenct %s:%s", ip.toStdString().c_str(), port.toStdString().c_str());
        QMessageBox::warning(this, "Connection error", _mytcpSocket->errorString() , QMessageBox::Yes, QMessageBox::Yes);
    }
}


void Widget::cameraError(QCamera::Error)
{
    QMessageBox::warning(this, "Camera error", _camera->errorString() , QMessageBox::Yes, QMessageBox::Yes);
}

void Widget::audioError(QString err)
{
    QMessageBox::warning(this, "Audio error", err, QMessageBox::Yes);
}

void Widget::datasolve(MESG *msg)
{
    if(msg->msg_type == CREATE_MEETING_RESPONSE)
    {
        int roomno;
        memcpy(&roomno, msg->data, msg->len);

        if(roomno != 0)
        {
            QMessageBox::information(this, "Room No", QString("房间号：%1").arg(roomno), QMessageBox::Yes, QMessageBox::Yes);

            ui->groupBox_2->setTitle(QString("主屏幕(房间号: %1)").arg(roomno));
            ui->outlog->setText(QString("创建成功 房间号: %1").arg(roomno) );
            _createmeet = true;
            ui->exitmeetBtn->setDisabled(false);
            ui->openVedio->setDisabled(false);
            ui->joinmeetBtn->setDisabled(true);

            WRITE_LOG("succeed creating room %d", roomno);
            //添加用户自己
            addPartner(_mytcpSocket->getlocalip());
            mainip = _mytcpSocket->getlocalip();
            ui->groupBox_2->setTitle(QHostAddress(mainip).toString());
            ui->mainshow_label->setPixmap(QPixmap::fromImage(QImage(":/myImage/1.jpg").scaled(ui->mainshow_label->size())));
        }
        else
        {
            _createmeet = false;
            QMessageBox::information(this, "Room Information", QString("无可用房间"), QMessageBox::Yes, QMessageBox::Yes);
            ui->outlog->setText(QString("无可用房间"));
            ui->createmeetBtn->setDisabled(false);
            WRITE_LOG("no empty room");
        }
    }
    else if(msg->msg_type == JOIN_MEETING_RESPONSE)
    {
        qint32 c;
        memcpy(&c, msg->data, msg->len);
        if(c == 0)
        {
            QMessageBox::information(this, "Meeting Error", tr("会议不存在") , QMessageBox::Yes, QMessageBox::Yes);
            ui->outlog->setText(QString("会议不存在"));
            WRITE_LOG("meeting not exist");
            ui->exitmeetBtn->setDisabled(true);
            ui->openVedio->setDisabled(true);
            ui->joinmeetBtn->setDisabled(false);
            ui->connServer->setDisabled(true);
            _joinmeet = false;
        }
        else if(c == -1)
        {
            QMessageBox::warning(this, "Meeting information", "成员已满，无法加入" , QMessageBox::Yes, QMessageBox::Yes);
            ui->outlog->setText(QString("成员已满，无法加入"));
            WRITE_LOG("full room, cannot join");
        }
        else if (c > 0)
        {
            QMessageBox::warning(this, "Meeting information", "加入成功" , QMessageBox::Yes, QMessageBox::Yes);
            ui->outlog->setText(QString("加入成功"));
            WRITE_LOG("succeed joining room");
            //添加用户自己
            addPartner(_mytcpSocket->getlocalip());
            mainip = _mytcpSocket->getlocalip();
            ui->groupBox_2->setTitle(QHostAddress(mainip).toString());
            ui->mainshow_label->setPixmap(QPixmap::fromImage(QImage(":/myImage/1.jpg").scaled(ui->mainshow_label->size())));
            ui->joinmeetBtn->setDisabled(true);
            ui->exitmeetBtn->setDisabled(false);
            ui->createmeetBtn->setDisabled(true);
            _joinmeet = true;
        }
    }
    else if(msg->msg_type == IMG_RECV)
    {
        QHostAddress a(msg->ip);
        qDebug() << a.toString();
        QImage img;
        img.loadFromData(msg->data, msg->len);
        if(partner.count(msg->ip) == 1)
        {
            Partner* p = partner[msg->ip];
            p->setpic(img);
        }
        else
        {
            Partner* p = addPartner(msg->ip);
            p->setpic(img);
        }

        if(msg->ip == mainip)
        {
            ui->mainshow_label->setPixmap(QPixmap::fromImage(img).scaled(ui->mainshow_label->size()));
        }
        repaint();
    }
    else if(msg->msg_type == TEXT_RECV)
    {
        QString str = QString::fromStdString(std::string((char *)msg->data, msg->len));
        //qDebug() << str;
        QString time = QString::number(QDateTime::currentDateTimeUtc().toTime_t());
        ChatMessage *message = new ChatMessage(ui->listWidget);
        QListWidgetItem *item = new QListWidgetItem();
        dealMessageTime(time);
        dealMessage(message, item, str, time, QHostAddress(msg->ip).toString() ,ChatMessage::User_She);
        if(str.contains('@' + QHostAddress(_mytcpSocket->getlocalip()).toString()))
        {
            QSound::play(":/myEffect/2.wav");
        }
    }
    else if(msg->msg_type == PARTNER_JOIN)
    {
        Partner* p = addPartner(msg->ip);
        if(p)
        {
            p->setpic(QImage(":/myImage/1.jpg"));
            ui->outlog->setText(QString("%1 join meeting").arg(QHostAddress(msg->ip).toString()));
            iplist.append(QString("@") + QHostAddress(msg->ip).toString());
            ui->plainTextEdit->setCompleter(iplist);
        }
    }
    else if(msg->msg_type == PARTNER_EXIT)
    {
        removePartner(msg->ip);
        if(mainip == msg->ip)
        {
            ui->mainshow_label->setPixmap(QPixmap::fromImage(QImage(":/myImage/1.jpg").scaled(ui->mainshow_label->size())));
        }
        if(iplist.removeOne(QString("@") + QHostAddress(msg->ip).toString()))
        {
            ui->plainTextEdit->setCompleter(iplist);
        }
        else
        {
            qDebug() << QHostAddress(msg->ip).toString() << "not exist";
            WRITE_LOG("%s not exist",QHostAddress(msg->ip).toString().toStdString().c_str());
        }
        ui->outlog->setText(QString("%1 exit meeting").arg(QHostAddress(msg->ip).toString()));
    }
    else if (msg->msg_type == CLOSE_CAMERA)
    {
	    closeImg(msg->ip);
    }
    else if (msg->msg_type == PARTNER_JOIN2)
    {
        uint32_t ip;
        int other = msg->len / sizeof(uint32_t), pos = 0;
        for (int i = 0; i < other; i++)
        {
            memcpy_s(&ip, sizeof(uint32_t), msg->data + pos , sizeof(uint32_t));
            pos += sizeof(uint32_t);
			Partner* p = addPartner(ip);
            if (p)
            {
                p->setpic(QImage(":/myImage/1.jpg"));
                iplist << QString("@") + QHostAddress(ip).toString();
            }
        }
        ui->plainTextEdit->setCompleter(iplist);
        ui->openVedio->setDisabled(false);
    }
    else if(msg->msg_type == RemoteHostClosedError)
    {

        clearPartner();
        _mytcpSocket->disconnectFromHost();
        _mytcpSocket->wait();
        ui->outlog->setText(QString("关闭与服务器的连接"));
        ui->createmeetBtn->setDisabled(true);
        ui->exitmeetBtn->setDisabled(true);
        ui->connServer->setDisabled(false);
        ui->joinmeetBtn->setDisabled(true);
        //清除聊天记录
        while(ui->listWidget->count() > 0)
        {
            QListWidgetItem *item = ui->listWidget->takeItem(0);
            ChatMessage *chat = (ChatMessage *)ui->listWidget->itemWidget(item);
            delete item;
            delete chat;
        }
        iplist.clear();
        ui->plainTextEdit->setCompleter(iplist);
        if(_createmeet || _joinmeet) QMessageBox::warning(this, "Meeting Information", "会议结束" , QMessageBox::Yes, QMessageBox::Yes);
    }
    else if(msg->msg_type == OtherNetError)
    {
        QMessageBox::warning(NULL, "Network Error", "网络异常" , QMessageBox::Yes, QMessageBox::Yes);
        clearPartner();
        _mytcpSocket->disconnectFromHost();
        _mytcpSocket->wait();
        ui->outlog->setText(QString("网络异常......"));
    }
    if(msg->data)
    {
        free(msg->data);
        msg->data = NULL;
    }
    if(msg)
    {
        free(msg);
        msg = NULL;
    }
}

Partner* Widget::addPartner(quint32 ip)
{
	if (partner.contains(ip)) return NULL;
    Partner *p = new Partner(ui->scrollAreaWidgetContents ,ip);
    if (p == NULL)
    {
        qDebug() << "new Partner error";
        return NULL;
    }
    else
    {
		connect(p, SIGNAL(sendip(quint32)), this, SLOT(recvip(quint32)));
		partner.insert(ip, p);
        //往副屏幕加入Partner对象
		ui->verticalLayout_3->addWidget(p, 1);

		//当有人员加入时，开启滑动条滑动事件，开启输入(只有自己时，不打开)
        if (partner.size() > 1)
        {
			connect(this, SIGNAL(volumnChange(int)), _ainput, SLOT(setVolumn(int)), Qt::UniqueConnection);
			connect(this, SIGNAL(volumnChange(int)), _aoutput, SLOT(setVolumn(int)), Qt::UniqueConnection);
            ui->openAudio->setDisabled(false);
            ui->sendmsg->setDisabled(false);
            _aoutput->startPlay();
        }
		return p;
    }
}

void Widget::removePartner(quint32 ip)
{
    if(partner.contains(ip))
    {
        Partner *p = partner[ip];
        disconnect(p, SIGNAL(sendip(quint32)), this, SLOT(recvip(quint32)));
        ui->verticalLayout_3->removeWidget(p);
        delete p;
        partner.remove(ip);

        //只有自已一个人时，关闭传输音频
        if (partner.size() <= 1)
        {
			disconnect(_ainput, SLOT(setVolumn(int)));
			disconnect(_aoutput, SLOT(setVolumn(int)));
			_ainput->stopCollect();
            _aoutput->stopPlay();
            ui->openAudio->setText(QString(OPENAUDIO).toUtf8());
            ui->openAudio->setDisabled(true);
        }
    }
}

void Widget::clearPartner()
{
    ui->mainshow_label->setPixmap(QPixmap());
    if(partner.size() == 0) return;

    QMap<quint32, Partner*>::iterator iter =   partner.begin();
    while (iter != partner.end())
    {
        quint32 ip = iter.key();
        iter++;
        Partner *p =  partner.take(ip);
        ui->verticalLayout_3->removeWidget(p);
        delete p;
        p = nullptr;
    }

    //关闭传输音频
	disconnect(_ainput, SLOT(setVolumn(int)));
    disconnect(_aoutput, SLOT(setVolumn(int)));
    //关闭音频播放与采集
	_ainput->stopCollect();
    _aoutput->stopPlay();
	ui->openAudio->setText(QString(CLOSEAUDIO).toUtf8());
	ui->openAudio->setDisabled(true);
    

    //关闭图片传输线程
    if(_imgThread->isRunning())
    {
        _imgThread->quit();
        _imgThread->wait();
    }
    ui->openVedio->setText(QString(OPENVIDEO).toUtf8());
    ui->openVedio->setDisabled(true);
}

void Widget::recvip(quint32 ip)
{
    if (partner.contains(mainip))
    {
        Partner* p = partner[mainip];
        p->setStyleSheet("border-width: 1px; border-style: solid; border-color:rgba(0, 0 , 255, 0.7)");
    }
	if (partner.contains(ip))
	{
		Partner* p = partner[ip];
		p->setStyleSheet("border-width: 1px; border-style: solid; border-color:rgba(255, 0 , 0, 0.7)");
	}
	ui->mainshow_label->setPixmap(QPixmap::fromImage(QImage(":/myImage/1.jpg").scaled(ui->mainshow_label->size())));
    mainip = ip;
    ui->groupBox_2->setTitle(QHostAddress(mainip).toString());
    qDebug() << ip;
}

/*
 * 加入会议
 */

void Widget::on_joinmeetBtn_clicked()
{
    QString roomNo = ui->meetno->text();

    QRegExp roomreg("^[1-9][0-9]{1,4}$");
    QRegExpValidator  roomvalidate(roomreg);
    int pos = 0;
//     if(roomvalidate.validate(roomNo, pos) != QValidator::Acceptable)
//     {
//         QMessageBox::warning(this, "RoomNo Error", "房间号不合法" , QMessageBox::Yes, QMessageBox::Yes);
//     }
//     else
//     {
//         //加入发送队列
//         emit PushText(JOIN_MEETING, roomNo);
//     }
    emit PushText(JOIN_MEETING, roomNo);
}

void Widget::on_horizontalSlider_valueChanged(int value)
{
    emit volumnChange(value);
}

void Widget::speaks(QString ip)
{
    ui->outlog->setText(QString(ip + " 正在说话").toUtf8());
}

//灵感源于sh80
void Widget::on_sendmsg_clicked()
{
    //处理消息发送线程的一共有三层：ui线程，sendtext线程，tcpsocket线程，这三个线程的关系类似于mvc架构
    //ui线程：发送pushText信号，调用sendtext中的push_text，将消息转发给sendtext
    //sendtext线程：push_text函数将收到的消息发送给线程中的textqueue文本队列，run函数循环读取textqueue，不为空时说明有新消息进入，此时pop_front，构造符合tcp_socket线程的消息结构体，将该结构体加入net_thread的消息队列queue_send（net_thread主要实现的就是消息队列，此处的作用是建立起了send_text线程和tcpsocket线程发送信息的桥梁）
    //tcpsocket线程：run函数循环pop出queue_send中的数据，如果有，则调用senddata按照正常流程发送（注意解决字节序，粘包拆包问题）
    QString msg = ui->plainTextEdit->toPlainText().trimmed();
    if(msg.size() == 0)
    {
        qDebug() << "empty";
        return;
    }
    qDebug()<<msg;
    ui->plainTextEdit->setPlainText("");
    QString time = QString::number(QDateTime::currentDateTimeUtc().toTime_t());
    ChatMessage *message = new ChatMessage(ui->listWidget);
    QListWidgetItem *item = new QListWidgetItem();
    dealMessageTime(time);
    dealMessage(message, item, msg, time, QHostAddress(_mytcpSocket->getlocalip()).toString() ,ChatMessage::User_Me);
    emit PushText(TEXT_SEND, msg);
    ui->sendmsg->setDisabled(true);
}

void Widget::dealMessage(ChatMessage *messageW, QListWidgetItem *item, QString text, QString time, QString ip ,ChatMessage::User_Type type)
{
    ui->listWidget->addItem(item);
    messageW->setFixedWidth(ui->listWidget->width());
    QSize size = messageW->fontRect(text);
    item->setSizeHint(size);
    messageW->setText(text, time, size, ip, type);
    ui->listWidget->setItemWidget(item, messageW);
}

void Widget::dealMessageTime(QString curMsgTime)
{
    bool isShowTime = false;
    if(ui->listWidget->count() > 0) {
        QListWidgetItem* lastItem = ui->listWidget->item(ui->listWidget->count() - 1);
        ChatMessage* messageW = (ChatMessage *)ui->listWidget->itemWidget(lastItem);
        int lastTime = messageW->time().toInt();
        int curTime = curMsgTime.toInt();
        qDebug() << "curTime lastTime:" << curTime - lastTime;
        isShowTime = ((curTime - lastTime) > 60); // 两个消息相差一分钟
//        isShowTime = true;
    } else {
        isShowTime = true;
    }
    if(isShowTime) {
        ChatMessage* messageTime = new ChatMessage(ui->listWidget);
        QListWidgetItem* itemTime = new QListWidgetItem();
        ui->listWidget->addItem(itemTime);
        QSize size = QSize(ui->listWidget->width() , 40);
        messageTime->resize(size);
        itemTime->setSizeHint(size);
        messageTime->setText(curMsgTime, curMsgTime, size);
        ui->listWidget->setItemWidget(itemTime, messageTime);
    }
}

void Widget::textSend()
{
    qDebug() << "send text over";
    QListWidgetItem* lastItem = ui->listWidget->item(ui->listWidget->count() - 1);
    ChatMessage* messageW = (ChatMessage *)ui->listWidget->itemWidget(lastItem);
    messageW->setTextSuccess();
    ui->sendmsg->setDisabled(false);
}
