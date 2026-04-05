//! 搜索模块 - 负责从各音乐平台搜索歌曲
use reqwest::blocking::Client;
use serde::Deserialize;
use std::vec::Vec;

const HTTP_USER_AGENT: &str =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36";

fn http_client() -> Result<Client, String> {
    Client::builder()
        .user_agent(HTTP_USER_AGENT)
        .build()
        .map_err(|e| e.to_string())
}

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

pub struct SearchPage {
    pub songs: Vec<SongInfo>,
    pub total: usize,
    pub page: u32,
    pub page_size: u32,
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
    totalnum: Option<u32>,
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
    #[serde(rename = "songCount")]
    song_count: Option<u32>,
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
pub fn search_qq_music_paged(keyword: &str, page: u32, page_size: u32) -> Result<SearchPage, String> {
    let page = if page == 0 { 1 } else { page };
    let page_size = if page_size == 0 { 10 } else { page_size };
    let w = urlencoding::encode(keyword);
    let url = format!(
        "https://c.y.qq.com/soso/fcgi-bin/client_search_cp?w={}&p={}&n={}",
        w, page, page_size
    );

    let resp = http_client()?.get(&url).send().map_err(|e| e.to_string())?;
    if !resp.status().is_success() {
        return Err(format!("QQ 音乐搜索 HTTP {}", resp.status()));
    }
    let resp = resp.text().map_err(|e| e.to_string())?;

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
    
    let total = qq_resp.data.song.totalnum.unwrap_or(songs.len() as u32) as usize;
    Ok(SearchPage {
        songs,
        total,
        page,
        page_size,
    })
}

/// 从网易云音乐搜索歌曲
/// 
/// # 参数
/// - keyword: 搜索关键词
/// 
/// # 返回
/// 成功时返回歌曲列表，失败时返回错误信息
pub fn search_netease_music_paged(keyword: &str, page: u32, page_size: u32) -> Result<SearchPage, String> {
    let page = if page == 0 { 1 } else { page };
    let page_size = if page_size == 0 { 10 } else { page_size };
    let offset = (page - 1) * page_size;
    let s = urlencoding::encode(keyword);
    let url = format!(
        "https://music.163.com/api/search/get/?s={}&type=1&limit={}&offset={}",
        s, page_size, offset
    );

    let resp = http_client()?
        .get(&url)
        .header("Referer", "https://music.163.com/")
        .send()
        .map_err(|e| e.to_string())?;
    if !resp.status().is_success() {
        return Err(format!("网易云搜索 HTTP {}", resp.status()));
    }
    let resp = resp.json::<NeteaseMusicResponse>().map_err(|e| e.to_string())?;
    
    let mut songs = Vec::new();
    let mut total = 0usize;
    if let Some(result) = resp.result {
        total = result.song_count.unwrap_or(result.songs.len() as u32) as usize;
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
    
    Ok(SearchPage {
        songs,
        total,
        page,
        page_size,
    })
}

/// 搜索歌曲 (统一搜索，支持多平台)
/// 
/// # 参数
/// - keyword: 搜索关键词
/// - platform: 平台 ("tx" 为 QQ 音乐, "wy" 为网易云音乐, "auto" 优先 QQ 再网易云)
/// 
/// # 返回
/// 成功时返回歌曲列表，失败时返回错误信息
pub fn search_music(keyword: &str, platform: &str) -> Result<Vec<SongInfo>, String> {
    Ok(search_music_paged(keyword, platform, 1, 10)?.songs)
}

pub fn search_music_paged(keyword: &str, platform: &str, page: u32, page_size: u32) -> Result<SearchPage, String> {
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
        "tx" => search_qq_music_paged(keyword, page, page_size),
        "wy" => search_netease_music_paged(keyword, page, page_size),
        "auto" => {
            match search_qq_music_paged(keyword, page, page_size) {
                Ok(ret) if !ret.songs.is_empty() => Ok(ret),
                _ => search_netease_music_paged(keyword, page, page_size),
            }
        }
        _ => unreachable!("平台验证已通过"),
    }
}
