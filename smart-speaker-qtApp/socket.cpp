#include "socket.h"

// 初始化父类QObject
Socket::Socket(QObject *parent) : QObject(parent)
{
    // 初始化socket
    m_socket = new QTcpSocket(this);
    m_socket->connectToHost(QHostAddress(IP), PORT);

    // 转发 tcpsocket readyRead信号
    connect(m_socket, &QTcpSocket::readyRead, this, &Socket::readyRead);

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
            // 从“已连接”→“非连接”（掉线）
            ConnectState = false;
            QMessageBox::information(nullptr, "连接提示", "服务器出小差了，正在尝试重新连接服务器\n将返回登录界面");
            emit disconnectedFromServer(); // 发射掉线信号
        }
        lastConnectState = currentState;  // 更新上一次状态
    }

    // 处于未连接时，尝试重连
    if (currentState != QTcpSocket::ConnectedState){
        m_socket->abort();  // 终止无效连接
        m_socket->connectToHost(QHostAddress(IP), PORT);
    }
}

void Socket::sendDisconnectedFromServer(void)
{
    emit disconnectedFromServer(); // 发射掉线信号
}

void Socket::ReadData(QJsonObject& root)
{
    char rcvbuf[1024] = {0};
    int size = 0;
    // 1. 读取数据长度len
    while(size < (int)sizeof(int)){
        size += m_socket->read(rcvbuf + size, sizeof(int) - size);
    }
    int data_len = *(int*)rcvbuf;
    size = 0;
    memset(rcvbuf, 0, sizeof(rcvbuf));
    // 2. 读取数据
    while(size < data_len){
        size += m_socket->read(rcvbuf + size, data_len - size);
    }

    qDebug()<<"收到"<<data_len<<"字节"<<",消息："<<rcvbuf;

    //数据解析
    QJsonParseError parseErr; // 添加错误捕获，方便排查
    QJsonDocument dt = QJsonDocument::fromJson(rcvbuf, &parseErr);
    if(dt.isNull())
    {
        qDebug() << "JSON 解析失败：" << parseErr.errorString();
        return;
    }
    root = dt.object(); // 将字符串解析为json对象

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

