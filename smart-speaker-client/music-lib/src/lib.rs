//! 音乐下载器库 - 提供 C 语言 API 接口
mod api;
mod downloader;
mod search;

use std::ffi::{CStr, CString, c_char, c_void};
use std::path::Path;
use std::alloc::{alloc, dealloc, Layout};

/// C 语言进度回调函数类型定义
pub type CProgressCallback = extern "C" fn(u64, u64, *mut c_void);

/// 操作结果枚举
#[repr(C)]
pub enum Result {
    Ok = 0,
    InvalidParam = 1,
    ApiError = 2,
    DownloadError = 3,
}

/// C 语言音乐信息结构体
#[repr(C)]
pub struct CMusicInfo {
    id: [c_char; 64],
    name: [c_char; 128],
    artist: [c_char; 128],
    album: [c_char; 128],
    source: [c_char; 8],
    url: *mut c_char,
}

/// C 语言音乐搜索结果结构体
#[repr(C)]
pub struct CMusicSearchResult {
    results: *mut CMusicInfo,
    count: usize,
}

/// 获取音乐下载链接
/// 
/// # 参数
/// - source: 音乐平台源 ("kw", "mg", "kg", "tx", "wy")
/// - song_id: 歌曲 ID
/// - quality: 音质 ("128k", "320k", "flac" 等)
/// 
/// # 返回
/// 成功时返回音乐下载链接（需要调用 music_free_string 释放），失败时返回 NULL
#[unsafe(no_mangle)]
pub extern "C" fn music_get_url(
    source: *const c_char,
    song_id: *const c_char,
    quality: *const c_char,
) -> *mut c_char {
    if source.is_null() || song_id.is_null() || quality.is_null() {
        return std::ptr::null_mut();
    }
    
    let source = unsafe { CStr::from_ptr(source).to_str().unwrap() };
    let song_id = unsafe { CStr::from_ptr(song_id).to_str().unwrap() };
    let quality = unsafe { CStr::from_ptr(quality).to_str().unwrap() };
    
    match api::get_music_url(source, song_id, quality) {
        Ok(url) => CString::new(url).unwrap().into_raw(),
        Err(e) => {
            eprintln!("获取下载链接错误: {}", e);
            std::ptr::null_mut()
        }
    }
}

/// 下载音乐 (使用默认文件名，以 song_id 命名)
/// 
/// # 参数
/// - source: 音乐平台源 ("tx" 为 QQ 音乐, "wy" 为网易云音乐)
/// - song_id: 歌曲 ID
/// - quality: 音质 ("128k", "320k", "flac" 等)
/// - output_dir: 输出目录
/// - callback: 进度回调函数（可选）
/// - user_data: 用户自定义数据指针
/// 
/// # 返回
/// 操作结果（Result::Ok 表示成功）
#[unsafe(no_mangle)]
pub extern "C" fn music_download(
    source: *const c_char,
    song_id: *const c_char,
    quality: *const c_char,
    output_dir: *const c_char,
    callback: Option<CProgressCallback>,
    user_data: *mut c_void,
) -> Result {
    if source.is_null() || song_id.is_null() || quality.is_null() || output_dir.is_null() {
        return Result::InvalidParam;
    }
    
    let source = unsafe { CStr::from_ptr(source).to_str().unwrap() };
    let song_id = unsafe { CStr::from_ptr(song_id).to_str().unwrap() };
    let quality = unsafe { CStr::from_ptr(quality).to_str().unwrap() };
    let output_dir = unsafe { CStr::from_ptr(output_dir).to_str().unwrap() };
    
    let ext = downloader::get_extension(quality);
    let output_path = Path::new(output_dir).join(format!("{}.{}", song_id, ext));
    let output_path_str = output_path.to_str().unwrap();
    
    let url = match api::get_music_url(source, song_id, quality) {
        Ok(u) => u,
        Err(e) => {
            eprintln!("API 错误: {}", e);
            return Result::ApiError;
        }
    };
    
    match downloader::Downloader::new().download(&url, output_path_str, callback, user_data) {
        Ok(_) => Result::Ok,
        Err(e) => {
            eprintln!("下载错误: {}", e);
            Result::DownloadError
        }
    }
}

/// 下载音乐 (自定义文件名)
/// 
/// # 参数
/// - source: 音乐平台源 ("tx" 为 QQ 音乐, "wy" 为网易云音乐)
/// - song_id: 歌曲 ID
/// - quality: 音质 ("128k", "320k", "flac" 等)
/// - output_path: 完整输出路径
/// - callback: 进度回调函数（可选）
/// - user_data: 用户自定义数据指针
/// 
/// # 返回
/// 操作结果（Result::Ok 表示成功）
#[unsafe(no_mangle)]
pub extern "C" fn music_download_with_path(
    source: *const c_char,
    song_id: *const c_char,
    quality: *const c_char,
    output_path: *const c_char,
    callback: Option<CProgressCallback>,
    user_data: *mut c_void,
) -> Result {
    if source.is_null() || song_id.is_null() || quality.is_null() || output_path.is_null() {
        return Result::InvalidParam;
    }
    
    let source = unsafe { CStr::from_ptr(source).to_str().unwrap() };
    let song_id = unsafe { CStr::from_ptr(song_id).to_str().unwrap() };
    let quality = unsafe { CStr::from_ptr(quality).to_str().unwrap() };
    let output_path = unsafe { CStr::from_ptr(output_path).to_str().unwrap() };
    
    let url = match api::get_music_url(source, song_id, quality) {
        Ok(u) => u,
        Err(e) => {
            eprintln!("API 错误: {}", e);
            return Result::ApiError;
        }
    };
    
    match downloader::Downloader::new().download(&url, output_path, callback, user_data) {
        Ok(_) => Result::Ok,
        Err(e) => {
            eprintln!("下载错误: {}", e);
            Result::DownloadError
        }
    }
}

/// 根据音质获取文件扩展名
/// 
/// # 参数
/// - quality: 音质
/// 
/// # 返回
/// 文件扩展名（需要调用 music_free_string 释放）
#[unsafe(no_mangle)]
pub extern "C" fn music_get_extension(quality: *const c_char) -> *mut c_char {
    if quality.is_null() {
        return std::ptr::null_mut();
    }
    let q = unsafe { CStr::from_ptr(quality).to_str().unwrap() };
    let ext = downloader::get_extension(q);
    CString::new(ext).unwrap().into_raw()
}

/// 释放由 music_get_extension 返回的字符串
/// 
/// # 参数
/// - s: 要释放的字符串指针
#[unsafe(no_mangle)]
pub extern "C" fn music_free_string(s: *mut c_char) {
    if !s.is_null() {
        let _ = unsafe { CString::from_raw(s) };
    }
}

/// 搜索音乐
/// 
/// # 参数
/// - keyword: 搜索关键词
/// - platform: 平台 ("tx" 为 QQ 音乐, "wy" 为网易云音乐, "auto" 自动选择)
/// - result: 搜索结果输出指针
/// 
/// # 返回
/// 操作结果（Result::Ok 表示成功）
#[unsafe(no_mangle)]
pub extern "C" fn music_search(
    keyword: *const c_char,
    platform: *const c_char,
    result: *mut CMusicSearchResult,
) -> Result {
    if keyword.is_null() || platform.is_null() || result.is_null() {
        return Result::InvalidParam;
    }
    
    let keyword = unsafe { CStr::from_ptr(keyword).to_str().unwrap() };
    let platform = unsafe { CStr::from_ptr(platform).to_str().unwrap() };
    
    let songs = match search::search_music(keyword, platform) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("搜索错误: {}", e);
            return Result::ApiError;
        }
    };
    
    let count = songs.len();
    if count == 0 {
        unsafe {
            (*result).results = std::ptr::null_mut();
            (*result).count = 0;
        }
        return Result::Ok;
    }
    
    let layout = Layout::array::<CMusicInfo>(count).unwrap();
    let results_ptr = unsafe { alloc(layout) as *mut CMusicInfo };
    
    if results_ptr.is_null() {
        return Result::InvalidParam;
    }
    
    for (i, song) in songs.into_iter().enumerate() {
        unsafe {
            let c_info = &mut *results_ptr.add(i);
            
            let id_c = CString::new(song.id.clone()).unwrap();
            id_c.as_bytes_with_nul()
                .iter()
                .enumerate()
                .take(63)
                .for_each(|(j, &b)| {
                    c_info.id[j] = b as c_char;
                });
            c_info.id[63] = 0;
            
            let name_c = CString::new(song.name.clone()).unwrap();
            name_c.as_bytes_with_nul()
                .iter()
                .enumerate()
                .take(127)
                .for_each(|(j, &b)| {
                    c_info.name[j] = b as c_char;
                });
            c_info.name[127] = 0;
            
            let artist_c = CString::new(song.artist.clone()).unwrap();
            artist_c.as_bytes_with_nul()
                .iter()
                .enumerate()
                .take(127)
                .for_each(|(j, &b)| {
                    c_info.artist[j] = b as c_char;
                });
            c_info.artist[127] = 0;
            
            let album_c = CString::new(song.album.clone()).unwrap();
            album_c.as_bytes_with_nul()
                .iter()
                .enumerate()
                .take(127)
                .for_each(|(j, &b)| {
                    c_info.album[j] = b as c_char;
                });
            c_info.album[127] = 0;
            
            let source_c = CString::new(song.source.clone()).unwrap();
            source_c.as_bytes_with_nul()
                .iter()
                .enumerate()
                .take(7)
                .for_each(|(j, &b)| {
                    c_info.source[j] = b as c_char;
                });
            c_info.source[7] = 0;
            
            c_info.url = std::ptr::null_mut();
        }
    }
    
    unsafe {
        (*result).results = results_ptr;
        (*result).count = count;
    }
    
    Result::Ok
}

/// 释放搜索结果
/// 
/// # 参数
/// - result: 搜索结果指针
#[unsafe(no_mangle)]
pub extern "C" fn music_free_search_result(result: *mut CMusicSearchResult) {
    if result.is_null() {
        return;
    }
    
    let results_ptr = unsafe { (*result).results };
    let count = unsafe { (*result).count };
    
    if !results_ptr.is_null() && count > 0 {
        for i in 0..count {
            unsafe {
                let c_info = &mut *results_ptr.add(i);
                if !c_info.url.is_null() {
                    let _ = CString::from_raw(c_info.url);
                }
            }
        }
        
        let layout = Layout::array::<CMusicInfo>(count).unwrap();
        unsafe {
            dealloc(results_ptr as *mut u8, layout);
        }
    }
    
    unsafe {
        (*result).results = std::ptr::null_mut();
        (*result).count = 0;
    }
}
