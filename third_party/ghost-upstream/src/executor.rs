use std::collections::HashMap;
use std::io::Read;
use std::process::{Child, Command as ProcessCommand, Stdio};

use crate::parser::{expand_env, ChainOp, ParsedCommand, Pipeline, Program};

fn get_login_shell_path() -> String {
    let shell = std::env::var("SHELL").unwrap_or_else(|_| "/bin/zsh".into());
    let result = ProcessCommand::new(&shell)
        .arg("-l")
        .arg("-i")
        .arg("-c")
        .arg("echo $PATH")
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .output();

    let login_path = match result {
        Ok(out) => {
            let p = String::from_utf8_lossy(&out.stdout).trim().to_string();
            if p.is_empty() { String::new() } else { p }
        }
        Err(_) => String::new(),
    };

    let extra_dirs = [
        "/usr/local/bin",
        "/usr/local/sbin",
        "/opt/homebrew/bin",
        "/opt/homebrew/sbin",
        "/usr/bin",
        "/bin",
        "/usr/sbin",
        "/sbin",
        "/opt/X11/bin",
        "/Library/Apple/usr/bin",
    ];

    let home = std::env::var("HOME").unwrap_or_default();
    let home_dirs = if !home.is_empty() {
        vec![
            format!("{}/.local/bin", home),
            format!("{}/.cargo/bin", home),
            format!("{}/.nvm/current/bin", home),
            format!("{}/.bun/bin", home),
            format!("{}/.deno/bin", home),
            format!("{}/.go/bin", home),
            format!("{}/bin", home),
        ]
    } else {
        vec![]
    };

    let mut all_dirs: Vec<String> = Vec::new();
    
    for d in login_path.split(':') {
        let d = d.to_string();
        if !d.is_empty() && !all_dirs.contains(&d) {
            all_dirs.push(d);
        }
    }
    for d in &extra_dirs {
        let d = d.to_string();
        if !all_dirs.contains(&d) {
            all_dirs.push(d);
        }
    }
    for d in &home_dirs {
        if !all_dirs.contains(d) {
            all_dirs.push(d.clone());
        }
    }

    all_dirs.join(":")
}

const ALL_BUILTINS: &[&str] = &[
    "cd", "echo", "pwd", "exit", "quit", "history", "export", "jobs", "clear", "help", "hello",
    "seek", "curl", "ftp", "ssh", "wget", "grep", "rg", "jq", "wc", "sort", "uniq", "head",
    "tail", "cut", "tr", "rev", "sed", "tee", "printf", "seq", "yes", "cat", "diff", "calc",
    "uuid", "base64", "md5", "sha256", "date", "find", "tree", "touch", "file", "stat",
    "which", "whoami", "hostname", "uname",
    "launch", "term", "name",
    "list", "copy", "move", "remove", "delete", "del",
    "makedir", "newdir", "createdir", "removedir",
    "link", "findfile", "treeview", "touchfile",
    "print", "where", "rename", "bye", "spill", "edit",
];

fn levenshtein(a: &str, b: &str) -> usize {
    let a: Vec<char> = a.chars().collect();
    let b: Vec<char> = b.chars().collect();
    let mut dp = vec![vec![0; b.len() + 1]; a.len() + 1];
    for i in 0..=a.len() { dp[i][0] = i; }
    for j in 0..=b.len() { dp[0][j] = j; }
    for i in 1..=a.len() {
        for j in 1..=b.len() {
            let cost = if a[i-1] == b[j-1] { 0 } else { 1 };
            dp[i][j] = (dp[i-1][j] + 1).min(dp[i][j-1] + 1).min(dp[i-1][j-1] + cost);
        }
    }
    dp[a.len()][b.len()]
}

fn format_cmd_not_found(program: &str) -> String {
    // Find similar builtins within edit distance 2
    let mut suggestions: Vec<&str> = ALL_BUILTINS.iter()
        .filter(|b| levenshtein(&program.to_lowercase(), &b.to_lowercase()) <= 2)
        .copied()
        .collect();
    suggestions.sort_by_key(|b| levenshtein(&program.to_lowercase(), &b.to_lowercase()));

    let mut msg = format!("command not found: {}\n", program);
    if !suggestions.is_empty() {
        msg.push_str("did you mean: ");
        for (i, s) in suggestions.iter().take(3).enumerate() {
            if i > 0 { msg.push_str(", "); }
            msg.push_str(s);
        }
        msg.push_str("?\n");
    }
    msg
}

pub struct CommandResult {
    pub status: i32,
    pub stdout: String,
    pub stderr: String,
}

impl CommandResult {
    pub fn new() -> Self {
        CommandResult {
            status: 0,
            stdout: String::new(),
            stderr: String::new(),
        }
    }

    pub fn ok(stdout: &str) -> Self {
        CommandResult {
            status: 0,
            stdout: stdout.to_string(),
            stderr: String::new(),
        }
    }

    pub fn err(stderr: &str) -> Self {
        CommandResult {
            status: 1,
            stdout: String::new(),
            stderr: stderr.to_string(),
        }
    }

    pub fn code(status: i32, stdout: &str, stderr: &str) -> Self {
        CommandResult {
            status,
            stdout: stdout.to_string(),
            stderr: stderr.to_string(),
        }
    }
}

pub struct Executor {
    pub env: HashMap<String, String>,
    pub last_status: i32,
    pub background_jobs: Vec<(u32, Child)>,
    pub background_cmds: Vec<String>,
    pub pending_pty: Option<crate::pty::PtySession>,
    pub pending_editor: Option<(String, String)>,
}

impl Executor {
    pub fn new() -> Self {
        let mut env = HashMap::new();
        for (k, v) in std::env::vars() {
            env.insert(k, v);
        }

        let login_path = get_login_shell_path();
        if !login_path.is_empty() {
            env.insert("PATH".into(), login_path);
        }

        env.insert("?".into(), "0".into());
        Executor {
            env,
            last_status: 0,
            background_jobs: Vec::new(),
            background_cmds: Vec::new(),
            pending_pty: None,
            pending_editor: None,
        }
    }

    pub fn execute(
        &mut self,
        program: &Program,
        builtin_handler: &dyn Fn(&str, &[String], &mut Executor) -> Option<CommandResult>,
    ) -> CommandResult {
        let mut skip_next = false;
        let mut result = CommandResult::new();

        for stmt in &program.statements {
            if skip_next {
                skip_next = false;
                continue;
            }

            result = self.run_pipeline(&stmt.pipeline, builtin_handler);

            match stmt.op {
                ChainOp::And => {
                    if result.status != 0 {
                        skip_next = true;
                    }
                }
                ChainOp::Or => {
                    if result.status == 0 {
                        skip_next = true;
                    }
                }
                _ => {}
            }
        }

        self.last_status = result.status;
        self.env.insert("?".into(), result.status.to_string());
        result
    }

    fn run_pipeline(
        &mut self,
        pipeline: &Pipeline,
        builtin_handler: &dyn Fn(&str, &[String], &mut Executor) -> Option<CommandResult>,
    ) -> CommandResult {
        let commands = &pipeline.commands;

        if commands.is_empty() {
            return CommandResult::new();
        }

        if commands.len() == 1 {
            return self.run_single_command(&commands[0], builtin_handler, pipeline.background);
        }

        self.run_piped_commands(commands, pipeline.background)
    }

    fn run_single_command(
        &mut self,
        cmd: &ParsedCommand,
        builtin_handler: &dyn Fn(&str, &[String], &mut Executor) -> Option<CommandResult>,
        background: bool,
    ) -> CommandResult {
        let program = expand_env(&cmd.program, &self.env);
        let args: Vec<String> = cmd
            .args
            .iter()
            .map(|a| expand_env(a, &self.env))
            .collect();

        if let Some(result) = builtin_handler(&program, &args, self) {
            self.last_status = result.status;
            self.env.insert("?".into(), result.status.to_string());
            return result;
        }

        // Use PTY for all external commands (not piped, not redirected, not background)
        if cmd.stdin_file.is_none() && cmd.stdout_file.is_none() && !background {
            return self.run_with_pty(&program, &args);
        }

        let stdin_file = self.open_stdin(cmd);
        let stdout_file = self.open_stdout(cmd);

        let mut command = ProcessCommand::new(&program);
        command.args(&args);
        command.env_clear();
        for (k, v) in &self.env {
            if k != "?" {
                command.env(k, v);
            }
        }

        if let Some(ref f) = stdin_file {
            command.stdin(Stdio::from(f.try_clone().unwrap()));
        } else {
            command.stdin(Stdio::null());
        }

        if let Some(ref f) = stdout_file {
            command.stdout(Stdio::from(f.try_clone().unwrap()));
            command.stderr(Stdio::inherit());
        } else if background {
            command.stdout(Stdio::piped());
            command.stderr(Stdio::piped());
        } else {
            command.stdout(Stdio::piped());
            command.stderr(Stdio::piped());
        }

        match command.spawn() {
            Ok(mut child) => {
                if background {
                    let job_num = self.background_jobs.len() as u32 + 1;
                    let pid = child.id();
                    let _ = child.stdout.take();
                    let _ = child.stderr.take();
                    self.background_jobs.push((job_num, child));
                    self.background_cmds.push(program.clone());
                    CommandResult::code(
                        0,
                        &format!("[{}] {} (pid: {})", job_num, program, pid),
                        "",
                    )
                } else {
                    let mut stdout_buf = Vec::new();
                    let mut stderr_buf = Vec::new();

                    let mut stdout = child.stdout.take();
                    let mut stderr = child.stderr.take();

                    let status = child.wait();

                    if let Some(ref mut s) = stdout {
                        let _ = s.read_to_end(&mut stdout_buf);
                    }
                    if let Some(ref mut s) = stderr {
                        let _ = s.read_to_end(&mut stderr_buf);
                    }

                    match status {
                        Ok(s) => {
                            let code = s.code().unwrap_or(1);
                            self.last_status = code;
                            self.env.insert("?".into(), code.to_string());
                            let stdout_str = String::from_utf8_lossy(&stdout_buf).to_string();
                            let stderr_str = String::from_utf8_lossy(&stderr_buf).to_string();
                            let mut stdout_str = stdout_str;
                            let mut stderr_str = stderr_str;
                            if !stdout_str.is_empty() && !stdout_str.ends_with('\n') {
                                stdout_str.push('\n');
                            }
                            if !stderr_str.is_empty() && !stderr_str.ends_with('\n') {
                                stderr_str.push('\n');
                            }
                            CommandResult::code(code, &stdout_str, &stderr_str)
                        }
                        Err(e) => {
                            CommandResult::code(127, "", &format!("ghost: failed to execute '{}': {}\n", program, e))
                        }
                    }
                }
            }
            Err(e) => {
                let mut msg = format_cmd_not_found(&program);
                if e.kind() != std::io::ErrorKind::NotFound {
                    msg.push_str(&format!("({})\n", e));
                }
                CommandResult::code(127, "", &msg)
            }
        }
    }

    fn run_with_pty(&mut self, program: &str, args: &[String]) -> CommandResult {
        let cwd = std::env::current_dir()
            .unwrap_or_default()
            .display()
            .to_string();

        match crate::pty::PtySession::new(program, args, &cwd, &self.env) {
            Ok(mut session) => {
                std::thread::sleep(std::time::Duration::from_millis(100));
                session.poll();

                if !session.alive {
                    let content = session.parser.screen().contents();
                    let content = content.trim_end_matches('\n').to_string();
                    if content.is_empty() {
                        return CommandResult::new();
                    }
                    return CommandResult::ok(&format!("{}\n", content));
                } else {
                    self.pending_pty = Some(session);
                    return CommandResult::code(1000, "", "");
                }
            }
            Err(e) => CommandResult::code(
                127,
                "",
                &format!("ghost: failed to execute '{}': {}\n", program, e),
            ),
        }
    }

    fn run_piped_commands(&mut self, commands: &[ParsedCommand], background: bool) -> CommandResult {
        let len = commands.len();
        let mut prev_stdout: Option<std::process::ChildStdout> = None;
        let mut children: Vec<Child> = Vec::new();

        for (i, cmd) in commands.iter().enumerate() {
            let program = expand_env(&cmd.program, &self.env);
            let args: Vec<String> = cmd
                .args
                .iter()
                .map(|a| expand_env(a, &self.env))
                .collect();

            let mut command = ProcessCommand::new(&program);
            command.args(&args);

            if i == 0 {
                if let Some(ref file) = cmd.stdin_file {
                    let path = expand_env(file, &self.env);
                    match std::fs::File::open(&path) {
                        Ok(f) => {
                            command.stdin(Stdio::from(f));
                        }
                        Err(e) => {
                            return CommandResult::err(&format!("{}: {}", path, e));
                        }
                    }
                } else {
                    command.stdin(Stdio::null());
                }
            } else if let Some(prev) = prev_stdout.take() {
                command.stdin(Stdio::from(prev));
            }

            if i < len - 1 {
                command.stdout(Stdio::piped());
            } else {
                if let Some(ref file) = cmd.stdout_file {
                    let path = expand_env(file, &self.env);
                    let file_result = if cmd.append {
                        std::fs::OpenOptions::new()
                            .write(true)
                            .create(true)
                            .append(true)
                            .open(&path)
                    } else {
                        std::fs::OpenOptions::new()
                            .write(true)
                            .create(true)
                            .truncate(true)
                            .open(&path)
                    };
                    match file_result {
                        Ok(f) => {
                            command.stdout(Stdio::from(f));
                        }
                        Err(e) => {
                            return CommandResult::err(&format!("{}: {}", path, e));
                        }
                    }
                } else {
                    command.stdout(Stdio::piped());
                }
            }
            command.stderr(Stdio::piped());

            match command.spawn() {
                Ok(mut child) => {
                    prev_stdout = child.stdout.take();
                    children.push(child);
                }
                Err(_) => {
                    return CommandResult::err(&format_cmd_not_found(&program));
                }
            }
        }

        if background {
            let last = children.pop();
            if let Some(child) = last {
                let job_num = self.background_jobs.len() as u32 + 1;
                let pid = child.id();
                self.background_jobs.push((job_num, child));
                self.background_cmds
                    .push(commands.last().unwrap().program.clone());
                return CommandResult::code(
                    0,
                    &format!("[{}] {} (pid: {})", job_num, commands.last().unwrap().program, pid),
                    "",
                );
            }
            return CommandResult::new();
        }

        let mut stdout_buf = Vec::new();
        let mut stderr_buf = Vec::new();

        let mut last_status = 0;
        for mut child in children {
            let mut err = child.stderr.take();
            let status = child.wait();
            if let Some(ref mut s) = err {
                let _ = s.read_to_end(&mut stderr_buf);
            }
            match status {
                Ok(s) => {
                    last_status = s.code().unwrap_or(1);
                }
                Err(_) => {
                    last_status = 1;
                }
            }
        }

        if let Some(prev) = prev_stdout {
            let mut s = prev;
            let _ = std::io::Read::read_to_end(&mut s, &mut stdout_buf);
        }

        let mut stdout_str = String::from_utf8_lossy(&stdout_buf).to_string();
        let mut stderr_str = String::from_utf8_lossy(&stderr_buf).to_string();
        if !stdout_str.is_empty() && !stdout_str.ends_with('\n') {
            stdout_str.push('\n');
        }
        if !stderr_str.is_empty() && !stderr_str.ends_with('\n') {
            stderr_str.push('\n');
        }

        self.last_status = last_status;
        self.env.insert("?".into(), last_status.to_string());
        CommandResult::code(last_status, &stdout_str, &stderr_str)
    }

    fn open_stdin(&self, cmd: &ParsedCommand) -> Option<std::fs::File> {
        cmd.stdin_file.as_ref().and_then(|path| {
            let expanded = expand_env(path, &self.env);
            std::fs::File::open(&expanded).ok()
        })
    }

    fn open_stdout(&self, cmd: &ParsedCommand) -> Option<std::fs::File> {
        cmd.stdout_file.as_ref().and_then(|path| {
            let expanded = expand_env(path, &self.env);
            let mut opts = std::fs::OpenOptions::new();
            opts.write(true).create(true);
            if cmd.append {
                opts.append(true);
            } else {
                opts.truncate(true);
            }
            opts.open(&expanded).ok()
        })
    }

    pub fn reap_background_jobs(&mut self) -> Vec<(u32, String, i32)> {
        let mut finished = Vec::new();
        let mut still_running = Vec::new();
        let mut cmds = Vec::new();

        for (i, (job_num, mut child)) in self.background_jobs.drain(..).enumerate() {
            match child.try_wait() {
                Ok(Some(status)) => {
                    let code = status.code().unwrap_or(0);
                    let cmd = self.background_cmds.get(i).cloned().unwrap_or_default();
                    finished.push((job_num, cmd, code));
                }
                Ok(None) => {
                    still_running.push((job_num, child));
                    cmds.push(self.background_cmds.get(i).cloned().unwrap_or_default());
                }
                Err(_) => {
                    still_running.push((job_num, child));
                    cmds.push(self.background_cmds.get(i).cloned().unwrap_or_default());
                }
            }
        }

        self.background_jobs = still_running;
        self.background_cmds = cmds;
        finished
    }
}
