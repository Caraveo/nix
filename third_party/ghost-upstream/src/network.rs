use crate::executor::CommandResult;
use serde::Deserialize;

const SEEK_BASE: &str = "https://seek.grid-compute.com/api/search";

pub fn handle_network_builtin(name: &str, args: &[String]) -> Option<CommandResult> {
    match name {
        "seek" => Some(cmd_seek(args)),
        "curl" => Some(cmd_curl(args)),
        "ftp" => Some(cmd_ftp(args)),
        "ssh" => Some(cmd_ssh(args)),
        "wget" => Some(cmd_curl(args)),
        _ => None,
    }
}

// ── SEEK ──────────────────────────────────────────────────────────────────

#[derive(Debug, Deserialize)]
struct SeekResponse {
    query: String,
    results: Vec<SeekResult>,
    #[serde(default)]
    total: u64,
    #[serde(default)]
    took_ms: u64,
    #[serde(default)]
    next_cursor: Option<String>,
}

#[derive(Debug, Deserialize)]
struct SeekResult {
    #[serde(default)]
    id: String,
    #[serde(default)]
    title: String,
    #[serde(default)]
    url: String,
    #[serde(default)]
    domain: String,
    #[serde(default)]
    snippet: String,
    #[serde(default)]
    score: Option<f64>,
}

fn cmd_seek(args: &[String]) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("usage: seek <query> [--site domain.com] [--page-size N]");
    }

    let mut query = String::new();
    let mut site: Option<String> = None;
    let mut page_size = 10u32;
    let mut i = 0;

    while i < args.len() {
        match args[i].as_str() {
            "--site" | "-s" => {
                i += 1;
                if i < args.len() { site = Some(args[i].clone()); }
            }
            "--page-size" | "-n" => {
                i += 1;
                if i < args.len() {
                    page_size = args[i].parse::<u32>().unwrap_or(10).min(50).max(1);
                }
            }
            "--view" => {
                i += 1; // skip view mode
            }
            _ => {
                if !query.is_empty() { query.push(' '); }
                query.push_str(&args[i]);
            }
        }
        i += 1;
    }

    if query.is_empty() {
        return CommandResult::err("seek: query is required");
    }

    let mut req = ureq::get(SEEK_BASE)
        .query("q", &query)
        .query("page_size", &page_size.to_string());

    if let Some(ref s) = site {
        req = req.query("site", s);
    }

    match req.call() {
        Ok(response) => {
            match response.into_json::<SeekResponse>() {
                Ok(seek) => {
                    let output = format_seek_results(&seek);
                    CommandResult::ok(&output)
                }
                Err(e) => {
                    CommandResult::err(&format!("seek: failed to parse response: {}", e))
                }
            }
        }
        Err(e) => {
            CommandResult::err(&format!("seek: request failed: {}", e))
        }
    }
}

fn format_seek_results(resp: &SeekResponse) -> String {
    let mut out = String::new();
    out.push_str(&format!("=== SEEK Search: \"{}\" ===\n", resp.query));
    out.push_str(&format!("Total: {} results | {}ms\n\n", format_num(resp.total), resp.took_ms));

    if resp.results.is_empty() {
        out.push_str("No results found.\n");
        return out;
    }

    for (i, r) in resp.results.iter().enumerate() {
        let score = r.score.map(|s| format!("{:.2}", s)).unwrap_or("N/A".to_string());
        let title = if r.title.is_empty() { "(untitled)" } else { &r.title };

        out.push_str(&format!(" {}.  {}\n", i + 1, title));
        if !r.url.is_empty() {
            out.push_str(&format!("     {}\n", r.url));
        }
        out.push_str(&format!("     domain: {} | score: {}\n", r.domain, score));
        if !r.snippet.is_empty() {
            let snippet = if r.snippet.len() > 200 {
                format!("{}...", &r.snippet[..197])
            } else {
                r.snippet.clone()
            };
            out.push_str(&format!("     {}\n", snippet));
        }
        out.push('\n');
    }

    let shown = resp.results.len();
    out.push_str(&format!("=== {} of {} results shown ===\n", shown, format_num(resp.total)));

    if resp.next_cursor.is_some() {
        out.push_str("(more results available — use: seek <query> --next)\n");
    }

    out
}

fn format_num(n: u64) -> String {
    let s = n.to_string();
    let chars: Vec<char> = s.chars().collect();
    let mut result = String::new();
    for (i, c) in chars.iter().enumerate() {
        if i > 0 && (chars.len() - i) % 3 == 0 {
            result.push(',');
        }
        result.push(*c);
    }
    result
}

// ── CURL ──────────────────────────────────────────────────────────────────

fn cmd_curl(args: &[String]) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("usage: curl <url> [-o file] [-X METHOD] [-d data] [-H header]");
    }

    let mut url: Option<String> = None;
    let mut output_file: Option<String> = None;
    let mut method = "GET".to_string();
    let mut data: Option<String> = None;
    let mut headers: Vec<(String, String)> = Vec::new();
    let mut silent = false;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-o" | "--output" => {
                i += 1;
                if i < args.len() { output_file = Some(args[i].clone()); }
            }
            "-X" | "--request" => {
                i += 1;
                if i < args.len() { method = args[i].clone(); }
            }
            "-d" | "--data" => {
                i += 1;
                if i < args.len() { data = Some(args[i].clone()); method = "POST".to_string(); }
            }
            "-H" | "--header" => {
                i += 1;
                if i < args.len() {
                    if let Some(pos) = args[i].find(':') {
                        let key = args[i][..pos].trim().to_string();
                        let val = args[i][pos + 1..].trim().to_string();
                        headers.push((key, val));
                    }
                }
            }
            "-s" | "--silent" => { silent = true; }
            "-L" | "--location" => {} // follow redirects (ureq does by default)
            _ => {
                if url.is_none() && !args[i].starts_with('-') {
                    url = Some(args[i].clone());
                }
            }
        }
        i += 1;
    }

    let url = match url {
        Some(u) => u,
        None => return CommandResult::err("curl: no URL specified"),
    };

    let url = if url.starts_with("http://") || url.starts_with("https://") {
        url
    } else {
        format!("https://{}", url)
    };

    let method_upper = method.to_uppercase();
    let mut req = match method_upper.as_str() {
        "GET" => ureq::get(&url),
        "POST" => ureq::post(&url),
        "PUT" => ureq::put(&url),
        "DELETE" => ureq::delete(&url),
        "PATCH" => ureq::patch(&url),
        "HEAD" => ureq::head(&url),
        _ => ureq::request(&method_upper, &url),
    };

    for (k, v) in &headers {
        req = req.set(k, v);
    }

    let response = if let Some(d) = data {
        req.send_string(&d)
    } else {
        req.call()
    };

    match response {
        Ok(resp) => {
            let status = resp.status();
            let content_type = resp.header("Content-Type").unwrap_or("").to_string();

            if let Some(ref file) = output_file {
                match resp.into_string() {
                    Ok(body) => {
                        match std::fs::write(file, &body) {
                            Ok(_) => {
                                if silent {
                                    return CommandResult::new();
                                }
                                CommandResult::ok(&format!("Saved {} bytes to {}\n", body.len(), file))
                            }
                            Err(e) => CommandResult::err(&format!("curl: write error: {}", e)),
                        }
                    }
                    Err(e) => CommandResult::err(&format!("curl: read error: {}", e)),
                }
            } else {
                match resp.into_string() {
                    Ok(body) => {
                        let mut out = String::new();
                        if !silent {
                            out.push_str(&format!("HTTP {} {}\n", status, content_type));
                            out.push_str(&format!("Content-Length: {} bytes\n\n", body.len()));
                        }
                        out.push_str(&body);
                        if !body.ends_with('\n') { out.push('\n'); }
                        CommandResult::ok(&out)
                    }
                    Err(e) => CommandResult::err(&format!("curl: {}", e)),
                }
            }
        }
        Err(e) => {
            CommandResult::err(&format!("curl: {}", e))
        }
    }
}

// ── FTP ───────────────────────────────────────────────────────────────────

fn cmd_ftp(args: &[String]) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("usage: ftp <host> [command] [args]\n  Commands: ls, get <remote> [local], put <local> [remote], pwd, cd <dir>, mkdir <dir>");
    }

    let host = &args[0];
    let cmd = if args.len() > 1 { args[1].as_str() } else { "ls" };

    let port = 21;
    let host_clean = host.trim_start_matches("ftp://").trim_start_matches("ftps://");

    use suppaftp::FtpStream;

    let mut ftp_stream = match FtpStream::connect(format!("{}:{}", host_clean, port)) {
        Ok(s) => s,
        Err(e) => return CommandResult::err(&format!("ftp: connect failed: {}", e)),
    };

    // Anonymous login by default
    let user = std::env::var("FTP_USER").unwrap_or_else(|_| "anonymous".to_string());
    let pass = std::env::var("FTP_PASS").unwrap_or_else(|_| "anonymous@".to_string());
    if let Err(e) = ftp_stream.login(&user, &pass) {
        return CommandResult::err(&format!("ftp: login failed: {}", e));
    }

    let result = match cmd {
        "ls" | "dir" | "list" => {
            match ftp_stream.nlst(None) {
                Ok(list) => {
                    let mut out = format!("FTP: {} \n\n", host);
                    for item in &list {
                        out.push_str(&format!("  {}\n", item));
                    }
                    out.push_str(&format!("\n{} items\n", list.len()));
                    CommandResult::ok(&out)
                }
                Err(e) => CommandResult::err(&format!("ftp: ls failed: {}", e)),
            }
        }
        "pwd" => {
            match ftp_stream.pwd() {
                Ok(p) => CommandResult::ok(&format!("{}\n", p)),
                Err(e) => CommandResult::err(&format!("ftp: pwd failed: {}", e)),
            }
        }
        "cd" | "cwd" => {
            if args.len() < 3 {
                return CommandResult::err("ftp: cd requires a directory");
            }
            match ftp_stream.cwd(&args[2]) {
                Ok(_) => CommandResult::ok(&format!("Changed to {}\n", args[2])),
                Err(e) => CommandResult::err(&format!("ftp: cd failed: {}", e)),
            }
        }
        "mkdir" => {
            if args.len() < 3 {
                return CommandResult::err("ftp: mkdir requires a directory name");
            }
            match ftp_stream.mkdir(&args[2]) {
                Ok(_) => CommandResult::ok(&format!("Created {}\n", args[2])),
                Err(e) => CommandResult::err(&format!("ftp: mkdir failed: {}", e)),
            }
        }
        "get" => {
            if args.len() < 3 {
                return CommandResult::err("ftp: get requires a remote file path");
            }
            let remote = &args[2];
            let local = if args.len() > 3 { args[3].clone() } else {
                std::path::Path::new(remote).file_name()
                    .map(|f| f.to_string_lossy().to_string())
                    .unwrap_or_else(|| "download".to_string())
            };
            match ftp_stream.retr_as_buffer(remote) {
                Ok(data) => {
                    let bytes = data.into_inner();
                    match std::fs::write(&local, &bytes) {
                        Ok(_) => CommandResult::ok(&format!("Downloaded {} ({} bytes) to {}\n", remote, bytes.len(), local)),
                        Err(e) => CommandResult::err(&format!("ftp: write failed: {}", e)),
                    }
                }
                Err(e) => CommandResult::err(&format!("ftp: get failed: {}", e)),
            }
        }
        "put" => {
            if args.len() < 3 {
                return CommandResult::err("ftp: put requires a local file path");
            }
            let local = &args[2];
            let remote = if args.len() > 3 { args[3].clone() } else {
                std::path::Path::new(local).file_name()
                    .map(|f| f.to_string_lossy().to_string())
                    .unwrap_or_else(|| "upload".to_string())
            };
            match std::fs::read(local) {
                Ok(data) => {
                    let mut reader = std::io::Cursor::new(data);
                    match ftp_stream.put_file(&remote, &mut reader) {
                        Ok(_) => CommandResult::ok(&format!("Uploaded {} to {}\n", local, remote)),
                        Err(e) => CommandResult::err(&format!("ftp: put failed: {}", e)),
                    }
                }
                Err(e) => CommandResult::err(&format!("ftp: read local file failed: {}", e)),
            }
        }
        "quit" | "bye" | "exit" => {
            let _ = ftp_stream.quit();
            CommandResult::ok("FTP session closed.\n")
        }
        _ => {
            CommandResult::err(&format!("ftp: unknown command '{}'", cmd))
        }
    };

    let _ = ftp_stream.quit();
    result
}

// ── SSH ───────────────────────────────────────────────────────────────────

fn cmd_ssh(args: &[String]) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("usage: ssh user@host [command]\n  SSL/TLS is enabled by default.\n  Options: -p <port> -i <keyfile> -t (allocate tty)");
    }

    let mut ssh_args: Vec<String> = Vec::new();

    // Security defaults
    ssh_args.push("-o".into());
    ssh_args.push("StrictHostKeyChecking=accept-new".into());
    ssh_args.push("-o".into());
    ssh_args.push("ConnectTimeout=10".into());
    ssh_args.push("-o".into());
    ssh_args.push("ServerAliveInterval=30".into());
    ssh_args.push("-o".into());
    ssh_args.push("ServerAliveCountMax=3".into());

    // Pass through user args
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-t" => { ssh_args.push("-t".into()); }
            "-p" => {
                ssh_args.push("-p".into());
                i += 1;
                if i < args.len() { ssh_args.push(args[i].clone()); }
            }
            "-i" => {
                ssh_args.push("-i".into());
                i += 1;
                if i < args.len() { ssh_args.push(args[i].clone()); }
            }
            _ => { ssh_args.push(args[i].clone()); }
        }
        i += 1;
    }

    let output = std::process::Command::new("ssh")
        .args(&ssh_args)
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .output();

    match output {
        Ok(out) => {
            let stdout = String::from_utf8_lossy(&out.stdout).to_string();
            let stderr = String::from_utf8_lossy(&out.stderr).to_string();
            let code = out.status.code().unwrap_or(1);
            CommandResult::code(code, &stdout, &stderr)
        }
        Err(e) => {
            CommandResult::err(&format!("ssh: {} (is ssh installed?)", e))
        }
    }
}
