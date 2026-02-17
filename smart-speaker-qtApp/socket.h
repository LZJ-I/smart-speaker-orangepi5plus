#ifndef SOCKET_H
#define SOCKET_H

#include <QObject>
#include <QTcpSocket>
#include <QHostAddress>
#include <QMessageBox>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>


// #define IP  "10.102.178.47"  //虚拟机IP
#define IP "39.102.121.199"     //阿里云IP
#define PORT 8888

class Socket : public QObject
{
    Q_OBJECT
public:

    explicit Socket(QObject *parent = nullptr);
    ~Socket();
public:
    void WriteData(const QJsonObject &json);        // 发送数据给服务器
    void ReadData(QJsonObject& root);                // 从服务器读取数据
    bool ConnectState;  // 当前的连接状态（1为连接）
    void sendDisconnectedFromServer(void);  //发送掉线信号
private:
    QTcpSocket* m_socket;       // socket
    QTimer *connectTimer;       // 定时连接的定时器
    QTcpSocket::SocketState lastConnectState = QTcpSocket::UnconnectedState;  // 记录上一次 连接状态

private slots:
    void tryConnect();  // 每秒尝试连接的槽函数

signals:
    void readyRead();   // 自定义信号，转发QTcpSocket的readyRead
    void disconnectedFromServer(); // 自定义信号：掉线通知
};

#endif // SOCKET_H
