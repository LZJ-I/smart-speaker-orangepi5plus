#include "player.h"
#include "ui_player.h"
#include <QJsonArray>
#include <QApplication>
#include <QDateTime>
#include <QButtonGroup>
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

void populateListFromJson(QListWidget *list, const QJsonArray &arr)
{
    for (const QJsonValue &val : arr) {
        QString title, subtitle, source, songId;
        QString kind = QStringLiteral("song");
        QString displayName;
        if (val.isString()) {
            displayName = val.toString();
        } else if (val.isObject()) {
            QJsonObject o = val.toObject();
            subtitle = o[QStringLiteral("subtitle")].toString();
            title = o[QStringLiteral("title")].toString();
            source = o[QStringLiteral("source")].toString();
            songId = o[QStringLiteral("id")].toString();
            kind = o[QStringLiteral("kind")].toString();
            if (kind.isEmpty()) kind = QStringLiteral("song");
            if (title.isEmpty()) title = o[QStringLiteral("song_name")].toString();
            if (subtitle.isEmpty()) subtitle = o[QStringLiteral("singer")].toString();
            displayName = buildMusicDisplay(title, subtitle);
        }
        if (displayName.isEmpty()) continue;

        QListWidgetItem *item = new QListWidgetItem(displayName);
        item->setData(MusicTitleRole, title);
        item->setData(MusicSubtitleRole, subtitle);
        item->setData(MusicSourceRole, source);
        item->setData(MusicIdRole, songId);
        item->setData(MusicKindRole, kind);
        list->addItem(item);
    }
}

void sendPlayFromItem(Socket *socket, const QString &appid, const QString &deviceid,
                      QListWidgetItem *item)
{
    if (!item) return;
    QString itemKind = item->data(MusicKindRole).toString();
    QJsonObject root;
    root["cmd"] = (itemKind == QStringLiteral("playlist"))
                      ? QStringLiteral("app_play_playlist")
                      : QStringLiteral("app_play_assign_song");
    root["appid"] = appid;
    root["deviceid"] = deviceid;
    root["music"] = item->text();
    root["title"] = item->data(MusicTitleRole).toString();
    root["subtitle"] = item->data(MusicSubtitleRole).toString();
    root["source"] = item->data(MusicSourceRole).toString();
    root["id"] = item->data(MusicIdRole).toString();
    socket->WriteData(root);
}

}

Player::Player(Socket *socket, QString appid, QString deviceid, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Player)
{
    ui->setupUi(this);
    m_appid = appid;
    m_socket = socket;
    m_deviceid = deviceid;
    m_get_music_flag = true;
    m_lastPlaylistRequestMs = 0;
    m_playlistVersion = -1;

    applyStyleSheet();

    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->addButton(ui->tab_playlist_button, 0);
    m_tabGroup->addButton(ui->tab_search_button, 1);
    m_tabGroup->setExclusive(true);

    connect(ui->search_keyword_edit, &QLineEdit::returnPressed,
            this, &Player::on_song_search_button_clicked);

    connect(m_socket, &Socket::readyRead, this, &Player::server_reply_slot);

    reportTimer = new QTimer(this);
    reportTimer->setInterval(1000);
    connect(reportTimer, &QTimer::timeout, this, &Player::tryreport);
    reportTimer->start();
}

Player::~Player()
{
    delete ui;
}

void Player::applyStyleSheet()
{
    setStyleSheet(QStringLiteral(
        "QWidget#Player { background-color: #0d1117; }"
        "QLabel { color: #e6edf3; }"
        "QLabel#label_4 { font-size: 26px; font-weight: bold; color: #ffffff; }"
        "QLabel#deviceid_label { font-size: 12px; color: #8b949e; }"
        "QLabel#current_music_label {"
        "  font-size: 15px; font-weight: bold; color: #ffffff;"
        "  background-color: #161b22; border-radius: 10px; padding: 8px;"
        "}"
        "QLabel#vol_label { font-size: 18px; color: #e6edf3; }"
        "QLabel#playlist_page_label { color: #8b949e; font-size: 13px; }"

        "QLineEdit#search_keyword_edit {"
        "  background-color: #161b22; color: #e6edf3;"
        "  border: 1px solid #30363d; border-radius: 10px;"
        "  padding: 6px 14px; font-size: 14px;"
        "}"
        "QLineEdit#search_keyword_edit:focus { border-color: #58a6ff; }"

        "QPushButton {"
        "  background-color: #21262d; color: #e6edf3;"
        "  border: 1px solid #30363d; border-radius: 10px;"
        "  padding: 8px 16px; font-size: 14px;"
        "}"
        "QPushButton:hover { background-color: #30363d; border-color: #484f58; }"
        "QPushButton:pressed { background-color: #3a424a; }"

        "QPushButton#song_search_button, QPushButton#playlist_search_button {"
        "  background-color: #238636; color: #ffffff;"
        "  border: 1px solid #2ea043; font-weight: bold;"
        "}"
        "QPushButton#song_search_button:hover, QPushButton#playlist_search_button:hover {"
        "  background-color: #2ea043; border-color: #3fb950;"
        "}"

        "QPushButton#tab_playlist_button, QPushButton#tab_search_button {"
        "  background-color: transparent; color: #8b949e;"
        "  border: none; border-bottom: 2px solid transparent;"
        "  border-radius: 0; padding: 8px 24px;"
        "  font-size: 14px; font-weight: bold;"
        "}"
        "QPushButton#tab_playlist_button:checked, QPushButton#tab_search_button:checked {"
        "  color: #58a6ff; border-bottom: 2px solid #58a6ff;"
        "}"
        "QPushButton#tab_playlist_button:hover, QPushButton#tab_search_button:hover {"
        "  color: #c9d1d9;"
        "}"

        "QListWidget {"
        "  background-color: #161b22; color: #e6edf3;"
        "  border: 1px solid #21262d; border-radius: 10px;"
        "  padding: 4px; font-size: 14px; outline: none;"
        "}"
        "QListWidget::item {"
        "  padding: 10px 14px; border-radius: 6px; margin: 2px 4px;"
        "}"
        "QListWidget::item:selected { background-color: #1f3a5f; color: #ffffff; }"
        "QListWidget::item:hover:!selected { background-color: #1c2633; }"

        "QPushButton#play_button {"
        "  background-color: #58a6ff; color: #0d1117;"
        "  border: none; border-radius: 25px;"
        "  font-size: 22px; font-weight: bold;"
        "}"
        "QPushButton#play_button:hover { background-color: #79c0ff; }"
        "QPushButton#prev_button, QPushButton#next_button { font-size: 20px; }"
        "QPushButton#volsub_button, QPushButton#voladd_button { font-size: 14px; font-weight: bold; }"

        "QRadioButton { color: #c9d1d9; font-size: 14px; spacing: 8px; }"
        "QRadioButton::indicator {"
        "  width: 18px; height: 18px; border-radius: 9px;"
        "  border: 2px solid #484f58; background-color: #0d1117;"
        "}"
        "QRadioButton::indicator:checked { background-color: #58a6ff; border-color: #58a6ff; }"
        "QRadioButton::indicator:hover { border-color: #58a6ff; }"

        "QScrollBar:vertical {"
        "  background-color: #161b22; width: 8px; border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background-color: #30363d; border-radius: 4px; min-height: 20px;"
        "}"
        "QScrollBar::handle:vertical:hover { background-color: #484f58; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QStackedWidget { background-color: transparent; }"
    ));
}

void Player::switchToTab(int index)
{
    ui->list_stacked_widget->setCurrentIndex(index);
    if (index == 0)
        ui->tab_playlist_button->setChecked(true);
    else
        ui->tab_search_button->setChecked(true);
}

void Player::tryreport()
{
    if (m_socket->ConnectState) {
        QJsonObject json;
        json["cmd"] = "app_report";
        json["appid"] = m_appid;
        json["deviceid"] = m_deviceid;
        m_socket->WriteData(json);
    }
}

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
            player_search_result_handler(root);
        else if (cmd.contains(QStringLiteral("reply_app_")))
            player_app_reply_option_handler(root);
        else if (cmd == "device_offline") {
            m_socket->markDisconnectDueToDeviceClient();
            m_socket->sendDisconnectedFromServer();
        } else
            qDebug() << "出现未知cmd指令:" << cmd;
    }
}

void Player::player_app_reply_option_handler(QJsonObject& root)
{
    QString cmd = root["cmd"].toString();
    QString result = root["result"].toString();

    if (result == "offline") {
        QMessageBox::information(nullptr, "请求结果", "您绑定的设备不在线，请检查设备状态！");
        return;
    } else if (result == "failure") {
        QMessageBox::information(nullptr, "请求结果", "设备设置失败，请检查设备状态！");
        return;
    }

    if (cmd == "reply_app_start_play") {
        ui->play_button->setText("||");
    } else if (cmd == "reply_app_stop_play") {
        ui->play_button->setText("▶");
    } else if (cmd == "reply_app_suspend_play") {
        ui->play_button->setText("▶");
    } else if (cmd == "reply_app_continue_play") {
        ui->play_button->setText("||");
    } else if (cmd == "reply_app_play_next_song") {
        ui->play_button->setText("||");
    } else if (cmd == "reply_app_play_prev_song") {
        ui->play_button->setText("||");
    } else if (cmd == "reply_app_add_volume") {
        int cur_vol = ui->vol_label->text().toInt();
        if (cur_vol <= 90) ui->vol_label->setNum(cur_vol + 10);
    } else if (cmd == "reply_app_sub_volume") {
        int cur_vol = ui->vol_label->text().toInt();
        if (cur_vol >= 10) ui->vol_label->setNum(cur_vol - 10);
    } else if (cmd == "reply_app_order_mode") {
        ui->order_radioButton->setChecked(true);
    } else if (cmd == "reply_app_single_mode") {
        ui->single_radioButton->setChecked(true);
    } else if (cmd == "reply_app_play_assign_song" || cmd == "reply_app_play_playlist") {
        ui->play_button->setText("||");
    } else if (cmd == "reply_app_playlist_next_page" || cmd == "reply_app_playlist_prev_page") {
        if (result == "success") player_get_music_list();
    }
}

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

    int mode = root["cur_mode"].toInt();
    QString music = root["cur_music"].toString();
    QString singer = root["cur_singer"].toString();
    int vol = root["cur_volume"].toInt();
    QString deviceid = root["deviceid"].toString();
    m_state = root["state"].toString();

    if (mode == 0) ui->order_radioButton->setChecked(true);
    else if (mode == 2) ui->single_radioButton->setChecked(true);

    if (!music.isEmpty()) {
        ui->current_music_label->setText(
            QStringLiteral("<div style='text-align:center'>"
                           "<span style='font-size:15px;font-weight:bold;color:#ffffff;'>%1</span><br/>"
                           "<span style='font-size:12px;color:#8b949e;'>%2</span></div>")
                .arg(music.toHtmlEscaped(), singer.toHtmlEscaped()));
    }
    syncCurrentTrackSelection(currentIndex);
    ui->vol_label->setNum(vol);
    ui->deviceid_label->setText("DevID:" + deviceid);
    updatePlaylistPageLabelFromJson(root);

    if (m_state == "suspend") ui->play_button->setText("▶");
    else if (m_state == "play") ui->play_button->setText("||");
    else if (m_state == "stop") ui->play_button->setText("▶");
}

void Player::player_get_music_list(void)
{
    QJsonObject json;
    json["cmd"] = "app_get_music_list";
    m_socket->WriteData(json);
}

void Player::player_upload_music_list_handler(QJsonObject& root)
{
    const int currentIndex = root[QStringLiteral("current_index")].toInt(-1);
    m_playlistVersion = root[QStringLiteral("playlist_version")].toInt(m_playlistVersion);
    m_get_music_flag = false;

    ui->music_listWidget->clear();

    QJsonArray musicArray = root["music"].toArray();
    if (musicArray.isEmpty())
        musicArray = root["items"].toArray();

    populateListFromJson(ui->music_listWidget, musicArray);
    syncCurrentTrackSelection(currentIndex);
    updatePlaylistPageLabelFromJson(root);
}

void Player::player_search_result_handler(QJsonObject& root)
{
    ui->search_result_listWidget->clear();

    QJsonArray items = root["items"].toArray();
    if (items.isEmpty())
        items = root["music"].toArray();

    populateListFromJson(ui->search_result_listWidget, items);

    int count = ui->search_result_listWidget->count();
    ui->tab_search_button->setText(QStringLiteral("搜索结果 (%1)").arg(count));
    switchToTab(1);
}

void Player::updatePlaylistPageLabelFromJson(const QJsonObject &root)
{
    const int plPage = root[QStringLiteral("playlist_page")].toInt(0);
    const int plTotal = root[QStringLiteral("playlist_total_pages")].toInt(0);
    if (plPage > 0 && plTotal > 0)
        ui->playlist_page_label->setText(QStringLiteral("页 %1/%2").arg(plPage).arg(plTotal));
}

void Player::on_play_button_clicked()
{
    QJsonObject root;
    if (m_state == "stop")
        root["cmd"] = "app_start_play";
    else if (m_state == "suspend")
        root["cmd"] = "app_continue_play";
    else if (m_state == "play")
        root["cmd"] = "app_suspend_play";
    else
        root["cmd"] = "app_continue_play";
    m_socket->WriteData(root);
}

void Player::on_prev_button_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_play_prev_song";
    m_socket->WriteData(root);
}

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

void Player::on_volsub_button_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_sub_volume";
    m_socket->WriteData(root);
}

void Player::on_voladd_button_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_add_volume";
    m_socket->WriteData(root);
}

void Player::on_single_radioButton_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_single_mode";
    m_socket->WriteData(root);
}

void Player::on_order_radioButton_clicked()
{
    QJsonObject root;
    root["cmd"] = "app_order_mode";
    m_socket->WriteData(root);
}

void Player::on_music_listWidget_doubleClicked(const QModelIndex &index)
{
    QListWidgetItem *item = ui->music_listWidget->item(index.row());
    if (!item) return;
    setMusicItemSelectedAndBold(index.row());
    sendPlayFromItem(m_socket, m_appid, m_deviceid, item);
}

void Player::on_search_result_listWidget_doubleClicked(const QModelIndex &index)
{
    QListWidgetItem *item = ui->search_result_listWidget->item(index.row());
    if (!item) return;
    sendPlayFromItem(m_socket, m_appid, m_deviceid, item);
}

void Player::on_song_search_button_clicked()
{
    const QString keyword = ui->search_keyword_edit->text().trimmed();
    if (keyword.isEmpty()) return;
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
    if (keyword.isEmpty()) return;
    QJsonObject root;
    root["cmd"] = "music.search.playlist";
    root["keyword"] = keyword;
    root["source"] = "all";
    root["page"] = 1;
    root["page_size"] = 30;
    m_socket->WriteData(root);
}

void Player::on_tab_playlist_button_clicked()
{
    ui->list_stacked_widget->setCurrentIndex(0);
}

void Player::on_tab_search_button_clicked()
{
    ui->list_stacked_widget->setCurrentIndex(1);
}

void Player::clearMusicItemBold()
{
    const int count = ui->music_listWidget->count();
    for (int i = 0; i < count; ++i) {
        QListWidgetItem *item = ui->music_listWidget->item(i);
        if (item) {
            QFont font = item->font();
            font.setBold(false);
            item->setFont(font);
        }
    }
}

void Player::setMusicItemSelectedAndBold(int index)
{
    const int count = ui->music_listWidget->count();
    if (index < 0 || index >= count) return;
    clearMusicItemBold();
    QListWidgetItem *targetItem = ui->music_listWidget->item(index);
    if (targetItem) {
        ui->music_listWidget->setCurrentItem(targetItem);
        QFont font = targetItem->font();
        font.setBold(true);
        targetItem->setFont(font);
    }
}

int Player::getCurrentSelectedMusicIndex()
{
    QListWidgetItem *currentItem = ui->music_listWidget->currentItem();
    if (!currentItem) return -1;
    return ui->music_listWidget->row(currentItem);
}

int Player::getMusicListCount()
{
    return ui->music_listWidget->count();
}

void Player::syncCurrentTrackSelection(int currentIndex)
{
    if (currentIndex >= 0) {
        setMusicItemSelectedAndBold(currentIndex);
        return;
    }
    if (!m_currentSongId.isEmpty()) {
        const int count = ui->music_listWidget->count();
        for (int i = 0; i < count; ++i) {
            QListWidgetItem *item = ui->music_listWidget->item(i);
            if (!item) continue;
            if (item->data(MusicIdRole).toString() == m_currentSongId &&
                item->data(MusicSourceRole).toString() == m_currentSource) {
                setMusicItemSelectedAndBold(i);
                return;
            }
        }
    }
}
