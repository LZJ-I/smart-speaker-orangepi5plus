#include "server.h"

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <memory> 
#include <sys/types.h>
#include <dirent.h>
#include <algorithm>       // std::shuffle 必需
#include <random>          // std::default_random_engine 必需
#include <chrono>          // std::chrono 时间种子必需
#include <functional>      // std::min
#include <event2/listener.h>

// 构造函数
Server::Server()
{
    // 初始化事件集合
    m_eventbase = event_base_new();
    // 初始化数据库
    m_database = new Database();
    if(m_database->database_connect() == false)     // 连接数据库
    {
        return;
    }
    debug("数据库连接成功！字符集已设为 utf8");



    if(m_database->database_init_table() == false)  // 初始化数据库表
    {
        Server::debug("数据库初始化表失败");
        return;
    }
    debug("数据库初始化表成功！");
    
    // 初始化 PlayerInfo 对象
    m_player_info = new PlayerInfo();
    // 初始化定时器
    m_player_info->player_start_timer(this);
}


// 析构函数
Server::~Server()
{
    // 释放数据库对象
    if(m_database != NULL)  
    {
        delete m_database;              
        printf("数据库已断开连接\n");
    }
    // 清理事件基础对象
    if(m_eventbase != NULL) 
    {
        event_base_free(m_eventbase);   
        printf("事件基础对象已释放\n");
    }
    // 释放 PlayerInfo
    if(m_player_info != NULL)
    {
        delete m_player_info;
        m_player_info = NULL;
        debug("PlayerInfo 已释放");
    }
}

// 获取 事件的集合
event_base* Server::server_get_eventbase(void)
{
    return m_eventbase;
}



// 监听函数
void Server::listen(const char* ip, int port)
{
    /*
            base：事件基础对象
            cb：回调函数
            ptr：用户指针(一般传this， 用于调用非static成员函数)
            flags：监听选项（
                LEV_OPT_CLOSE_ON_FREE：当监听器被释放时，关闭所有连接
                LEV_OPT_REUSEABLE：允许地址重用（允许在短时间内重新绑定到相同的地址和端口，方便调试）
            ）
            backlog：连接队列长度
            sockaddr: 服务端信息结构体
            socklen：地址长度
    */
    struct sockaddr_in server_info;
    memset(&server_info, 0, sizeof(server_info));
    server_info.sin_family = AF_INET;
    server_info.sin_addr.s_addr = inet_addr(ip);
    server_info.sin_port = htons(port);
    int socketlen = sizeof(server_info);
    // 创建监听对象, 将监听对象绑定到事件基础对象上
    struct evconnlistener* listener = evconnlistener_new_bind(m_eventbase, listener_cb, this,
            LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, 5, (struct sockaddr*)&server_info, socketlen);
    if(listener == NULL)
    {
        perror("evconnlistener_new_bind");
        return;
    }

    // 监听集合
    event_base_dispatch(m_eventbase);    // 进入事件循环，等待事件发生(没有任何事件时，退出事件循环)

    // 释放监听对象
    evconnlistener_free(listener);
    // 释放事件基础对象
    event_base_free(m_eventbase);
}



// 监听到新的客户端连接回调函数
// evconnlistener：监听对象
// fd：新连接的socket文件描述符
// c：客户端地址信息
// socklen：地址长度
// arg：用户指针(一般传this， 用于调用非static成员函数)
void Server::listener_cb(struct evconnlistener *l, evutil_socket_t fd, struct sockaddr *c, int socklen, void *arg)
{
    Server* s = (Server*)arg;   // 从用户指针中获取服务器对象
    struct sockaddr_in* client_info = (struct sockaddr_in*)c;

    s->debug("[新的客户端连接]: %s:%d", inet_ntoa(client_info->sin_addr), ntohs(client_info->sin_port));



    // 创建bufferevent对象，将其绑定到socket文件描述符上(BEV_OPT_CLOSE_ON_FREE: 当bufferevent被释放时，关闭socket文件描述符/缓冲区)
    struct bufferevent* bev = bufferevent_socket_new(s->m_eventbase, fd, BEV_OPT_CLOSE_ON_FREE);
    if(bev == NULL)
    {
        perror("bufferevent_socket_new");
        return;
    }
    // 为bufferevent设置回调函数
    bufferevent_setcb(bev, read_cb, NULL, event_cb, s);
    // 使能bufferevent事件（EV_READ：可读事件）
    bufferevent_enable(bev, EV_READ);
}

// bufferevent读取回调函数（有数据可读时调用）
void Server::read_cb(struct bufferevent *bev, void *ctx)
{
    Server* s = (Server*)ctx;   // 从用户指针中获取服务器对象

    // 1. 从bufferevent中读取数据, 并解析为json 根节点
    Json::Value root = {0};
    if(s->server_read_data(bev, root) == false)
    {
        Server::debug("数据读取或解析失败，跳过该消息");
        return;
    }
    // 2. 校验cmd字段（存在性 + 字符串类型）
    if(!root.isMember("cmd") || !root["cmd"].isString())
    {
        Server::debug("JSON格式错误：缺少cmd字段或cmd类型非法");
        return;
    }

    // 3. 处理合法命令
    std::string cmd = root["cmd"].asString();
    
    // 根据不同命令，调用不同的处理函数
    if(cmd == "get_music")
    {
        s->debug("[消息类型] 嵌入式端获取音乐列表");
        s->server_get_music(bev, root);
    }

    else if(cmd == "device_report")
    {
        //s->debug("[消息类型] 嵌入式端上报当前状态");
        s->m_player_info->player_device_update_infolist(bev, root, s); // 客户端上报数据
    }

    else if(cmd == "upload_music_list")
    {
        s->debug("[消息类型] 嵌入式端上传音乐列表");
        s->m_player_info->player_device_update_music_list(bev, root, s); // 更新音箱数据列表
    }
    // 如果为 APP上报 任务
    else if(root["cmd"].asString() == "app_report")
    {
        // Server::debug("[消息类型] 应用端上报当前状态");
        s->m_player_info->player_app_update_infolist(bev, root, s); // APP上报数据
    }
    // 如果为 APP注册 任务
    else if(root["cmd"].asString() == "app_register")
    {
        s->m_player_info->player_app_register(bev, root, s);    // APP注册 任务
    }
    else if(root["cmd"].asString() == "app_bind")        
    {
        s->m_player_info->player_app_bind(bev, root, s);    // APP绑定 设备
    }
    // 如果为 APP登录 任务
    else if(root["cmd"].asString() == "app_login")
    {
        s->m_player_info->player_app_login(bev, root, s);    // APP注册 任务
    }

/*
    app_report（APP 上报信息）
    app_start_play（请求开始播放）
    app_stop_play（请求结束播放）
    app_suspend_play（请求暂停播放）
    app_continue_play（请求继续播放）
    app_play_next_song（请求下一首歌）
    app_play_prev_song（请求上一首歌）
    app_add_volume（请求增加音量）
    app_sub_volume（请求减少音量）
    app_order_mode（请求顺序播放模式）
    app_single_mode（请求单曲循环模式）
    app_random_mode（请求随机播放模式）
    app_get_music_list（获取嵌入式端歌曲列表）
*/
    else if(cmd == "app_start_play" || cmd == "app_stop_play" ||            // 开始播放/结束播放
        cmd == "app_suspend_play" || cmd == "app_continue_play" ||      // 暂停播放/继续播放
        cmd == "app_play_next_song" || cmd == "app_play_prev_song" ||   // 请求下一首歌/上一首歌
        cmd == "app_add_volume" || cmd == "app_sub_volume" ||           // 请求增加音量/减少音量
        cmd == "app_order_mode" || cmd == "app_single_mode" || cmd == "app_random_mode" ||  // 请求顺序播放模式/单曲循环模式/随机播放模式
        cmd == "app_get_music_list")    // 获取嵌入式端歌曲列表
    {
        // 只要是APP控制单片机的命令，都调用这个函数。在回复时APP只需要在cmd关键字前面加上reply_）
        s->server_app_option(bev, root); 
    }

    // 嵌入式端回复APP的命令
    else if(cmd == "reply_app_start_play" || cmd == "reply_app_stop_play" ||            // 开始播放/结束播放
        cmd == "reply_app_suspend_play" || cmd == "reply_app_continue_play" ||      // 暂停播放/继续播放
        cmd == "reply_app_play_next_song" || cmd == "reply_app_play_prev_song" ||   // 请求下一首歌/上一首歌
        cmd == "reply_app_add_volume" || cmd == "reply_app_sub_volume" ||           // 请求增加音量/减少音量
        cmd == "reply_app_order_mode" || cmd == "reply_app_single_mode" || cmd == "reply_app_random_mode")  // 请求顺序播放模式/单曲循环模式/随机播放模式
    {
        // 只要是回复APP的命令，都直接转发
        s->server_device_reply_handle(bev, root);
    }
    else
    {
        s->debug("未知命令：%s", cmd.c_str());
    }
    
}
// 嵌入式端 回复 APP 
bool Server::server_device_reply_handle(struct bufferevent* bev, Json::Value& root)
{
    // 校验cmd字段（存在性 + 字符串类型）
    if(!root.isMember("cmd") || !root["cmd"].isString())
    {
        Server::debug("JSON格式错误：缺少cmd字段或cmd类型非法");
        return false;
    }

    // 通过bev（device_bev）找到 app的bev
    for(auto it = m_player_info->player_get_m_player_list()->begin(); it != m_player_info->player_get_m_player_list()->end(); it++)
    {
        if(it->m_device_bev == bev) // 如果链表中 有 device_bev 等于 bev 的节点
        {
            // 找到对应的 app_bev
            bufferevent* app_bev = it->m_app_bev;
            // 直接转发给 app_bev
            if(server_send_data(app_bev, root) == false)
            {
                Server::debug("发送回复消息失败");
                return false;
            }
            Server::debug("[回复应用端]: %s", root["cmd"].asString().c_str());
            return true;
        }
    }
    return true;
}



// 应用端 不同选项 【命令】 执行流程
bool Server::server_app_option(struct bufferevent* bev, Json::Value& root)
{
    // 校验cmd字段（存在性 + 字符串类型）
    if(!root.isMember("cmd") || !root["cmd"].isString())
    {
        Server::debug("JSON格式错误：缺少cmd字段或cmd类型非法");
        return false;
    }
    
    bool is_online = false;
    // 判断 音箱 是否在线
    auto it = m_player_info->player_get_m_player_list()->begin();
    for(it = m_player_info->player_get_m_player_list()->begin(); it != m_player_info->player_get_m_player_list()->end(); it++)
    {
        if(it->m_app_bev == bev) // 如果链表中 有 app_bev 等于 bev 的节点
        {
            // 在线
            is_online = true;
            break;
        }
    }
    // 如果 音箱 不在线，告知应用端
    if(!is_online)
    {
        // 在cmd关键字中添加reply_前缀
        root["cmd"] = "reply_" + root["cmd"].asString();
        // 在root下添加"result": "offline" （结果为离线）
        root["result"] = "offline";
        // 发送回复消息
        if(server_send_data(bev, root) == false)
        {
            Server::debug("发送回复消息失败");
            return false;
        }
        Server::debug("[回复应用端]: 嵌入式端不在线");
        return true;
    }
    // 如果 音箱 在线，那么直接转发给 音箱
    if(server_send_data(it->m_device_bev, root) == false)
    {
        Server::debug("发送回复消息失败");
        return false;
    }
    Server::debug("[转发应用端命令]: %s", root["cmd"].asString().c_str());


    return true;
}


// 嵌入式端 获取音乐列表 【命令】 执行流程
// 如果没有该歌手的音乐，返回数组内补空字符串，元素个数为1
// 如果该歌手的音乐不足MAX_MUSIC_NUM，返回数组内补空字符串，元素个数为MAX_MUSIC_NUM
// 如果有该歌手的音乐有MAX_MUSIC_NUM以上，返回数组MAX_MUSIC_NUM个随机音乐
bool Server::server_get_music(struct bufferevent* bev, Json::Value& root)
{
    // 校验singer字段（存在性 + 字符串类型）
    if(!root.isMember("singer") || !root["singer"].isString())
    {
        Server::debug("JSON格式错误：缺少singer字段或singer类型非法");
        return false;
    }

    Json::Value cmd = "reply_music";
    Json::Value music_list; // 音乐列表数组


    std::string singer = root["singer"].asString();
    // Server::debug("歌手名称: %s", singer.c_str());

    // 开始获取音乐数据(存放在了/var/www/html/music/)
    std::string music_path = MUSIC_PATH + singer;
    // 遍历音乐目录下的所有文件
    DIR* dir = opendir(music_path.c_str());
    if(dir == NULL)
    {
        Server::debug("找不到该歌手，目录不存在: %s", music_path.c_str());
        // 3. 构建回复消息（没有该歌手的音乐）
        Json::Value reply;
        reply["cmd"] = cmd;
        reply["music"].append("");  // 补空
        // 4. 发送回复消息
        server_send_data(bev, reply);
        // Server::debug("[回复获取音乐列表]: %s", reply.toStyledString().c_str());
        return false;
    }
    struct dirent* entry;
    std::vector<std::string> all_valid_musics;  // 临时存储所有符合条件的MP3路径
    
    // 1.收集所有有效MP3文件（过滤非文件、非MP3）
    while ((entry = readdir(dir)) != NULL)
    {
        // 1. 过滤非普通文件
        if (entry->d_type != DT_REG)
            continue;

        // 2. 过滤非MP3文件
        if (strstr(entry->d_name, ".mp3") == NULL)
            continue;

        // 3. 构建完整路径（歌手/文件名），存入临时列表
        std::string music_file = singer + "/" + entry->d_name;
        all_valid_musics.push_back(music_file);
    }
    closedir(dir);  // 关闭目录
    
    // 2.随机选取最多GET_MAX_MUSIC首歌曲（不重复）
    // 生成高质量随机种子
    unsigned random_seed = std::chrono::system_clock::now().time_since_epoch().count(); // 基于当前时间生成随机种子
    // 对所有有效歌曲洗牌（打乱顺序）
    std::shuffle(all_valid_musics.begin(), all_valid_musics.end(), std::default_random_engine(random_seed));

    // 选取：取前N首（N = 最小(目标数量, 有效歌曲总数)）
    int select_num = std::min(GET_MAX_MUSIC, static_cast<int>(all_valid_musics.size()));
    for (int i = 0; i < select_num; ++i)
    {
        music_list.append(all_valid_musics[i]);  // 加入最终列表
    }

    // 第三步：补充空字符串至GET_MAX_MUSIC首
    while (music_list.size() < GET_MAX_MUSIC)
    {
        music_list.append("");  // 不足时补空
    }
    // 打印测试一下
    // Server::debug("音乐列表: %s", music_list.toStyledString().c_str());



    // 3. 构建回复消息
    Json::Value reply;
    reply["cmd"] = cmd;
    reply["music"] = music_list;

    // 4. 发送回复消息    
    server_send_data(bev, reply);

    return true;

}

bool Server::server_send_data(struct bufferevent* bev, Json::Value& root)
{
     // Server::debug("发送数据: %s", root.toStyledString().c_str());
    // 1. 计算消息长度（JSON字符串长度）
    std::string data = root.toStyledString();
    unsigned int msg_len = data.size();
    // 将前四个字节存储int，后面为信息
    char msg_buf[sizeof(int) + msg_len] = {0};
    memcpy(msg_buf, &msg_len, sizeof(int));
    memcpy(msg_buf + sizeof(int), data.c_str(), msg_len);
    // 2. 发送消息体
    if(bufferevent_write(bev, msg_buf, sizeof(int) + msg_len) != 0)
    {
        Server::debug("发送消息体失败");
        return false;
    }

    return true;
}

// 从读取回调函数中提取内容, 返回解析到的json 根节点
bool Server::server_read_data(struct bufferevent* bev, Json::Value& root)
{
    // 1. 读取消息长度（4字节int）
    char len_buf[sizeof(int)] = {0};
    int read_len = 0;
    while(read_len < (int)sizeof(int))
    {
        int n = bufferevent_read(bev, len_buf + read_len, sizeof(int) - read_len);
        if(n <= 0)
        {
            Server::debug("读取消息长度失败（连接关闭或错误）");
            return false;
        }
        read_len += n;
    }
    int msg_len = *(int*)len_buf;

    // 2. 校验消息长度
    const int MAX_MSG_LEN = 1024 * 1024; // 限制最大1MB
    if(msg_len <= 0 || msg_len > MAX_MSG_LEN)
    {
        Server::debug("无效消息长度：%d（允许范围：1-%d）", msg_len, MAX_MSG_LEN);
        return false;
    }

    // 3. 读取消息体（用std::string避免栈溢出）
    std::string msg;
    msg.resize(msg_len); // 动态分配空间
    read_len = 0;
    while(read_len < msg_len)
    {
        int n = bufferevent_read(bev, &msg[read_len], msg_len - read_len);
        if(n <= 0)
        {
            Server::debug("读取消息体失败");
            return false;
        }
        read_len += n;
    }
    // Server::debug("[收到消息] 消息长度: %d 消息: %s", msg_len, msg.c_str());

    // 1. 创建 CharReaderBuilder（用于构建 CharReader）
    Json::CharReaderBuilder readerBuilder;
    // 2. 获取 CharReader 实例（用智能指针自动释放，避免内存泄漏）
    std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
    // 3. 存放解析结果的根节点
    
    // 4. 存放解析错误信息（若失败则输出）
    std::string errMsg;

    // 调用 parse 解析 JSON
    // 参数说明：
    // - msg: JSON 字符串起始地址
    // - msg + len: JSON 字符串结束地址（避免越界）
    // - &root: 解析结果存入该节点
    // - &errMsg: 错误信息存入该字符串
    bool parse_ok  = reader->parse(
        msg.data(),                // 起始指针
        msg.data() + msg_len,      // 结束指针（必须！否则可能读取垃圾数据）
        &root,              // 输出：解析后的根节点
        &errMsg             // 输出：错误信息（解析失败时非空）
    );
    if(!parse_ok)
    {
        Server::debug("JSON解析失败：%s", errMsg.c_str());
        return false;
    }
    // 5. 校验：JSON必须是对象类型（否则无法访问key）
    if(root.type() != Json::objectValue)
    {
        Server::debug("JSON类型错误：必须是对象类型（当前是%s）", Json::Value(root.type()).toStyledString().c_str());
        return false;
    }

    return true;
}

// bufferevent事件回调函数（有异常事件发生时调用）
void Server::event_cb(struct bufferevent *bev, short what, void *ctx)
{
    Server *s = (Server*)ctx;
    // 如果发生异常事件（即不是read也不是write）
    if(what & BEV_EVENT_EOF)
    {
        // 判断是音箱下线还是 app下线
        // app下线则 清除节点部分
        // 音箱下线则 删除节点
        
        auto plist = s->m_player_info->player_get_m_player_list();  // 获取音箱 信息链表
        for(auto it = plist->begin(); it != plist->end(); it++)
        {
            if(it->m_app_bev == bev) // app下线
            {
                Server::debug("[有APP下线了] APPID：%s", it->m_appid.c_str());
                it->m_appid.clear();        // 清空 APPID
                // 释放 APP的 bufferevent
                if(it->m_app_bev != nullptr)
                {
                    bufferevent_free(it->m_app_bev);
                    it->m_app_bev = nullptr;
                }
                break;
            }
            else if(it->m_device_bev == bev) // 音箱下线
            {
                Server::debug("[有音箱下线了] 音箱ID：%s", it->m_deviceid.c_str());
                // 释放 dev的 bufferevent
                if(it->m_device_bev != nullptr)
                {
                    bufferevent_free(it->m_device_bev);
                    it->m_device_bev = nullptr;
                }
                // 通知APP掉线了
                if(it->m_app_bev != nullptr)
                {
                    Json::Value json;
                    json["cmd"] = "device_offline";
                    s->server_send_data(it->m_app_bev, json);
                }
                // 释放 APP的 bufferevent
                if(it->m_app_bev != nullptr)
                {
                    bufferevent_free(it->m_app_bev);
                    it->m_app_bev = nullptr;
                }
                plist->erase(it); // 删除 节点
                break;
            }
        }
    }
}

void Server::debug(const char* s, ...)
{
    va_list args;       // 可变参数列表
    va_start(args, s);  // 初始化可变参数列表
    char buf[1024] = {0};   // 格式化后的字符串缓冲区
    vsnprintf(buf, sizeof(buf), s, args);   // 格式化字符串
    va_end(args);   // 结束可变参数列表

    time_t t = time(NULL);
    char time_buf[64] = {0};
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
#if 1
    std::cout << "[" << time_buf << "] " << buf << std::endl;
#else
    // 输出到当前文件夹的/logs/debug.log文件
    
#endif  
}


// 获取数据库句柄
Database* Server::server_get_database(void)
{
    return m_database;
}



