#ifndef WIDGET_H
#define WIDGET_H

#include "AudioInput.h"
#include <QWidget>
#include <QVideoFrame>
#include <QTcpSocket>
#include "mytcpsocket.h"
#include <QCamera>
#include "sendtext.h"
#include "recvsolve.h"
#include "partner.h"
#include "netheader.h"
#include <QMap>
#include "AudioOutput.h"
#include "chatmessage.h"
#include <QStringListModel>

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class QCameraImageCapture;
class MyVideoSurface;
class SendImg;
class QListWidgetItem;
class Widget : public QWidget
{
    Q_OBJECT
private:
    static QRect pos;
    quint32 mainip; //主屏幕显示的IP图像
    QCamera *_camera; //摄像头
    QCameraImageCapture *_imagecapture; //截屏
    bool  _createmeet; //是否创建会议
    bool _joinmeet; // 加入会议
    bool _openCamera; //是否开启摄像头

    MyVideoSurface *_myvideosurface;

    QVideoFrame mainshow;

    SendImg *_sendImg;
    QThread *_imgThread;

    SendText * _sendText;
    QThread *_textThread;



    RecvSolve *_recvThread;

    //socket
    MyTcpSocket * _mytcpSocket;
    void paintEvent(QPaintEvent *event);

    QMap<quint32, Partner *> partner; //用于记录房间用户
    Partner* addPartner(quint32);
    void removePartner(quint32);
    void clearPartner(); //退出会议，或者会议结束
    void closeImg(quint32); //根据IP重置图像

    void dealMessage(ChatMessage *messageW, QListWidgetItem *item, QString text, QString time, QString ip ,ChatMessage::User_Type type); //用户发送文本
    void dealMessageTime(QString curMsgTime); //处理时间

    //音频
    AudioInput* _ainput;
    QThread* _ainputThread;
    AudioOutput* _aoutput;

    QStringList iplist;

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    //创建会议
    void on_createmeetBtn_clicked();
    //退出会议
    void on_exitmeetBtn_clicked();
    //开启摄像头
    void on_openVedio_clicked();
    //开启音频
    void on_openAudio_clicked();
    //连接服务器
    void on_connServer_clicked();

    void cameraError(QCamera::Error);
    void audioError(QString);
//    void mytcperror(QAbstractSocket::SocketError);
    //接收信息
    void datasolve(MESG *);
    void recvip(quint32);
    //视频发送模块核心，捕获摄像头帧画面
    void cameraImageCapture(QVideoFrame frame);
    //加入会议
    void on_joinmeetBtn_clicked();

    void on_horizontalSlider_valueChanged(int value);
    void speaks(QString);

    //消息发送模块的核心
    void on_sendmsg_clicked();

    void textSend();

signals:
    void pushImg(QImage);
    void PushText(MSG_TYPE, QString = "");
    void stopAudio();
    void startAudio();
    void volumnChange(int);
private:
    Ui::Widget *ui;
};
#endif // WIDGET_H
