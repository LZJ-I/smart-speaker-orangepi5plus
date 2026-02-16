#include "database.h"

#include <openssl/sha.h>  // SHA256哈希依赖
#include <iomanip>        // 哈希转十六进制用
#include <sstream>      //

// 数据库构造函数
Database::Database()
{
    m_sql = mysql_init(NULL);   // 初始化数据库
}

// 数据库析构函数
Database::~Database()
{
    mysql_close(m_sql);   // 关闭数据库连接
}

// 数据库连接函数
bool Database::database_connect(void)
{
    // 先连接到MySQL服务器
    m_sql = mysql_real_connect(m_sql, DB_HOST, DB_USER, DB_PASS, NULL, 0, NULL, 0);
    if (m_sql == NULL) {
        std::cout << "服务器连接失败: " << mysql_error(m_sql) << std::endl;
        return false;
    }

    // 检查数据库是否存在
    std::string check_query = "SHOW DATABASES LIKE '";
    char escaped_db[256];

    mysql_real_escape_string(m_sql, escaped_db, DB_NAME, strlen(DB_NAME));
    check_query += escaped_db; // 添加转义后的数据库名
    check_query += "'";
    if (mysql_query(m_sql, check_query.c_str())) {  // 执行语句，检查数据库是否存在
        std::cout << "检查数据库失败: " << mysql_error(m_sql) << std::endl;
        mysql_close(m_sql); // 关闭连接
        return false;
    }

    // 检查查询结果是否为空
    MYSQL_RES* result = mysql_store_result(m_sql);
    bool db_exists = (result && mysql_num_rows(result) > 0);    // 如果结果集不为空，说明数据库存在
    if (result) mysql_free_result(result); // 释放结果集

    // 如果数据库不存在，就创建它
    if (!db_exists) {
        std::string create_query = "CREATE DATABASE ";  // 创建数据库的SQL
        create_query += DB_NAME;
        if (mysql_query(m_sql, create_query.c_str())) {     // 执行创建数据库
            std::cout << "创建数据库失败: " << mysql_error(m_sql) << std::endl;
            mysql_close(m_sql);
            return false;
        }
        std::cout << "未检测到数据库，数据库已自动创建: " << DB_NAME << std::endl;
    }

    // 选择数据库
    if (mysql_query(m_sql, "USE " DB_NAME)) {
        std::cout << "选择数据库失败: " << mysql_error(m_sql) << std::endl;
        mysql_close(m_sql);
        return false;
    }

    // 设置字符集
    mysql_set_character_set(m_sql, "utf8");

    return true;
}



// 数据库断开连接函数
bool Database::database_disconnect(void)
{
    mysql_close(m_sql);
    std::cout << "数据库连接已断开" << std::endl;
    return true;
}

// 数据库初始化表函数
bool Database::database_init_table(void) {
    // 检查账户表是否存在
    std::string check_table = "SHOW TABLES LIKE '";
    char escaped_table[256];
    mysql_real_escape_string(m_sql, escaped_table, DB_ACCOUNT_TABLE, strlen(DB_ACCOUNT_TABLE));
    check_table += escaped_table; // 添加转义后的表名
    check_table += "'";
    
    if (mysql_query(m_sql, check_table.c_str())) {
        std::cout << "检查表失败: " << mysql_error(m_sql) << std::endl;
        return false;
    }

    MYSQL_RES* result = mysql_store_result(m_sql);
    bool table_exists = (result && mysql_num_rows(result) > 0);     // 如果结果集不为空，说明表存在
    if (result) mysql_free_result(result); 

    // 如果账户表不存在，就创建它
    if (!table_exists) {
        // 创建账户表（带密码哈希字段）
        std::string create_table = 
            "CREATE TABLE " DB_ACCOUNT_TABLE " ("       // 创建账户表（带密码哈希字段）
            "id INT AUTO_INCREMENT PRIMARY KEY,"        // 账户ID，自动递增，主键
            "appid VARCHAR(50) NOT NULL UNIQUE,"        // appid，唯一，非空
            "deviceid VARCHAR(50) DEFAULT NULL,"        // 设备ID，默认为空
            "password_hash CHAR(64) NOT NULL, "         // 密码哈希值，存储SHA256哈希值
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP" // 创建时间，默认当前时间
            ")";
        
        if (mysql_query(m_sql, create_table.c_str())) {
            std::cout << "创建表失败: " << mysql_error(m_sql) << std::endl;
            return false;
        }
        std::cout << "未检测到 " << DB_ACCOUNT_TABLE << " 表，已自动创建" << std::endl;
    }
    return true;
}



// 辅助函数：SHA256哈希（输入明文，返回64位十六进制哈希字符串）
std::string Database::sha256_hash(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];  // SHA256固定输出32字节（256位）
    SHA256_CTX sha256;
    SHA256_Init(&sha256);    // 初始化哈希上下文
    SHA256_Update(&sha256, input.c_str(), input.size());    // 输入明文密码
    SHA256_Final(hash, &sha256);    // 生成32字节哈希值

    // 转为64位十六进制字符串  32字节 → 64位十六进制字符串（1字节=2位十六进制）
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// 注册函数：输入appid和明文密码，返回注册结果
// 注册成功返回0
// appid已存在返回1
// 其他错误返回-1
int Database::user_register(const std::string& username, const std::string& password) {
    // 1. 参数合法性检查（appid/密码非空，长度限制）
    if (username.size() <= 3) {
        std::cout << "注册失败：appid不能小于3个字符" << std::endl;
        return -1;
    }
    if (password.size() <= 3) {
        std::cout << "注册失败：密码不能小于3个字符" << std::endl;
        return -1;
    }

    // 2. 转义appid（防止SQL注入）
    char escaped_username[128];  // appid最大50，转义后最多100字节
    mysql_real_escape_string(m_sql, escaped_username, username.c_str(), username.size());

    // 3. 检查appid是否已存在
    std::string check_sql = "SELECT appid FROM " DB_ACCOUNT_TABLE " WHERE appid = '";
    check_sql += escaped_username;
    check_sql += "'";

    if (mysql_query(m_sql, check_sql.c_str())) {
        std::cout << "查询appid失败：" << mysql_error(m_sql) << std::endl;
        return -1;
    }

    MYSQL_RES* result = mysql_store_result(m_sql);
    if (result && mysql_num_rows(result) > 0) {
        std::cout << "注册失败：appid '" << username << "' 已存在" << std::endl;
        mysql_free_result(result);
        return 1;
    }
    if (result) mysql_free_result(result);  // 释放结果集

    // 4. 对密码进行SHA256哈希（不存储明文）
    std::string password_hash = sha256_hash(password);
    // std::cout<< "密码：" << password<< "转存后哈希："<<password_hash << std::endl;
    // 5. 转义密码哈希（虽然哈希是0-9a-f，但转义更安全）
    char escaped_hash[130];  // 64位哈希，转义后最多130字节
    mysql_real_escape_string(m_sql, escaped_hash, password_hash.c_str(), password_hash.size());

    // 6. 构造插入SQL，插入新用户
    std::string insert_sql = "INSERT INTO " DB_ACCOUNT_TABLE " (appid, password_hash) VALUES ('";
    insert_sql += escaped_username;
    insert_sql += "', '";
    insert_sql += escaped_hash;
    insert_sql += "')";

    if (mysql_query(m_sql, insert_sql.c_str())) {
        std::cout << "注册失败：" << mysql_error(m_sql) << std::endl;
        return -1;
    }

    // 7. 注册成功
    unsigned long user_id = mysql_insert_id(m_sql);
    std::cout << "注册成功！appid：" << username << "，用户ID：" << user_id << std::endl;
    return 0;
}


// 登录验证函数：输入appid和明文密码，返回验证结果
// 返回值说明：
//  0  登录成功，且已绑定deviceid（ 将deviceid赋值， 其余情况为空）
//  1  appid不存在
//  2  密码错误
//  3  登录成功，但用户未绑定deviceid
// -1  服务器/数据库错误（参数非法、SQL执行失败等）
int Database::user_login(const std::string& username, const std::string& password, std::string& deviceid)
{
    // 1. 基础参数校验（避免无效数据库请求）
    if (username.empty() || username.size() <= 3 || password.empty() || password.size() <= 3) {
        std::cout << "登录失败：appid或密码格式非法" << std::endl;
        return -1;
    }
    deviceid.clear();

    // 2. appid SQL转义（防止SQL注入，必须执行）
    char escaped_username[128] = {0}; // appid最大50字符，转义后足够存储
    mysql_real_escape_string(m_sql, escaped_username, username.c_str(), username.size());

    // 3. 构造查询SQL：查询appid对应的密码哈希和deviceid（新增deviceid字段查询）
    std::string query_sql = "SELECT id, password_hash, deviceid FROM " DB_ACCOUNT_TABLE 
                           " WHERE appid = '" + std::string(escaped_username) + "' LIMIT 1";

    // 执行SQL查询
    if (mysql_query(m_sql, query_sql.c_str())) {
        std::cout << "登录查询失败：" << mysql_error(m_sql) << " [SQL: " << query_sql << "]" << std::endl;
        return -1;
    }

    // 提取查询结果集
    MYSQL_RES* result = mysql_store_result(m_sql);
    if (!result) {
        std::cout << "登录结果集获取失败：" << mysql_error(m_sql) << std::endl;
        return -1;
    }

    // 4. 检查appid是否存在（结果集行数为0 → 不存在）
    if (mysql_num_rows(result) == 0) {
        std::cout << "登录失败：appid '" << username << "' 不存在" << std::endl;
        mysql_free_result(result); // 释放结果集（避免内存泄漏）
        return 1;
    }

    // 5. 提取数据库中的用户ID、密码哈希、deviceid（新增deviceid提取）
    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row || !row[0] || !row[1]) { // row[0] = id, row[1] = password_hash, row[2] = deviceid
        std::cout << "登录失败：数据库字段读取异常" << std::endl;
        mysql_free_result(result);
        return -1;
    }
    std::string db_user_id = row[0];          // 用户ID
    std::string db_password_hash = row[1];    // 数据库存储的SHA256哈希
    std::string db_deviceid = row[2] ? row[2] : ""; // deviceid（处理NULL值为空字符串）
    mysql_free_result(result); // 释放结果集

    // 6. 对输入密码进行SHA256哈希（与注册时的加密规则一致）
    std::string input_password_hash = sha256_hash(password);

    // 7. 比对哈希值（避免明文传输和存储，安全校验）
    if (input_password_hash != db_password_hash) {
        std::cout << "登录失败：密码错误（appid：" << username << "）" << std::endl;
        return 2;
    }

    // 8. 判断deviceid是否绑定
    if (db_deviceid.empty()) {
        // 登录成功，但未绑定deviceid
        std::cout << "登录成功：appid = " << username << "，用户ID = " << db_user_id << "（未绑定deviceid）" << std::endl;
        return 3;
    } else {
        // 登录成功，且已绑定deviceid
        deviceid = db_deviceid; // 将找到的deviceid返回。
        std::cout << "登录成功：appid = " << username << "，用户ID = " << db_user_id << "，绑定的deviceid = " << db_deviceid << std::endl;
        return 0;
    }
}


// 设备与appid绑定函数：输入deviceid和appid，返回绑定结果
// 返回值说明：
//  0  绑定成功
//  1  deviceid已被其他appid绑定
// -1  过程出错（参数非法、SQL执行失败、appid不存在等）
int Database::user_bind(const std::string& deviceid, const std::string& appid)
{
    // 1. 基础参数校验
    if (deviceid.empty() || deviceid.size() > 50 ||  // deviceid对应表中VARCHAR(50)
        appid.empty() || appid.size() <= 3) {        // appid需符合现有长度限制（>3字符）
        std::cout << "绑定失败：deviceid或appid格式非法" << std::endl;
        return -1;
    }

    // 2. SQL转义（防止SQL注入）
    char escaped_deviceid[128] = {0};  // 预留足够空间存储转义后的数据
    char escaped_appid[128] = {0};
    mysql_real_escape_string(m_sql, escaped_deviceid, deviceid.c_str(), deviceid.size());
    mysql_real_escape_string(m_sql, escaped_appid, appid.c_str(), appid.size());

    // 3. 构造查询SQL：检查deviceid是否存在
    std::string query_sql = "SELECT appid FROM " DB_ACCOUNT_TABLE
                           " WHERE deviceid = '" + std::string(escaped_deviceid) + "' LIMIT 1";

    // 执行查询
    if (mysql_query(m_sql, query_sql.c_str())) {
        std::cout << "绑定失败：查询deviceid异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    // 获取查询结果集
    MYSQL_RES* result = mysql_store_result(m_sql);
    if (!result) {
        std::cout << "绑定失败：结果集获取异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    // 4. 检查deviceid是否存在：存在则直接返回1
    if (mysql_num_rows(result) > 0) {
        mysql_free_result(result);  // 及时释放结果集
        std::cout << "绑定失败：deviceid '" << deviceid << "' 已被绑定" << std::endl;
        return 1;
    }

    // 5. deviceid不存在，释放结果集后继续校验appid
    mysql_free_result(result);

    // 搜索appid对应的记录
    std::string query_app_sql = "SELECT id FROM " DB_ACCOUNT_TABLE
                               " WHERE appid = '" + std::string(escaped_appid) + "' LIMIT 1";
    if (mysql_query(m_sql, query_app_sql.c_str())) {
        std::cout << "绑定失败：查询appid异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    MYSQL_RES* app_result = mysql_store_result(m_sql);
    if (!app_result) {
        std::cout << "绑定失败：appid结果集获取异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    // 检查appid是否存在
    if (mysql_num_rows(app_result) == 0) {
        std::cout << "绑定失败：appid '" << appid << "' 不存在" << std::endl;
        mysql_free_result(app_result);
        return -1;
    }
    mysql_free_result(app_result);  // 释放appid查询结果集

    // 6. 执行更新：将deviceid写入appid对应的记录
    std::string update_sql = "UPDATE " DB_ACCOUNT_TABLE
                            " SET deviceid = '" + std::string(escaped_deviceid) + "'"
                            " WHERE appid = '" + std::string(escaped_appid) + "'";

    if (mysql_query(m_sql, update_sql.c_str())) {
        std::cout << "绑定失败：更新deviceid异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    // 验证更新结果
    if (mysql_affected_rows(m_sql) <= 0) {
        std::cout << "绑定失败：未找到可更新的appid记录（可能已被其他进程修改）" << std::endl;
        return -1;
    }

    // 绑定成功（通过appid写入deviceid）
    std::cout << "绑定成功：appid '" << appid << "' 已绑定deviceid '" << deviceid << "'" << std::endl;
    return 0;
}
