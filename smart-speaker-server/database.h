#ifndef __DATABASE_H
#define __DATABASE_H

#include <iostream>
#include <mysql/mysql.h>
#include <cstring>

// 数据库连接信息
#define DB_HOST "localhost"
#define DB_NAME "musicplayer_info"
#define DB_USER "musicplayer"
#define DB_PASS "musicplayer"
// 数据库 账户 表名
#define DB_ACCOUNT_TABLE "account"



class Database
{
    private:
        MYSQL* m_sql;   // 数据库句柄
        // 辅助函数：SHA256哈希（将密码转为64位十六进制哈希串）
        std::string sha256_hash(const std::string& input);
    public:
        Database();                         // 数据库构造函数
        ~Database();                        // 数据库析构函数
        bool database_connect(void);        // 数据库连接函数
        bool database_disconnect(void);     // 数据库断开连接函数
        bool database_init_table(void);     // 数据库初始化表函数

        int user_register(const std::string& username, const std::string& password);    // 注册函数
        int user_login(const std::string& username, const std::string& password, std::string& deviceid);       // 登录函数
        int user_bind(const std::string& deviceid, const std::string& appid);         // 绑定函数
};


#endif
