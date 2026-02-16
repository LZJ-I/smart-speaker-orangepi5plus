#include <iostream>
using namespace std;

#include "server.h"



int main()
{
    cout << "hello world" << endl;
    // 创建服务器对象
    Server s;               // 创建服务器对象
    s.listen(IP, PORT);     // 监听客户端连接

    return 0;
}

