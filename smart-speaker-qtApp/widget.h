#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

#include "socket.h"
#include "bind.h"
#include "player.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT
public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void on_register_button_clicked();
    void on_login_button_clicked();
    void server_reply_slot(void);       // 注册/登录界面接收到服务器消息槽函数
    void onSocketDisconnected();        // 槽函数：处理掉线逻辑
private:
    Ui::Widget *ui;
    Socket* m_socket;
    Player *m_player = nullptr; // 管理Player界面指针
    Bind *m_bind = nullptr;     // 管理Bind界面指针
    void app_register_reply_handler(QJsonObject& root);         // 处理服务器回复的 [注册] 结果
    void app_login_reply_handler(QJsonObject& root);            // 处理服务器回复的 [登录] 结果

};
#endif // WIDGET_H
