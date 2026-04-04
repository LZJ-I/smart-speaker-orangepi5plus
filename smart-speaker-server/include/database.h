#ifndef __DATABASE_H
#define __DATABASE_H

#include <cstring>
#include <iostream>
#include <mysql/mysql.h>
#include <string>

#ifndef DB_HOST
#define DB_HOST "localhost"
#endif
#ifndef DB_NAME
#define DB_NAME "musicplayer_info"
#endif
#ifndef DB_USER
#define DB_USER "musicplayer"
#endif
#ifndef DB_PASS
#define DB_PASS "musicplayer"
#endif
#ifndef DB_ACCOUNT_TABLE
#define DB_ACCOUNT_TABLE "account"
#endif

class Database
{
private:
    MYSQL *m_sql;
    std::string sha256_hash(const std::string &input);

public:
    Database();
    ~Database();
    bool database_connect(void);
    bool database_disconnect(void);
    bool database_init_table(void);

    int user_register(const std::string &username, const std::string &password);
    int user_login(const std::string &username, const std::string &password, std::string &deviceid);
    int user_bind(const std::string &deviceid, const std::string &appid);
};

#endif
