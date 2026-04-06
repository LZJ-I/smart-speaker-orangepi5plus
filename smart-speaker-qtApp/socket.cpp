#include "socket.h"
#include <QtGlobal>
#include <QDebug>
#include <cstring>

// 初始化父类QObject
Socket::Socket(QObject *parent) : QObject(parent)
{
    // 初始化socket
    m_socket = new QTcpSocket(this);
    m_socket->connectToHost(QHostAddress(IP), PORT);

    connect(m_socket, &QTcpSocket::readyRead, this, &Socket::readyRead);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &Socket::onTcpError);
#else
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &Socket::onTcpError);
#endif

    ConnectState = false;

    // 循环尝试重连服务器
    // 1. 初始化定时器
    connectTimer = new QTimer(this);
    connectTimer->setInterval(1000);  // 定时1秒
    // 2. 连接关键信号槽
    connect(connectTimer, &QTimer::timeout, this, &Socket::tryConnect);         // 定时触发 尝试连接服务器
    // 3. 启动定时器，连接尝试
    connectTimer->start();
}
Socket::~Socket()
{
    delete m_socket;
    delete connectTimer;
}
// 每秒触发的连接逻辑（已连接则跳过，未连接则重试）
void Socket::tryConnect()
{
    QTcpSocket::SocketState currentState = m_socket->state();   //记录当前状态

    // 仅当状态变化时，发送对应信号
    if (currentState != lastConnectState){
        if (currentState == QTcpSocket::ConnectedState){
            // 从“非连接”→“已连接”
            ConnectState = true;
            QMessageBox::information(nullptr, "连接提示", "已成功连接服务器");
        }else if (lastConnectState == QTcpSocket::ConnectedState){
            ConnectState = false;
            const bool playerAlreadyNotified = m_disconnectDueToDeviceClient;
            m_disconnectDueToDeviceClient = false;
            bool deviceClientOff = playerAlreadyNotified;
            if (!deviceClientOff) {
                QJsonObject r;
                while (readOneJson(r)) {
                    if (r[QStringLiteral("cmd")].toString() == QStringLiteral("device_offline")) {
                        deviceClientOff = true;
                        break;
                    }
                }
            }
            if (deviceClientOff) {
                QMessageBox::information(nullptr, "客户端离线",
                    "音箱嵌入式端已断开连接，与服务器的会话已结束。\n请检查设备是否在线（可说「在线模式」恢复）。\n将返回登录界面。");
            } else {
                QMessageBox::information(nullptr, "连接提示",
                    "与服务器连接已断开，正在尝试重新连接。\n将返回登录界面。");
            }
            if (!playerAlreadyNotified)
                emit disconnectedFromServer();
        }
        lastConnectState = currentState;  // 更新上一次状态
    }

    // 未连上时才重连；Connecting/HostLookup 中禁止 abort，否则每秒打断握手，永远连不上
    if (currentState == QTcpSocket::ConnectedState)
        return;
    if (currentState == QTcpSocket::ConnectingState || currentState == QTcpSocket::HostLookupState)
        return;
    m_socket->abort();
    m_socket->connectToHost(QHostAddress(IP), PORT);
}

void Socket::onTcpError(QAbstractSocket::SocketError)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        return;
    qWarning() << "TCP" << IP << PORT << m_socket->errorString();
}

void Socket::sendDisconnectedFromServer(void)
{
    emit disconnectedFromServer(); // 发射掉线信号
}

void Socket::markDisconnectDueToDeviceClient(void)
{
    m_disconnectDueToDeviceClient = true;
}

bool Socket::readOneJson(QJsonObject &root)
{
    root = QJsonObject();
    if (m_socket->bytesAvailable() < 4)
        return false;
    char lenbuf[4];
    if (m_socket->peek(lenbuf, 4) != 4)
        return false;
    int data_len = 0;
    std::memcpy(&data_len, lenbuf, sizeof(data_len));
    if (data_len <= 0 || data_len > 1024 * 1024)
        return false;
    if (m_socket->bytesAvailable() < 4 + data_len)
        return false;
    m_socket->read(lenbuf, 4);
    QByteArray body = m_socket->read(data_len);
    if (body.size() != data_len)
        return false;
    QJsonParseError parseErr;
    QJsonDocument dt = QJsonDocument::fromJson(body, &parseErr);
    if (dt.isNull() || !dt.isObject()) {
        qDebug() << "JSON 解析失败：" << parseErr.errorString();
        return false;
    }
    root = dt.object();
    return true;
}

void Socket::ReadData(QJsonObject& root)
{
    if (!readOneJson(root))
        root = QJsonObject();
}


void Socket::WriteData(const QJsonObject &json)
{
    QJsonDocument d(json);
    QByteArray SendData = d.toJson();
    int len = SendData.size();
    //第一次发送数据长度
    m_socket->write((char *)&len,sizeof(int));
    //发送数据本身
    m_socket->write(SendData);
    // qDebug()<<"发送消息:"<< SendData;
}

