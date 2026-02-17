#include "player.h"
#include "ui_player.h"
#include <QJsonArray>
#include <cstring>
#include <QMessageBox>
Player::Player(Socket *socket, QString appid, QString deviceid, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Player)
{
    ui->setupUi(this);
    this->m_appid = appid;
    this->m_socket = socket;
    this->m_deviceid = deviceid;

    m_get_music_flag = true;

    // 绑定读取数据的信号和槽
    connect(m_socket, &Socket::readyRead, this, &Player::server_reply_slot);

    // 循环上报服务器
    // 1. 初始化定时器
    reportTimer = new QTimer(this);
    reportTimer->setInterval(1000);  // 定时1秒
    // 2. 连接关键信号槽
    connect(reportTimer, &QTimer::timeout, this, &Player::tryreport);         // 定时触发 尝试连接服务器
    // 3. 启动定时器，连接尝试
    reportTimer->start();
}

Player::~Player()
{
    delete ui;
}

// 循环 上报
void Player::tryreport()
{
    if(m_socket->ConnectState)  // 只有在 已连接的情况下才发送
    {
        // 1. 组装json
        QJsonObject json;
        json["cmd"] = "app_report";
        json["appid"] = m_appid;
        json["deviceid"] = m_deviceid;
        // 2. 发送给服务器
        m_socket->WriteData(json);
    }
}

// 接收消息槽函数
void Player::server_reply_slot(void)
{
    QJsonObject root;
    m_socket->ReadData(root);

    //具体的逻辑处理
    QString cmd = root["cmd"].toString();
    if(cmd == "device_report")                  // 设备 上报
        player_device_report_handler(root);
    else if(cmd == "upload_music_list")         // 嵌入式端 上传 歌曲列表
        player_upload_music_list_handler(root);
    else if(cmd.contains("reply_app_"))   // 只要是[APP 请求的 结果]都此函数处理（因为所有app除了请求歌曲列表，都会有reply_app_的字串）
        player_app_reply_option_handler(root);
    else if(cmd == "device_offline"){    // 服务器告知app 你的设备掉线了
        QMessageBox::information(nullptr, "设备掉线", "你的设备掉线了，app即将断开。请查看设备状态，再来尝试。\n尝试对他说:“在线模式”来恢复状态。");
        m_socket->sendDisconnectedFromServer();
    }
    else
        qDebug()<<"出现未知cmd指令:"<<cmd;
}

// 只要是[APP 请求的 结果]都此函数处理
void Player::player_app_reply_option_handler(QJsonObject& root)
{
    // 检查结果
    QString cmd = root["cmd"].toString();
    QString result = root["result"].toString();

    //设备离线 和 设备返回失败提示
    if(result == "offline"){
        QMessageBox::information(nullptr, "请求结果", "您绑定的设备不在线，请检查设备状态！");
        return;
    }
    else if(result == "failure"){
        QMessageBox::information(nullptr, "请求结果", "设备设置失败，请检查设备状态！");
        return;
    }
    // 成功--区分回复类别 用于修改ui
    if (cmd == "reply_app_start_play") {                // 开始播放
        ui->play_button->setText("||");
    } else if (cmd == "reply_app_stop_play") {          // 结束播放
        ui->play_button->setText("▶");
    } else if (cmd == "reply_app_suspend_play") {       // 暂停播放
        ui->play_button->setText("▶");
    } else if (cmd == "reply_app_continue_play") {      // 继续播放
        ui->play_button->setText("||");
    } else if (cmd == "reply_app_play_next_song") {     // 下一首
        ui->play_button->setText("||");
        // 设置音乐列表为下一个节点，如果为最后一个则不设置
        int currentIndex = getCurrentSelectedMusicIndex();  // 选中当前歌曲的索引
        int totalMusicCount = getMusicListCount();
        // 非最后一首才切换（因为服务器会更新歌曲列表）
        if (currentIndex != -1 && currentIndex < totalMusicCount) {
            setMusicItemSelectedAndBold(currentIndex + 1);
        }
    } else if (cmd == "reply_app_play_prev_song") {     // 上一首
       ui->play_button->setText("||");
       // 设置音乐列表为上一个节点，如果为第一个则不设置
          int currentIndex = getCurrentSelectedMusicIndex();
          // 非第一首才切换（因为服务器会更新歌曲列表）
          if (currentIndex > 1) {
              setMusicItemSelectedAndBold(currentIndex - 1);
          }
    } else if (cmd == "reply_app_add_volume") {         // 加音量
        int cur_vol = ui->vol_label->text().toInt();
        if(cur_vol <= 90)
            ui->vol_label->setNum(cur_vol + 10);
    } else if (cmd == "reply_app_sub_volume") {         // 减音量
        int cur_vol = ui->vol_label->text().toInt();
        if(cur_vol >= 10)
            ui->vol_label->setNum(cur_vol - 10);
    } else if (cmd == "reply_app_order_mode") {         // 顺序播放
        ui->order_radioButton->setChecked(true);
    } else if (cmd == "reply_app_single_mode") {        // 单曲循环
        ui->single_radioButton->setChecked(true);
    }
}


// 嵌入式端 [上报] 处理函数
void Player::player_device_report_handler(QJsonObject& root)
{
    // 第一次接收到上报消息后，向嵌入式端请求歌曲列表
    if(m_get_music_flag){
        player_get_music_list();
        m_get_music_flag = false;
    }
    // 解析数据
    int mode = root["cur_mode"].toInt();            // 当前模式 0顺序 2 单曲
    QString music = root["cur_music"].toString();   // 当前歌曲
    QString singer = root["cur_singer"].toString(); // 当前歌手
    int vol = root["cur_volume"].toInt();           // 当前音量
    QString deviceid = root["deviceid"].toString(); // 上传设备id
    m_state = root["state"].toString();       // 当前状态 （play、 suspend、stop）
    // 更新ui
    if(mode == 0)       ui->order_radioButton->setChecked(true);
    else if(mode == 2)  ui->single_radioButton->setChecked(true);;

    QString singer_music = singer+"/"+music;
    ui->current_music_label->setText(singer_music);
    // 设置音乐列表，当前播放的歌曲加粗
    int totalItemCount = ui->music_listWidget->count(); // 获取列表总数
    for (int i = 1; i < totalItemCount; ++i) {
        QListWidgetItem *item = ui->music_listWidget->item(i);
        if (item && item->text() == singer_music) { // 找到这一项
            setMusicItemSelectedAndBold(i);
            break;
        }
    }

    ui->vol_label->setNum(vol);

    ui->deviceid_label->setText("DevID:"+deviceid);

    if(m_state == "suspend")    ui->play_button->setText("▶");
    else if(m_state == "play")  ui->play_button->setText("||");
    else if(m_state == "stop")  ui->play_button->setText("▶");

}

// app 向 嵌入式端[请求歌曲列表]
void Player::player_get_music_list(void)
{
    // 1. 组装json
    QJsonObject json;
    json["cmd"] = "app_get_music_list";
    // 2. 发送给服务器
    m_socket->WriteData(json);
}

// 嵌入式端 [上传歌曲列表] 处理函数
void Player::player_upload_music_list_handler(QJsonObject& root)
{
    // 1. 清空列表
    ui->music_listWidget->clear();

    // 2. 添加第一行固定标题：音乐列表（浅灰背景+黑色字体）
    QListWidgetItem *titleItem = new QListWidgetItem("音乐列表");
    titleItem->setBackground(QBrush(QColor(240, 240, 240)));
    titleItem->setForeground(Qt::black);
    titleItem->setTextAlignment(Qt::AlignCenter);   // 居中对齐
    titleItem->setFlags(titleItem->flags() & ~Qt::ItemIsSelectable);
    ui->music_listWidget->addItem(titleItem);   // 插入

    // 3. 提取音乐列表数据
    QJsonArray musicArray = root["music"].toArray();

    // 4. 遍历添加音乐项
    for (const QJsonValue& musicVal : musicArray) {
        if (musicVal.isString()) {
            QString musicName = musicVal.toString();
            QListWidgetItem *musicItem = new QListWidgetItem(musicName);
            musicItem->setTextAlignment(Qt::AlignCenter);   //设置居中对齐
            ui->music_listWidget->addItem(musicItem);   // 插入
        }
    }

    qDebug() << "[音乐列表上传] 成功加载" << musicArray.size() << "首歌曲";
}


// 点击play按钮
void Player::on_play_button_clicked()
{
    // 判断当前状态
    QJsonObject root;
    if(m_state == "stop"){
        root["cmd"] = "app_start_play";         // 如果为 停止状态，则发送开始播放
    }
    else if(m_state == "suspend"){
        root["cmd"] = "app_continue_play";      // 如果为 暂停状态，则发送继续
    }
    else if(m_state == "play"){
        root["cmd"] = "app_suspend_play";       // 如果为 继续状态，发送暂停
    }else
        root["cmd"] = "app_continue_play";      // 默认为 发送继续
    m_socket->WriteData(root);
}

// 点击上一首 按钮
void Player::on_prev_button_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_play_prev_song";
    m_socket->WriteData(root);
}

// 点击下一首 按钮
void Player::on_next_button_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_play_next_song";
    m_socket->WriteData(root);
}

// 点击音量--
void Player::on_volsub_button_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_sub_volume";
    m_socket->WriteData(root);
}

// 点击音量++
void Player::on_voladd_button_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_add_volume";
    m_socket->WriteData(root);
}

// 点击单曲循环
void Player::on_single_radioButton_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_single_mode";
    m_socket->WriteData(root);
}

// 点击顺序播放
void Player::on_order_radioButton_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_order_mode";
    m_socket->WriteData(root);
}

// 如果在音乐列表上双击
void Player::on_music_listWidget_doubleClicked(const QModelIndex &index)
{
    int clickRow = index.row(); // 获取点击的行数
    // 跳过标题项（0行）
    if (clickRow == 0) {
        return;
    }

    QListWidgetItem *clickItem = ui->music_listWidget->item(clickRow);
    if (!clickItem) {
        return;
    }

    QString targetMusic = clickItem->text();
    qDebug() << "[双击播放] 选中歌曲：" << targetMusic;
#if 0
    // 1. 设置列表选中并加粗
    setMusicItemSelectedAndBold(clickRow);

    // 2. 发送播放指定歌曲指令到嵌入式端（需嵌入式支持该CMD）
    QJsonObject root;
    root["cmd"] = "app_play_assign_song";  // 播放指定歌曲指令
    root["appid"] = m_appid;
    root["deviceid"] = m_deviceid;
    root["music"] = targetMusic;           // 传递选中的歌曲名（歌手/歌曲格式）
    m_socket->WriteData(root);

#endif
}


// 清除所有歌曲项的加粗样式
void Player::clearMusicItemBold()
{
    int totalItemCount = ui->music_listWidget->count();
    // 从1开始（0是标题项）遍历所有歌曲项
    for (int i = 1; i < totalItemCount; ++i) {
        QListWidgetItem *item = ui->music_listWidget->item(i);
        if (item) {
            QFont font = item->font();
            font.setBold(false);
            item->setFont(font);
        }
    }
}

// 设置指定索引的歌曲项为选中并加粗
void Player::setMusicItemSelectedAndBold(int index)
{
    int totalItemCount = ui->music_listWidget->count();
    // 校验索引有效性（1开始，小于总项数）
    if (index < 1 || index >= totalItemCount) {
        return;
    }

    // 先清除所有加粗样式
    clearMusicItemBold();

    QListWidgetItem *targetItem = ui->music_listWidget->item(index);
    if (targetItem) {
        // 选中当前项
        ui->music_listWidget->setCurrentItem(targetItem);
        // 设置加粗字体
        QFont font = targetItem->font();
        font.setBold(true);
        targetItem->setFont(font);
    }
}

// 获取当前选中的歌曲索引（-1=无选中/标题项）
int Player::getCurrentSelectedMusicIndex()
{
    QListWidgetItem *currentItem = ui->music_listWidget->currentItem();
    if (!currentItem) {
        return -1;
    }

    int currentRow = ui->music_listWidget->row(currentItem);
    // 标题项（0行）返回-1
    return (currentRow == 0) ? -1 : currentRow;
}

// 获取歌曲列表总数（不含标题项）
int Player::getMusicListCount()
{
    int totalItemCount = ui->music_listWidget->count();
    // 总数-1（减去标题项），最小返回0
    return qMax(0, totalItemCount - 1);
}
