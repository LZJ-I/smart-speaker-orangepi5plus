#include "database.h"

#include <iomanip>
#include <openssl/sha.h>
#include <sstream>

namespace {

void print_mysql_setup_hint(void)
{
    std::cerr << "\n[MySQL] 请在本机用管理员进入 mysql（例如: sudo mysql），执行:\n\n"
              << "CREATE DATABASE IF NOT EXISTS " DB_NAME " CHARACTER SET utf8mb4;\n"
              << "CREATE USER IF NOT EXISTS '" DB_USER "'@'localhost' IDENTIFIED BY '" DB_PASS "';\n"
              << "GRANT ALL ON " DB_NAME ".* TO '" DB_USER "'@'localhost';\n"
              << "FLUSH PRIVILEGES;\n\n"
              << "当前程序使用的连接: 主机=" DB_HOST " 用户=" DB_USER " 数据库=" DB_NAME "\n"
              << "若已改密码，请同步修改 include/database.h 中的 DB_USER/DB_PASS 后重新 make。\n"
              << "并确认 mysqld 已启动: sudo systemctl start mariadb 或 mysql\n\n";
}

}  // namespace

Database::Database() : m_sql(NULL) { m_sql = mysql_init(NULL); }

Database::~Database()
{
    if (m_sql) {
        mysql_close(m_sql);
        m_sql = NULL;
    }
}

bool Database::database_connect(void)
{
    if (!m_sql) {
        m_sql = mysql_init(NULL);
    }
    if (!m_sql) {
        std::cerr << "[MySQL] mysql_init 失败" << std::endl;
        print_mysql_setup_hint();
        return false;
    }

    if (!mysql_real_connect(m_sql, DB_HOST, DB_USER, DB_PASS, NULL, 0, NULL, 0)) {
        std::cerr << "[MySQL] 连接失败: " << mysql_error(m_sql) << " (errno=" << mysql_errno(m_sql) << ")"
                  << std::endl;
        print_mysql_setup_hint();
        return false;
    }

    std::string check_query = "SHOW DATABASES LIKE '";
    char escaped_db[256];
    mysql_real_escape_string(m_sql, escaped_db, DB_NAME, strlen(DB_NAME));
    check_query += escaped_db;
    check_query += "'";
    if (mysql_query(m_sql, check_query.c_str())) {
        std::cerr << "[MySQL] 检查数据库是否存在失败: " << mysql_error(m_sql) << std::endl;
        print_mysql_setup_hint();
        return false;
    }

    MYSQL_RES *result = mysql_store_result(m_sql);
    bool db_exists = (result && mysql_num_rows(result) > 0);
    if (result)
        mysql_free_result(result);

    if (!db_exists) {
        std::string create_query = "CREATE DATABASE ";
        create_query += DB_NAME;
        if (mysql_query(m_sql, create_query.c_str())) {
            std::cerr << "[MySQL] 库 " DB_NAME " 不存在且自动创建失败: " << mysql_error(m_sql) << std::endl;
            print_mysql_setup_hint();
            return false;
        }
        std::cout << "未检测到数据库，数据库已自动创建: " << DB_NAME << std::endl;
    }

    std::string use_db = "USE ";
    use_db += DB_NAME;
    if (mysql_query(m_sql, use_db.c_str())) {
        std::cerr << "[MySQL] 无法 USE 数据库 " DB_NAME ": " << mysql_error(m_sql) << std::endl;
        print_mysql_setup_hint();
        return false;
    }

    mysql_set_character_set(m_sql, "utf8mb4");
    return true;
}

bool Database::database_disconnect(void)
{
    if (m_sql) {
        mysql_close(m_sql);
        m_sql = NULL;
    }
    std::cout << "数据库连接已断开" << std::endl;
    return true;
}

bool Database::database_init_table(void)
{
    std::string check_table = "SHOW TABLES LIKE '";
    char escaped_table[256];
    mysql_real_escape_string(m_sql, escaped_table, DB_ACCOUNT_TABLE, strlen(DB_ACCOUNT_TABLE));
    check_table += escaped_table;
    check_table += "'";

    if (mysql_query(m_sql, check_table.c_str())) {
        std::cerr << "[MySQL] 检查表 " DB_ACCOUNT_TABLE " 失败: " << mysql_error(m_sql) << std::endl;
        print_mysql_setup_hint();
        return false;
    }

    MYSQL_RES *result = mysql_store_result(m_sql);
    bool table_exists = (result && mysql_num_rows(result) > 0);
    if (result)
        mysql_free_result(result);

    if (!table_exists) {
        std::string create_table =
            "CREATE TABLE " DB_ACCOUNT_TABLE " ("
            "id INT AUTO_INCREMENT PRIMARY KEY,"
            "appid VARCHAR(50) NOT NULL UNIQUE,"
            "deviceid VARCHAR(50) DEFAULT NULL,"
            "password_hash CHAR(64) NOT NULL, "
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
            ")";

        if (mysql_query(m_sql, create_table.c_str())) {
            std::cerr << "[MySQL] 表 " DB_ACCOUNT_TABLE " 不存在且自动创建失败: " << mysql_error(m_sql)
                      << std::endl;
            print_mysql_setup_hint();
            return false;
        }
        std::cout << "未检测到 " << DB_ACCOUNT_TABLE << " 表，已自动创建" << std::endl;
    }
    return true;
}

std::string Database::sha256_hash(const std::string &input)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(input.c_str()), input.size(), hash);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

int Database::user_register(const std::string &username, const std::string &password)
{
    if (username.size() <= 3) {
        std::cout << "注册失败：appid不能小于3个字符" << std::endl;
        return -1;
    }
    if (password.size() <= 3) {
        std::cout << "注册失败：密码不能小于3个字符" << std::endl;
        return -1;
    }

    char escaped_username[128];
    mysql_real_escape_string(m_sql, escaped_username, username.c_str(), username.size());

    std::string check_sql = "SELECT appid FROM " DB_ACCOUNT_TABLE " WHERE appid = '";
    check_sql += escaped_username;
    check_sql += "'";

    if (mysql_query(m_sql, check_sql.c_str())) {
        std::cout << "查询appid失败：" << mysql_error(m_sql) << std::endl;
        return -1;
    }

    MYSQL_RES *result = mysql_store_result(m_sql);
    if (result && mysql_num_rows(result) > 0) {
        std::cout << "注册失败：appid '" << username << "' 已存在" << std::endl;
        mysql_free_result(result);
        return 1;
    }
    if (result)
        mysql_free_result(result);

    std::string password_hash = sha256_hash(password);
    char escaped_hash[130];
    mysql_real_escape_string(m_sql, escaped_hash, password_hash.c_str(), password_hash.size());

    std::string insert_sql = "INSERT INTO " DB_ACCOUNT_TABLE " (appid, password_hash) VALUES ('";
    insert_sql += escaped_username;
    insert_sql += "', '";
    insert_sql += escaped_hash;
    insert_sql += "')";

    if (mysql_query(m_sql, insert_sql.c_str())) {
        std::cout << "注册失败：" << mysql_error(m_sql) << std::endl;
        return -1;
    }

    unsigned long user_id = mysql_insert_id(m_sql);
    std::cout << "注册成功！appid：" << username << "，用户ID：" << user_id << std::endl;
    return 0;
}

int Database::user_login(const std::string &username, const std::string &password, std::string &deviceid)
{
    if (username.empty() || username.size() <= 3 || password.empty() || password.size() <= 3) {
        std::cout << "登录失败：appid或密码格式非法" << std::endl;
        return -1;
    }
    deviceid.clear();

    char escaped_username[128] = {0};
    mysql_real_escape_string(m_sql, escaped_username, username.c_str(), username.size());

    std::string query_sql = "SELECT id, password_hash, deviceid FROM " DB_ACCOUNT_TABLE
                            " WHERE appid = '" +
                            std::string(escaped_username) + "' LIMIT 1";

    if (mysql_query(m_sql, query_sql.c_str())) {
        std::cout << "登录查询失败：" << mysql_error(m_sql) << std::endl;
        return -1;
    }

    MYSQL_RES *result = mysql_store_result(m_sql);
    if (!result) {
        std::cout << "登录结果集获取失败：" << mysql_error(m_sql) << std::endl;
        return -1;
    }

    if (mysql_num_rows(result) == 0) {
        std::cout << "登录失败：appid '" << username << "' 不存在" << std::endl;
        mysql_free_result(result);
        return 1;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row || !row[0] || !row[1]) {
        std::cout << "登录失败：数据库字段读取异常" << std::endl;
        mysql_free_result(result);
        return -1;
    }
    std::string db_user_id = row[0];
    std::string db_password_hash = row[1];
    std::string db_deviceid = row[2] ? row[2] : "";
    mysql_free_result(result);

    std::string input_password_hash = sha256_hash(password);

    if (input_password_hash != db_password_hash) {
        std::cout << "登录失败：密码错误（appid：" << username << "）" << std::endl;
        return 2;
    }

    if (db_deviceid.empty()) {
        std::cout << "登录成功：appid = " << username << "，用户ID = " << db_user_id << "（未绑定deviceid）" << std::endl;
        return 3;
    }
    deviceid = db_deviceid;
    std::cout << "登录成功：appid = " << username << "，用户ID = " << db_user_id << "，绑定的deviceid = " << db_deviceid
              << std::endl;
    return 0;
}

int Database::user_bind(const std::string &deviceid, const std::string &appid)
{
    if (deviceid.empty() || deviceid.size() > 50 || appid.empty() || appid.size() <= 3) {
        std::cout << "绑定失败：deviceid或appid格式非法" << std::endl;
        return -1;
    }

    char escaped_deviceid[128] = {0};
    char escaped_appid[128] = {0};
    mysql_real_escape_string(m_sql, escaped_deviceid, deviceid.c_str(), deviceid.size());
    mysql_real_escape_string(m_sql, escaped_appid, appid.c_str(), appid.size());

    std::string query_sql = "SELECT appid FROM " DB_ACCOUNT_TABLE " WHERE deviceid = '" +
                            std::string(escaped_deviceid) + "' LIMIT 1";

    if (mysql_query(m_sql, query_sql.c_str())) {
        std::cout << "绑定失败：查询deviceid异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    MYSQL_RES *result = mysql_store_result(m_sql);
    if (!result) {
        std::cout << "绑定失败：结果集获取异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    if (mysql_num_rows(result) > 0) {
        mysql_free_result(result);
        std::cout << "绑定失败：deviceid '" << deviceid << "' 已被绑定" << std::endl;
        return 1;
    }

    mysql_free_result(result);

    std::string query_app_sql =
        "SELECT id FROM " DB_ACCOUNT_TABLE " WHERE appid = '" + std::string(escaped_appid) + "' LIMIT 1";
    if (mysql_query(m_sql, query_app_sql.c_str())) {
        std::cout << "绑定失败：查询appid异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    MYSQL_RES *app_result = mysql_store_result(m_sql);
    if (!app_result) {
        std::cout << "绑定失败：appid结果集获取异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    if (mysql_num_rows(app_result) == 0) {
        std::cout << "绑定失败：appid '" << appid << "' 不存在" << std::endl;
        mysql_free_result(app_result);
        return -1;
    }
    mysql_free_result(app_result);

    std::string update_sql = "UPDATE " DB_ACCOUNT_TABLE " SET deviceid = '" + std::string(escaped_deviceid) +
                             "' WHERE appid = '" + std::string(escaped_appid) + "'";

    if (mysql_query(m_sql, update_sql.c_str())) {
        std::cout << "绑定失败：更新deviceid异常 - " << mysql_error(m_sql) << std::endl;
        return -1;
    }

    if (mysql_affected_rows(m_sql) <= 0) {
        std::cout << "绑定失败：未找到可更新的appid记录（可能已被其他进程修改）" << std::endl;
        return -1;
    }

    std::cout << "绑定成功：appid '" << appid << "' 已绑定deviceid '" << deviceid << "'" << std::endl;
    return 0;
}
