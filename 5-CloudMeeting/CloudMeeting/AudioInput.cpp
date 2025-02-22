#include "AudioInput.h"
#include "netheader.h"
#include <QAudioFormat>
#include <QDebug>
#include <QThread>
#include <QFile>

//用于获取音频数据，并通过a线程发送至缓存消息队列，再由b线程发送至网络消息队列

extern QUEUE_DATA<MESG> queue_send;
extern QUEUE_DATA<MESG> queue_recv;

AudioInput::AudioInput(QObject* parent)
	: QThread(parent)
{
	recvbuf = (char*)malloc(MB * 2);
	QAudioFormat format;
	//set format
	format.setSampleRate(8000);
	format.setChannelCount(1);
	format.setSampleSize(16);
	format.setCodec("audio/pcm");
	format.setByteOrder(QAudioFormat::LittleEndian);
	format.setSampleType(QAudioFormat::UnSignedInt);

	QAudioDeviceInfo info = QAudioDeviceInfo::defaultInputDevice();
	if (!info.isFormatSupported(format))
	{
		qWarning() << "Default format not supported, trying to use the nearest.";
		format = info.nearestFormat(format);
	}
	audio = new QAudioInput(format, this);

	connect(audio, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateChanged(QAudio::State)));
}

AudioInput::~AudioInput()
{
	delete audio;
}

 void AudioInput::run()
 {
 	WRITE_LOG("start sending Audio thread: 0x%p", QThread::currentThreadId());
 	m_isCanRun = true;
 	for (;;)
 	{
 		queue_lock.lock(); //加锁
 
 		while (audioque.size() == 0)
 		{
 			//qDebug() << this << QThread::currentThreadId();
 			bool f = queue_waitCond.wait(&queue_lock, WAITSECONDS * 1000);
 			if (f == false) //timeout
 			{
 				QMutexLocker locker(&m_lock);//	函数退出时m_lock自动解锁
 				if (m_isCanRun == false)
 				{
 					queue_lock.unlock();
 					WRITE_LOG("stop sending Audio thread: 0x%p", QThread::currentThreadId());
 					return;
 				}
 			}
 		}
 
 		MESG* audiomsg = new MESG(*audioque.front());
 		//        qDebug() << "取出队列:" << QThread::currentThreadId();
 		audioque.pop_front();
 		queue_lock.unlock();//解锁
 		queue_waitCond.wakeOne(); //唤醒添加线程
 
		queue_send.push_msg(audiomsg);
		//queue_send.push_msg(audiomsg);
 
 	}
 }

void AudioInput::startCollect()
{
	if (audio->state() == QAudio::ActiveState) return;
	WRITE_LOG("start collecting audio");
	inputdevice = audio->start();
	connect(inputdevice, SIGNAL(readyRead()), this, SLOT(onreadyRead()));
}

void AudioInput::stopCollect()
{
	if (audio->state() == QAudio::StoppedState) return;
	disconnect(this, SLOT(onreadyRead()));
	audio->stop();
	WRITE_LOG("stop collecting audio");
	inputdevice = nullptr;

	QMutexLocker locker(&m_lock);
	m_isCanRun = false;
}

void AudioInput::onreadyRead()
{
	static int num = 0, totallen  = 0;
	if (inputdevice == nullptr) return;
	//消息帧规定为4MB，所以要读两次
	int len = inputdevice->read(recvbuf + totallen, 2 * MB - totallen);
	if (num < 2)
	{
		totallen += len;
		num++;
		return;
	}
	totallen += len;
	qDebug() << "totallen = " << totallen;
	MESG* msg = (MESG*)malloc(sizeof(MESG));
	if (msg == nullptr)
	{
		qWarning() << __LINE__ << "malloc fail";
	}
	else
	{
		memset(msg, 0, sizeof(MESG));
		msg->msg_type = AUDIO_SEND;
		//压缩数据，转base64
		QByteArray rr(recvbuf, totallen);
		QByteArray cc = qCompress(rr).toBase64();

		msg->len = cc.size();

		msg->data = (uchar*)malloc(msg->len);
		if (msg->data == nullptr)
		{
			qWarning() << "malloc mesg.data fail";
		}
		else
		{
			memset(msg->data, 0, msg->len);
			memcpy_s(msg->data, msg->len, cc.data(), cc.size());
			//queue_send.push_msg(msg);

			queue_lock.lock();
			while (audioque.size() > QUEUE_MAXSIZE)
			{
				queue_waitCond.wait(&queue_lock);
			}
			audioque.push_back(msg);
			queue_lock.unlock();
			queue_waitCond.wakeOne();

		}
	}
	totallen = 0;
	num = 0;
}

QString AudioInput::errorString()
{
	if (audio->error() == QAudio::OpenError)
	{
		return QString("AudioInput An error occurred opening the audio device").toUtf8();
	}
	else if (audio->error() == QAudio::IOError)
	{
		return QString("AudioInput An error occurred during read/write of audio device").toUtf8();
	}
	else if (audio->error() == QAudio::UnderrunError)
	{
		return QString("AudioInput Audio data is not being fed to the audio device at a fast enough rate").toUtf8();
	}
	else if (audio->error() == QAudio::FatalError)
	{
		return QString("AudioInput A non-recoverable error has occurred, the audio device is not usable at this time.");
	}
	else
	{
		return QString("AudioInput No errors have occurred").toUtf8();
	}
}

void AudioInput::handleStateChanged(QAudio::State newState)
{
	switch (newState)
	{
		case QAudio::StoppedState:
			if (audio->error() != QAudio::NoError)
			{
				stopCollect();
				emit audioinputerror(errorString());
			}
			else
			{
				qWarning() << "stop recording";
			}
			break;
		case QAudio::ActiveState:
			//start recording
			qWarning() << "start recording";
			break;
		default:
			//
			break;
	}
}

void AudioInput::setVolumn(int v)
{
	qDebug() << v;
	audio->setVolume(v / 100.0);
}
