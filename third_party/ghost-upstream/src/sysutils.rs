use crate::executor::CommandResult;
use base64::Engine;
use sha2::Digest;
use std::process::Stdio;

pub fn handle(name: &str, args: &[String]) -> Option<CommandResult> {
    match name {
        "calc" => Some(cmd_calc(args)),
        "uuid" => Some(cmd_uuid(args)),
        "base64" => Some(cmd_base64(args)),
        "md5" => Some(cmd_md5(args)),
        "sha256" => Some(cmd_sha256(args)),
        "date" => Some(cmd_date(args)),
        "find" => Some(cmd_find(args)),
        "tree" => Some(cmd_tree(args)),
        "touch" => Some(cmd_touch(args)),
        "file" => Some(cmd_file(args)),
        "stat" => Some(cmd_stat(args)),
        "which" => Some(cmd_which(args)),
        "whoami" => Some(cmd_whoami()),
        "hostname" => Some(cmd_hostname()),
        "uname" => Some(cmd_uname()),
        "ping" => Some(cmd_ping(args)),
        _ => None,
    }
}

fn cmd_calc(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: calc <expression>\n  e.g. calc 2+2*3, calc 'sqrt(144)', calc '10 % 3'"); }
    let expr = args.join(" ");
    match eval_expr(&expr) {
        Ok(v) => {
            let out = if v.fract() == 0.0 { format!("{}\n", v as i64) } else { format!("{}\n", v) };
            CommandResult::ok(&out)
        }
        Err(e) => CommandResult::err(&format!("calc: {}", e)),
    }
}

fn eval_expr(s: &str) -> Result<f64, String> {
    let s = s.trim();
    if s.is_empty() { return Err("empty expression".into()); }
    let chars: Vec<char> = s.chars().collect();
    let mut pos = 0;
    parse_expr(&chars, &mut pos)
}

fn parse_expr(c: &[char], pos: &mut usize) -> Result<f64, String> {
    let mut left = parse_term(c, pos)?;
    while *pos < c.len() {
        match c[*pos] { '+' => { *pos += 1; left += parse_term(c, pos)?; }
            '-' => { *pos += 1; left -= parse_term(c, pos)?; } _ => break }
    }
    Ok(left)
}

fn parse_term(c: &[char], pos: &mut usize) -> Result<f64, String> {
    let mut left = parse_factor(c, pos)?;
    while *pos < c.len() {
        match c[*pos] { '*' => { *pos += 1; left *= parse_factor(c, pos)?; }
            '/' => { *pos += 1; let r = parse_factor(c, pos)?; if r == 0.0 { return Err("division by zero".into()); } left /= r; }
            '%' => { *pos += 1; left %= parse_factor(c, pos)?; }
            _ => break }
    }
    Ok(left)
}

fn parse_factor(c: &[char], pos: &mut usize) -> Result<f64, String> {
    while *pos < c.len() && c[*pos].is_whitespace() { *pos += 1; }
    if *pos >= c.len() { return Err("unexpected end".into()); }
    if c[*pos] == '(' {
        *pos += 1;
        let v = parse_expr(c, pos)?;
        if *pos < c.len() && c[*pos] == ')' { *pos += 1; }
        return Ok(v);
    }
    if c[*pos] == '-' { *pos += 1; return Ok(-parse_factor(c, pos)?); }
    if c[*pos].is_alphabetic() {
        let start = *pos;
        while *pos < c.len() && (c[*pos].is_alphanumeric() || c[*pos] == '_') { *pos += 1; }
        let name: String = c[start..*pos].iter().collect();
        if *pos < c.len() && c[*pos] == '(' {
            *pos += 1;
            let arg = parse_expr(c, pos)?;
            if *pos < c.len() && c[*pos] == ')' { *pos += 1; }
            return Ok(match name.as_str() {
                "sqrt" => arg.sqrt(), "abs" => arg.abs(), "floor" => arg.floor(),
                "ceil" => arg.ceil(), "round" => arg.round(), "sin" => arg.sin(),
                "cos" => arg.cos(), "tan" => arg.tan(), "log" => arg.log10(),
                "ln" => arg.ln(), "exp" => arg.exp(), _ => return Err(format!("unknown function: {}", name)),
            });
        }
        return Ok(match name.as_str() { "pi" => std::f64::consts::PI, "e" => std::f64::consts::E, _ => return Err(format!("unknown: {}", name)) });
    }
    let start = *pos;
    while *pos < c.len() && (c[*pos].is_numeric() || c[*pos] == '.') { *pos += 1; }
    let num_str: String = c[start..*pos].iter().collect();
    num_str.parse::<f64>().map_err(|_| format!("bad number: {}", num_str))
}

fn cmd_uuid(args: &[String]) -> CommandResult {
    let count = if args.is_empty() { 1 } else { args[0].parse::<usize>().unwrap_or(1) };
    let mut out = String::new();
    for _ in 0..count {
        let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos();
        let mut bytes = [0u8; 16];
        for (i, b) in bytes.iter_mut().enumerate() {
            *b = ((now >> (i * 4)) & 0xFF) as u8;
        }
        bytes[6] = (bytes[6] & 0x0F) | 0x40;
        bytes[8] = (bytes[8] & 0x3F) | 0x80;
        let hex: String = bytes.iter().map(|b| format!("{:02x}", b)).collect();
        out.push_str(&format!("{}-{}-{}-{}-{}\n", &hex[0..8], &hex[8..12], &hex[12..16], &hex[16..20], &hex[20..32]));
    }
    CommandResult::ok(&out)
}

fn cmd_base64(args: &[String]) -> CommandResult {
    let decode = args.iter().any(|a| a == "-d" || a == "--decode");
    let input: Vec<String> = args.iter().filter(|a| !a.starts_with('-')).cloned().collect();
    if input.is_empty() { return CommandResult::err("usage: base64 [-d] <text or file>"); }
    if decode {
        let s = input.join(" ");
        match base64::engine::general_purpose::STANDARD.decode(s.trim()) {
            Ok(bytes) => CommandResult::ok(&String::from_utf8_lossy(&bytes).to_string()),
            Err(e) => CommandResult::err(&format!("base64: {}", e)),
        }
    } else {
        if let Ok(content) = std::fs::read_to_string(&input[0]) {
            CommandResult::ok(&format!("{}\n", base64::engine::general_purpose::STANDARD.encode(content)))
        } else {
            let s = input.join(" ");
            CommandResult::ok(&format!("{}\n", base64::engine::general_purpose::STANDARD.encode(s)))
        }
    }
}

fn cmd_md5(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: md5 <file or string>"); }
    let input = &args[0];
    let data = if std::path::Path::new(input).is_file() {
        match std::fs::read(input) { Ok(d) => d, Err(e) => return CommandResult::err(&format!("md5: {}", e)) }
    } else { input.as_bytes().to_vec() };
    let hash = md5::Md5::digest(&data);
    CommandResult::ok(&format!("{:x}\n", hash))
}

fn cmd_sha256(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: sha256 <file or string>"); }
    let input = &args[0];
    let data = if std::path::Path::new(input).is_file() {
        match std::fs::read(input) { Ok(d) => d, Err(e) => return CommandResult::err(&format!("sha256: {}", e)) }
    } else { input.as_bytes().to_vec() };
    let hash = sha2::Sha256::digest(&data);
    CommandResult::ok(&format!("{:x}\n", hash))
}

fn cmd_date(args: &[String]) -> CommandResult {
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    let secs = now.as_secs();
    let days = secs / 86400;
    let h = (secs / 3600) % 24;
    let m = (secs / 60) % 60;
    let s = secs % 60;
    let out = if args.is_empty() {
        format!("{:02}:{:02}:{:02} (day {}, {} total seconds)\n", h, m, s, days, secs)
    } else {
        match args[0].as_str() {
            "+%s" => format!("{}\n", secs),
            "+%H:%M:%S" => format!("{:02}:{:02}:{:02}\n", h, m, s),
            "+%u" => format!("{}\n", secs),
            _ => format!("{:02}:{:02}:{:02}\n", h, m, s),
        }
    };
    CommandResult::ok(&out)
}

fn cmd_find(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: find <path> [-name pattern] [-type f|d]"); }
    let mut path = ".".to_string();
    let mut name_pattern: Option<String> = None;
    let mut file_type: Option<char> = None;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-name" => { i += 1; if i < args.len() { name_pattern = Some(args[i].clone()); } }
            "-type" => { i += 1; if i < args.len() { file_type = args[i].chars().next(); } }
            a if !a.starts_with('-') => { path = a.to_string(); }
            _ => {}
        }
        i += 1;
    }
    let mut results = Vec::new();
    find_recursive(&path, &name_pattern, file_type, &mut results);
    if results.is_empty() { CommandResult::ok("") }
    else { CommandResult::ok(&format!("{}\n", results.join("\n"))) }
}

fn find_recursive(dir: &str, pattern: &Option<String>, file_type: Option<char>, results: &mut Vec<String>) {
    let entries = match std::fs::read_dir(dir) { Ok(e) => e, Err(_) => return };
    for entry in entries.flatten() {
        let path = entry.path();
        let path_str = path.display().to_string();
        let meta = match entry.metadata() { Ok(m) => m, Err(_) => continue };
        let type_ok = match file_type { Some('f') => meta.is_file(), Some('d') => meta.is_dir(), _ => true };
        let name_ok = match pattern {
            Some(p) => { let name = entry.file_name().to_string_lossy().to_string(); matches_glob(&name, p) }
            None => true,
        };
        if type_ok && name_ok { results.push(path_str.clone()); }
        if meta.is_dir() { find_recursive(&path_str, pattern, file_type, results); }
    }
}

fn matches_glob(name: &str, pattern: &str) -> bool {
    let re = pattern.replace("*", ".*").replace("?", ".");
    regex::Regex::new(&format!("^{}$", re)).map(|r| r.is_match(name)).unwrap_or(false)
}

fn cmd_tree(args: &[String]) -> CommandResult {
    let path = if args.is_empty() { "." } else { &args[0] };
    let max_depth = if args.len() > 1 { args[1].parse::<usize>().unwrap_or(3) } else { 3 };
    let mut out = String::new();
    out.push_str(&format!("{}\n", path));
    tree_recursive(path, "", max_depth, 0, &mut out);
    CommandResult::ok(&out)
}

fn tree_recursive(dir: &str, prefix: &str, max_depth: usize, depth: usize, out: &mut String) {
    if depth >= max_depth { return; }
    let entries = match std::fs::read_dir(dir) { Ok(e) => e, Err(_) => return };
    let entries: Vec<_> = entries.filter_map(|e| e.ok()).collect();
    let count = entries.len();
    for (i, entry) in entries.iter().enumerate() {
        let name = entry.file_name().to_string_lossy().to_string();
        let is_last = i == count - 1;
        let branch = if is_last { "└── " } else { "├── " };
        out.push_str(&format!("{}{}{}\n", prefix, branch, name));
        if entry.metadata().map(|m| m.is_dir()).unwrap_or(false) {
            let new_prefix = if is_last { format!("{}    ", prefix) } else { format!("{}│   ", prefix) };
            let path_str = entry.path().display().to_string();
            tree_recursive(&path_str, &new_prefix, max_depth, depth + 1, out);
        }
    }
}

fn cmd_touch(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: touch <file>"); }
    let file = &args[0];
    match std::fs::OpenOptions::new().write(true).create(true).truncate(false).open(file) {
        Ok(_) => CommandResult::ok(&format!("{}\n", file)),
        Err(e) => CommandResult::err(&format!("touch: {}", e)),
    }
}

fn cmd_file(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: file <path>"); }
    let path = std::path::Path::new(&args[0]);
    if !path.exists() { return CommandResult::err(&format!("file: {}: not found", args[0])); }
    let meta = match std::fs::metadata(path) { Ok(m) => m, Err(e) => return CommandResult::err(&format!("file: {}", e)) };
    let kind = if meta.is_dir() { "directory" }
        else if meta.is_symlink() { "symlink" }
        else {
            let bytes = std::fs::read(path).unwrap_or_default();
            if bytes.starts_with(&[0x7f, 0x45, 0x4c, 0x46]) { "ELF binary" }
            else if bytes.starts_with(&[0x4d, 0x5a]) { "PE executable" }
            else if bytes.starts_with(b"#!/") { "script" }
            else if bytes.starts_with(b"{") || bytes.starts_with(b"[") { "JSON data" }
            else if bytes.starts_with(b"<?xml") { "XML document" }
            else if bytes.starts_with(b"\x89PNG") { "PNG image" }
            else if bytes.starts_with(b"\xff\xd8\xff") { "JPEG image" }
            else if bytes.starts_with(b"%PDF") { "PDF document" }
            else { "text/ASCII" }
        };
    CommandResult::ok(&format!("{}: {} ({} bytes)\n", args[0], kind, meta.len()))
}

fn cmd_stat(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: stat <file>"); }
    let path = std::path::Path::new(&args[0]);
    let meta = match std::fs::metadata(path) { Ok(m) => m, Err(e) => return CommandResult::err(&format!("stat: {}", e)) };
    let kind = if meta.is_dir() { "directory" } else { "file" };
    CommandResult::ok(&format!(
        "  File: {}\n  Type: {}\n  Size: {} bytes\n  Read-only: {}\n  Modified: {:?}\n",
        args[0], kind, meta.len(), meta.permissions().readonly(), meta.modified().unwrap_or(std::time::SystemTime::now())
    ))
}

fn cmd_which(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: which <command>"); }
    let cmd = &args[0];
    let builtins: &[&str] = &["cd","echo","pwd","exit","quit","history","export","jobs","clear","help","hello",
        "seek","curl","ftp","ssh","wget","grep","jq","wc","sort","uniq","head","tail","cut","tr","rev","sed","tee",
        "printf","seq","yes","cat","diff","calc","uuid","base64","md5","sha256","date","find","tree","touch",
        "file","stat","which","whoami","hostname","uname","ping","dig","ssl","ip","headers","weather","qr",
        "shorten","currency","com","theme","alias"];
    if builtins.contains(&cmd.as_str()) {
        return CommandResult::ok(&format!("{}: built-in command\n", cmd));
    }
    let path_dirs: Vec<std::path::PathBuf> = std::env::var("PATH").unwrap_or_default()
        .split(if cfg!(windows) { ';' } else { ':' }).map(std::path::PathBuf::from).collect();
    for dir in path_dirs {
        let full = dir.join(cmd);
        if full.exists() {
            #[cfg(unix)]
            { use std::os::unix::fs::PermissionsExt; if let Ok(m) = std::fs::metadata(&full) { if m.permissions().mode() & 0o111 != 0 {
                return CommandResult::ok(&format!("{}\n", full.display())); } } }
            #[cfg(windows)]
            { return CommandResult::ok(&format!("{}\n", full.display())); }
        }
    }
    CommandResult::code(1, "", &format!("{}: not found\n", cmd))
}

fn cmd_whoami() -> CommandResult {
    let user = std::env::var("USER").or_else(|_| std::env::var("USERNAME")).unwrap_or_else(|_| "unknown".into());
    CommandResult::ok(&format!("{}\n", user))
}

fn cmd_hostname() -> CommandResult {
    let host = std::env::var("HOSTNAME").unwrap_or_else(|_| {
        std::process::Command::new("hostname").output()
            .ok().and_then(|o| String::from_utf8(o.stdout).ok())
            .map(|s| s.trim().to_string()).unwrap_or_else(|| "unknown".into())
    });
    CommandResult::ok(&format!("{}\n", host))
}

fn cmd_uname() -> CommandResult {
    let os = if cfg!(target_os = "macos") { "Darwin" }
        else if cfg!(target_os = "linux") { "Linux" }
        else if cfg!(target_os = "windows") { "Windows" }
        else { "Unknown" };
    let arch = if cfg!(target_arch = "x86_64") { "x86_64" } else if cfg!(target_arch = "aarch64") { "aarch64" } else { "unknown" };
    CommandResult::ok(&format!("{} {} Ghost Shell\n", os, arch))
}

fn cmd_ping(args: &[String]) -> CommandResult {
    let has_c = args.iter().any(|a| a == "-c" || a.starts_with("-c"));
    let mut cmd_args: Vec<String> = if !has_c {
        let mut v = vec!["-c".to_string(), "4".to_string()];
        v.extend(args.iter().cloned());
        v
    } else {
        args.to_vec()
    };

    let host = cmd_args.iter().find(|a| !a.starts_with('-')).cloned().unwrap_or_default();
    if host.is_empty() {
        return CommandResult::err("usage: ping <host>\n  e.g. ping google.com, ping -c 10 example.com");
    }

    let ping_bin = if cfg!(target_os = "macos") { "/sbin/ping" } else { "/usr/bin/ping" };
    match std::process::Command::new(ping_bin)
        .args(&cmd_args)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output()
    {
        Ok(out) => {
            let stdout = String::from_utf8_lossy(&out.stdout);
            let stderr = String::from_utf8_lossy(&out.stderr);
            let code = out.status.code().unwrap_or(1);
            CommandResult::code(code, &stdout, &stderr)
        }
        Err(e) => CommandResult::err(&format!("ping: {}", e)),
    }
}
