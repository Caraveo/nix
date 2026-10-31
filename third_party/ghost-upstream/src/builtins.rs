use std::path::Path;
use std::process::Stdio;

use crate::executor::CommandResult;
use crate::executor::Executor;
use crate::parser::expand_env;

pub fn handle_builtin(name: &str, args: &[String], executor: &mut Executor) -> Option<CommandResult> {
    let resolved = match name {
        "list" => "ls",
        "copy" => "cp",
        "move" => "mv",
        "remove" | "delete" | "del" => "rm",
        "makedir" | "newdir" | "createdir" => "mkdir",
        "removedir" => "rmdir",
        "link" => "ln",
        "findfile" => "find",
        "treeview" => "tree",
        "touchfile" => "touch",
        "print" => "echo",
        "where" => "which",
        "rename" => "name",
        "quit" | "bye" => "exit",
        other => other,
    };
    match resolved {
        "cd" => Some(builtin_cd(args, executor)),
        "echo" => Some(builtin_echo(args, executor)),
        "pwd" => Some(builtin_pwd()),
        "exit" | "quit" => Some(builtin_exit(args)),
        "history" => Some(builtin_history(args, executor)),
        "export" => Some(builtin_export(args, executor)),
        "jobs" => Some(builtin_jobs(executor)),
        "hello" => Some(builtin_hello()),
        "clear" => Some(builtin_clear()),
        "help" => Some(builtin_help()),
        "launch" => Some(builtin_launch(args, executor)),
        "term" => Some(builtin_term(args, executor)),
        "name" => Some(builtin_name(args, executor)),
        "spill" => Some(builtin_spill(args, executor)),
        "edit" => Some(builtin_edit(args, executor)),
        _ => crate::network::handle_network_builtin(resolved, args)
            .or_else(|| crate::textproc::handle(resolved, args))
            .or_else(|| crate::sysutils::handle(resolved, args))
            .or_else(|| crate::fileops::handle(resolved, args)),
    }
}

fn builtin_hello() -> CommandResult {
    let msg = r#"
============================================================================
  GHOST SHELL v0.8.0
  A standalone GUI shell — not a terminal emulator, not a TUI.
  A real native desktop application built in Rust with egui.
============================================================================

  WHAT IS GHOST SHELL?

  Ghost Shell is a self-contained desktop application that lets you run
  system commands in a graphical interface. It does not run inside a
  terminal — it opens its own window with panels and visual controls.

  WHAT DOES IT DO?

  - Runs any command your system can run (ls, grep, cargo, git, grid, etc.)
  - Captures stdout and stderr into a single scrolling results area
  - Supports pipes (|), redirects (>, >>, <), chaining (&&, ||, ;),
    background jobs (&), and environment variable expansion ($VAR)
  - 80+ built-in commands — no external dependencies needed for common ops
  - Tab completion — scans your $PATH for executables as you type
  - Safety checks — destructive commands ask confirmation with a
    detailed list of every file that will be affected
  - 5 themes — DarkCyan, Matrix, Solarized, Gruvbox, Light
  - Clickable https:// links in output
  - Drag & drop files from Finder into the input box
  - Git status in the status bar (branch + dirty flag)
  - Command history with Up/Down navigation

  BUILT-IN COMMANDS (80+)

  Shell:     cd, echo, pwd, export, jobs, clear, help, hello, exit, history
  Network:   seek (SEEK search), curl, ftp, ssh, wget
  Text:      grep, jq, wc, sort, uniq, head, tail, cut, tr, rev, sed, tee,
             printf, seq, yes, cat, diff
  System:    calc, uuid, base64, md5, sha256, date, find, tree, touch, file,
             stat, which, whoami, hostname, uname
  Files:     ls, cp, mv, rm, mkdir, rmdir, ln, chmod, chown, chgrp, umask,
             readlink, basename, dirname, realpath, nl, tac, expand, unexpand,
             paste, comm, shuf, fold, mktemp, du, df
  Archives:  tar, gzip, gunzip, zip, unzip

  SILENT COMMANDS

  cd, pwd, export, clear, jobs, history, exit, help — these run silently
  (no output in results) and just update shell state or show a brief
  status message in the status bar.

  SUGGESTIONS — THINGS TO TRY

  1. ls -la                              List files with details
  2. ls | grep .rs                       Filter for Rust files
  3. echo hello && echo done             Chain commands
  4. seek rust web framework             Search the web via SEEK
  5. tar -czf out.tar.gz src/            Create a gzip archive
  6. calc 2+2*8                          Evaluate a math expression
  7. edit Cargo.toml                     Open built-in editor (creates if missing)
  8. rm -rf testdir                      (triggers safety dialog)
  9. help                                Show keybindings
  10. echo $HOME                         Environment variable expansion

============================================================================
  Type any command below and press Enter to begin.
============================================================================
"#;
    CommandResult::ok(msg)
}

fn builtin_cd(args: &[String], executor: &Executor) -> CommandResult {
    let target = if args.is_empty() {
        executor
            .env
            .get("HOME")
            .or_else(|| executor.env.get("USERPROFILE"))
            .cloned()
            .unwrap_or_else(|| "/".to_string())
    } else {
        expand_env(&args[0], &executor.env)
    };

    let path = Path::new(&target);
    let resolved = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir().unwrap_or_default().join(path)
    };

    match std::env::set_current_dir(&resolved) {
        Ok(_) => CommandResult::new(),
        Err(e) => CommandResult::err(&format!("cd: {}: {}", target, e)),
    }
}

fn builtin_echo(args: &[String], executor: &Executor) -> CommandResult {
    let mut output_parts = Vec::new();
    let mut i = 0;
    let mut interpret_escapes = false;
    let mut no_newline = false;

    while i < args.len() {
        match args[i].as_str() {
            "-n" => {
                no_newline = true;
                i += 1;
            }
            "-e" => {
                interpret_escapes = true;
                i += 1;
            }
            "-E" => {
                interpret_escapes = false;
                i += 1;
            }
            _ => break,
        }
    }

    for arg in &args[i..] {
        let expanded = expand_env(arg, &executor.env);
        if interpret_escapes {
            output_parts.push(interpret_escape_sequences(&expanded));
        } else {
            output_parts.push(expanded);
        }
    }

    let joined = output_parts.join(" ");
    let output = if no_newline { joined } else { format!("{}\n", joined) };
    CommandResult::ok(&output)
}

fn interpret_escape_sequences(s: &str) -> String {
    let mut result = String::new();
    let mut chars = s.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\\' {
            match chars.next() {
                Some('n') => result.push('\n'),
                Some('t') => result.push('\t'),
                Some('r') => result.push('\r'),
                Some('\\') => result.push('\\'),
                Some('0') => result.push('\0'),
                Some(c) => {
                    result.push('\\');
                    result.push(c);
                }
                None => result.push('\\'),
            }
        } else {
            result.push(c);
        }
    }
    result
}

fn builtin_pwd() -> CommandResult {
    match std::env::current_dir() {
        Ok(path) => CommandResult::ok(&format!("{}\n", path.display())),
        Err(e) => CommandResult::err(&format!("pwd: {}", e)),
    }
}

fn builtin_exit(args: &[String]) -> CommandResult {
    let code = if args.is_empty() {
        0
    } else {
        args[0].parse::<i32>().unwrap_or(0)
    };
    std::process::exit(code);
}

fn builtin_history(_args: &[String], _executor: &Executor) -> CommandResult {
    let out = "(use the history sidebar on the left)\n".to_string();
    CommandResult::ok(&out)
}

fn builtin_export(args: &[String], executor: &mut Executor) -> CommandResult {
    if args.is_empty() {
        let mut sorted: Vec<_> = executor.env.iter().collect();
        sorted.sort_by(|a, b| a.0.cmp(b.0));
        let mut out = String::new();
        for (k, v) in sorted {
            if k != "?" {
                out.push_str(&format!("export {}=\"{}\"\n", k, v));
            }
        }
        return CommandResult::ok(&out);
    }

    for arg in args {
        let expanded = expand_env(arg, &executor.env);
        if let Some(eq_pos) = expanded.find('=') {
            let key = expanded[..eq_pos].to_string();
            let value = expanded[eq_pos + 1..].to_string();
            std::env::set_var(&key, &value);
            executor.env.insert(key, value);
        } else {
            std::env::var(&expanded)
                .map(|v| {
                    executor.env.insert(expanded.clone(), v);
                })
                .unwrap_or_else(|_| {});
        }
    }
    CommandResult::new()
}

fn builtin_jobs(executor: &Executor) -> CommandResult {
    if executor.background_jobs.is_empty() {
        return CommandResult::ok("No background jobs.\n");
    }
    let mut out = String::new();
    for (job_num, child) in &executor.background_jobs {
        out.push_str(&format!("[{}]  pid: {}  running\n", job_num, child.id()));
    }
    CommandResult::ok(&out)
}

fn builtin_clear() -> CommandResult {
    CommandResult::code(999, "", "")
}

fn builtin_help() -> CommandResult {
    let help = r#"Ghost Shell — Available Commands:

Builtins:
  cd [dir]        Change directory
  echo [args]     Print text (supports -n, -e flags)
  pwd             Print working directory
  history         Show command history
  export VAR=val  Set environment variable
  jobs            List background jobs
  launch <cmd>    Open interactive TUI app in a new Terminal window
  term <cmd>      Run an interactive app with Ghost terminal emulation
  edit <file>     Open file in built-in editor (creates if missing)
  spill <file>    Print file contents (cat)
  name <file>     Rename file keeping its extension
  clear           Clear the output screen
  help            Show this help
  hello           Show welcome message with feature list
  exit [code]     Exit Ghost Shell

Shell Features:
  Pipes:           cmd1 | cmd2
  Redirects:       cmd > file, cmd >> file, cmd < file
  Chaining:        cmd1 && cmd2 || cmd3 ; cmd4
  Background:      cmd &
  Variables:       $VAR, ${VAR}
  Quotes:          "text", 'text'

Keybindings:
  Tab              Auto-complete (in shell) / Insert tab (in editor)
  Up/Down          History navigation
  Ctrl+T           New tab
  Ctrl+L           Clear output
  Ctrl+C           Clear input line
  Ctrl+D           Quit
  Ctrl+S           Save file (in editor)
  Esc              Close editor / Cancel / Quit
  Page Up/Down     Scroll output
"#;
    CommandResult::ok(help)
}

fn builtin_launch(args: &[String], executor: &Executor) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("launch: usage: launch <command> [args...]\n");
    }

    let expanded: Vec<String> = args
        .iter()
        .map(|a| expand_env(a, &executor.env))
        .collect();
    let cmd_str = expanded.join(" ");

    let cwd = std::env::current_dir().unwrap_or_default();
    let cwd_str = cwd.display().to_string();

    let mut env_exports = String::new();
    for (k, v) in &executor.env {
        if k != "?" && k != "PATH" {
            env_exports.push_str(&format!("export {}={:?}; ", k, v));
        }
    }
    if let Some(path) = executor.env.get("PATH") {
        env_exports.push_str(&format!("export PATH={:?}; ", path));
    }

    let script = format!(
        "cd {:?}; {}{}; exit",
        cwd_str, env_exports, cmd_str
    );

    let osa = format!(
        "tell application \"Terminal\" to do script \"{}\"",
        script.replace('\\', "\\\\").replace('"', "\\\"")
    );

    let result = std::process::Command::new("osascript")
        .arg("-e")
        .arg(&osa)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output();

    match result {
        Ok(out) => {
            if out.status.success() {
                CommandResult::ok(&format!("launched: {} (in new Terminal window)\n", cmd_str))
            } else {
                let err = String::from_utf8_lossy(&out.stderr).to_string();
                CommandResult::err(&format!("launch: failed to open Terminal: {}\n", err.trim()))
            }
        }
        Err(e) => CommandResult::err(&format!("launch: {}\n", e)),
    }
}

fn builtin_term(args: &[String], executor: &mut Executor) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("term: usage: term <command> [args...]\n");
    }

    let expanded: Vec<String> = args
        .iter()
        .map(|a| expand_env(a, &executor.env))
        .collect();
    let program = &expanded[0];
    let cmd_args = &expanded[1..];

    let cwd = std::env::current_dir()
        .unwrap_or_default()
        .display()
        .to_string();

    match crate::pty::PtySession::new(program, cmd_args, &cwd, &executor.env) {
        Ok(session) => {
            executor.pending_pty = Some(session);
            CommandResult::code(1000, "", "")
        }
        Err(e) => CommandResult::err(&format!("term: failed to start '{}': {}\n", program, e)),
    }
}

fn builtin_name(args: &[String], executor: &Executor) -> CommandResult {
    if args.len() < 2 {
        return CommandResult::err("name: usage: name <file> <new_name>\n");
    }

    let filename = expand_env(&args[0], &executor.env);
    let new_name = expand_env(&args[1], &executor.env);

    let ext = std::path::Path::new(&filename)
        .extension()
        .map(|e| format!(".{}", e.to_string_lossy()))
        .unwrap_or_default();

    let new_name = if new_name.contains('.') {
        new_name
    } else {
        format!("{}{}", new_name, ext)
    };

    match std::fs::rename(&filename, &new_name) {
        Ok(_) => CommandResult::ok(&format!("renamed: {} -> {}\n", filename, new_name)),
        Err(e) => CommandResult::err(&format!("name: {}: {}\n", filename, e)),
    }
}

fn builtin_spill(args: &[String], executor: &Executor) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("spill: usage: spill <file>\n");
    }

    let filename = expand_env(&args[0], &executor.env);
    match std::fs::read_to_string(&filename) {
        Ok(content) => {
            if content.is_empty() {
                CommandResult::ok("")
            } else {
                CommandResult::ok(&format!("{}\n", content))
            }
        }
        Err(e) => CommandResult::err(&format!("spill: {}: {}\n", filename, e)),
    }
}

fn builtin_edit(args: &[String], executor: &mut Executor) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("edit: usage: edit <file>\n");
    }

    let path = expand_env(&args[0], &executor.env);

    let content = match std::fs::read_to_string(&path) {
        Ok(c) => c,
        Err(_) => {
            if let Err(e) = std::fs::write(&path, "") {
                return CommandResult::err(&format!("edit: cannot create {}: {}\n", path, e));
            }
            if let Ok(user) = std::env::var("USER") {
                let _ = std::process::Command::new("chown")
                    .args([&user, &path])
                    .output();
            }
            String::new()
        }
    };

    executor.pending_editor = Some((path, content));
    CommandResult::code(1001, "", "")
}
