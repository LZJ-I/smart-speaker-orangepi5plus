#ifndef SOCKET_H
#define SOCKET_H

#include <QObject>
#include <QTcpSocket>
#include <QHostAddress>
#include <QMessageBox>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QAbstractSocket>


// #define IP "39.102.121.199"     //阿里云IP
#define IP "192.168.137.100"
#define PORT 8888

class Socket : public QObject
{
    Q_OBJECT
public:

    explicit Socket(QObject *parent = nullptr);
    ~Socket();
public:
    void WriteData(const QJsonObject &json);        // 发送数据给服务器
    void ReadData(QJsonObject& root);                // 从服务器读取一条（不完整则 root 为空）
    bool readOneJson(QJsonObject &root);             // 缓冲区凑齐一条则解析并返回 true
    bool ConnectState;  // 当前的连接状态（1为连接）
    void sendDisconnectedFromServer(void);  //发送掉线信号
    void markDisconnectDueToDeviceClient(void);

private:
    QTcpSocket* m_socket;       // socket
    QTimer *connectTimer;       // 定时连接的定时器
    QTcpSocket::SocketState lastConnectState = QTcpSocket::UnconnectedState;  // 记录上一次 连接状态
    bool m_disconnectDueToDeviceClient = false;

private slots:
    void tryConnect();
    void onTcpError(QAbstractSocket::SocketError e);

signals:
    void readyRead();   // 自定义信号，转发QTcpSocket的readyRead
    void disconnectedFromServer(); // 自定义信号：掉线通知
};

#endif // SOCKET_H
