//! 下载器模块 - 负责下载音乐文件并管理进度
use reqwest::blocking::Client;
use std::fs::{self, File};
use std::io::{Write, Read};
use std::path::Path;

/// 进度回调函数类型定义
pub type ProgressCallback = extern "C" fn(u64, u64, *mut std::ffi::c_void);

/// 下载器结构体
pub struct Downloader {
    client: Client,
}

impl Downloader {
    /// 创建一个新的下载器实例
    /// 
    /// # 返回
    /// 新的 Downloader 实例
    pub fn new() -> Self {
        Self { client: Client::new() }
    }
    
    /// 检查响应是否是 JSON 错误
    fn is_json_error_response(data: &[u8]) -> bool {
        if data.len() < 2 {
            return false;
        }
        
        let trimmed = data.trim_ascii_start();
        if trimmed.is_empty() {
            return false;
        }
        
        // 检查是否以 { 或 [ 开头（JSON 特征）
        trimmed[0] == b'{' || trimmed[0] == b'['
    }
    
    /// 下载文件
    /// 
    /// # 参数
    /// - url: 下载链接
    /// - path: 保存路径
    /// - callback: 进度回调函数（可选）
    /// - user_data: 用户自定义数据指针
    /// 
    /// # 返回
    /// 成功时返回 Ok(())，失败时返回错误信息
    pub fn download(
        &self,
        url: &str,
        path: &str,
        callback: Option<ProgressCallback>,
        user_data: *mut std::ffi::c_void,
    ) -> Result<(), String> {
        let path_obj = Path::new(path);
        if let Some(parent) = path_obj.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        
        let mut resp = self.client.get(url).send().map_err(|e| e.to_string())?;
        
        // 先读取一部分数据检查是否是 JSON 错误
        let mut first_buf = [0u8; 1024];
        let first_read = resp.read(&mut first_buf).map_err(|e| e.to_string())?;
        
        if first_read > 0 && Self::is_json_error_response(&first_buf[..first_read]) {
            // 尝试读取完整响应作为错误信息
            let mut error_content = Vec::from(&first_buf[..first_read]);
            let mut temp_buf = [0u8; 8192];
            loop {
                match resp.read(&mut temp_buf) {
                    Ok(0) => break,
                    Ok(n) => error_content.extend_from_slice(&temp_buf[..n]),
                    Err(_) => break,
                }
            }
            
            let error_msg = String::from_utf8_lossy(&error_content).to_string();
            return Err(format!("下载失败: {}", error_msg));
        }
        
        let total = resp.content_length().unwrap_or(0);
        let mut file = File::create(path).map_err(|e| e.to_string())?;
        
        // 写入已经读取的第一部分
        if first_read > 0 {
            file.write_all(&first_buf[..first_read]).map_err(|e| e.to_string())?;
        }
        
        let mut downloaded = first_read as u64;
        let mut buf = [0u8; 8192];
        
        if let Some(cb) = callback {
            cb(downloaded, total, user_data);
        }
        
        loop {
            match resp.read(&mut buf) {
                Ok(0) => break,
                Ok(n) => {
                    file.write_all(&buf[..n]).map_err(|e| e.to_string())?;
                    downloaded += n as u64;
                    if let Some(cb) = callback {
                        cb(downloaded, total, user_data);
                    }
                }
                Err(e) => {
                    if downloaded > 0 && downloaded == total {
                        break;
                    }
                    return Err(e.to_string());
                }
            }
        }
        Ok(())
    }
}

/// 根据音质获取文件扩展名
/// 
/// # 参数
/// - quality: 音质
/// 
/// # 返回
/// 文件扩展名（如 "mp3", "flac"）
pub fn get_extension(quality: &str) -> &'static str {
    match quality {
        "128k" | "320k" => "mp3",
        "flac" | "flac24bit" | "hires" | "atmos" | "atmos_plus" | "master" => "flac",
        _ => "mp3"
    }
}
