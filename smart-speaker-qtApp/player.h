#ifndef PLAYER_H
#define PLAYER_H

#include <QWidget>
#include "socket.h"

class QButtonGroup;

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
    QTimer *reportTimer;
    bool m_get_music_flag;
    qint64 m_lastPlaylistRequestMs;
    int m_playlistVersion;
    QString m_state;
    QString m_currentSource;
    QString m_currentSongId;
    QButtonGroup *m_tabGroup;

    void player_device_report_handler(QJsonObject& root);
    void player_get_music_list(void);
    void player_upload_music_list_handler(QJsonObject& root);
    void player_search_result_handler(QJsonObject& root);
    void player_app_reply_option_handler(QJsonObject& root);
    void applyStyleSheet();
    void switchToTab(int index);

private:
    void clearMusicItemBold();
    void setMusicItemSelectedAndBold(int index);
    int getCurrentSelectedMusicIndex();
    int getMusicListCount();
    void syncCurrentTrackSelection(int currentIndex = -1);
    void updatePlaylistPageLabelFromJson(const QJsonObject &root);

private slots:
    void server_reply_slot(void);
    void tryreport();
    void on_play_button_clicked();
    void on_prev_button_clicked();
    void on_next_button_clicked();
    void on_volsub_button_clicked();
    void on_voladd_button_clicked();
    void on_single_radioButton_clicked();
    void on_order_radioButton_clicked();
    void on_music_listWidget_doubleClicked(const QModelIndex &index);
    void on_search_result_listWidget_doubleClicked(const QModelIndex &index);
    void on_playlist_page_prev_button_clicked();
    void on_playlist_page_next_button_clicked();
    void on_song_search_button_clicked();
    void on_playlist_search_button_clicked();
    void on_tab_playlist_button_clicked();
    void on_tab_search_button_clicked();
};

#endif // PLAYER_H
