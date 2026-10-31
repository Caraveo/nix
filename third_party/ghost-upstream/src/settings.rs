use std::ffi::c_void;
use std::path::PathBuf;

use serde::Deserialize;

use crate::app::{App, Theme};

extern "C" {
    fn dlopen(filename: *const i8, flag: i32) -> *mut c_void;
    fn dlsym(handle: *mut c_void, symbol: *const i8) -> *mut c_void;
}

const RTLD_NOW: i32 = 2;

#[derive(Deserialize, Default, Clone)]
pub struct SettingsFile {
    #[serde(default = "default_theme")]
    pub theme: String,
    #[serde(default = "default_font_size")]
    pub font_size: f64,
    #[serde(default = "default_pty_cols")]
    pub pty_cols: i64,
    #[serde(default = "default_pty_rows")]
    pub pty_rows: i64,
    #[serde(default = "default_true")]
    pub auto_switch: bool,
    #[serde(default = "default_true")]
    pub startup_msg: bool,
    #[serde(default = "default_true")]
    pub safety: bool,
}

fn default_theme() -> String { "Light".into() }
fn default_font_size() -> f64 { 13.0 }
fn default_pty_cols() -> i64 { 120 }
fn default_pty_rows() -> i64 { 40 }
fn default_true() -> bool { true }

impl SettingsFile {
    pub fn path() -> Option<PathBuf> {
        let home = std::env::var("HOME").ok()?;
        Some(PathBuf::from(format!("{}/.config/ghost/settings.json", home)))
    }

    pub fn load() -> Option<Self> {
        let path = Self::path()?;
        let content = std::fs::read_to_string(&path).ok()?;
        serde_json::from_str(&content).ok()
    }

    pub fn apply_to(&self, app: &mut App) {
        match self.theme.as_str() {
            "DarkCyan" => app.theme = Theme::DarkCyan,
            "Matrix" => app.theme = Theme::Matrix,
            "Solarized" => app.theme = Theme::Solarized,
            "Gruvbox" => app.theme = Theme::Gruvbox,
            "Light" => app.theme = Theme::Light,
            _ => {}
        }
        app.font_size = self.font_size as f32;
        app.pty_cols = self.pty_cols as u16;
        app.pty_rows = self.pty_rows as u16;
        app.auto_switch_pty = self.auto_switch;
        app.show_startup_msg = self.startup_msg;
        app.safety_enabled = self.safety;
    }
}

pub struct SettingsWatcher {
    last_mtime: Option<std::time::SystemTime>,
}

impl SettingsWatcher {
    pub fn new() -> Self {
        Self { last_mtime: None }
    }

    pub fn check_and_apply(&mut self, app: &mut App) {
        let path = match SettingsFile::path() {
            Some(p) => p,
            None => return,
        };

        let mtime = match std::fs::metadata(&path).and_then(|m| m.modified()) {
            Ok(t) => t,
            Err(_) => return,
        };

        if self.last_mtime == Some(mtime) {
            return;
        }

        self.last_mtime = Some(mtime);

        if let Some(settings) = SettingsFile::load() {
            settings.apply_to(app);
        }
    }
}

pub fn show_native_settings() -> bool {
    let exe = match std::env::current_exe() {
        Ok(e) => e,
        Err(_) => return false,
    };

    let dylib = exe
        .parent()
        .and_then(|p| p.parent())
        .map(|p| p.join("Frameworks").join("libghost_settings.dylib"));

    let dylib = match dylib {
        Some(d) => d,
        None => return false,
    };

    if !dylib.exists() {
        return false;
    }

    unsafe {
        let path = std::ffi::CString::new(dylib.to_string_lossy().as_ref()).unwrap();
        let lib = dlopen(path.as_ptr(), RTLD_NOW);
        if lib.is_null() {
            return false;
        }
        let sym = std::ffi::CString::new("ghost_show_settings").unwrap();
        let func = dlsym(lib, sym.as_ptr());
        if func.is_null() {
            return false;
        }
        let show: extern "C" fn() = std::mem::transmute(func);
        show();
        true
    }
}

static DYLIB_PATH: std::sync::OnceLock<PathBuf> = std::sync::OnceLock::new();

fn find_dylib() -> Option<PathBuf> {
    if let Some(p) = DYLIB_PATH.get() {
        return Some(p.clone());
    }
    let exe = std::env::current_exe().ok()?;
    let dylib = exe
        .parent()?
        .parent()?
        .join("Frameworks")
        .join("libghost_settings.dylib");
    if dylib.exists() {
        let _ = DYLIB_PATH.set(dylib.clone());
        Some(dylib)
    } else {
        None
    }
}

pub fn setup_menu() {
    let dylib = match find_dylib() {
        Some(d) => d,
        None => return,
    };

    unsafe {
        let path = std::ffi::CString::new(dylib.to_string_lossy().as_ref()).unwrap();
        let lib = dlopen(path.as_ptr(), RTLD_NOW);
        if lib.is_null() {
            return;
        }
        let sym = std::ffi::CString::new("ghost_setup_menu").unwrap();
        let func = dlsym(lib, sym.as_ptr());
        if func.is_null() {
            return;
        }
        let setup: extern "C" fn() = std::mem::transmute(func);
        setup();
    }
}

pub fn configure_main_window() {
    let dylib = match find_dylib() {
        Some(d) => d,
        None => return,
    };

    unsafe {
        let path = std::ffi::CString::new(dylib.to_string_lossy().as_ref()).unwrap();
        let lib = dlopen(path.as_ptr(), RTLD_NOW);
        if lib.is_null() { return; }
        let sym = std::ffi::CString::new("ghost_configure_main_window").unwrap();
        let func = dlsym(lib, sym.as_ptr());
        if func.is_null() { return; }
        let configure: extern "C" fn() = std::mem::transmute(func);
        configure();
    }
}

/// Returns menu action: 0=none, 1=new tab, 2=close tab, 3=clear, 4=toggle help
pub fn consume_menu_action() -> i32 {
    let dylib = match find_dylib() {
        Some(d) => d,
        None => return 0,
    };

    unsafe {
        let path = std::ffi::CString::new(dylib.to_string_lossy().as_ref()).unwrap();
        let lib = dlopen(path.as_ptr(), RTLD_NOW);
        if lib.is_null() {
            return 0;
        }
        let sym = std::ffi::CString::new("ghost_consume_menu_action").unwrap();
        let func = dlsym(lib, sym.as_ptr());
        if func.is_null() {
            return 0;
        }
        let poll: extern "C" fn() -> i32 = std::mem::transmute(func);
        poll()
    }
}
