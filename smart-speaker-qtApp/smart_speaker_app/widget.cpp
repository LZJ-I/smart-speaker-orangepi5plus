#include "widget.h"
#include "ui_widget.h"


Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);


    // 创建自定义的 socket类 的 实例
    m_socket = new Socket(this);

    // 绑定读取数据的信号和槽
    connect(m_socket, &Socket::readyRead, this, &Widget::server_reply_slot);
    // 监听掉线信号
    connect(m_socket, &Socket::disconnectedFromServer, this, &Widget::onSocketDisconnected);
}

Widget::~Widget()
{
    delete ui;
}

// 掉线后的核心处理逻辑
void Widget::onSocketDisconnected()
{
    // 1. 关闭并释放Player界面（如果存在）
    if (m_player != nullptr) {
        m_player->close();
        delete m_player; // 释放内存
        m_player = nullptr;
    }

    // 2. 关闭并释放Bind界面（如果存在）
    if (m_bind != nullptr) {
        m_bind->close();
        delete m_bind; // 释放内存
        m_bind = nullptr;
    }

    // 3. 重新激活登录界面的信号槽（之前登录成功/进入绑定后断开过）
    disconnect(m_socket, &Socket::readyRead, this, &Widget::server_reply_slot);
    connect(m_socket, &Socket::readyRead, this, &Widget::server_reply_slot);

    // 4. 显示登录界面，提示用户
    this->show();
    // QMessageBox::warning(nullptr, "连接状态", "已断开与服务器的连接，已返回登录界面");
}


// 接收服务器消息
void Widget::server_reply_slot(void)
{
    QJsonObject root;
    m_socket->ReadData(root);

    //具体的逻辑处理
    QString cmd = root["cmd"].toString();
    if(cmd == "reply_app_register")         // app注册
        app_register_reply_handler(root);
    else if(cmd == "reply_app_login")       // app登录
        app_login_reply_handler(root);
    else
        qDebug()<<"出现未知cmd指令:"<<cmd;
}

// 处理服务器 回复的 app登录结果
void Widget::app_login_reply_handler(QJsonObject& root)
{
    QString res = root["result"].toString();
    if(res == "success"){
        // 登录成功
        QMessageBox::information(nullptr, "登录结果", "登录成功");
        // 断开Socket信号与Widget槽的连接，Widget不再接收消息
        disconnect(m_socket, &Socket::readyRead, this, &Widget::server_reply_slot);
        // 显示控制界面(传输的数据为 socket、 appid和 deviceid)
        m_player = new Player(m_socket, ui->appid_lineEdit->text(), root["deviceid"].toString());
        this->hide();    //隐藏此界面
        m_player->show(); //显示控制界面
    }else if(res == "notbind"){
        // 未绑定设备（登陆成功）
        QMessageBox::information(nullptr, "登录结果", "登录成功，您还未绑定设备，请先绑定设备");
        // 断开Socket信号与Widget槽的连接，Widget不再接收消息
        disconnect(m_socket, &Socket::readyRead, this, &Widget::server_reply_slot);
        // 显示绑定界面
        m_bind = new Bind(m_socket, ui->appid_lineEdit->text());
        this->hide();    //隐藏此界面
        m_bind->show(); //显示绑定界面
    }
    else if(res == "idshort"){
        // appid太短
        QMessageBox::information(nullptr, "登录结果", "appid过短，请重新尝试");
    }
    else if(res == "passhort"){
        // 密码太短
        QMessageBox::information(nullptr, "登录结果", "密码过短，请重新尝试");
    }
    else if(res == "idnotexist"){
        // 用户名不存在
        QMessageBox::information(nullptr, "登录结果", "用户不存在，请先注册");
    }else if(res == "passerr"){
        // 密码错误
        QMessageBox::information(nullptr, "登录结果", "密码错误，请重试");
    }
    else{
        // 其他错误
        QMessageBox::information(nullptr, "登录结果", "服务器错误");
    }
}
// 处理服务器 回复的 app注册结果
void Widget::app_register_reply_handler(QJsonObject& root)
{
    QString res = root["result"].toString();
    if(res == "success"){
        // 注册成功
        QMessageBox::information(nullptr, "注册结果", "注册成功");
    }
    else if(res == "idshort"){
        // appid太短
        QMessageBox::information(nullptr, "注册结果", "appid过短，请重新尝试");
    }
    else if(res == "passhort"){
        // 密码太短
        QMessageBox::information(nullptr, "注册结果", "密码过短，请重新尝试");
    }
    else if(res == "idexist"){
        // 用户已存在
        QMessageBox::information(nullptr, "注册结果", "用户已存在，请换一个");
    }else{
        // 其他错误
        QMessageBox::information(nullptr, "注册结果", "服务器错误");
    }
}

// 注册按钮槽函数
void Widget::on_register_button_clicked()
{
    // 1. 获取 appid 和 password
    QString appid = ui->appid_lineEdit->text();
    QString password = ui->Password_lineEdit->text();
    // 2. 组装json
    QJsonObject json;
    json["cmd"] = "app_register";
    json["appid"] = appid;
    json["password"] = password;
    // 3. 发送给服务器
    m_socket->WriteData(json);
}


// 登录按钮槽函数
void Widget::on_login_button_clicked()
{
    // 1. 获取 appid 和 password
    QString appid = ui->appid_lineEdit->text();
    QString password = ui->Password_lineEdit->text();
    // 2. 组装json
    QJsonObject json;
    json["cmd"] = "app_login";
    json["appid"] = appid;
    json["password"] = password;
    // 3. 发送给服务器
    m_socket->WriteData(json);
}
