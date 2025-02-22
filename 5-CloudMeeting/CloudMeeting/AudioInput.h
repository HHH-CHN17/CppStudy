#pragma once

#include <QObject>
#include <QThread>
#include <QAudioInput>
#include <QIODevice>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include "netheader.h"

//录音功能，与摄像头camera一起使用，不过camera在qt里面自带，所以不需要写camera的源码
class AudioInput : public QThread
{
	Q_OBJECT
private:
	QAudioInput *audio;
	QIODevice* inputdevice;
	char* recvbuf;
	void run() override;
	QQueue<MESG*> audioque;
	QMutex queue_lock;
	QWaitCondition queue_waitCond; 
	QMutex m_lock;
	volatile bool m_isCanRun;
public:
	AudioInput(QObject *par = 0);
	~AudioInput();
private slots:
	void onreadyRead();
	void handleStateChanged(QAudio::State);
	QString errorString();
	void setVolumn(int);
public slots:
	void startCollect();
	void stopCollect();
signals:
	void audioinputerror(QString);
};
