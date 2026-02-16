#include "bind.h"
#include "ui_bind.h"
#include "player.h"

Bind::Bind(Socket *socket, QString appid, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Bind)
{
    ui->setupUi(this);
    this->m_appid = appid;
    this->m_socket = socket;

    // 绑定读取数据的信号和槽
    connect(m_socket, &Socket::readyRead, this, &Bind::server_reply_slot);
}

Bind::~Bind()
{
    delete ui;
}

void Bind::server_reply_slot(void)
{
    QJsonObject root;
    m_socket->ReadData(root);

    QString cmd = root["cmd"].toString();
    if(cmd != "reply_app_bind")         // app绑定
        qDebug()<<"出现未知cmd指令:"<<cmd;

    QString res = root["result"].toString();
    if(res == "success"){
        // 绑定成功
        QMessageBox::information(nullptr, "绑定结果", "绑定成功");
        // 断开Socket信号与Bind槽的连接，Widget不再接收消息
        disconnect(m_socket, &Socket::readyRead, this, &Bind::server_reply_slot);
        // 显示控制界面(传输的数据为 socket、 appid和 deviceid)
        Player* m_player = new Player(m_socket, m_appid, root["deviceid"].toString());
        this->hide();    //隐藏此界面
        m_player->show(); //显示控制界面
    }
    else if(res == "isbind"){
        // 设备ID已经被绑定了
        QMessageBox::information(nullptr, "绑定结果", "此设备ID已经被其他APP绑定了");
    }
    else if(res == "devidshort"){
        // 输入的deviceid太短了
        QMessageBox::information(nullptr, "绑定结果", "输入的设备ID过短，请重新尝试");
    }
    else{
        // 其他错误
        QMessageBox::information(nullptr, "绑定结果", "服务器错误");
    }

}


// 单击绑定按钮
void Bind::on_bind_button_clicked()
{
    // 1. 获取要绑定的device id
    QString deviceid = ui->devuceid_lineEdit->text();
    // 2. 组装json
    QJsonObject json;
    json["cmd"] = "app_bind";
    json["appid"] = m_appid;
    json["deviceid"] = deviceid;
    // 3. 发送给服务器
    m_socket->WriteData(json);
}
