#ifndef FTP_H
#define FTP_H

#include "ftp_global.h"
#include <QObject>
#include <QAbstractSocket>
#include <QString>
#ifndef CHAR_CR
#define CHAR_CR QString("\r\n")
#endif

class QTcpSocket;
class QTcpServer;
class QFile;
class Ftp : public QObject
{
     Q_OBJECT
public:
    enum Mode {PASV,PORT};
    enum Type {BINARY='I',ASCII='A',EBCDIC='E',LOCAL='L'};
    enum CMD{CMD_PUT,CMD_GET,CMD_LIST,CMD_OTHER};

public:
    explicit Ftp();
    ~Ftp();
    explicit Ftp(QString ip,quint16 port,QString username,QString passwd);
    void login(QString username,QString passwd);
    bool connectToHost(QString ip,quint16 port);
    void mode(Mode transfer_mode){m_mode=transfer_mode;}
    void type(Type transfer_type){m_type=transfer_type;}
    void put(QString local_filename, QString remote_filename, qint64 offset=0, bool is_append=false);
    void get(QString local_filename,QString remote_filename,qint64 offset=0);
    qint64 fileSize(QString remote_filename);
    void list(QString remote_dir);
    void rawCommand(QString cmd);
    bool connectStatus(){return b_isConnected;}
    bool loginStatus(){return b_isLogined;}

private:
    void setTransferProperty();
    void addDataChannel();

private:
    QString str_ip;
    quint16 n_port;
    QString str_username;
    QString str_password;
    Mode m_mode;
    Type m_type;
    CMD m_cmdType;

    bool b_isConnected;
    bool b_isLogined;
    bool b_stop;

    QByteArray m_data;
    QTcpSocket *p_cmdSocket;
    QTcpSocket *p_dataSocket;
    QTcpServer *p_listener;

    QFile *p_file;
    qint64 n_transferValue;
    qint64 n_transferTotal;
    qint64 n_remoteFileSize;
signals:
    void failToDataChannel();
    void loginSuccess();
    void execCmdResult(QString result);
    void transferDataProgress(qint64 transfer_size,qint64 total_size);
    void transferFinished();
    void error(int code,QString desc);
    void remoteDirInfo(QStringList dirInfo);
    void logout();

public slots:
    void connectError(QAbstractSocket::SocketError code);
    void stopTransfer(){b_stop=true;}
private slots:
    void connected();
    void readCmdResult();
    void getPORTSocket();
    void writeData();
    void readData();
    void readDataFinished();
    void transferData();
    void readDirInfo();
    void clearDataSocket();
};

#endif // FTP_H
