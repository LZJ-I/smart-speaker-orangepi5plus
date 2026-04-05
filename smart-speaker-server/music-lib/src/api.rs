//! API 模块 - 负责与音乐 API 交互，获取音乐下载链接
use serde::Deserialize;
use reqwest::blocking::Client;

const API_URL: &str = "https://source.shiqianjiang.cn/api/music";
const API_KEY: &str = "CERU_KEY-53DAC0A06479925D774AC4B8A5F8A87E25A4D6333120";

/// 支持的下载平台
const SUPPORTED_DOWNLOAD_SOURCES: &[&str] = &["kw", "mg", "kg", "tx", "wy"];

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

/// 支持的音质（所有平台的合集）
const SUPPORTED_QUALITIES: &[&str] = &[
    "128k", "320k", "flac", "flac24bit", "hires", "atmos", "atmos_plus", "master"
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
        _ => &[],
    }
}

/// 验证某个平台是否支持指定音质
fn is_quality_supported_for_platform(source: &str, quality: &str) -> bool {
    get_platform_qualities(source).contains(&quality)
}

/// API 响应结构体
#[derive(Deserialize)]
pub struct ApiResponse {
    pub code: u32,
    pub url: Option<String>,
    pub message: Option<String>,
}

/// 获取音乐下载链接
/// 
/// # 参数
/// - source: 音乐平台源 ("kw", "mg", "kg", "tx", "wy")
/// - song_id: 歌曲 ID
/// - quality: 音质 ("128k", "320k", "flac" 等)
/// 
/// # 返回
/// 成功时返回音乐下载链接，失败时返回错误信息
pub fn get_music_url(source: &str, song_id: &str, quality: &str) -> Result<String, String> {
    // 验证平台
    if !is_valid_download_source(source) {
        return Err(format!(
            "无效的下载平台 '{}'，支持的平台有: {}",
            source,
            SUPPORTED_DOWNLOAD_SOURCES.join(", ")
        ));
    }
    
    // 验证歌曲 ID
    if song_id.trim().is_empty() {
        return Err("歌曲 ID 不能为空".to_string());
    }
    
    // 验证音质
    if !is_valid_quality(quality) {
        return Err(format!(
            "无效的音质 '{}'，支持的音质有: {}",
            quality,
            SUPPORTED_QUALITIES.join(", ")
        ));
    }
    
    // 验证该平台是否支持此音质
    if !is_quality_supported_for_platform(source, quality) {
        let platform_qualities = get_platform_qualities(source);
        return Err(format!(
            "平台 '{}' 不支持音质 '{}'，该平台支持的音质有: {}",
            source,
            quality,
            platform_qualities.join(", ")
        ));
    }
    
    let url = format!("{}/url?source={}&songId={}&quality={}", API_URL, source, song_id, quality);
    
    let resp = Client::new()
        .get(&url)
        .header("X-API-Key", API_KEY)
        .send()
        .map_err(|e| e.to_string())?;
    
    let api_resp = resp.json::<ApiResponse>()
        .map_err(|e| e.to_string())?;
    
    match api_resp.code {
        200 => api_resp.url.ok_or_else(|| "No URL".to_string()),
        403 => Err(api_resp.message.unwrap_or("权限不足或Key失效".to_string())),
        429 => Err(api_resp.message.unwrap_or("请求过速，请稍后再试".to_string())),
        500 => Err(api_resp.message.unwrap_or("API 错误".to_string())),
        _ => Err(format!("API 错误: {}", api_resp.code)),
    }
}
