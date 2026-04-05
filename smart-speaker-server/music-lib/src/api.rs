//! API 模块 - 负责与音乐 API 交互，获取音乐下载链接
use serde::Deserialize;
use reqwest::blocking::Client;
use std::sync::Mutex;

const DEFAULT_API_URL: &str = "https://source.shiqianjiang.cn/api/music";

const DEFAULT_LX_USER_AGENT: &str = "lx-music-request/2.12.0";

struct OnlineConfig {
    api_url: String,
    api_key: String,
    user_agent: String,
}

static ONLINE: Mutex<Option<OnlineConfig>> = Mutex::new(None);

pub fn configure_online(api_url: &str, api_key: &str, user_agent: &str) {
    let mut g = ONLINE.lock().unwrap();
    *g = Some(OnlineConfig {
        api_url: api_url.to_string(),
        api_key: api_key.to_string(),
        user_agent: user_agent.to_string(),
    });
}

pub fn music_api_url() -> String {
    let g = ONLINE.lock().unwrap();
    match *g {
        Some(ref c) => {
            let u = c.api_url.trim();
            if u.is_empty() {
                DEFAULT_API_URL.to_string()
            } else {
                u.to_string()
            }
        }
        None => DEFAULT_API_URL.to_string(),
    }
}

pub fn is_music_api_configured() -> bool {
    music_api_key().is_some()
}

pub fn music_api_key() -> Option<String> {
    let g = ONLINE.lock().unwrap();
    g.as_ref().and_then(|c| {
        let k = c.api_key.trim();
        if k.is_empty() {
            None
        } else {
            Some(k.to_string())
        }
    })
}

fn music_user_agent_for_client() -> String {
    let g = ONLINE.lock().unwrap();
    match *g {
        Some(ref c) => {
            let u = c.user_agent.trim();
            if u.is_empty() {
                DEFAULT_LX_USER_AGENT.to_string()
            } else {
                u.to_string()
            }
        }
        None => DEFAULT_LX_USER_AGENT.to_string(),
    }
}

/// 支持的下载平台（与常见洛雪音源脚本 `MUSIC_SOURCE` 对齐）
const SUPPORTED_DOWNLOAD_SOURCES: &[&str] = &["kw", "mg", "kg", "tx", "wy", "git"];

/// 酷我音乐 (kw) 支持的音质
const KW_QUALITIES: &[&str] = &["128k", "320k", "flac", "flac24bit", "hires"];

/// 咪咕音乐 (mg) 支持的音质
const MG_QUALITIES: &[&str] = &["128k", "320k", "flac", "flac24bit", "hires"];

/// 酷狗音乐 (kg) 支持的音质
const KG_QUALITIES: &[&str] = &["128k", "320k", "flac", "flac24bit", "hires", "atmos", "master"];

/// QQ 音乐 (tx) 支持的音质
const TX_QUALITIES: &[&str] = &["128k", "320k", "flac", "flac24bit", "hires", "atmos", "atmos_plus", "master"];

/// 网易云音乐 (wy) 支持的音质
const WY_QUALITIES: &[&str] = &["128k", "320k", "flac", "flac24bit", "hires", "atmos", "master"];

/// Gitee/Git 源 (git) 支持的音质
const GIT_QUALITIES: &[&str] = &["128k", "320k", "flac"];

/// 支持的音质（所有平台的合集）
const SUPPORTED_QUALITIES: &[&str] = &[
    "128k", "320k", "flac", "flac24bit", "hires", "atmos", "atmos_plus", "master",
];

/// 验证下载平台是否有效
fn is_valid_download_source(source: &str) -> bool {
    SUPPORTED_DOWNLOAD_SOURCES.contains(&source)
}

/// 验证音质是否有效
fn is_valid_quality(quality: &str) -> bool {
    SUPPORTED_QUALITIES.contains(&quality)
}

/// 获取某个平台支持的音质列表
fn get_platform_qualities(source: &str) -> &'static [&'static str] {
    match source {
        "kw" => KW_QUALITIES,
        "mg" => MG_QUALITIES,
        "kg" => KG_QUALITIES,
        "tx" => TX_QUALITIES,
        "wy" => WY_QUALITIES,
        "git" => GIT_QUALITIES,
        _ => &[],
    }
}

fn url_request_client() -> Result<Client, String> {
    let ua = music_user_agent_for_client();

    Client::builder()
        .user_agent(ua)
        .redirect(reqwest::redirect::Policy::limited(5))
        .build()
        .map_err(|e| e.to_string())
}

/// 验证某个平台是否支持指定音质
fn is_quality_supported_for_platform(source: &str, quality: &str) -> bool {
    get_platform_qualities(source).contains(&quality)
}

/// 直链是否为 AAC（128k 酷我等易返回 `.aac`；无 libav 时 GStreamer 播不动）
fn url_looks_like_aac(url: &str) -> bool {
    let u = url.to_ascii_lowercase();
    u.contains(".aac") || u.contains("format=aac") || u.contains("br=aac")
}

/// API 响应结构体
#[derive(Deserialize)]
pub struct ApiResponse {
    pub code: u32,
    pub url: Option<String>,
    pub message: Option<String>,
}

fn fetch_music_url(source: &str, song_id: &str, quality: &str, api_key: &str) -> Result<String, String> {
    let base = music_api_url().trim_end_matches('/').to_string();
    let url = format!(
        "{}/url?source={}&songId={}&quality={}",
        base, source, song_id, quality
    );

    let resp = url_request_client()?
        .get(&url)
        .header("Content-Type", "application/json")
        .header("X-API-Key", api_key)
        .send()
        .map_err(|e| e.to_string())?;

    let api_resp = resp.json::<ApiResponse>().map_err(|e| e.to_string())?;

    match api_resp.code {
        200 => api_resp.url.ok_or_else(|| "No URL".to_string()),
        403 => Err(api_resp.message.unwrap_or("权限不足或Key失效".to_string())),
        429 => Err(api_resp.message.unwrap_or("请求过速，请稍后再试".to_string())),
        500 => Err(api_resp.message.unwrap_or("API 错误".to_string())),
        _ => Err(format!("API 错误: {}", api_resp.code)),
    }
}

/// 获取音乐下载链接
pub fn get_music_url(source: &str, song_id: &str, quality: &str) -> Result<String, String> {
    let api_key = music_api_key().ok_or_else(|| {
        "未在 data/config/music.toml 配置 music_api_key，在线取链已关闭".to_string()
    })?;

    if !is_valid_download_source(source) {
        return Err(format!(
            "无效的下载平台 '{}'，支持的平台有: {}",
            source,
            SUPPORTED_DOWNLOAD_SOURCES.join(", ")
        ));
    }

    if song_id.trim().is_empty() {
        return Err("歌曲 ID 不能为空".to_string());
    }

    if !is_valid_quality(quality) {
        return Err(format!(
            "无效的音质 '{}'，支持的音质有: {}",
            quality,
            SUPPORTED_QUALITIES.join(", ")
        ));
    }

    if !is_quality_supported_for_platform(source, quality) {
        let platform_qualities = get_platform_qualities(source);
        return Err(format!(
            "平台 '{}' 不支持音质 '{}'，该平台支持的音质有: {}",
            source,
            quality,
            platform_qualities.join(", ")
        ));
    }

    let url = fetch_music_url(source, song_id, quality, &api_key)?;

    if url_looks_like_aac(&url) && quality == "128k" && is_quality_supported_for_platform(source, "320k") {
        if let Ok(u) = fetch_music_url(source, song_id, "320k", &api_key) {
            if !url_looks_like_aac(&u) {
                return Ok(u);
            }
        }
    }

    Ok(url)
}
