#include "widget.h"

#include <QApplication>
#include <QNetworkProxyFactory>

int main(int argc, char *argv[])
{
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QApplication a(argc, argv);
    Widget w;
    w.show();
    return a.exec();
}
