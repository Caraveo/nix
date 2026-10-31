use std::collections::HashMap;
use std::path::PathBuf;

#[derive(Clone)]
pub struct OutputLine {
    pub text: String,
    pub kind: LineKind,
}

#[derive(Clone, PartialEq)]
pub enum LineKind {
    Normal,
    Prompt,
    Stdout,
    Stderr,
    Info,
    Warning,
    Error,
}

const SILENT_BUILTINS: &[&str] = &[
    "cd", "pwd", "export", "clear", "jobs", "history", "exit", "quit", "help", "term", "x", "edit",
];

pub fn is_silent_command(input: &str) -> bool {
    let trimmed = input.trim();
    if trimmed.is_empty() { return true; }
    if trimmed.contains('|') || trimmed.contains('>') || trimmed.contains('<') { return false; }
    if trimmed.contains("&&") || trimmed.contains("||") { return false; }
    let first = trimmed.split_whitespace().next().unwrap_or("");
    SILENT_BUILTINS.contains(&first)
}

#[derive(Clone, Copy, PartialEq)]
pub enum Theme {
    DarkCyan,
    Matrix,
    Solarized,
    Gruvbox,
    Light,
}

impl Theme {
    pub fn name(&self) -> &'static str {
        match self {
            Theme::DarkCyan => "Dark Cyan",
            Theme::Matrix => "Matrix",
            Theme::Solarized => "Solarized",
            Theme::Gruvbox => "Gruvbox",
            Theme::Light => "Light",
        }
    }

    pub fn all() -> &'static [Theme] {
        &[Theme::DarkCyan, Theme::Matrix, Theme::Solarized, Theme::Gruvbox, Theme::Light]
    }

    pub fn next(self) -> Self {
        let all = Self::all();
        let idx = all.iter().position(|t| *t == self).unwrap_or(0);
        all[(idx + 1) % all.len()].clone()
    }
}

pub struct ThemeColors {
    pub bg: egui::Color32,
    pub panel: egui::Color32,
    pub border: egui::Color32,
    pub cyan: egui::Color32,
    pub green: egui::Color32,
    pub red: egui::Color32,
    pub yellow: egui::Color32,
    pub white: egui::Color32,
    pub gray: egui::Color32,
    pub dark: egui::Color32,
    pub active: egui::Color32,
}

impl ThemeColors {
    pub fn from(theme: Theme) -> Self {
        match theme {
            Theme::DarkCyan => ThemeColors {
                bg: egui::Color32::from_rgba_premultiplied(14, 14, 22, 210),
                panel: egui::Color32::from_rgba_premultiplied(24, 24, 38, 180),
                border: egui::Color32::from_rgba_premultiplied(60, 60, 90, 80),
                cyan: egui::Color32::from_rgb(0, 210, 230),
                green: egui::Color32::from_rgb(90, 230, 110),
                red: egui::Color32::from_rgb(250, 90, 90),
                yellow: egui::Color32::from_rgb(250, 210, 90),
                white: egui::Color32::from_rgb(225, 225, 240),
                gray: egui::Color32::from_rgb(110, 110, 135),
                dark: egui::Color32::from_rgba_premultiplied(50, 50, 75, 160),
                active: egui::Color32::from_rgba_premultiplied(0, 80, 100, 180),
            },
            Theme::Matrix => ThemeColors {
                bg: egui::Color32::from_rgba_premultiplied(0, 8, 0, 210),
                panel: egui::Color32::from_rgba_premultiplied(0, 20, 0, 180),
                border: egui::Color32::from_rgba_premultiplied(0, 80, 0, 70),
                cyan: egui::Color32::from_rgb(0, 255, 120),
                green: egui::Color32::from_rgb(0, 255, 0),
                red: egui::Color32::from_rgb(255, 90, 90),
                yellow: egui::Color32::from_rgb(190, 255, 90),
                white: egui::Color32::from_rgb(0, 255, 0),
                gray: egui::Color32::from_rgb(0, 130, 0),
                dark: egui::Color32::from_rgba_premultiplied(0, 50, 0, 160),
                active: egui::Color32::from_rgba_premultiplied(0, 100, 0, 180),
            },
            Theme::Solarized => ThemeColors {
                bg: egui::Color32::from_rgba_premultiplied(0, 43, 54, 215),
                panel: egui::Color32::from_rgba_premultiplied(7, 54, 66, 185),
                border: egui::Color32::from_rgba_premultiplied(48, 150, 150, 80),
                cyan: egui::Color32::from_rgb(52, 207, 210),
                green: egui::Color32::from_rgb(143, 163, 10),
                red: egui::Color32::from_rgb(230, 60, 57),
                yellow: egui::Color32::from_rgb(191, 147, 10),
                white: egui::Color32::from_rgb(240, 240, 225),
                gray: egui::Color32::from_rgb(98, 120, 127),
                dark: egui::Color32::from_rgba_premultiplied(54, 77, 82, 160),
                active: egui::Color32::from_rgba_premultiplied(48, 150, 150, 180),
            },
            Theme::Gruvbox => ThemeColors {
                bg: egui::Color32::from_rgba_premultiplied(28, 28, 28, 210),
                panel: egui::Color32::from_rgba_premultiplied(40, 40, 40, 180),
                border: egui::Color32::from_rgba_premultiplied(80, 74, 68, 80),
                cyan: egui::Color32::from_rgb(152, 202, 134),
                green: egui::Color32::from_rgb(162, 161, 36),
                red: egui::Color32::from_rgb(214, 46, 39),
                yellow: egui::Color32::from_rgb(250, 189, 47),
                white: egui::Color32::from_rgb(240, 224, 183),
                gray: egui::Color32::from_rgb(156, 141, 126),
                dark: egui::Color32::from_rgba_premultiplied(80, 73, 69, 160),
                active: egui::Color32::from_rgba_premultiplied(162, 161, 36, 180),
            },
            Theme::Light => ThemeColors {
                bg: egui::Color32::from_rgba_premultiplied(250, 250, 252, 220),
                panel: egui::Color32::from_rgba_premultiplied(240, 240, 248, 190),
                border: egui::Color32::from_rgba_premultiplied(200, 200, 220, 100),
                cyan: egui::Color32::from_rgb(0, 140, 190),
                green: egui::Color32::from_rgb(10, 170, 70),
                red: egui::Color32::from_rgb(210, 50, 50),
                yellow: egui::Color32::from_rgb(190, 150, 10),
                white: egui::Color32::from_rgb(30, 30, 45),
                gray: egui::Color32::from_rgb(130, 130, 145),
                dark: egui::Color32::from_rgba_premultiplied(190, 190, 200, 160),
                active: egui::Color32::from_rgba_premultiplied(210, 235, 250, 200),
            },
        }
    }
}

pub struct Tab {
    pub name: String,
    pub results: Vec<OutputLine>,
    pub pty: Option<crate::pty::PtySession>,
    pub scroll_to_bottom: bool,
}

pub struct App {
    pub input: String,
    pub tabs: Vec<Tab>,
    pub active_tab: usize,
    pub history: Vec<String>,
    pub env: HashMap<String, String>,
    pub last_status: i32,
    pub background_jobs: Vec<(u32, String)>,
    pub should_quit: bool,
    pub cwd: PathBuf,
    pub suggestion: Option<String>,
    pub completion_list: Vec<String>,
    pub show_completions: bool,
    pub confirm_mode: bool,
    pub pending_command: String,
    pub pending_reason: String,
    pub pending_changes: Vec<String>,
    pub pending_execution: Option<String>,
    pub show_help: bool,
    pub selected_history: Option<usize>,
    pub input_focused: bool,
    pub status_message: String,
    pub theme: Theme,
    pub git_branch: String,
    pub git_dirty: bool,
    pub clipboard_text: String,
    pub show_settings: bool,
    pub editor: Option<crate::editor::EditorState>,
    pub show_editor: bool,
    pub font_size: f32,
    pub pty_cols: u16,
    pub pty_rows: u16,
    pub auto_switch_pty: bool,
    pub show_startup_msg: bool,
    pub safety_enabled: bool,
    pub pty_rect: egui::Rect,
    pub pty_cell_w: f32,
    pub pty_cell_h: f32,
}

impl App {
    pub fn new() -> Self {
        let mut env = HashMap::new();
        for (k, v) in std::env::vars() { env.insert(k, v); }
        env.insert("?".into(), "0".into());
        let cwd = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("/"));

        let mut results = Vec::new();
        results.push(OutputLine { text: "Ghost Shell v0.8.0".into(), kind: LineKind::Info });
        results.push(OutputLine { text: "Type 'help' for keybindings, 'hello' for features.".into(), kind: LineKind::Info });
        results.push(OutputLine { text: "Drag & drop files into the input to insert paths.".into(), kind: LineKind::Info });
        results.push(OutputLine { text: String::new(), kind: LineKind::Normal });

        App {
            input: String::new(),
            tabs: vec![Tab { name: "shell".into(), results, pty: None, scroll_to_bottom: true }],
            active_tab: 0,
            history: Vec::new(),
            env,
            last_status: 0,
            background_jobs: Vec::new(),
            should_quit: false,
            cwd,
            suggestion: None,
            completion_list: Vec::new(),
            show_completions: false,
            confirm_mode: false,
            pending_command: String::new(),
            pending_reason: String::new(),
            pending_changes: Vec::new(),
            pending_execution: None,
            show_help: false,
            selected_history: None,
            input_focused: true,
            status_message: String::new(),
            theme: Theme::Light,
            git_branch: String::new(),
            git_dirty: false,
            clipboard_text: String::new(),
            show_settings: false,
            editor: None,
            show_editor: false,
            font_size: 13.0,
            pty_cols: 120,
            pty_rows: 40,
            auto_switch_pty: true,
            show_startup_msg: true,
            safety_enabled: true,
            pty_rect: egui::Rect::NOTHING,
            pty_cell_w: 8.0,
            pty_cell_h: 16.0,
        }
    }

    pub fn colors(&self) -> ThemeColors { ThemeColors::from(self.theme) }

    pub fn results(&self) -> &[OutputLine] { &self.tabs[self.active_tab].results }
    pub fn pty(&self) -> &Option<crate::pty::PtySession> { &self.tabs[self.active_tab].pty }
    pub fn pty_mut(&mut self) -> &mut Option<crate::pty::PtySession> { &mut self.tabs[self.active_tab].pty }
    pub fn scroll_to_bottom(&self) -> bool { self.tabs[self.active_tab].scroll_to_bottom }
    pub fn set_scroll(&mut self, v: bool) { self.tabs[self.active_tab].scroll_to_bottom = v; }

    pub fn add_output(&mut self, text: &str, kind: LineKind) {
        for line in text.lines() {
            self.tabs[self.active_tab].results.push(OutputLine { text: line.to_string(), kind: kind.clone() });
        }
        if text.is_empty() {
            self.tabs[self.active_tab].results.push(OutputLine { text: String::new(), kind: LineKind::Normal });
        }
        self.tabs[self.active_tab].scroll_to_bottom = true;
    }

    pub fn add_prompt(&mut self, prompt: &str, command: &str) {
        self.tabs[self.active_tab].results.push(OutputLine { text: format!("{} {}", prompt, command), kind: LineKind::Prompt });
        self.tabs[self.active_tab].scroll_to_bottom = true;
    }

    pub fn clear_results(&mut self) {
        self.tabs[self.active_tab].results.clear();
        self.tabs[self.active_tab].scroll_to_bottom = true;
    }

    pub fn reset_input(&mut self) {
        self.input.clear();
        self.suggestion = None;
        self.completion_list.clear();
        self.show_completions = false;
    }

    pub fn display_cwd(&self) -> String {
        let s = self.cwd.display().to_string();
        if let Some(home) = self.env.get("HOME") {
            if s.starts_with(home) { return format!("~{}", &s[home.len()..]); }
        }
        if let Some(home) = self.env.get("USERPROFILE") {
            if s.starts_with(home) { return format!("~{}", &s[home.len()..]); }
        }
        s
    }

    pub fn prompt(&self) -> String {
        let user = self.env.get("USER").or_else(|| self.env.get("USERNAME")).map(|s| s.as_str()).unwrap_or("user");
        format!("{} {} $", user, self.display_cwd())
    }

    pub fn update_suggestion(&mut self, builtins: &[&str]) {
        if self.input.is_empty() { self.suggestion = None; return; }
        let first = self.input.split_whitespace().next().unwrap_or("");
        let comps = crate::completion::get_completions(first, builtins);
        for c in &comps {
            if c.len() > first.len() && c.starts_with(first) { self.suggestion = Some(c.clone()); return; }
        }
        for h in self.history.iter().rev() {
            if h.starts_with(&self.input) && h.len() > self.input.len() { self.suggestion = Some(h.clone()); return; }
        }
        self.suggestion = None;
    }

    pub fn history_prev(&mut self) {
        if self.history.is_empty() { return; }
        match self.selected_history {
            None => { self.selected_history = Some(self.history.len() - 1); }
            Some(i) => { if i > 0 { self.selected_history = Some(i - 1); } }
        }
        if let Some(i) = self.selected_history { self.input = self.history[i].clone(); self.suggestion = None; }
    }

    pub fn history_next(&mut self) {
        match self.selected_history {
            Some(i) => {
                if i + 1 < self.history.len() { self.selected_history = Some(i + 1); self.input = self.history[i + 1].clone(); }
                else { self.selected_history = None; self.input.clear(); }
            }
            None => {}
        }
        self.suggestion = None;
    }

    pub fn export_results(&self) -> String {
        self.results().iter().map(|l| l.text.clone()).collect::<Vec<_>>().join("\n")
    }

    pub fn switch_tab(&mut self, i: usize) {
        if i < self.tabs.len() { self.active_tab = i; self.input_focused = true; }
    }

    pub fn new_shell_tab(&mut self) {
        self.tabs.push(Tab {
            name: "shell".into(),
            results: vec![],
            pty: None,
            scroll_to_bottom: true,
        });
        self.active_tab = self.tabs.len() - 1;
        self.input.clear();
        self.input_focused = true;
    }

    pub fn new_pty_tab(&mut self, name: String, pty: crate::pty::PtySession) {
        self.tabs.push(Tab {
            name,
            results: vec![],
            pty: Some(pty),
            scroll_to_bottom: true,
        });
        self.active_tab = self.tabs.len() - 1;
    }

    pub fn close_tab(&mut self, i: usize) {
        if self.tabs.len() <= 1 { return; }
        if i >= self.tabs.len() { return; }
        self.tabs.remove(i);
        if self.active_tab >= self.tabs.len() {
            self.active_tab = self.tabs.len() - 1;
        } else if i < self.active_tab {
            self.active_tab -= 1;
        }
        self.input_focused = true;
    }

    pub fn poll_all_ptys(&mut self) -> Vec<usize> {
        let mut finished = Vec::new();
        for (i, tab) in self.tabs.iter_mut().enumerate() {
            if let Some(pty) = tab.pty.as_mut() {
                pty.poll();
                if !pty.alive {
                    let cmd = pty.command.clone();
                    let content = pty.parser.screen().contents();
                    let content = content.trim_end_matches('\n').to_string();
                    tab.pty = None;
                    tab.scroll_to_bottom = true;
                    if !content.is_empty() {
                        for line in content.lines() {
                            tab.results.push(OutputLine { text: line.to_string(), kind: LineKind::Stdout });
                        }
                    }
                    tab.results.push(OutputLine { text: format!("[{} exited]", cmd), kind: LineKind::Info });
                    finished.push(i);
                }
            }
        }
        finished
    }

    pub fn update_git_status(&mut self) {
        let cwd = std::env::current_dir().unwrap_or_default();
        let git_dir = cwd.join(".git");
        if !git_dir.exists() {
            self.git_branch.clear();
            self.git_dirty = false;
            return;
        }
        let branch = std::process::Command::new("git")
            .args(["rev-parse", "--abbrev-ref", "HEAD"])
            .current_dir(&cwd)
            .output();
        if let Ok(out) = branch {
            if out.status.success() {
                self.git_branch = String::from_utf8_lossy(&out.stdout).trim().to_string();
            }
        }
        let dirty = std::process::Command::new("git")
            .args(["status", "--porcelain"])
            .current_dir(&cwd)
            .output();
        if let Ok(out) = dirty {
            self.git_dirty = !out.stdout.is_empty();
        }
    }
}
