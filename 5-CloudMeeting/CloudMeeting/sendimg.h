#ifndef SENDIMG_H
#define SENDIMG_H

#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QImage>
#include <QVideoFrame>
#include <QWaitCondition>

//图片发送，由于视频传输是按帧来传，每一帧相当于一张图，此处是对图片进行处理，封装，校验
//该类其实也属于中间层
class SendImg : public QThread
{
    Q_OBJECT
private:
    QQueue<QByteArray> imgqueue;
    QMutex queue_lock;
    QWaitCondition queue_waitCond;
    void run() override;
    QMutex m_lock;
    volatile bool m_isCanRun;
public:
    SendImg(QObject *par = NULL);

    void pushToQueue(QImage);
public slots:
    void ImageCapture(QImage); //捕获到视频帧
    void clearImgQueue(); //线程结束时，清空视频帧队列
    void stopImmediately();
};

#endif // SENDIMG_H
