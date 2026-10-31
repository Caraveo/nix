mod app;
mod builtins;
mod completion;
mod editor;
mod executor;
mod fileops;
mod gui;
mod network;
mod parser;
mod pty;
mod safety;
mod settings;
mod sysutils;
mod textproc;

use app::{App, LineKind};
use executor::Executor;
use std::time::Instant;

pub const BUILTINS: &[&str] = &[
    "cd", "echo", "pwd", "exit", "quit", "history", "export", "jobs", "clear", "help", "hello",
    "seek", "curl", "ftp", "ssh", "wget",
    "grep", "rg", "jq", "wc", "sort", "uniq", "head", "tail", "cut", "tr", "rev", "sed", "tee",
    "printf", "seq", "yes", "cat", "diff",
    "calc", "uuid", "base64", "md5", "sha256", "date", "find", "tree", "touch",
    "file", "stat", "which", "whoami", "hostname", "uname",
    "ls", "cp", "mv", "rm", "mkdir", "rmdir", "ln", "chmod", "chown", "chgrp", "umask",
    "readlink", "basename", "dirname", "realpath",
    "nl", "tac", "expand", "unexpand", "paste", "comm", "shuf", "fold", "mktemp",
    "du", "df",
    "tar", "gzip", "gunzip", "zip", "unzip",
    "launch", "term", "name",
    "list", "copy", "move", "remove", "delete", "del",
    "makedir", "newdir", "createdir", "removedir",
    "link", "findfile", "treeview", "touchfile",
    "print", "where", "rename", "bye", "spill", "edit",
];

struct GhostApp {
    app: App,
    executor: Executor,
    git_check_timer: Instant,
    settings_watcher: settings::SettingsWatcher,
}

impl eframe::App for GhostApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // Ctrl+T opens new tab
        if ctx.input(|i| i.key_pressed(egui::Key::T) && i.modifiers.ctrl) {
            self.app.new_shell_tab();
        }

        // Poll all PTY sessions across tabs
        let finished = self.app.poll_all_ptys();
        if !finished.is_empty() {
            self.app.input_focused = true;
        }

        // Request repaint if any tab has a running PTY
        let any_pty = self.app.tabs.iter().any(|t| t.pty.is_some());
        if any_pty {
            ctx.request_repaint();
        }

        gui::setup(ctx, self.app.theme);
        gui::render(ctx, &mut self.app);

        // Check for native settings changes
        self.settings_watcher.check_and_apply(&mut self.app);

        // Poll native menu actions: 1=new tab, 2=close tab, 3=clear, 4=toggle help
        match settings::consume_menu_action() {
            1 => self.app.new_shell_tab(),
            2 => {
                if self.app.tabs.len() > 1 {
                    self.app.close_tab(self.app.active_tab);
                }
            }
            3 => self.app.clear_results(),
            4 => self.app.show_help = !self.app.show_help,
            _ => {}
        }

        // Process pending command
        if let Some(cmd) = self.app.pending_execution.take() {
            self.run(&cmd);
            self.app.input_focused = true;
        }

        // Reap background jobs
        let finished = self.executor.reap_background_jobs();
        for (job_num, cmd, code) in finished {
            self.app.add_output(&format!("[{}] {} finished (exit: {})", job_num, cmd, code), LineKind::Info);
            self.app.background_jobs.retain(|(n, _)| *n != job_num);
        }

        // Sync background job count
        self.app.background_jobs = self.executor.background_jobs.iter().map(|(n, _)| {
            (*n, self.executor.background_cmds.get(*n as usize - 1).cloned().unwrap_or_default())
        }).collect();

        // Sync executor env back to app
        for (k, v) in &self.executor.env {
            self.app.env.insert(k.clone(), v.clone());
        }

        // Update git status every 3 seconds
        if self.git_check_timer.elapsed().as_secs() >= 3 {
            self.app.update_git_status();
            self.git_check_timer = Instant::now();
        }

        self.app.update_suggestion(BUILTINS);

        if self.app.should_quit {
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
        }
        if !self.executor.background_jobs.is_empty() {
            ctx.request_repaint();
        }

        // Clear old status messages after a few seconds
        if !self.app.status_message.is_empty() && self.git_check_timer.elapsed().as_secs() >= 5 {
            self.app.status_message.clear();
        }
    }
}

impl GhostApp {
    fn run(&mut self, input: &str) {
        let trimmed = input.trim();
        self.app.history.push(input.to_string());
        self.app.set_scroll(true);

        // Silent commands — no output
        if app::is_silent_command(trimmed) {
            self.run_silent(trimmed);
            self.app.update_git_status();
            return;
        }

        // Real command — add prompt + output to results
        let prompt = self.app.prompt();
        self.app.add_prompt(&prompt, input);

        let program = match parser::parse(input) {
            Ok(p) => p,
            Err(e) => {
                self.app.add_output(&format!("parse error: {}", e), LineKind::Error);
                self.app.last_status = 1;
                return;
            }
        };

        let result = self.executor.execute(&program, &|name, args, exec| {
            builtins::handle_builtin(name, args, exec)
        });

        if result.status == 999 {
            self.app.clear_results();
            return;
        }

        if result.status == 1000 {
            if let Some(pty) = self.executor.pending_pty.take() {
                let cmd = pty.command.clone();
                self.app.new_pty_tab(cmd, pty);
            }
            return;
        }

        if result.status == 1001 {
            if let Some((path, content)) = self.executor.pending_editor.take() {
                self.app.editor = Some(crate::editor::EditorState::new(path, content));
                self.app.show_editor = true;
            }
            return;
        }

        if !result.stdout.is_empty() {
            self.app.add_output(result.stdout.trim_end(), LineKind::Stdout);
        }
        if !result.stderr.is_empty() {
            self.app.add_output(result.stderr.trim_end(), LineKind::Stderr);
        }
        if result.status != 0 && result.stdout.is_empty() && result.stderr.is_empty() {
            self.app.add_output(&format!("Process exited with code {}", result.status), LineKind::Warning);
        }

        self.app.last_status = result.status;
        self.app.env.insert("?".into(), result.status.to_string());
        self.app.cwd = std::env::current_dir().unwrap_or_else(|_| self.app.cwd.clone());
        self.app.update_git_status();
    }

    fn run_silent(&mut self, input: &str) {
        let trimmed = input.trim();
        let first = trimmed.split_whitespace().next().unwrap_or("");

        match first {
            "cd" => {
                let program = match parser::parse(input) {
                    Ok(p) => p,
                    Err(_) => return,
                };
                let result = self.executor.execute(&program, &|name, args, exec| {
                    builtins::handle_builtin(name, args, exec)
                });
                self.app.last_status = result.status;
                self.app.env.insert("?".into(), result.status.to_string());
                self.app.cwd = std::env::current_dir().unwrap_or_else(|_| self.app.cwd.clone());
                if !result.stderr.is_empty() {
                    self.app.status_message = result.stderr.trim().to_string();
                } else {
                    self.app.status_message = format!("cd -> {}", self.app.display_cwd());
                }
            }
            "pwd" => {
                self.app.status_message = format!("pwd: {}", self.app.display_cwd());
                self.app.last_status = 0;
            }
            "export" => {
                let program = match parser::parse(input) { Ok(p) => p, Err(_) => return };
                self.executor.execute(&program, &|name, args, exec| {
                    builtins::handle_builtin(name, args, exec)
                });
                self.app.status_message = "export OK".into();
            }
            "clear" => {
                self.app.clear_results();
                self.app.status_message = "Cleared.".into();
            }
            "jobs" => {
                let n = self.executor.background_jobs.len();
                self.app.status_message = format!("{} background job(s)", n);
            }
            "history" => {
                self.app.status_message = format!("{} commands in history", self.app.history.len());
            }
            "exit" | "quit" => { self.app.should_quit = true; }
            "x" => {
                if self.app.tabs.len() > 1 {
                    self.app.close_tab(self.app.active_tab);
                } else {
                    self.app.add_output("I'm sorry, I am afraid I can't do that.", LineKind::Info);
                }
            }
            "help" => { self.app.show_help = true; }
            "term" => {
                let program = match parser::parse(input) { Ok(p) => p, Err(_) => return };
                self.executor.execute(&program, &|name, args, exec| {
                    builtins::handle_builtin(name, args, exec)
                });
                if let Some(pty) = self.executor.pending_pty.take() {
                    let cmd = pty.command.clone();
                    self.app.new_pty_tab(cmd, pty);
                }
            }
            "edit" => {
                let program = match parser::parse(input) { Ok(p) => p, Err(_) => return };
                self.executor.execute(&program, &|name, args, exec| {
                    builtins::handle_builtin(name, args, exec)
                });
                if let Some((path, content)) = self.executor.pending_editor.take() {
                    self.app.editor = Some(crate::editor::EditorState::new(path, content));
                    self.app.show_editor = true;
                }
            }
            _ => {}
        }

        for (k, v) in &self.executor.env {
            self.app.env.insert(k.clone(), v.clone());
        }
    }
}

fn main() -> Result<(), eframe::Error> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("Ghost Shell v0.8.0")
            .with_title_shown(false)
            .with_titlebar_shown(false)
            .with_titlebar_buttons_shown(false)
            .with_fullsize_content_view(true)
            .with_inner_size([900.0, 600.0])
            .with_min_inner_size([500.0, 350.0])
            .with_icon(load_icon()),
        ..Default::default()
    };
    eframe::run_native("Ghost Shell", options, Box::new(|_cc| {
        setup_terminal_font(&_cc.egui_ctx);
        settings::setup_menu();
        Ok(Box::new(GhostApp {
            app: App::new(),
            executor: Executor::new(),
            git_check_timer: Instant::now(),
            settings_watcher: settings::SettingsWatcher::new(),
        }))
    }))
}

fn setup_terminal_font(ctx: &egui::Context) {
    let mut fonts = egui::FontDefinitions::default();
    fonts.font_data.insert(
        "SF Mono".to_owned(),
        egui::FontData::from_static(include_bytes!("/System/Library/Fonts/SFNSMono.ttf")),
    );
    fonts.font_data.insert(
        "Apple Braille".to_owned(),
        egui::FontData::from_static(include_bytes!("/System/Library/Fonts/Apple Braille.ttf")),
    );
    let monospace = fonts
        .families
        .get_mut(&egui::FontFamily::Monospace)
        .expect("default monospace family must exist");
    monospace.insert(0, "Apple Braille".to_owned());
    monospace.insert(0, "SF Mono".to_owned());
    ctx.set_fonts(fonts);
}

fn load_icon() -> egui::IconData {
    eframe::icon_data::from_png_bytes(include_bytes!(
        "../ghost Exports/ghost-macOS-Dock-1024x1024.png"
    ))
    .expect("embedded Ghost app icon must be a valid PNG")
}
