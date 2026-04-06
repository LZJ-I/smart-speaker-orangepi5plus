//! 搜索模块 - 负责从各音乐平台搜索歌曲
use reqwest::blocking::Client;
use serde::Deserialize;
use serde_json::Value;
use std::cmp::Ordering;
use std::collections::HashSet;
use std::time::{Duration, Instant};
use std::vec::Vec;

/// lx `renderer/utils/request.js` → `fetchData` 默认 UA
const HTTP_USER_AGENT: &str = "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/69.0.3497.100 Safari/537.36";

/// lx `fetchData` 默认 `timeout`（ms），`all` 并发与 `auto` 顺序单次请求相同
const HTTP_SEARCH_TIMEOUT: Duration = Duration::from_secs(15);
/// `auto` 顺序：最多 5 源 × 单次 15s 量级的预算
const PRIORITY_SEARCH_TOTAL: Duration = Duration::from_secs(75);

/// lx `store/search/music/state.ts`：`listInfos.all.limit`
const LX_AGG_SOURCE_LIMIT: u32 = 30;

fn http_client() -> Result<Client, String> {
    Client::builder()
        .user_agent(HTTP_USER_AGENT)
        .timeout(HTTP_SEARCH_TIMEOUT)
        .build()
        .map_err(|e| e.to_string())
}

/// 支持的搜索平台
const SUPPORTED_SEARCH_PLATFORMS: &[&str] = &["tx", "wy", "kw", "kg", "mg", "auto", "all"];

/// 验证搜索平台是否有效
fn is_valid_search_platform(platform: &str) -> bool {
    SUPPORTED_SEARCH_PLATFORMS.contains(&platform)
}

/// 歌曲信息结构体
#[derive(Clone)]
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
    /// lx 聚合：`Math.max(...allPage)`；0 表示由 `music_search_page` 用 total/page_size 推导
    pub max_page: u32,
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
        max_page: 0,
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
        max_page: 0,
    })
}

fn empty_search_page(page: u32, page_size: u32) -> SearchPage {
    SearchPage {
        songs: Vec::new(),
        total: 0,
        page,
        page_size,
        max_page: 0,
    }
}

fn value_as_string(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        Value::Number(n) => n.to_string(),
        Value::Bool(b) => b.to_string(),
        _ => String::new(),
    }
}

fn kugou_singer_str(v: &Value) -> String {
    if let Some(s) = v.as_str() {
        return s.to_string();
    }
    if let Some(arr) = v.as_array() {
        let mut parts = Vec::new();
        for it in arr {
            if let Some(n) = it.get("name").and_then(|x| x.as_str()) {
                parts.push(n);
            } else if let Some(s) = it.as_str() {
                parts.push(s);
            }
        }
        return parts.join("、");
    }
    String::new()
}

fn migu_sign(keyword: &str, timestamp_ms: &str) -> (String, &'static str) {
    const DEVICE_ID: &str = "963B7AA0D21511ED807EE5846EC87D20";
    const SIG_MD5: &str = "6cdc72a439cef99a3418d2a78aa28c73";
    const MID: &str = "yyapp2d16148780a1dcc7408e06336b98cfd50";
    let raw = format!("{}{}{}{}{}", keyword, SIG_MD5, MID, DEVICE_ID, timestamp_ms);
    let digest = md5::compute(raw.as_bytes());
    let sign = digest.iter().map(|b| format!("{:02x}", b)).collect();
    (sign, DEVICE_ID)
}

fn migu_singer_list(v: &Value) -> String {
    if let Some(s) = v.as_str() {
        return s.to_string();
    }
    if let Some(arr) = v.as_array() {
        let mut parts = Vec::new();
        for it in arr {
            if let Some(n) = it.get("name").and_then(|x| x.as_str()) {
                parts.push(n);
            }
        }
        return parts.join("、");
    }
    String::new()
}

fn push_kugou_track(item: &Value, songs: &mut Vec<SongInfo>, ids: &mut HashSet<String>) {
    let id = value_as_string(item.get("Audioid").unwrap_or(&Value::Null));
    if id.is_empty() {
        return;
    }
    let hash = value_as_string(item.get("FileHash").unwrap_or(&Value::Null));
    let key = format!("{}|{}", id, hash);
    if !ids.insert(key) {
        return;
    }
    songs.push(SongInfo {
        id,
        name: item
            .get("SongName")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string(),
        artist: item
            .get("Singers")
            .map(|v| kugou_singer_str(v))
            .unwrap_or_default(),
        album: item
            .get("AlbumName")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string(),
        source: "kg".to_string(),
    });
}

fn parse_migu_songs(srd: &Value) -> (Vec<SongInfo>, usize) {
    let mut songs = Vec::new();
    let mut seen: HashSet<String> = HashSet::new();
    let total = srd
        .get("totalCount")
        .map(|t| {
            t.as_str()
                .and_then(|s| s.parse().ok())
                .unwrap_or_else(|| t.as_u64().unwrap_or(0) as usize)
        })
        .unwrap_or(0);
    if let Some(outer) = srd.get("resultList").and_then(|x| x.as_array()) {
        for group in outer {
            if let Some(inner) = group.as_array() {
                for data in inner {
                    let sid = value_as_string(data.get("songId").unwrap_or(&Value::Null));
                    let cid = value_as_string(data.get("copyrightId").unwrap_or(&Value::Null));
                    if sid.is_empty() || cid.is_empty() {
                        continue;
                    }
                    if !seen.insert(cid) {
                        continue;
                    }
                    songs.push(SongInfo {
                        id: sid,
                        name: data
                            .get("name")
                            .and_then(|x| x.as_str())
                            .unwrap_or("")
                            .to_string(),
                        artist: data
                            .get("singerList")
                            .map(|v| migu_singer_list(v))
                            .unwrap_or_default(),
                        album: data
                            .get("album")
                            .and_then(|x| x.as_str())
                            .unwrap_or("")
                            .to_string(),
                        source: "mg".to_string(),
                    });
                }
            }
        }
    }
    (songs, total)
}

/// 酷我（与 lx `kw/musicSearch.js` 一致）
pub fn search_kuwo_music_paged(keyword: &str, page: u32, page_size: u32) -> Result<SearchPage, String> {
    let page = if page == 0 { 1 } else { page };
    let page_size = if page_size == 0 { 10 } else { page_size };
    let pn = page.saturating_sub(1);

    for _attempt in 0u32..3u32 {
        let q = urlencoding::encode(keyword);
        let url = format!(
            "http://search.kuwo.cn/r.s?client=kt&all={}&pn={}&rn={}&uid=794762570&ver=kwplayer_ar_9.2.2.1&vipver=1&show_copyright_off=1&newver=1&ft=music&cluster=0&strategy=2012&encoding=utf8&rformat=json&vermerge=1&mobi=1&issubtitle=1",
            q, pn, page_size
        );
        let resp = http_client()?.get(&url).send().map_err(|e| e.to_string())?;
        if !resp.status().is_success() {
            return Err(format!("酷我搜索 HTTP {}", resp.status()));
        }
        let root: Value = resp.json().map_err(|e| e.to_string())?;
        let total = root
            .get("TOTAL")
            .map(|t| {
                t.as_str()
                    .and_then(|s| s.parse().ok())
                    .unwrap_or_else(|| t.as_u64().unwrap_or(0) as usize)
            })
            .unwrap_or(0);
        let show = root
            .get("SHOW")
            .and_then(|t| t.as_str())
            .unwrap_or("1");
        if total != 0 && show == "0" {
            continue;
        }
        let abslist = root
            .get("abslist")
            .and_then(|x| x.as_array())
            .cloned()
            .unwrap_or_default();
        let mut songs = Vec::new();
        for item in &abslist {
            if item.get("N_MINFO").is_none() {
                continue;
            }
            let rid = value_as_string(item.get("MUSICRID").unwrap_or(&Value::Null));
            let id = rid
                .strip_prefix("MUSIC_")
                .unwrap_or(rid.as_str())
                .to_string();
            if id.is_empty() {
                continue;
            }
            songs.push(SongInfo {
                id,
                name: item
                    .get("SONGNAME")
                    .and_then(|x| x.as_str())
                    .unwrap_or("")
                    .to_string(),
                artist: item
                    .get("ARTIST")
                    .and_then(|x| x.as_str())
                    .unwrap_or("")
                    .to_string(),
                album: item
                    .get("ALBUM")
                    .and_then(|x| x.as_str())
                    .unwrap_or("")
                    .to_string(),
                source: "kw".to_string(),
            });
        }
        if songs.is_empty() && total != 0 {
            continue;
        }
        return Ok(SearchPage {
            songs,
            total,
            page,
            page_size,
            max_page: 0,
        });
    }
    Err("酷我搜索失败".to_string())
}

/// 酷狗（与 lx `kg/musicSearch.js` 一致）
pub fn search_kugou_music_paged(keyword: &str, page: u32, page_size: u32) -> Result<SearchPage, String> {
    let page = if page == 0 { 1 } else { page };
    let page_size = if page_size == 0 { 10 } else { page_size };

    for _attempt in 0u32..3u32 {
        let k = urlencoding::encode(keyword);
        let url = format!(
            "https://songsearch.kugou.com/song_search_v2?keyword={}&page={}&pagesize={}&userid=0&clientver=&platform=WebFilter&filter=2&iscorrection=1&privilege_filter=0&area_code=1",
            k, page, page_size
        );
        let resp = http_client()?.get(&url).send().map_err(|e| e.to_string())?;
        if !resp.status().is_success() {
            return Err(format!("酷狗搜索 HTTP {}", resp.status()));
        }
        let root: Value = resp.json().map_err(|e| e.to_string())?;
        if root.get("error_code").and_then(|c| c.as_i64()) != Some(0) {
            continue;
        }
        let data = root.get("data").ok_or_else(|| "酷狗响应无 data".to_string())?;
        let total = data
            .get("total")
            .map(|t| t.as_u64().unwrap_or(0) as usize)
            .unwrap_or(0);
        let lists = data
            .get("lists")
            .and_then(|x| x.as_array())
            .cloned()
            .unwrap_or_default();
        let mut songs = Vec::new();
        let mut ids: HashSet<String> = HashSet::new();
        for item in &lists {
            push_kugou_track(item, &mut songs, &mut ids);
            if let Some(grp) = item.get("Grp").and_then(|g| g.as_array()) {
                for ch in grp {
                    push_kugou_track(ch, &mut songs, &mut ids);
                }
            }
        }
        if songs.is_empty() && total > 0 {
            continue;
        }
        return Ok(SearchPage {
            songs,
            total,
            page,
            page_size,
            max_page: 0,
        });
    }
    Err("酷狗搜索失败".to_string())
}

/// 咪咕（与 lx `mg/musicSearch.js` 签名与 v3 接口一致）
pub fn search_migu_music_paged(keyword: &str, page: u32, page_size: u32) -> Result<SearchPage, String> {
    let page = if page == 0 { 1 } else { page };
    let page_size = if page_size == 0 { 10 } else { page_size };
    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis().to_string())
        .unwrap_or_else(|_| "0".into());
    let (sign, device_id) = migu_sign(keyword, &ts);
    let q = urlencoding::encode(keyword);
    let url = format!(
        "https://jadeite.migu.cn/music_search/v3/search/searchAll?isCorrect=0&isCopyright=1&searchSwitch=%7B%22song%22%3A1%2C%22album%22%3A0%2C%22singer%22%3A0%2C%22tagSong%22%3A1%2C%22mvSong%22%3A0%2C%22bestShow%22%3A1%2C%22songlist%22%3A0%2C%22lyricSong%22%3A0%7D&pageSize={}&text={}&pageNo={}&sort=0&sid=USS",
        page_size, q, page
    );
    let resp = http_client()?
        .get(&url)
        .header("uiVersion", "A_music_3.6.1")
        .header("deviceId", device_id)
        .header("timestamp", &ts)
        .header("sign", &sign)
        .header("channel", "0146921")
        .header(
            "User-Agent",
            "Mozilla/5.0 (Linux; U; Android 11.0.0; zh-cn; MI 11 Build/OPR1.170623.032) AppleWebKit/534.30 (KHTML, like Gecko) Version/4.0 Mobile Safari/534.30",
        )
        .send()
        .map_err(|e| e.to_string())?;
    if !resp.status().is_success() {
        return Err(format!("咪咕搜索 HTTP {}", resp.status()));
    }
    let root: Value = resp.json().map_err(|e| e.to_string())?;
    if root.get("code").and_then(|c| c.as_str()) != Some("000000") {
        let info = root
            .get("info")
            .and_then(|x| x.as_str())
            .unwrap_or("咪咕搜索失败");
        return Err(info.to_string());
    }
    let srd_val = root
        .get("songResultData")
        .cloned()
        .unwrap_or(Value::Null);
    let (songs, total) = parse_migu_songs(&srd_val);
    Ok(SearchPage {
        songs,
        total,
        page,
        page_size,
        max_page: 0,
    })
}

/// lx `common/utils/common.ts`：`similar`
fn levenshtein_distance_chars(short_s: &str, long_s: &str) -> usize {
    let a: Vec<char> = short_s.chars().collect();
    let b: Vec<char> = long_s.chars().collect();
    let al = a.len();
    let bl = b.len();
    if al == 0 {
        return bl;
    }
    if bl == 0 {
        return al;
    }
    let mut dp = vec![0usize; bl + 1];
    for j in 0..=bl {
        dp[j] = j;
    }
    for i in 1..=al {
        let mut prev = dp[0];
        dp[0] = i;
        for j in 1..=bl {
            let tmp = dp[j];
            let cost = usize::from(a[i - 1] != b[j - 1]);
            dp[j] = (dp[j] + 1).min(dp[j - 1] + 1).min(prev + cost);
            prev = tmp;
        }
    }
    dp[bl]
}

fn lx_similar(a: &str, b: &str) -> f64 {
    let a = a.trim();
    let b = b.trim();
    if a.is_empty() || b.is_empty() {
        return 0.0;
    }
    let (short_s, long_s) = if a.len() > b.len() { (b, a) } else { (a, b) };
    let bl = long_s.chars().count();
    if bl == 0 {
        return 0.0;
    }
    let dist = levenshtein_distance_chars(short_s, long_s);
    1.0 - (dist as f64 / bl as f64)
}

fn lx_id_key(song: &SongInfo) -> String {
    format!("{}_{}", song.source, song.id)
}

fn source_all_page(total: usize, fetch_limit: u32) -> u32 {
    if total == 0 || fetch_limit == 0 {
        0
    } else {
        ((total as u64 + u64::from(fetch_limit) - 1) / u64::from(fetch_limit)) as u32
    }
}

/// lx `store/search/music/action.ts`：`setLists`（`allPage >= page`、拼接、`deduplicationList`、`handleSortList`）
fn merge_all_sources_lx(
    keyword: &str,
    page: u32,
    fetch_limit: u32,
    ordered: &[(Vec<SongInfo>, usize)],
) -> (Vec<SongInfo>, usize, u32) {
    let mut combined: Vec<SongInfo> = Vec::new();
    let mut max_total: usize = 0;
    let mut max_all_page: u32 = 0;
    for (songs, total) in ordered {
        max_total = max_total.max(*total);
        let ap = source_all_page(*total, fetch_limit);
        max_all_page = max_all_page.max(ap);
        if ap < page {
            continue;
        }
        combined.extend(songs.iter().cloned());
    }
    let mut seen: HashSet<String> = HashSet::new();
    combined.retain(|s| seen.insert(lx_id_key(s)));
    let mut scored: Vec<(f64, usize, SongInfo)> = combined
        .into_iter()
        .enumerate()
        .map(|(i, s)| {
            let haystack = format!("{} {}", s.name, s.artist);
            (lx_similar(keyword, &haystack), i, s)
        })
        .collect();
    scored.sort_by(|a, b| {
        b.0.partial_cmp(&a.0)
            .unwrap_or(Ordering::Equal)
            .then_with(|| a.1.cmp(&b.1))
    });
    let songs: Vec<SongInfo> = scored.into_iter().map(|(_, _, s)| s).collect();
    (songs, max_total, max_all_page)
}

/// `auto`：lx `musicSdk/index` 源顺序 kw→kg→tx→wy→mg；单次 HTTP 15s；总预算 `PRIORITY_SEARCH_TOTAL`
fn search_priority_paged(keyword: &str, page: u32, page_size: u32) -> Result<SearchPage, String> {
    const ORDER: &[&str] = &["kw", "kg", "tx", "wy", "mg"];
    let start = Instant::now();
    let mut last_note = String::new();
    for p in ORDER {
        let elapsed = start.elapsed();
        if elapsed >= PRIORITY_SEARCH_TOTAL {
            last_note = format!(
                "顺序搜总时限{}s已到（单次 HTTP {}s）",
                PRIORITY_SEARCH_TOTAL.as_secs(),
                HTTP_SEARCH_TIMEOUT.as_secs()
            );
            break;
        }
        if PRIORITY_SEARCH_TOTAL.saturating_sub(elapsed) < HTTP_SEARCH_TIMEOUT {
            last_note = format!(
                "顺序搜总时限{}s：剩余时间不足单次 HTTP {}s，未再试下一源",
                PRIORITY_SEARCH_TOTAL.as_secs(),
                HTTP_SEARCH_TIMEOUT.as_secs()
            );
            break;
        }
        let r = match *p {
            "tx" => search_qq_music_paged(keyword, page, page_size),
            "wy" => search_netease_music_paged(keyword, page, page_size),
            "kw" => search_kuwo_music_paged(keyword, page, page_size),
            "kg" => search_kugou_music_paged(keyword, page, page_size),
            "mg" => search_migu_music_paged(keyword, page, page_size),
            _ => continue,
        };
        match r {
            Ok(sp) if !sp.songs.is_empty() => return Ok(sp),
            Ok(_) => last_note = format!("{}:无结果", p),
            Err(e) => last_note = format!("{}: {}", p, e),
        }
    }
    Err(format!("顺序搜索无有效结果; {}", last_note))
}

pub fn search_all_paged(keyword: &str, page: u32, _page_size: u32) -> Result<SearchPage, String> {
    let page = if page == 0 { 1 } else { page };
    let fetch_limit = LX_AGG_SOURCE_LIMIT;
    let kw = keyword.to_string();

    let (r_kw, r_kg, r_tx, r_wy, r_mg) = std::thread::scope(|s| {
        let a = kw.clone();
        let h_kw = s.spawn(move || search_kuwo_music_paged(&a, page, fetch_limit));
        let b = kw.clone();
        let h_kg = s.spawn(move || search_kugou_music_paged(&b, page, fetch_limit));
        let c = kw.clone();
        let h_tx = s.spawn(move || search_qq_music_paged(&c, page, fetch_limit));
        let d = kw.clone();
        let h_wy = s.spawn(move || search_netease_music_paged(&d, page, fetch_limit));
        let e = kw.clone();
        let h_mg = s.spawn(move || search_migu_music_paged(&e, page, fetch_limit));
        let thread_err = || Err("搜索线程异常".to_string());
        (
            h_kw.join().unwrap_or_else(|_| thread_err()),
            h_kg.join().unwrap_or_else(|_| thread_err()),
            h_tx.join().unwrap_or_else(|_| thread_err()),
            h_wy.join().unwrap_or_else(|_| thread_err()),
            h_mg.join().unwrap_or_else(|_| thread_err()),
        )
    });

    let mut err_notes: Vec<String> = Vec::new();
    let p_kw = match r_kw {
        Ok(p) => p,
        Err(e) => {
            err_notes.push(format!("kw: {}", e));
            empty_search_page(page, fetch_limit)
        }
    };
    let p_kg = match r_kg {
        Ok(p) => p,
        Err(e) => {
            err_notes.push(format!("kg: {}", e));
            empty_search_page(page, fetch_limit)
        }
    };
    let p_tx = match r_tx {
        Ok(p) => p,
        Err(e) => {
            err_notes.push(format!("tx: {}", e));
            empty_search_page(page, fetch_limit)
        }
    };
    let p_wy = match r_wy {
        Ok(p) => p,
        Err(e) => {
            err_notes.push(format!("wy: {}", e));
            empty_search_page(page, fetch_limit)
        }
    };
    let p_mg = match r_mg {
        Ok(p) => p,
        Err(e) => {
            err_notes.push(format!("mg: {}", e));
            empty_search_page(page, fetch_limit)
        }
    };

    let ordered = [
        (p_kw.songs, p_kw.total),
        (p_kg.songs, p_kg.total),
        (p_tx.songs, p_tx.total),
        (p_wy.songs, p_wy.total),
        (p_mg.songs, p_mg.total),
    ];
    let (songs, total, max_page) = merge_all_sources_lx(keyword, page, fetch_limit, &ordered);

    if songs.is_empty() {
        let mut msg = String::from("聚合搜索无结果");
        for n in err_notes {
            msg.push_str("; ");
            msg.push_str(&n);
        }
        return Err(msg);
    }

    let out_len = songs.len() as u32;
    Ok(SearchPage {
        songs,
        total,
        page,
        page_size: out_len,
        max_page,
    })
}

/// 搜索歌曲 (统一搜索，支持多平台)
/// 
/// # 参数
/// - keyword: 搜索关键词
/// - platform: 单源 tx/wy/kw/kg/mg；`auto` 顺序 kw→kg→tx→wy→mg（lx，单次 HTTP 15s、总≤75s）；`all` 多源并发（lx 每源 limit=30、合并/去重/相似度排序，HTTP 15s）
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
        "kw" => search_kuwo_music_paged(keyword, page, page_size),
        "kg" => search_kugou_music_paged(keyword, page, page_size),
        "mg" => search_migu_music_paged(keyword, page, page_size),
        "auto" => search_priority_paged(keyword, page, page_size),
        "all" => search_all_paged(keyword, page, page_size),
        _ => unreachable!("平台验证已通过"),
    }
}
