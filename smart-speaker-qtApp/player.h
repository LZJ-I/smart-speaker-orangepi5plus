#ifndef PLAYER_H
#define PLAYER_H

#include <QWidget>
#include "socket.h"

namespace Ui {
class Player;
}

class Player : public QWidget
{
    Q_OBJECT

public:
    explicit Player(Socket* socket, QString appid, QString deviceid, QWidget *parent = nullptr);
    ~Player();

private:
    Ui::Player *ui;
    Socket* m_socket;
    QString m_appid;
    QString m_deviceid;
    QTimer *reportTimer;       // 定时上报 的定时器
    bool m_get_music_flag;
    qint64 m_lastPlaylistRequestMs;
    int m_playlistVersion;
    QString m_state;
    QString m_currentSource;
    QString m_currentSongId;
    void player_device_report_handler(QJsonObject& root);   // 嵌入式端 [上报消息] 处理函数;
    void player_get_music_list(void);   // app 向 嵌入式端[请求歌曲列表]
    void player_upload_music_list_handler(QJsonObject& root);   // 嵌入式端 [上传歌曲列表] 处理函数
    void player_app_reply_option_handler(QJsonObject& root);    // [处理app请求后的回复]

private:   /* 处理列表样式相关 */
    // 清除所有歌曲项的加粗样式（跳过标题项）
    void clearMusicItemBold();
    // 设置指定索引的歌曲项为选中并加粗（index从1开始，0是标题）
    void setMusicItemSelectedAndBold(int index);
    // 获取当前选中的歌曲索引（返回-1表示无选中/标题项）
    int getCurrentSelectedMusicIndex();
    // 获取歌曲列表总数（不含标题项）
    int getMusicListCount();
    void syncCurrentTrackSelection(int currentIndex = -1);
    void updatePlaylistPageLabelFromJson(const QJsonObject &root);
private slots:
    void server_reply_slot(void);   // 接收到服务器信息的槽函数
    void tryreport();               // 每秒定时上报一次的槽函数
    void on_play_button_clicked();
    void on_prev_button_clicked();
    void on_next_button_clicked();
    void on_volsub_button_clicked();
    void on_voladd_button_clicked();
    void on_single_radioButton_clicked();
    void on_order_radioButton_clicked();
    void on_music_listWidget_doubleClicked(const QModelIndex &index);
    void on_playlist_page_prev_button_clicked();
    void on_playlist_page_next_button_clicked();
    void on_song_search_button_clicked();
    void on_playlist_search_button_clicked();
};

#endif // PLAYER_H
