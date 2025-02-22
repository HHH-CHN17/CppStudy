#ifndef MYTCPSOCKET_H
#define MYTCPSOCKET_H

#include <QThread>
#include <QTcpSocket>
#include <QMutex>
#include "netheader.h"
#ifndef MB
#define MB (1024 * 1024)
#endif

typedef unsigned char uchar;

//显然易见，自定义的tcp协议
class MyTcpSocket: public QThread
{
    Q_OBJECT
public:
    ~MyTcpSocket();
    MyTcpSocket(QObject *par=NULL);
    //本函数用于建立客户端tcp连接，和demo里的不同，为了代码的健硕性需要多一些判断
    bool connectToServer(QString, QString, QIODevice::OpenModeFlag);
    QString errorString();
    void disconnectFromHost();
    quint32 getlocalip();
private:
    void run() override;
    qint64 readn(char *, quint64, int);
    QTcpSocket *_socktcp;
    QThread *_sockThread;
    uchar *sendbuf;
    uchar* recvbuf;
    quint64 hasrecvive;
 
    QMutex m_lock;
    volatile bool m_isCanRun;
private slots:
    bool connectServer(QString, QString, QIODevice::OpenModeFlag);
    void sendData(MESG *);
    void closeSocket();

public slots:
    //接收数据模块的核心，显然通过readyread信号调用
    void recvFromSocket();
    void stopImmediately();
    void errorDetect(QAbstractSocket::SocketError error);
signals:
    void socketerror(QAbstractSocket::SocketError);
    void sendTextOver();
};

#endif // MYTCPSOCKET_H
