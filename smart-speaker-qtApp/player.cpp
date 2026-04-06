#include "player.h"
#include "ui_player.h"
#include <QJsonArray>
#include <QApplication>
#include <QDateTime>
#include <QPalette>
#include <cstring>
#include <QMessageBox>

namespace {

enum MusicItemRole {
    MusicTitleRole = Qt::UserRole + 1,
    MusicSubtitleRole,
    MusicSourceRole,
    MusicIdRole,
    MusicKindRole
};

QString buildMusicDisplay(const QString &title, const QString &subtitle)
{
    return subtitle.isEmpty() ? title : (subtitle + QStringLiteral("/") + title);
}

}

Player::Player(Socket *socket, QString appid, QString deviceid, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Player)
{
    ui->setupUi(this);
    this->m_appid = appid;
    this->m_socket = socket;
    this->m_deviceid = deviceid;

    m_get_music_flag = true;
    m_lastPlaylistRequestMs = 0;
    m_playlistVersion = -1;

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
    while (m_socket->readOneJson(root)) {
        QString cmd = root["cmd"].toString();
        if (cmd == "device_report")
            player_device_report_handler(root);
        else if (cmd == "upload_music_list")
            player_upload_music_list_handler(root);
        else if (cmd == "music.search.song.reply" || cmd == "music.search.playlist.reply")
            player_upload_music_list_handler(root);
        else if (cmd.contains(QStringLiteral("reply_app_")))
            player_app_reply_option_handler(root);
        else if (cmd == "device_offline") {
            m_socket->markDisconnectDueToDeviceClient();
            m_socket->sendDisconnectedFromServer();
        } else
            qDebug() << "出现未知cmd指令:" << cmd;
    }
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
    } else if (cmd == "reply_app_play_prev_song") {     // 上一首
       ui->play_button->setText("||");
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
    } else if (cmd == "reply_app_play_assign_song" || cmd == "reply_app_play_playlist") {
        ui->play_button->setText("||");
    } else if (cmd == "reply_app_playlist_next_page" || cmd == "reply_app_playlist_prev_page") {
        if (result == "success") {
            player_get_music_list();
        }
    }
}


// 嵌入式端 [上报] 处理函数
void Player::player_device_report_handler(QJsonObject& root)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int previousPlaylistVersion = m_playlistVersion;
    const int reportPlaylistVersion = root[QStringLiteral("playlist_version")].toInt(m_playlistVersion);
    const int currentIndex = root[QStringLiteral("current_index")].toInt(-1);
    m_currentSource = root[QStringLiteral("current_source")].toString();
    m_currentSongId = root[QStringLiteral("current_song_id")].toString();
    m_playlistVersion = reportPlaylistVersion;
    if (m_get_music_flag) {
        player_get_music_list();
        m_get_music_flag = false;
        m_lastPlaylistRequestMs = now;
    } else if (reportPlaylistVersion >= 0 && reportPlaylistVersion != previousPlaylistVersion &&
               (now - m_lastPlaylistRequestMs >= 2000)) {
        player_get_music_list();
        m_lastPlaylistRequestMs = now;
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
    syncCurrentTrackSelection(currentIndex);

    ui->vol_label->setNum(vol);

    ui->deviceid_label->setText("DevID:"+deviceid);

    updatePlaylistPageLabelFromJson(root);

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
    const int currentIndex = root[QStringLiteral("current_index")].toInt(-1);
    m_playlistVersion = root[QStringLiteral("playlist_version")].toInt(m_playlistVersion);
    m_get_music_flag = false;

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
    if (musicArray.isEmpty()) {
        musicArray = root["items"].toArray();
    }

    for (const QJsonValue &musicVal : musicArray) {
        QString title;
        QString subtitle;
        QString source;
        QString songId;
        QString kind = QStringLiteral("song");
        QString musicName;
        if (musicVal.isString()) {
            musicName = musicVal.toString();
        } else if (musicVal.isObject()) {
            QJsonObject o = musicVal.toObject();
            subtitle = o[QStringLiteral("subtitle")].toString();
            title = o[QStringLiteral("title")].toString();
            source = o[QStringLiteral("source")].toString();
            songId = o[QStringLiteral("id")].toString();
            kind = o[QStringLiteral("kind")].toString();
            if (kind.isEmpty())
                kind = QStringLiteral("song");
            if (title.isEmpty())
                title = o[QStringLiteral("song_name")].toString();
            if (subtitle.isEmpty())
                subtitle = o[QStringLiteral("singer")].toString();
            musicName = buildMusicDisplay(title, subtitle);
        }
        if (musicName.isEmpty())
            continue;
        QListWidgetItem *musicItem = new QListWidgetItem(musicName);
        musicItem->setTextAlignment(Qt::AlignCenter);
        musicItem->setData(MusicTitleRole, title);
        musicItem->setData(MusicSubtitleRole, subtitle);
        musicItem->setData(MusicSourceRole, source);
        musicItem->setData(MusicIdRole, songId);
        musicItem->setData(MusicKindRole, kind);
        {
            const QPalette pal = QApplication::palette();
            musicItem->setForeground(QBrush(pal.color(QPalette::Text)));
            musicItem->setBackground(QBrush(pal.color(QPalette::Base)));
        }
        ui->music_listWidget->addItem(musicItem);
    }
    syncCurrentTrackSelection(currentIndex);
    updatePlaylistPageLabelFromJson(root);
}

void Player::updatePlaylistPageLabelFromJson(const QJsonObject &root)
{
    const int plPage = root[QStringLiteral("playlist_page")].toInt(0);
    const int plTotal = root[QStringLiteral("playlist_total_pages")].toInt(0);
    if (plPage > 0 && plTotal > 0) {
        ui->playlist_page_label->setText(QStringLiteral("页 %1/%2").arg(plPage).arg(plTotal));
    }
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

void Player::on_playlist_page_prev_button_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_playlist_prev_page";
    m_socket->WriteData(root);
}

void Player::on_playlist_page_next_button_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_playlist_next_page";
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
    QString itemKind = clickItem->data(MusicKindRole).toString();
    setMusicItemSelectedAndBold(clickRow);
    QJsonObject root;
    root["cmd"] = (itemKind == QStringLiteral("playlist"))
                      ? QStringLiteral("app_play_playlist")
                      : QStringLiteral("app_play_assign_song");
    root["appid"] = m_appid;
    root["deviceid"] = m_deviceid;
    root["music"] = targetMusic;
    root["title"] = clickItem->data(MusicTitleRole).toString();
    root["subtitle"] = clickItem->data(MusicSubtitleRole).toString();
    root["source"] = clickItem->data(MusicSourceRole).toString();
    root["id"] = clickItem->data(MusicIdRole).toString();
    m_socket->WriteData(root);
}

void Player::on_song_search_button_clicked()
{
    const QString keyword = ui->search_keyword_edit->text().trimmed();
    if (keyword.isEmpty()) {
        return;
    }
    QJsonObject root;
    root["cmd"] = "music.search.song";
    root["keyword"] = keyword;
    root["source"] = "all";
    root["page"] = 1;
    root["page_size"] = 30;
    m_socket->WriteData(root);
}

void Player::on_playlist_search_button_clicked()
{
    const QString keyword = ui->search_keyword_edit->text().trimmed();
    if (keyword.isEmpty()) {
        return;
    }
    QJsonObject root;
    root["cmd"] = "music.search.playlist";
    root["keyword"] = keyword;
    root["source"] = "all";
    root["page"] = 1;
    root["page_size"] = 30;
    m_socket->WriteData(root);
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

void Player::syncCurrentTrackSelection(int currentIndex)
{
    if (currentIndex >= 0) {
        setMusicItemSelectedAndBold(currentIndex + 1);
        return;
    }
    if (!m_currentSongId.isEmpty()) {
        const int totalItemCount = ui->music_listWidget->count();
        for (int i = 1; i < totalItemCount; ++i) {
            QListWidgetItem *item = ui->music_listWidget->item(i);
            if (!item) {
                continue;
            }
            if (item->data(MusicIdRole).toString() == m_currentSongId &&
                item->data(MusicSourceRole).toString() == m_currentSource) {
                setMusicItemSelectedAndBold(i);
                return;
            }
        }
    }
}
