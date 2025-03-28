#include "mytcpsocket.h"
#include "netheader.h"
#include <QHostAddress>
#include <QtEndian>
#include <QMetaObject>
#include <QMutexLocker>

extern QUEUE_DATA<MESG> queue_send;
extern QUEUE_DATA<MESG> queue_recv;
extern QUEUE_DATA<MESG> audio_recv;

void MyTcpSocket::stopImmediately()
{
    {
        QMutexLocker lock(&m_lock);
        if(m_isCanRun == true) m_isCanRun = false;
    }
    //关闭read
    _sockThread->quit();
    _sockThread->wait();
}

void MyTcpSocket::closeSocket()
{
	if (_socktcp && _socktcp->isOpen())
	{
		_socktcp->close();
	}
}

MyTcpSocket::MyTcpSocket(QObject *par):QThread(par)
{
    qRegisterMetaType<QAbstractSocket::SocketError>();
	_socktcp = nullptr;

    _sockThread = new QThread(); //发送数据线程
    this->moveToThread(_sockThread);
	connect(_sockThread, SIGNAL(finished()), this, SLOT(closeSocket()));
    sendbuf =(uchar *) malloc(4 * MB);
    recvbuf = (uchar*)malloc(4 * MB);
    hasrecvive = 0;

}


void MyTcpSocket::errorDetect(QAbstractSocket::SocketError error)
{
    qDebug() <<"Sock error" <<QThread::currentThreadId();
    MESG * msg = (MESG *) malloc(sizeof (MESG));
    if (msg == NULL)
    {
        qDebug() << "errdect malloc error";
    }
    else
    {
        memset(msg, 0, sizeof(MESG));
		if (error == QAbstractSocket::RemoteHostClosedError)
		{
			msg->msg_type = RemoteHostClosedError;
		}
		else
		{
			msg->msg_type = OtherNetError;
		}
		queue_recv.push_msg(msg);
    }
}



void MyTcpSocket::sendData(MESG* send)
{
	if (_socktcp->state() == QAbstractSocket::UnconnectedState)
	{
        emit sendTextOver();
		if (send->data) free(send->data);
		if (send) free(send);
		return;
	}
	quint64 bytestowrite = 0;
	//构造消息头，防止tcp粘包，拆包
	sendbuf[bytestowrite++] = '$';

	//消息类型，src是参数1，dest是参数2，转换成网络字节序，即大端序，显然msg_type占两个字节吧
	//sendbuf + bytestowrite这个是内存地址的写法，表示src从dest的内存地址开始往后写，相当于追加
	qToBigEndian<quint16>(send->msg_type, sendbuf + bytestowrite);
	bytestowrite += 2;

	//发送者ip
	//注意：Linux中，客户端在调用connect时自动分配ip地址和port
	//Qt中，客户端在调用connecttohost时自动分配ip和port
	quint32 ip = _socktcp->localAddress().toIPv4Address();
	qToBigEndian<quint32>(ip, sendbuf + bytestowrite);
	bytestowrite += 4;

    if (send->msg_type == CREATE_MEETING || send->msg_type == AUDIO_SEND || send->msg_type == CLOSE_CAMERA || send->msg_type == IMG_SEND || send->msg_type == TEXT_SEND) //创建会议,发送音频,关闭摄像头，发送图片
	{
		//发送数据大小
		qToBigEndian<quint32>(send->len, sendbuf + bytestowrite);
		bytestowrite += 4;
	}
	else if (send->msg_type == JOIN_MEETING)
	{
		qToBigEndian<quint32>(send->len, sendbuf + bytestowrite);
		bytestowrite += 4;
		uint32_t room;
		memcpy(&room, send->data, send->len);
		qToBigEndian<quint32>(room, send->data);
	}
	
	//将数据拷入sendbuf
	memcpy(sendbuf + bytestowrite, send->data, send->len);
	bytestowrite += send->len;
	sendbuf[bytestowrite++] = '#'; //结尾字符
	//经过以上处理，MESG结构体中的数据全部以大端序的方式按顺序存入了sendbuf中，这个时候就能正式发送数据了

	//----------------write to server-------------------------
	qint64 hastowrite = bytestowrite;
	qint64 ret = 0, haswrite = 0;
	//由于tcp是面向字节流传输，所以传了几次不重要，反正服务端那边都能完整收到
	while ((ret = _socktcp->write((char*)sendbuf + haswrite, hastowrite - haswrite)) < hastowrite)
	{
		if (ret == -1 && _socktcp->error() == QAbstractSocket::TemporaryError)
		{
            ret = 0;
		}
		else if (ret == -1)
		{
			qDebug() << "network error";
			break;
		}
		haswrite += ret;
		hastowrite -= ret;
	}

	//没有进入事件循环/非gui线程需要加此函数保证数据写入socket中
	_socktcp->waitForBytesWritten();

    if(send->msg_type == TEXT_SEND)
    {
        emit sendTextOver(); //成功往内核发送文本信息
    }


	if (send->data)
	{
		free(send->data);
	}
	//free
	if (send)
	{
		free(send);
	}
}

/*
 * 发送线程
 */
void MyTcpSocket::run()
{
    //qDebug() << "send data" << QThread::currentThreadId();
    m_isCanRun = true; //标记可以运行
    /*
    *$_MSGType_IPV4_MSGSize_data_# //
    * 1 2 4 4 MSGSize 1
    *底层写数据线程
    */
    for(;;)
    {
        {
            QMutexLocker locker(&m_lock);
            if(m_isCanRun == false) return; //在每次循环判断是否可以运行，如果不行就退出循环
        }
        
        //构造消息体
        MESG * send = queue_send.pop_msg();
        if(send == NULL) continue;
		//思路来源：https://blog.csdn.net/luoyayun361/article/details/97915133
		//https://www.cnblogs.com/senior-engineer/p/7922053.html
		//https://blog.csdn.net/baojiboy/article/details/119409522 这个写的比较好，可以看看这个
		//qt元对象系统，异步排队调用，保证tcp传输数据的效率
        QMetaObject::invokeMethod(this, "sendData", Q_ARG(MESG*, send));
    }
}


qint64 MyTcpSocket::readn(char * buf, quint64 maxsize, int n)
{
    quint64 hastoread = n;
    quint64 hasread = 0;
    do
    {
        qint64 ret  = _socktcp->read(buf + hasread, hastoread);
        if(ret < 0)
        {
            return -1;
        }
        if(ret == 0)
        {
            return hasread;
        }
        hasread += ret;
        hastoread -= ret;
    }while(hastoread > 0 && hasread < maxsize);
    return hasread;
}


void MyTcpSocket::recvFromSocket()
{

    //qDebug() << "recv data socket" <<QThread::currentThread();
    /*
    *$_msgtype_ip_size_data_#
    */
	//处理数据接收线程的一共有三层：tcpsocket线程，recvsolve线程，ui线程，这三个线程的关系类似于mvc架构
	//tcpsocket线程：按顺序解析数据，根据msg_type对不同类型数据做不同类型处理，将最终构造好的消息结构体传入netthread中的queue_recv队列
	//recvsolve线程：run函数循环弹出queue_recv队列，如果非空则向ui线程发送datarecv信号
	//ui线程：datasolve函数响应该信号，解析数据，并展示给用户
    qint64 availbytes = _socktcp->bytesAvailable();
	if (availbytes <=0 )
	{
		return;
	}
	//正式往recvbuf后面追加数据了
    qint64 ret = _socktcp->read((char *) recvbuf + hasrecvive, availbytes);
    if (ret <= 0)
    {
        qDebug() << "error or no more data";
		return;
    }
    hasrecvive += ret;

	//防止tcp拆包
    //数据包不够
    if (hasrecvive < MSG_HEADER)
    {
        return;
    }
    else
    {
        quint32 data_size;
        qFromBigEndian<quint32>(recvbuf + 7, 4, &data_size);
        if ((quint64)data_size + 1 + MSG_HEADER <= hasrecvive) //收够一个包，要把包头和包尾的字节都算上
        {
            if (recvbuf[0] == '$' && recvbuf[MSG_HEADER + data_size] == '#') //且包结构正确
            {
				//接下来就是按顺序解析了，不难，就是很麻烦
				MSG_TYPE msgtype;
				uint16_t type;
				qFromBigEndian<uint16_t>(recvbuf + 1, 2, &type);
				msgtype = (MSG_TYPE)type;
				qDebug() << "recv data type: " << msgtype;
				if (msgtype == CREATE_MEETING_RESPONSE || msgtype == JOIN_MEETING_RESPONSE || msgtype == PARTNER_JOIN2)
				{
					if (msgtype == CREATE_MEETING_RESPONSE)
					{
						qint32 roomNo;
						qFromBigEndian<qint32>(recvbuf + MSG_HEADER, 4, &roomNo);

						MESG* msg = (MESG*)malloc(sizeof(MESG));

						if (msg == NULL)
						{
							qDebug() << __LINE__ << " CREATE_MEETING_RESPONSE malloc MESG failed";
						}
						else
						{
							memset(msg, 0, sizeof(MESG));
							msg->msg_type = msgtype;
							msg->data = (uchar*)malloc((quint64)data_size);
							if (msg->data == NULL)
							{
								free(msg);
								qDebug() << __LINE__ << "CREATE_MEETING_RESPONSE malloc MESG.data failed";
							}
							else
							{
								memset(msg->data, 0, (quint64)data_size);
								memcpy(msg->data, &roomNo, data_size);
								msg->len = data_size;
								queue_recv.push_msg(msg);
							}

						}
					}
					else if (msgtype == JOIN_MEETING_RESPONSE)
					{
						qint32 c;
						memcpy(&c, recvbuf + MSG_HEADER, data_size);

						MESG* msg = (MESG*)malloc(sizeof(MESG));

						if (msg == NULL)
						{
							qDebug() << __LINE__ << "JOIN_MEETING_RESPONSE malloc MESG failed";
						}
						else
						{
							memset(msg, 0, sizeof(MESG));
							msg->msg_type = msgtype;
							msg->data = (uchar*)malloc(data_size);
							if (msg->data == NULL)
							{
								free(msg);
								qDebug() << __LINE__ << "JOIN_MEETING_RESPONSE malloc MESG.data failed";
							}
							else
							{
								memset(msg->data, 0, data_size);
								memcpy(msg->data, &c, data_size);

								msg->len = data_size;
								queue_recv.push_msg(msg);
							}
						}
					}
					else if (msgtype == PARTNER_JOIN2)
					{
						MESG* msg = (MESG*)malloc(sizeof(MESG));
						if (msg == NULL)
						{
							qDebug() << "PARTNER_JOIN2 malloc MESG error";
						}
						else
						{
							memset(msg, 0, sizeof(MESG));
							msg->msg_type = msgtype;
							msg->len = data_size;
							msg->data = (uchar*)malloc(data_size);
							if (msg->data == NULL)
							{
								free(msg);
								qDebug() << "PARTNER_JOIN2 malloc MESG.data error";
							}
							else
							{
								memset(msg->data, 0, data_size);
								uint32_t ip;
								int pos = 0;
								for (int i = 0; i < data_size / sizeof(uint32_t); i++)
								{
									qFromBigEndian<uint32_t>(recvbuf + MSG_HEADER + pos, sizeof(uint32_t), &ip);
									memcpy_s(msg->data + pos, data_size - pos, &ip, sizeof(uint32_t));
									pos += sizeof(uint32_t);
								}
								queue_recv.push_msg(msg);
							}

						}
					}
				}
                else if (msgtype == IMG_RECV || msgtype == PARTNER_JOIN || msgtype == PARTNER_EXIT || msgtype == AUDIO_RECV || msgtype == CLOSE_CAMERA || msgtype == TEXT_RECV)
				{
					//read ipv4
					quint32 ip;
					qFromBigEndian<quint32>(recvbuf + 3, 4, &ip);

					if (msgtype == IMG_RECV)
					{
						//QString ss = QString::fromLatin1((char *)recvbuf + MSG_HEADER, data_len);
						QByteArray cc((char *) recvbuf + MSG_HEADER, data_size);
						QByteArray rc = QByteArray::fromBase64(cc);
						QByteArray rdc = qUncompress(rc);
						//将消息加入到接收队列
		//                qDebug() << roomNo;
						
						if (rdc.size() > 0)
						{
							MESG* msg = (MESG*)malloc(sizeof(MESG));
							if (msg == NULL)
							{
								qDebug() << __LINE__ << " malloc failed";
							}
							else
							{
								memset(msg, 0, sizeof(MESG));
								msg->msg_type = msgtype;
								msg->data = (uchar*)malloc(rdc.size()); // 10 = format + width + width
								if (msg->data == NULL)
								{
									free(msg);
									qDebug() << __LINE__ << " malloc failed";
								}
								else
								{
									memset(msg->data, 0, rdc.size());
									memcpy_s(msg->data, rdc.size(), rdc.data(), rdc.size());
									msg->len = rdc.size();
									msg->ip = ip;
									queue_recv.push_msg(msg);
								}
							}
						}
					}
					else if (msgtype == PARTNER_JOIN || msgtype == PARTNER_EXIT || msgtype == CLOSE_CAMERA)
					{
						MESG* msg = (MESG*)malloc(sizeof(MESG));
						if (msg == NULL)
						{
							qDebug() << __LINE__ << " malloc failed";
						}
						else
						{
							memset(msg, 0, sizeof(MESG));
							msg->msg_type = msgtype;
							msg->ip = ip;
							queue_recv.push_msg(msg);
						}
					}
					else if (msgtype == AUDIO_RECV)
					{
						//解压缩
						QByteArray cc((char*)recvbuf + MSG_HEADER, data_size);
 						QByteArray rc = QByteArray::fromBase64(cc);
 						QByteArray rdc = qUncompress(rc);

						if (rdc.size() > 0)
						{
							MESG* msg = (MESG*)malloc(sizeof(MESG));
							if (msg == NULL)
							{
								qDebug() << __LINE__ << "malloc failed";
							}
							else
							{
								memset(msg, 0, sizeof(MESG));
								msg->msg_type = AUDIO_RECV;
								msg->ip = ip;

								msg->data = (uchar*)malloc(rdc.size());
								if (msg->data == nullptr)
								{
                                    free(msg);
									qDebug() << __LINE__ << "malloc msg.data failed";
								}
								else
								{
									memset(msg->data, 0, rdc.size());
									memcpy_s(msg->data, rdc.size(), rdc.data(), rdc.size());
									msg->len = rdc.size();
									msg->ip = ip;
									audio_recv.push_msg(msg);
								}
							}
						}
					}
                    else if(msgtype == TEXT_RECV)
                    {
                        //解压缩
                        QByteArray cc((char *)recvbuf + MSG_HEADER, data_size);
                        std::string rr = qUncompress(cc).toStdString();
                        if(rr.size() > 0)
                        {
                            MESG* msg = (MESG*)malloc(sizeof(MESG));
                            if (msg == NULL)
                            {
                                qDebug() << __LINE__ << "malloc failed";
                            }
                            else
                            {
                                memset(msg, 0, sizeof(MESG));
                                msg->msg_type = TEXT_RECV;
                                msg->ip = ip;
                                msg->data = (uchar*)malloc(rr.size());
                                if (msg->data == nullptr)
                                {
                                    free(msg);
                                    qDebug() << __LINE__ << "malloc msg.data failed";
                                }
                                else
                                {
                                    memset(msg->data, 0, rr.size());
                                    memcpy_s(msg->data, rr.size(), rr.data(), rr.size());
                                    msg->len = rr.size();
                                    queue_recv.push_msg(msg);
                                }
                            }
                        }
                    }
                }
			}
            else
            {
                qDebug() << "package error";
            }
			//这里要把后面还没读取的数据拷到recvbuf的开头
			memmove_s(recvbuf, 4 * MB, recvbuf + MSG_HEADER + data_size + 1, hasrecvive - ((quint64)data_size + 1 + MSG_HEADER));
			hasrecvive -= ((quint64)data_size + 1 + MSG_HEADER);
        }
        else
        {
            return;
        }
    }
}


MyTcpSocket::~MyTcpSocket()
{
    delete sendbuf;
    delete _sockThread;
}



bool MyTcpSocket::connectServer(QString ip, QString port, QIODevice::OpenModeFlag flag)
{
    if(_socktcp == nullptr) _socktcp = new QTcpSocket(); //tcp
    _socktcp->connectToHost(ip, port.toUShort(), flag);
    connect(_socktcp, SIGNAL(readyRead()), this, SLOT(recvFromSocket()), Qt::UniqueConnection); //接受数据
    //处理套接字错误
    connect(_socktcp, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(errorDetect(QAbstractSocket::SocketError)),Qt::UniqueConnection);

	//阻塞等待连接服务器结果
	//当然也有非阻塞的方式：信号connected
	//单线程中慎用！
    if(_socktcp->waitForConnected(5000))
    {
        return true;
    }
	_socktcp->close();
    return false;
}


bool MyTcpSocket::connectToServer(QString ip, QString port, QIODevice::OpenModeFlag flag)
{
	_sockThread->start(); // 开启链接，与接受
	bool retVal;
	QMetaObject::invokeMethod(this, "connectServer", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, retVal),
								Q_ARG(QString, ip), Q_ARG(QString, port), Q_ARG(QIODevice::OpenModeFlag, flag));

	if (retVal)
	{
        this->start() ; //写数据
		return true;
	}
	else
	{
		return false;
	}
}

QString MyTcpSocket::errorString()
{
    return _socktcp->errorString();
}

void MyTcpSocket::disconnectFromHost()
{
    //write
    if(this->isRunning())
    {
        QMutexLocker locker(&m_lock);
        m_isCanRun = false;
    }

    if(_sockThread->isRunning()) //read
    {
        _sockThread->quit();
        _sockThread->wait();
    }

    //清空 发送 队列，清空接受队列
    queue_send.clear();
    queue_recv.clear();
	audio_recv.clear();
}


quint32 MyTcpSocket::getlocalip()
{
    if(_socktcp->isOpen())
    {
        return _socktcp->localAddress().toIPv4Address();
    }
    else
    {
        return -1;
    }
}
