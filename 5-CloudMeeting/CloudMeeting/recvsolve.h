#ifndef RECVSOLVE_H
#define RECVSOLVE_H
#include "netheader.h"
#include <QThread>
#include <QMutex>
/*
 * 接收线程
 * 从接收队列拿取数据
 * 数据接收模块，处于通信与界面之间的那一层，应该叫control中间层比较合适吧
 */
class RecvSolve : public QThread
{
    Q_OBJECT
public:
    RecvSolve(QObject *par = NULL);
    void run() override;
private:
    QMutex m_lock;
    bool m_isCanRun;
signals:
    void datarecv(MESG *);
public slots:
    void stopImmediately();
};

#endif // RECVSOLVE_H
