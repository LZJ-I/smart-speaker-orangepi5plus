#include "widget.h"

#include <QApplication>
#include <QIcon>
#include <QNetworkProxyFactory>

int main(int argc, char *argv[])
{
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(QStringLiteral(":/icons/lzj.png")));
    Widget w;
    w.show();
    return a.exec();
}
