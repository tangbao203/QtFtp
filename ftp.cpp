#include "ftp.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QHostAddress>
#include <QFileInfo>
#include <QDebug>
#include <QThread>


Ftp::Ftp():QObject(0),b_isConnected(false)
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
}

Ftp::Ftp(QString ip,quint16 port,QString username,QString passwd):QObject(0),b_isConnected(false)
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
    str_username=username;
    str_password=passwd;
    if(!connectToHost(str_ip,n_port))
        return;

    login(str_username,str_password);
}

Ftp::~Ftp()
{
    delete p_cmdSocket;
    delete p_listener;
}

void Ftp::setTransferProperty()
{
    QString cmd="TYPE "+QString(m_type)+CHAR_CR;
    p_cmdSocket->write(cmd.toLatin1());
}

void Ftp::login(QString username,QString passwd)
{
    if(!b_isConnected)
        return;

    QString cmd="USER "+username+CHAR_CR+"PASS "+passwd+CHAR_CR;

    p_cmdSocket->write(cmd.toLatin1());
    connect(p_cmdSocket,SIGNAL(readyRead()),this,SLOT(readCmdResult()));

    setTransferProperty();
}

bool Ftp::connectToHost(QString ip,quint16 port)
{
    p_cmdSocket->connectToHost(ip,port);
    connect(p_cmdSocket,SIGNAL(error(QAbstractSocket::SocketError)),this,SLOT(connectError(QAbstractSocket::SocketError)));
    connect(p_cmdSocket,SIGNAL(connected()),this,SLOT(connected()));
    if(!p_cmdSocket->waitForConnected())
        b_isConnected=false;

    return b_isConnected;
}

void Ftp::buildDataChannel()
{
    QString cmd="";
    if(m_mode==Ftp::PORT){
        QHostAddress host(str_ip);
        if(p_listener->isListening())
            p_listener->close();
        if(!p_listener->listen(host,0)){
            emit failToDataChannel();
            return;
        }

        quint16 port=p_listener->serverPort();
        quint32 ipVal=host.toIPv4Address();
        QString address=QString::number((ipVal&0xff000000)>>24)+QChar(',')+QString::number((ipVal&0xff0000)>>16)+QChar(',')+QString::number((ipVal&0xff00)>>8)+QChar(',')+QString::number(ipVal&0xff);
        address+=QChar(',')+QString::number((port&0xff00)>>8)+QChar(',')+QString::number(port&0xff);
        cmd="PORT "+address+CHAR_CR;
        p_cmdSocket->write(cmd.toLatin1());
        connect(p_listener,SIGNAL(newConnection()),this,SLOT(getPORTSocket()));
    }
    else{
        cmd="PASV"+CHAR_CR;
        p_cmdSocket->write(cmd.toLatin1());
    }

}

void Ftp::getPORTSocket()
{
    if(p_dataSocket!=NULL){
        p_dataSocket->close();
        delete p_dataSocket;
    }

    p_dataSocket=p_listener->nextPendingConnection();
    //start transfer data
    transferData();
}

void Ftp::put(QString local_filename,QString remote_filename,qint64 offset,bool is_append)
{
    buildDataChannel();

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
    b_stop=false;
    p_file->seek(offset);
    m_cmdType=Ftp::CMD_PUT;
    p_cmdSocket->write(cmd.toLatin1());
}

void Ftp::get(QString local_filename,QString remote_filename,qint64 offset)
{
    quint64 remoteFileSize=fileSize(remote_filename);
    if(remoteFileSize<=0)
        return;

    buildDataChannel();

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
    b_stop=false;
    m_cmdType=Ftp::CMD_GET;
    p_cmdSocket->write(cmd.toLatin1());
}

void Ftp::list(QString remote_dir)
{
    buildDataChannel();

    QString cmd="LIST "+remote_dir+CHAR_CR;

    m_cmdType=Ftp::CMD_LIST;
    p_cmdSocket->write(cmd.toLatin1());
}

void Ftp::rawCommand(QString cmd)
{
    if(cmd.length()<=0)
        return;

    m_cmdType=Ftp::CMD_OTHER;
    p_cmdSocket->write(cmd.toLatin1());
}

quint64 Ftp::fileSize(QString remote_filename)
{
    if(remote_filename.length()<=0)
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

void Ftp::connected(){
    b_isConnected=true;
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
                    QRegExp regexp("\((\\d{1,3}),(\\d{1,3}),(\\d{1,3}),(\\d{1,3}),(\\d{1,3}),(\\d{1,3})\)");
                    QString ip,port;
                    if(regexp.indexIn(result)!=-1){
                        ip=regexp.cap(1)+regexp.cap(2)+regexp.cap(3)+regexp.cap(4);
                        port=regexp.cap(5)+regexp.cap(6);
                    }

                    if(p_dataSocket!=NULL){
                        p_dataSocket->close();
                        delete p_dataSocket;
                        p_dataSocket=NULL;
                    }
                    p_dataSocket=new QTcpSocket;
                    p_dataSocket->connectToHost(QHostAddress(ip),(quint16)port.toInt());
                    if(!p_dataSocket->waitForConnected()){
                        emit error(11,"connect host fail at pasv!");
                        return;
                    }

                    //start transfer data
                    transferData();
                }
                break;
           case 213:{
                bool toInt=false;
                quint64 size=strlist.last().toLongLong(&toInt);
                if(toInt && size>0)
                    n_remoteFileSize=size;
                }
                break;
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
}

void Ftp::readDirInfo()
{
    QStringList dirInfo;
    while(p_dataSocket->bytesAvailable()>0){
        QString info=QString(p_dataSocket->readLine());
        if(info.length()<=0)
            continue;

        dirInfo.append(info);
    }
    emit remoteDirInfo(dirInfo);

    disconnect(p_dataSocket,SIGNAL(readyRead()),this,SLOT(readDirInfo()));
}

void Ftp::readDataFinished()
{
    p_file->close();
    p_dataSocket->close();
    disconnect(p_dataSocket,SIGNAL(readyRead()),this,SLOT(readData()));
    disconnect(p_dataSocket,SIGNAL(readChannelFinished()),this,SLOT(readDataFinished()));
    delete p_dataSocket;
    p_dataSocket=NULL;
    emit transferFinished();
    return;
}

void Ftp::readData()
{

    int bufsize=8*1024;//write size 8KB
    char buffer[bufsize];
    p_dataSocket->setReadBufferSize(bufsize*16);//socket buffer size 128KB

    while(p_dataSocket->bytesAvailable()>0){
        quint64 testee=p_dataSocket->bytesAvailable();
        p_dataSocket->read(buffer,bufsize);
        QByteArray data=p_dataSocket->read(bufsize);
        if(b_stop){
            p_file->close();
            p_dataSocket->close();
            disconnect(p_dataSocket,SIGNAL(readyRead()),this,SLOT(readData()));
            disconnect(p_dataSocket,SIGNAL(readChannelFinished()),this,SLOT(readDataFinished()));
            delete p_dataSocket;
            p_dataSocket=NULL;
            emit transferFinished();
            return;
        }

        qint64 bytesWrite=p_file->write(data);
        if(bytesWrite==-1){
            emit error(12,"write to file fail!");
            p_file->close();
            p_dataSocket->close();
            disconnect(p_dataSocket,SIGNAL(readyRead()),this,SLOT(readData()));
            disconnect(p_dataSocket,SIGNAL(readChannelFinished()),this,SLOT(readDataFinished()));
            delete p_dataSocket;
            p_dataSocket=NULL;
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
    quint64 read=p_file->read(buffer,bufsize);
    if(read==-1){
        emit(11,"can't read file!");
        p_file->close();
        p_dataSocket->close();
        disconnect(p_dataSocket,SIGNAL(bytesWritten(qint64)),this,SLOT(writeData()));
        delete p_dataSocket;
        p_dataSocket=NULL;
        return;
    }

    if(read==0 || b_stop){
        emit transferFinished();
        p_file->close();
        p_dataSocket->close();
        disconnect(p_dataSocket,SIGNAL(bytesWritten(qint64)),this,SLOT(writeData()));
        delete p_dataSocket;
        p_dataSocket=NULL;
        return;
    }

    if(read>0){
        quint64 bytesWrite=p_dataSocket->write(buffer,bufsize);
        if(bytesWrite==0){
            emit error(21,"write to server fail!");
            return;
        }

        n_transferValue+=bytesWrite;
        emit transferDataProgress(n_transferValue,n_transferTotal);
    }
}
