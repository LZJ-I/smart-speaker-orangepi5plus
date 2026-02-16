#include "player.h"
#include "server.h"

// 定时器 超时回调函数
void PlayerInfo::player_timer_cb(evutil_socket_t fd, short events, void* arg)
{
    PlayerInfo *p = (PlayerInfo*)arg;
    
    // 遍历 音箱数据链表，检查是否有超时的 嵌入式客户端/APP
    for(auto it = p->m_player_list->begin(); it != p->m_player_list->end(); ++it)
    {
        // 判断 app是否超时
        if(time(NULL) - it->m_app_last_time > TIMEOUT && !it->m_appid.empty()) //超过三次未更新时间，判断为超时
        {
            Server::debug("[超时的APP] APPID：%s", it->m_appid.c_str());
            it->m_appid.clear();        // 清空 APPID
            // 释放 APP的 bufferevent
            if(it->m_app_bev != nullptr)
            {
                bufferevent_free(it->m_app_bev);
                it->m_app_bev = nullptr;
            }
        }
        // 判断 device是否超时
        if(time(NULL) - it->m_device_last_time > TIMEOUT) //超过三次未更新时间，判断为超时
        {
            Server::debug("[有音箱超时了] 音箱ID：%s", it->m_deviceid.c_str());
            // 释放 dev的 bufferevent
            if(it->m_device_bev != nullptr)
            {
                bufferevent_free(it->m_device_bev);
                it->m_device_bev = nullptr;
            }
            // 释放 APP的 bufferevent
            if(it->m_app_bev != nullptr)
            {
                bufferevent_free(it->m_app_bev);
                it->m_app_bev = nullptr;
            }
            p->m_player_list->erase(it); // 删除超时项
        }
    }
}

void PlayerInfo::player_start_timer(Server* s)
{
    // 1. 设置 定时器超时时间
    struct timeval timeout;     
    evutil_timerclear(&timeout);
    timeout.tv_sec = 2;   // 秒
    timeout.tv_usec = 0;  // 微秒

    // 2. 创建定时器事件
    // 参数说明：event_base, fd(-1=定时器无需fd), 事件类型(EV_PERSIST=持续触发), 回调函数, 回调参数
    m_timer_event = event_new(
        s->server_get_eventbase(),
        -1,
        EV_PERSIST,  // 持续触发
        player_timer_cb,
        this         // 回调函数的参数
    );

    // 3. 校验定时器创建是否成功
    if (m_timer_event == NULL) {
        Server::debug("定时器事件创建失败");
        return;
    }

    // 4. 将定时器添加到事件循环
    if (event_add(m_timer_event, &timeout) != 0) {
        Server::debug("定时器添加到事件循环失败");
        event_free(m_timer_event); // 创建成功但添加失败，释放资源
        m_timer_event = NULL;
        return;
    }

    Server::debug("定时器已启动，超时时间：%d秒", timeout.tv_sec);
}

PlayerInfo::PlayerInfo()
{
    m_player_list = new std::list<PlayerInfo_t>();
    m_timer_event = NULL; // 初始化定时器对象指针
}

PlayerInfo::~PlayerInfo()
{
    // 释放 音箱数据列表
    if(m_player_list != NULL)
    {
        delete m_player_list;
        m_player_list = NULL;
    }
    // 释放定时器事件
    if (m_timer_event != NULL) {
        event_free(m_timer_event); // 释放动态创建的事件
        m_timer_event = NULL;
        Server::debug("定时器事件已释放");
    }
}

// 更新 列表信息列表、通知应用端（在嵌入式客户端上报时调用）
void PlayerInfo::player_device_update_infolist(struct bufferevent* bev, Json::Value& report, Server* s)
{
    bool is_exist = false;
    // 遍历链表，如果设备存在，那么更新链表并且转发给服务器
    auto it = m_player_list->begin();
    for(; it != m_player_list->end(); it++)
    {
        if(report["deviceid"].asString() == it->m_deviceid)     // 找到对应的设备
        {
            // 更新链表
            it->m_cur_singer = report["cur_singer"].asString();
            it->m_cur_music = report["cur_music"].asString();
            it->m_state = report["state"].asString();
            it->m_cur_volume = report["cur_volume"].asInt();
            it->m_cur_mode = report["cur_mode"].asInt();
            it->m_device_last_time = time(NULL);
            it->m_device_bev = bev;

            
            std::cerr<<"[dev]"; // 嵌入式端上报
            if(it->m_app_bev != nullptr)   // 如果应用端也连接了，那么转发给应用端
            {
                // 转发给服务器
                // Server::debug("嵌入式端上报当前状态 转发给应用端");
                std::cerr<<"[d转发a]"; 
                // 上报嵌入式端状态给APP
                s->server_send_data(it->m_app_bev, report); // 转发给应用端
            }
            // 转发给服务器
            is_exist = true;
            break;
        }
    }
    // 如果设备不存在，那么添加到链表中，并且转发给服务器
    if(!is_exist)
    {
        // 添加到链表中
        PlayerInfo_t player_info;
        player_info.m_deviceid = report["deviceid"].asString();
        player_info.m_cur_singer = report["cur_singer"].asString();
        player_info.m_cur_music = report["cur_music"].asString();
        player_info.m_state = report["state"].asString();
        player_info.m_cur_volume = report["cur_volume"].asInt();
        player_info.m_cur_mode = report["cur_mode"].asInt();
        player_info.m_device_last_time = time(NULL);
        player_info.m_device_bev = bev;
        player_info.m_app_bev = nullptr;
        
        m_player_list->push_back(player_info);
        Server::debug("嵌入式端首次上报，添加到链表中");
    }
}


// 更新 列表信息列表（在应用端上报时调用）
void PlayerInfo::player_app_update_infolist(struct bufferevent* bev, Json::Value& report, Server* s)
{
    // 遍历链表，如果设备存在，那么将自己的信息存入节点
    auto it = m_player_list->begin();
    for(; it != m_player_list->end(); it++)
    {
        if(report["deviceid"].asString() == it->m_deviceid)     // 找到APP 绑定的设备 id对应的节点
        {
            // 更新链表
            it->m_app_last_time = time(NULL);
            it->m_app_bev = bev;    
            it->m_appid = report["appid"].asString();
            std::cerr<<"[app]"; // 应用端上报
            break;
        }
    }
}


// 更新 列表信息列表（在嵌入式端上报音乐列表时调用）
void PlayerInfo::player_device_update_music_list(struct bufferevent* bev, Json::Value& report, Server* s)
{
    // 获取音乐列表
    Json::Value music_list = report["music"];
    // 打印测试一下
    // Server::debug("音乐列表: %s", music_list.toStyledString().c_str());
    // 服务器转发给APP
    for(auto it = m_player_list->begin(); it != m_player_list->end(); it++)
    {
        if(it->m_device_bev == bev) // 找到设备对应的节点
        {
            // 如果应用端也连接了，那么转发给应用端
            if(it->m_app_bev != nullptr)
            {
                // 转发给应用端
                Server::debug("嵌入式端上报音乐列表 转发给应用端");
                s->server_send_data(it->m_app_bev, report); // 转发给应用端
            }
            else
            {
                Server::debug("嵌入式端上报音乐列表 应用端未连接");
            }
        }
    }
}


// 获取 音箱数据列表
std::list<PlayerInfo_t>* PlayerInfo::player_get_m_player_list(void)
{
    return m_player_list;
}


// 处理APP注册
void PlayerInfo::player_app_register(struct bufferevent* bev, Json::Value& json, Server* s)
{
    Json::Value result;
    result["cmd"] = "reply_app_register";

    // 提取用户ID，提取用户密码
    std::string appid = json["appid"].asString();
    std::string password = json["password"].asString();

    do
    {
        // 判断用户名是否符合要求
        if(appid.size() <= 3){
            result["result"] = "idshort";    //账号太短
            break;
        }
        // 判断密码是否符合要求
        if(password.size() <= 3){
            result["result"] = "passhort";    //密码太短
            break;
        }
        // 插入用户
        int res = -1;
        res = s->server_get_database()->user_register(appid, password);
        if(res == 1){
            result["result"] = "idexist";    //用户已存在
            break;
        }else if(res == -1){
            result["result"] = "failuse";    //失败（服务器原因）
            break;
        }
        else{
            result["result"] = "success";    //成功
            break;
        }
    } while (1);
    
    // 发送结果
    s->server_send_data(bev, result);
}

// 处理APP绑定设备
void PlayerInfo::player_app_bind(struct bufferevent* bev, Json::Value& json, Server* s)
{
    Json::Value result;
    result["cmd"] = "reply_app_bind"; // 响应指令，与客户端请求对应
    
    // 提取请求中appid和deviceid
    std::string appid = json["appid"].asString();
    std::string deviceid = json["deviceid"].asString();
    
    do
    {
        if (deviceid.size() <= 3) {
            result["result"] = "devidshort"; // 设备ID过短
            break;
        }
        // 2. 调用数据库进行搜索，是否被绑定
        int res = s->server_get_database()->user_bind(deviceid, appid);
        // 根据数据库返回结果构造响应
        if (res == 1) {
            result["result"] = "isbind";    // deviceid已经被其他app绑定
        }
        else if (res == -1) {
            result["result"] = "failuse"; // 服务器/数据库错误
        } else { // res == 0 绑定成功
            result["result"] = "success";
        }
    } while (0); // 单次循环，仅用于break跳转

    result["deviceid"] = deviceid;
    // 发送响应给客户端
    s->server_send_data(bev, result);
}

// 处理APP登录
void PlayerInfo::player_app_login(struct bufferevent* bev, Json::Value& json, Server* s)
{
    Json::Value result;
    result["cmd"] = "reply_app_login"; // 响应指令，与客户端请求对应

    // 提取请求中的用户名（appid）和密码
    std::string appid = json["appid"].asString();
    std::string password = json["password"].asString();
    std::string deviceid = "";
    do
    {
        // 1. 基础合法性校验（与注册规则一致）
        if (appid.size() <= 3 ) {
            result["result"] = "idshort"; // 账号太短（≤3字符）
            break;
        }
        if (password.size() <= 3) {
            result["result"] = "passhort"; // 密码太短（修正注册时的提示错误）
            break;
        }

        // 2. 调用数据库进行登录验证
        int res = s->server_get_database()->user_login(appid, password, deviceid);
        // 根据数据库返回结果构造响应
        if (res == 1) {
            result["result"] = "idnotexist"; // appid不存在
        }else if(res == 2){
            result["result"] = "passerr"; // 密码错误
        }else if(res == 3){
            result["result"] = "notbind"; // 登录成功，但没有绑定deviceid号
        }
        else if (res == -1) {
            result["result"] = "failuse"; // 服务器/数据库错误
        } else { // res == 0 登录成功
            result["result"] = "success";
            result["deviceid"] = deviceid;
        }
    } while (0); // 单次循环，仅用于break跳转

    // 发送响应给客户端
    s->server_send_data(bev, result);
}
