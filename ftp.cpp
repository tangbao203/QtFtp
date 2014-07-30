#include "ftp.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QHostAddress>
#include <QFileInfo>
#include <QThread>
#include <QDataStream>

Ftp::Ftp(QString ip, quint16 port):QObject(0),b_isConnected(false)
{
    p_cmdSocket=new QTcpSocket;
    p_dataSocket=NULL;
    p_listener=new QTcpServer;
    m_mode=Ftp::PORT;
    m_type=Ftp::BINARY;
    m_cmdType=Ftp::CMD_OTHER;
    p_file=new QFile;
    n_transferValue=0;
    n_transferTotal=0;
    n_remoteFileSize=0;
    b_stop=false;
    str_ip=ip;
    n_port=port;

    connectToHost(str_ip,n_port);
}

Ftp::~Ftp()
{
    delete p_cmdSocket;
    delete p_listener;
    delete p_file;
}

void Ftp::setTransferProperty()
{
    QString cmd="TYPE "+QString(m_type)+CHAR_CR;
    p_cmdSocket->write(cmd.toLatin1());
}

void Ftp::login(QString username,QString passwd)
{
    //connect break!
    if(!b_isConnected)
        return;

    str_username=username;
    str_password=passwd;

    QString cmd="USER "+username+CHAR_CR+"PASS "+passwd+CHAR_CR;

    p_cmdSocket->write(cmd.toLatin1());

    //prepare receive all response data
    connect(p_cmdSocket,SIGNAL(readyRead()),this,SLOT(readCmdResult()),Qt::UniqueConnection);
    //set global transfer property
    setTransferProperty();
    connect(p_listener,SIGNAL(newConnection()),this,SLOT(getPORTSocket()));
}

void Ftp::login()
{
    //try to connect
    if(p_cmdSocket->state()==QAbstractSocket::UnconnectedState)
        connectToHost(str_ip,n_port);

    if(!b_isConnected)
        return;

    login(str_username,str_password);
}

bool Ftp::connectToHost(QString ip,quint16 port)
{
    p_cmdSocket->connectToHost(ip,port);
    connect(p_cmdSocket,SIGNAL(error(QAbstractSocket::SocketError)),this,SLOT(connectError(QAbstractSocket::SocketError)));

    b_isConnected=true;
    //block way run
    if(!p_cmdSocket->waitForConnected())
        b_isConnected=false;

    return b_isConnected;
}

void Ftp::addDataChannel()
{
    QString cmd="";
    if(m_mode==Ftp::PORT){
        QHostAddress host(str_ip);
        if(p_listener->isListening())
            p_listener->close();
        if(!p_listener->listen(host,0)){
            emit error(9,"fail for build data channel!");
            return;
        }

        quint16 port=p_listener->serverPort();
        quint32 ipVal=host.toIPv4Address();
        QString address=QString::number((ipVal&0xff000000)>>24)+QChar(',')+QString::number((ipVal&0xff0000)>>16)+QChar(',')+QString::number((ipVal&0xff00)>>8)+QChar(',')+QString::number(ipVal&0xff);
        address+=QChar(',')+QString::number((port&0xff00)>>8)+QChar(',')+QString::number(port&0xff);
        cmd="PORT "+address+CHAR_CR;
        p_cmdSocket->write(cmd.toLatin1());
    }
    else{
        cmd="PASV"+CHAR_CR;
        p_cmdSocket->write(cmd.toLatin1());
    }

}

void Ftp::getPORTSocket()
{
    p_dataSocket=p_listener->nextPendingConnection();
    //start transfer data
    transferData();
}

void Ftp::put(QString local_filename,QString remote_filename,qint64 offset,bool is_append)
{
    if(!b_isLogined || p_dataSocket!=NULL)
        return;

    QFileInfo info(local_filename);
    if(!info.exists() || !info.isReadable()){
        emit error(11,"can`t read local file!");
        return;
    }

    if(p_file->isOpen())
        p_file->close();

    p_file->setFileName(local_filename);
    if(!p_file->open(QIODevice::ReadOnly)){
        emit error(11,p_file->errorString());
        return;
    }

    QString cmd="ALLO "+QString::number(info.size()-offset)+CHAR_CR;
    if(is_append)
        cmd+="APPE "+remote_filename+CHAR_CR;
    if(offset>0)
        cmd+="REST "+QString::number(offset)+CHAR_CR;

    cmd+="STOR "+remote_filename+CHAR_CR;

    n_transferValue=offset;
    n_transferTotal=info.size();
    if(n_transferValue>=n_transferTotal){
        emit transferFinished();
        return;
    }

    //build data channel
    addDataChannel();
    b_stop=false;

    p_file->seek(offset);
    m_cmdType=Ftp::CMD_PUT;

    p_cmdSocket->write(cmd.toLatin1());
}

void Ftp::get(QString local_filename,QString remote_filename,qint64 offset)
{
    if(!b_isLogined || p_dataSocket!=NULL)
        return;

    qint64 remoteFileSize=fileSize(remote_filename);
    if(remoteFileSize<=0)
        return;


    if(p_file->isOpen())
        p_file->close();

    p_file->setFileName(local_filename);
    if(!p_file->open(QIODevice::WriteOnly)){
        emit error(11,p_file->errorString());
        return;
    }

    QString cmd="";
    if(offset>0)
        cmd+="REST "+QString::number(offset)+CHAR_CR;

    cmd+="RETR "+remote_filename+CHAR_CR;

    n_transferValue=offset;
    n_transferTotal=remoteFileSize;
    if(n_transferValue>=n_transferTotal){
        emit transferFinished();
        return;
    }

    //build data channel
    addDataChannel();

    b_stop=false;
    m_cmdType=Ftp::CMD_GET;
    p_cmdSocket->write(cmd.toLatin1());
}

void Ftp::list(QString remote_dir)
{
    if(!b_isLogined || p_dataSocket!=NULL)
        return;

    addDataChannel();

    QString cmd="LIST "+remote_dir+CHAR_CR;

    m_cmdType=Ftp::CMD_LIST;
    p_cmdSocket->write(cmd.toLatin1());
}

void Ftp::rawCommand(QString cmd)
{
    if(!b_isLogined || cmd.length()<=0)
        return;

    m_cmdType=Ftp::CMD_OTHER;
    p_cmdSocket->write(cmd.toLatin1());
}

qint64 Ftp::fileSize(QString remote_filename)
{
    if(!b_isLogined || remote_filename.length()<=0)
        return 0;

    n_remoteFileSize=0;
    QString cmd="SIZE "+remote_filename+CHAR_CR;
    if(p_cmdSocket->write(cmd.toLatin1())<=0 || !p_cmdSocket->waitForReadyRead())
        return 0;

    return n_remoteFileSize;
}

void Ftp::connectError(QAbstractSocket::SocketError code)
{
    switch(code){
    case QTcpSocket::ConnectionRefusedError:
        emit error(0,"connect resfuse error!");
        break;
    case QTcpSocket::RemoteHostClosedError:
        emit error(1,"remote host closed!");
        break;
    case QTcpSocket::HostNotFoundError:
        emit error(2,"host not found!");
        break;
    case QTcpSocket::SocketTimeoutError:
        emit error(3,"connect timeout!");
        break;
    case QTcpSocket::NetworkError:
        emit error(4,"network error!");
        break;
    default:
        emit error(code,"unkown error,please check tcp socket!");
    }

    b_isConnected=false;
}

void Ftp::readCmdResult()
{
    QByteArray data;

    while((data=p_cmdSocket->readLine()).length()>0){
        QString result=QString(data);
        QRegExp regexp("^\\d{3}\.+");

        if(!regexp.exactMatch(result))
            continue;

        QStringList strlist=result.split(' ');
        bool toInt=false;
        int code=strlist.first().toInt(&toInt);
        if(!toInt)
            continue;

        switch(code){
            case 230://login success
                emit loginSuccess();
                b_isLogined=true;
                break;
            case 227:{ //build pasv data channel
                    QRegExp regexp("\(?:(\\d{1,3}),(\\d{1,3}),(\\d{1,3}),(\\d{1,3}),(\\d{1,3}),(\\d{1,3})\)");
                    QString ip;quint16 port;
                    if(regexp.indexIn(result)!=-1){
                        ip=regexp.cap(1)+"."+regexp.cap(2)+"."+regexp.cap(3)+"."+regexp.cap(4);
                        port=(regexp.cap(5).toUInt()<<8)+regexp.cap(6).toUInt();
                    }

                    //add new data connect,(old connect ignore)
                    p_dataSocket=new QTcpSocket;
                    p_dataSocket->connectToHost(QHostAddress(ip),port);
                    if(!p_dataSocket->waitForConnected()){
                        emit error(9,"fial for build data channel!");
                        return;
                    }

                    //start transfer data
                    transferData();
                }
                break;
           case 213:{
                bool toInt=false;
                qint64 size=strlist.last().toLongLong(&toInt);
                if(toInt && size>0)
                    n_remoteFileSize=size;
                }break;
            case 421://FTP timeout
                b_isLogined=false;
                emit logout();
                break;
            case 530://ftp password error
                b_isLogined=false;
                p_cmdSocket->disconnectFromHost();
                p_cmdSocket->close();
                emit error(5,"Ftp login  error!");
            default:
                break;
        }

        emit execCmdResult(result);
    }
}

void Ftp::transferData()
{
    if(p_dataSocket==NULL)
        return;

    switch(m_cmdType){
    case Ftp::CMD_GET:
        //prepare to download
        connect(p_dataSocket,SIGNAL(readyRead()),this,SLOT(readData()));
        connect(p_dataSocket,SIGNAL(readChannelFinished()),this,SLOT(readDataFinished()));
        break;
    case Ftp::CMD_PUT:
        //prepare to upload
        connect(p_dataSocket,SIGNAL(bytesWritten(qint64)),this,SLOT(writeData()));
        writeData();
        break;
    case Ftp::CMD_LIST:
        //read dir info
        connect(p_dataSocket,SIGNAL(readyRead()),this,SLOT(readDirInfo()));
        break;
    default:
        break;
    }

    //clear data socket connect
    connect(p_dataSocket,SIGNAL(disconnected()),this,SLOT(clearDataSocket()));
}

void Ftp::clearDataSocket()
{
    if(p_dataSocket!=NULL && p_dataSocket!=0x0){
        p_dataSocket->close();
        delete p_dataSocket;
    }
    p_dataSocket=NULL;
}

void Ftp::readDirInfo()
{
    if(m_cmdType!=Ftp::CMD_LIST)
        return;

    QStringList dirInfo;
    while(p_dataSocket->bytesAvailable()>0){
        QString info=QString(p_dataSocket->readLine());
        if(info.length()<=0)
            continue;

        dirInfo.append(info);
    }

    emit remoteDirInfo(dirInfo);
    clearDataSocket();
}

void Ftp::readDataFinished()
{
    p_file->close();

    clearDataSocket();
    emit transferFinished();
    return;
}

void Ftp::readData()
{
    if(m_cmdType!=Ftp::CMD_GET)
        return;

    int bufsize=8*1024;//write size 8KB
    p_dataSocket->setReadBufferSize(bufsize*16);//socket buffer size 128KB

    while(p_dataSocket->bytesAvailable()>0){
        QByteArray data=p_dataSocket->read(bufsize);
        if(b_stop){
            readDataFinished();
            return;
        }

        qint64 bytesWrite=p_file->write(data);
        if(bytesWrite==-1){
            emit error(12,"fail for write to file!");
            p_file->close();
            clearDataSocket();
            return;
        }

        n_transferValue+=bytesWrite;
        emit transferDataProgress(n_transferValue,n_transferTotal);
    }
}

void Ftp::writeData()
{
    if(!p_file->isOpen())
        return;

    int bufsize=8*1024;
    char buffer[bufsize];

    QDataStream in(p_file);
    in.setVersion(QDataStream::Qt_5_2);
    int read=in.readRawData(buffer,bufsize);
    //qint64 read=p_file->read(buffer,bufsize);
    if(read==-1){
        emit error(11,"can't read file!");
        p_file->close();
        clearDataSocket();
        return;
    }

    if(read==0 || b_stop){
        p_file->close();
        p_dataSocket->disconnectFromHost();
        emit transferFinished();
        return;
    }

    if(read>0){
        qint64 bytesWrite=p_dataSocket->write(buffer,bufsize);
        if(bytesWrite==0){
            clearDataSocket();
            emit error(13,"fail for write to server!");
            return;
        }

        n_transferValue+=bytesWrite;
        emit transferDataProgress(n_transferValue,n_transferTotal);
    }
}
