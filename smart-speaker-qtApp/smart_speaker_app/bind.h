#ifndef BIND_H
#define BIND_H

#include <QWidget>
#include "socket.h"

namespace Ui {
class Bind;
}

class Bind : public QWidget
{
    Q_OBJECT

public:
    explicit Bind(Socket* socket, QString appid, QWidget *parent = nullptr);
    ~Bind();

private slots:
    void on_bind_button_clicked();
    void server_reply_slot(void);

private:
    Ui::Bind *ui;
    Socket* m_socket;
    QString m_appid;
};

#endif // BIND_H
