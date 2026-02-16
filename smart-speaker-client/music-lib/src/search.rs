//! 搜索模块 - 负责从各音乐平台搜索歌曲
use serde::Deserialize;
use reqwest::blocking::Client;
use std::vec::Vec;

/// 支持的搜索平台
const SUPPORTED_SEARCH_PLATFORMS: &[&str] = &["tx", "wy", "auto"];

/// 验证搜索平台是否有效
fn is_valid_search_platform(platform: &str) -> bool {
    SUPPORTED_SEARCH_PLATFORMS.contains(&platform)
}

/// 歌曲信息结构体
pub struct SongInfo {
    pub id: String,
    pub name: String,
    pub artist: String,
    pub album: String,
    pub source: String,
}

/// QQ 音乐 API 响应结构体
#[derive(Deserialize)]
struct QQMusicResponse {
    data: QQMusicData,
}

/// QQ 音乐数据结构体
#[derive(Deserialize)]
struct QQMusicData {
    song: QQMusicSong,
}

/// QQ 音乐歌曲列表结构体
#[derive(Deserialize)]
struct QQMusicSong {
    list: Vec<QQMusicSongItem>,
}

/// QQ 音乐歌曲项结构体
#[derive(Deserialize)]
struct QQMusicSongItem {
    songmid: String,
    songname: String,
    singer: Vec<QQMusicSinger>,
    albumname: String,
}

/// QQ 音乐歌手结构体
#[derive(Deserialize)]
struct QQMusicSinger {
    name: String,
}

/// 网易云音乐 API 响应结构体
#[derive(Deserialize)]
struct NeteaseMusicResponse {
    result: Option<NeteaseMusicResult>,
}

/// 网易云音乐搜索结果结构体
#[derive(Deserialize)]
struct NeteaseMusicResult {
    songs: Vec<NeteaseMusicSong>,
}

/// 网易云音乐歌曲结构体
#[derive(Deserialize)]
struct NeteaseMusicSong {
    id: i64,
    name: String,
    artists: Vec<NeteaseMusicArtist>,
    album: Option<NeteaseMusicAlbum>,
}

/// 网易云音乐艺术家结构体
#[derive(Deserialize)]
struct NeteaseMusicArtist {
    name: String,
}

/// 网易云音乐专辑结构体
#[derive(Deserialize)]
struct NeteaseMusicAlbum {
    name: String,
}

/// 从 QQ 音乐搜索歌曲
/// 
/// # 参数
/// - keyword: 搜索关键词
/// 
/// # 返回
/// 成功时返回歌曲列表，失败时返回错误信息
pub fn search_qq_music(keyword: &str) -> Result<Vec<SongInfo>, String> {
    let url = format!("https://c.y.qq.com/soso/fcgi-bin/client_search_cp?w={}", keyword);
    
    let resp = Client::new()
        .get(&url)
        .send()
        .map_err(|e| e.to_string())?
        .text()
        .map_err(|e| e.to_string())?;
    
    let json_str = resp.trim_start_matches("callback(").trim_end_matches(")");
    
    let qq_resp: QQMusicResponse = serde_json::from_str(json_str)
        .map_err(|e| e.to_string())?;
    
    let mut songs = Vec::new();
    for item in qq_resp.data.song.list {
        let artist = item.singer.first()
            .map(|s| s.name.clone())
            .unwrap_or_else(|| "".to_string());
        
        songs.push(SongInfo {
            id: item.songmid,
            name: item.songname,
            artist,
            album: item.albumname,
            source: "tx".to_string(),
        });
    }
    
    Ok(songs)
}

/// 从网易云音乐搜索歌曲
/// 
/// # 参数
/// - keyword: 搜索关键词
/// 
/// # 返回
/// 成功时返回歌曲列表，失败时返回错误信息
pub fn search_netease_music(keyword: &str) -> Result<Vec<SongInfo>, String> {
    let url = format!("https://music.163.com/api/search/get/?s={}&type=1&limit=10", keyword);
    
    let resp = Client::new()
        .get(&url)
        .send()
        .map_err(|e| e.to_string())?
        .json::<NeteaseMusicResponse>()
        .map_err(|e| e.to_string())?;
    
    let mut songs = Vec::new();
    if let Some(result) = resp.result {
        for item in result.songs {
            let artist = item.artists.first()
                .map(|a| a.name.clone())
                .unwrap_or_else(|| "".to_string());
            
            let album = item.album
                .map(|a| a.name)
                .unwrap_or_else(|| "".to_string());
            
            songs.push(SongInfo {
                id: item.id.to_string(),
                name: item.name,
                artist,
                album,
                source: "wy".to_string(),
            });
        }
    }
    
    Ok(songs)
}

/// 搜索歌曲 (统一搜索，支持多平台)
/// 
/// # 参数
/// - keyword: 搜索关键词
/// - platform: 平台 ("tx" 为 QQ 音乐, "wy" 为网易云音乐, "auto" 自动选择)
/// 
/// # 返回
/// 成功时返回歌曲列表，失败时返回错误信息
pub fn search_music(keyword: &str, platform: &str) -> Result<Vec<SongInfo>, String> {
    // 验证关键词
    if keyword.trim().is_empty() {
        return Err("搜索关键词不能为空".to_string());
    }
    
    // 验证平台
    if !is_valid_search_platform(platform) {
        return Err(format!(
            "无效的平台 '{}'，支持的平台有: {}",
            platform,
            SUPPORTED_SEARCH_PLATFORMS.join(", ")
        ));
    }
    
    match platform {
        "tx" => search_qq_music(keyword),
        "wy" => search_netease_music(keyword),
        "auto" => {
            match search_qq_music(keyword) {
                Ok(songs) if !songs.is_empty() => Ok(songs),
                _ => search_netease_music(keyword),
            }
        }
        _ => unreachable!("平台验证已通过"),
    }
}
