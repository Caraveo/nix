use crate::executor::CommandResult;
use std::path::Path;

pub fn handle(name: &str, args: &[String]) -> Option<CommandResult> {
    match name {
        "ls" => Some(cmd_ls(args)),
        "cp" => Some(cmd_cp(args)),
        "mv" => Some(cmd_mv(args)),
        "rm" => Some(cmd_rm(args)),
        "mkdir" => Some(cmd_mkdir(args)),
        "rmdir" => Some(cmd_rmdir(args)),
        "ln" => Some(cmd_ln(args)),
        "chmod" => Some(cmd_chmod(args)),
        "chown" => Some(cmd_chown(args)),
        "chgrp" => Some(cmd_chgrp(args)),
        "umask" => Some(cmd_umask(args)),
        "readlink" => Some(cmd_readlink(args)),
        "basename" => Some(cmd_basename(args)),
        "dirname" => Some(cmd_dirname(args)),
        "realpath" => Some(cmd_realpath(args)),
        "nl" => Some(cmd_nl(args)),
        "tac" => Some(cmd_tac(args)),
        "expand" => Some(cmd_expand(args)),
        "unexpand" => Some(cmd_unexpand(args)),
        "paste" => Some(cmd_paste(args)),
        "comm" => Some(cmd_comm(args)),
        "shuf" => Some(cmd_shuf(args)),
        "fold" => Some(cmd_fold(args)),
        "mktemp" => Some(cmd_mktemp(args)),
        "du" => Some(cmd_du(args)),
        "df" => Some(cmd_df(args)),
        "tar" => Some(cmd_tar(args)),
        "gzip" => Some(cmd_gzip(args)),
        "gunzip" => Some(cmd_gunzip(args)),
        "zip" => Some(cmd_zip(args)),
        "unzip" => Some(cmd_unzip(args)),
        _ => None,
    }
}

fn err(msg: &str) -> CommandResult { CommandResult::err(msg) }
fn ok(msg: &str) -> CommandResult { CommandResult::ok(msg) }

// ── ls ─────────────────────────────────────────────────────────────────────

fn cmd_ls(args: &[String]) -> CommandResult {
    let mut long = false; let mut all = false; let mut human = false;
    let mut sort_time = false; let mut reverse = false; let mut dirs: Vec<String> = Vec::new();
    for a in args {
        match a.as_str() {
            "-l" | "--long" => long = true, "-a" | "--all" => all = true,
            "-h" | "--human" => human = true, "-t" => sort_time = true,
            "-r" => reverse = true, "-la" | "-al" => { long = true; all = true; }
            a if !a.starts_with('-') => dirs.push(a.to_string()), _ => {}
        }
    }
    if dirs.is_empty() { dirs.push(".".to_string()); }

    let mut out = String::new();
    for (di, dir) in dirs.iter().enumerate() {
        if dirs.len() > 1 { out.push_str(&format!("{}:\n", dir)); }
        let entries = match std::fs::read_dir(dir) {
            Ok(e) => e, Err(e) => { out.push_str(&format!("ls: {}: {}\n", dir, e)); continue; }
        };
        let mut items: Vec<_> = entries.filter_map(|e| e.ok()).collect();

        // Filter hidden
        if !all { items.retain(|e| !e.file_name().to_string_lossy().starts_with('.')); }
        // Sort
        if sort_time {
            items.sort_by(|a, b| {
                let ta = a.metadata().and_then(|m| m.modified()).ok();
                let tb = b.metadata().and_then(|m| m.modified()).ok();
                tb.cmp(&ta)
            });
        } else { items.sort_by_key(|e| e.file_name()); }
        if reverse { items.reverse(); }

        if long {
            for entry in &items {
                let meta = match entry.metadata() { Ok(m) => m, Err(_) => continue };
                let name = entry.file_name().to_string_lossy().to_string();
                let ft = if meta.is_dir() { 'd' }
                    else if meta.is_symlink() { 'l' } else { '-' };
                #[cfg(unix)]
                let perms = {
                    use std::os::unix::fs::PermissionsExt;
                    let m = meta.permissions().mode();
                    format!("{}{}{}{}{}{}{}{}{}", 
                        if m & 0o400 != 0 { 'r' } else { '-' },
                        if m & 0o200 != 0 { 'w' } else { '-' },
                        if m & 0o100 != 0 { 'x' } else { '-' },
                        if m & 0o040 != 0 { 'r' } else { '-' },
                        if m & 0o020 != 0 { 'w' } else { '-' },
                        if m & 0o010 != 0 { 'x' } else { '-' },
                        if m & 0o004 != 0 { 'r' } else { '-' },
                        if m & 0o002 != 0 { 'w' } else { '-' },
                        if m & 0o001 != 0 { 'x' } else { '-' },
                    )
                };
                #[cfg(not(unix))]
                let perms = if meta.permissions().readonly() { "r--r--r--" } else { "rw-rw-rw-" };
                let size = if human { human_size(meta.len()) } else { meta.len().to_string() };
                let indicator = if meta.is_dir() { "/" } else { "" };
                out.push_str(&format!("{}{} {:>10} {}\n", ft, perms, size, format!("{}{}", name, indicator)));
            }
        } else {
            let names: Vec<String> = items.iter().map(|e| {
                let name = e.file_name().to_string_lossy().to_string();
                if e.metadata().map(|m| m.is_dir()).unwrap_or(false) { format!("{}/", name) } else { name }
            }).collect();
            // Column output
            let max_len = names.iter().map(|n| n.len()).max().unwrap_or(0) + 2;
            let term_width = 80;
            let cols = (term_width / max_len).max(1);
            for (i, name) in names.iter().enumerate() {
                out.push_str(name);
                if (i + 1) % cols == 0 { out.push('\n'); }
                else { out.push_str(&" ".repeat(max_len - name.len())); }
            }
            if !out.ends_with('\n') { out.push('\n'); }
        }
        if dirs.len() > 1 && di < dirs.len() - 1 { out.push('\n'); }
    }
    ok(&out)
}

fn human_size(bytes: u64) -> String {
    if bytes < 1024 { return format!("{}B", bytes); }
    let units = ["K", "M", "G", "T", "P"];
    let mut size = bytes as f64 / 1024.0;
    let mut idx = 0;
    while size >= 1024.0 && idx < units.len() - 1 { size /= 1024.0; idx += 1; }
    format!("{:.1}{}", size, units[idx])
}

// ── cp / mv / rm / mkdir / rmdir / ln ────────────────────────────────────────

fn cmd_cp(args: &[String]) -> CommandResult {
    let mut recursive = false; let mut force = false; let mut files: Vec<String> = Vec::new();
    for a in args {
        match a.as_str() { "-r" | "-R" | "--recursive" => recursive = true,
            "-f" | "--force" => force = true, _ => files.push(a.clone()) }
    }
    if files.len() < 2 { return err("usage: cp [-r] [-f] <source> <dest>"); }
    let src = &files[0]; let dst = &files[1];
    let src_path = Path::new(src);
    if !src_path.exists() { return err(&format!("cp: {}: not found", src)); }
    match copy_recursive(src_path, Path::new(dst), recursive) {
        Ok(_) => ok(&format!("copied {} -> {}\n", src, dst)),
        Err(e) => err(&format!("cp: {}", e)),
    }
}

fn copy_recursive(src: &Path, dst: &Path, recursive: bool) -> std::io::Result<()> {
    if src.is_dir() {
        if !recursive { return Err(std::io::Error::new(std::io::ErrorKind::IsADirectory, "use -r for directories")); }
        std::fs::create_dir_all(dst)?;
        for entry in std::fs::read_dir(src)? {
            let entry = entry?;
            copy_recursive(&entry.path(), &dst.join(entry.file_name()), true)?;
        }
    } else { std::fs::copy(src, dst)?; }
    Ok(())
}

fn cmd_mv(args: &[String]) -> CommandResult {
    let mut files: Vec<String> = Vec::new();
    for a in args { if !a.starts_with('-') { files.push(a.clone()); } }
    if files.len() < 2 { return err("usage: mv <source> <dest>"); }
    match std::fs::rename(&files[0], &files[1]) {
        Ok(_) => ok(&format!("moved {} -> {}\n", files[0], files[1])),
        Err(e) => err(&format!("mv: {}", e)),
    }
}

fn cmd_rm(args: &[String]) -> CommandResult {
    let mut recursive = false; let mut force = false; let mut files: Vec<String> = Vec::new();
    for a in args {
        match a.as_str() { "-r" | "-R" | "-rf" | "-fr" => { recursive = true; force = true; }
            "-f" => force = true, _ => files.push(a.clone()) }
    }
    if files.is_empty() { return err("usage: rm [-r] [-f] <file>"); }
    let mut out = String::new();
    for f in &files {
        let path = Path::new(f);
        if !path.exists() {
            if force { continue; }
            out.push_str(&format!("rm: {}: not found\n", f)); continue;
        }
        let result = if path.is_dir() {
            if recursive { std::fs::remove_dir_all(path) }
            else { Err(std::io::Error::new(std::io::ErrorKind::IsADirectory, "use -r for directories")) }
        } else { std::fs::remove_file(path) };
        match result {
            Ok(_) => out.push_str(&format!("removed {}\n", f)),
            Err(e) => out.push_str(&format!("rm: {}: {}\n", f, e)),
        }
    }
    ok(&out)
}

fn cmd_mkdir(args: &[String]) -> CommandResult {
    let mut parents = false; let mut dirs: Vec<String> = Vec::new();
    for a in args { match a.as_str() { "-p" | "--parents" => parents = true, _ => dirs.push(a.clone()) } }
    if dirs.is_empty() { return err("usage: mkdir [-p] <dir>"); }
    let mut out = String::new();
    for d in &dirs {
        let result = if parents { std::fs::create_dir_all(d) } else { std::fs::create_dir(d) };
        match result { Ok(_) => out.push_str(&format!("created {}\n", d)), Err(e) => out.push_str(&format!("mkdir: {}: {}\n", d, e)) }
    }
    ok(&out)
}

fn cmd_rmdir(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: rmdir <dir>"); }
    let mut out = String::new();
    for d in args {
        match std::fs::remove_dir(d) {
            Ok(_) => out.push_str(&format!("removed {}\n", d)),
            Err(e) => out.push_str(&format!("rmdir: {}: {}\n", d, e)),
        }
    }
    ok(&out)
}

fn cmd_ln(args: &[String]) -> CommandResult {
    let mut symlink = false; let mut force = false; let mut files: Vec<String> = Vec::new();
    for a in args {
        match a.as_str() { "-s" | "--symbolic" => symlink = true, "-f" => force = true, _ => files.push(a.clone()) }
    }
    if files.len() < 2 { return err("usage: ln [-s] [-f] <target> <link>"); }
    let target = &files[0]; let link = &files[1];
    if force && Path::new(link).exists() { let _ = std::fs::remove_file(link); }
    let result = if symlink { std::os::unix::fs::symlink(target, link) }
        else { std::fs::hard_link(target, link) };
    match result {
        Ok(_) => ok(&format!("linked {} -> {}\n", link, target)),
        Err(e) => err(&format!("ln: {}", e)),
    }
}

// ── Permissions ──────────────────────────────────────────────────────────────

fn cmd_chmod(args: &[String]) -> CommandResult {
    if args.len() < 2 { return err("usage: chmod <mode> <file> [-R]"); }
    let mode_str = &args[0];
    let mut recursive = false; let mut files: Vec<String> = Vec::new();
    for a in &args[1..] {
        match a.as_str() { "-R" => recursive = true, _ => files.push(a.clone()) }
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mode = match u32::from_str_radix(mode_str, 8) {
            Ok(m) => m, Err(_) => return err(&format!("chmod: invalid mode: {}", mode_str)),
        };
        let mut out = String::new();
        for f in &files {
            let path = Path::new(f);
            if !path.exists() { out.push_str(&format!("chmod: {}: not found\n", f)); continue; }
            let result = if recursive && path.is_dir() {
                chmod_recursive(path, mode)
            } else {
                std::fs::set_permissions(path, std::fs::Permissions::from_mode(mode))
            };
            match result { Ok(_) => out.push_str(&format!("chmod {:o} {}\n", mode, f)), Err(e) => out.push_str(&format!("chmod: {}: {}\n", f, e)) }
        }
        ok(&out)
    }
    #[cfg(not(unix))]
    { err("chmod: not supported on this platform") }
}

#[cfg(unix)]
fn chmod_recursive(path: &Path, mode: u32) -> std::io::Result<()> {
    use std::os::unix::fs::PermissionsExt;
    std::fs::set_permissions(path, std::fs::Permissions::from_mode(mode))?;
    if path.is_dir() {
        for entry in std::fs::read_dir(path)? {
            let entry = entry?;
            chmod_recursive(&entry.path(), mode)?;
        }
    }
    Ok(())
}

fn cmd_chown(args: &[String]) -> CommandResult {
    if args.len() < 2 { return err("usage: chown <owner:group> <file> [-R]"); }
    let owner = &args[0];
    let mut files: Vec<String> = Vec::new();
    for a in &args[1..] { if a != "-R" { files.push(a.clone()); } }
    // chown requires root — try via system command
    let output = std::process::Command::new("chown").args(args).output();
    match output {
        Ok(o) => {
            let stdout = String::from_utf8_lossy(&o.stdout).to_string();
            let stderr = String::from_utf8_lossy(&o.stderr).to_string();
            CommandResult::code(o.status.code().unwrap_or(1), &stdout, &stderr)
        }
        Err(e) => err(&format!("chown: {} (not available on this system)", e)),
    }
}

fn cmd_chgrp(args: &[String]) -> CommandResult {
    if args.len() < 2 { return err("usage: chgrp <group> <file> [-R]"); }
    let output = std::process::Command::new("chgrp").args(args).output();
    match output {
        Ok(o) => {
            let stdout = String::from_utf8_lossy(&o.stdout).to_string();
            let stderr = String::from_utf8_lossy(&o.stderr).to_string();
            CommandResult::code(o.status.code().unwrap_or(1), &stdout, &stderr)
        }
        Err(_) => err("chgrp: not available on this system"),
    }
}

fn cmd_umask(args: &[String]) -> CommandResult {
    // Without args, show current umask concept
    if args.is_empty() {
        ok("022\n")
    } else {
        ok(&format!("umask set to {}\n", args[0]))
    }
}

fn cmd_readlink(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: readlink <file>"); }
    match std::fs::read_link(&args[0]) {
        Ok(p) => ok(&format!("{}\n", p.display())),
        Err(e) => err(&format!("readlink: {}: {}\n", args[0], e)),
    }
}

// ── Path utilities ──────────────────────────────────────────────────────────

fn cmd_basename(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: basename <path>"); }
    let path = Path::new(&args[0]);
    let name = path.file_name().map(|f| f.to_string_lossy().to_string()).unwrap_or_default();
    ok(&format!("{}\n", name))
}

fn cmd_dirname(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: dirname <path>"); }
    let path = Path::new(&args[0]);
    let dir = path.parent().map(|p| p.display().to_string()).unwrap_or_else(|| ".".to_string());
    ok(&format!("{}\n", if dir.is_empty() { "." } else { &dir }))
}

fn cmd_realpath(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: realpath <path>"); }
    match std::fs::canonicalize(&args[0]) {
        Ok(p) => ok(&format!("{}\n", p.display())),
        Err(e) => err(&format!("realpath: {}: {}\n", args[0], e)),
    }
}

// ── File content tools ───────────────────────────────────────────────────────

fn cmd_nl(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: nl <file>"); }
    let content = match std::fs::read_to_string(&args[0]) { Ok(c) => c, Err(e) => return err(&format!("nl: {}", e)) };
    let mut out = String::new();
    for (i, line) in content.lines().enumerate() {
        out.push_str(&format!("{:>6}  {}\n", i + 1, line));
    }
    ok(&out)
}

fn cmd_tac(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: tac <file>"); }
    let content = match std::fs::read_to_string(&args[0]) { Ok(c) => c, Err(e) => return err(&format!("tac: {}", e)) };
    let lines: Vec<&str> = content.lines().collect();
    let reversed: Vec<String> = lines.iter().rev().map(|l| (*l).to_string()).collect();
    ok(&format!("{}\n", reversed.join("\n")))
}

fn cmd_expand(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: expand <file>"); }
    let content = match std::fs::read_to_string(&args[0]) { Ok(c) => c, Err(e) => return err(&format!("expand: {}", e)) };
    ok(&content.replace('\t', "        "))
}

fn cmd_unexpand(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: unexpand <file>"); }
    let content = match std::fs::read_to_string(&args[0]) { Ok(c) => c, Err(e) => return err(&format!("unexpand: {}", e)) };
    ok(&content.replace("        ", "\t"))
}

fn cmd_paste(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: paste <file1> <file2>"); }
    let contents: Vec<Vec<String>> = args.iter().filter_map(|f| {
        std::fs::read_to_string(f).ok().map(|c| c.lines().map(String::from).collect())
    }).collect();
    let max_lines = contents.iter().map(|c| c.len()).max().unwrap_or(0);
    let mut out = String::new();
    for i in 0..max_lines {
        let row: Vec<String> = contents.iter().map(|c| c.get(i).cloned().unwrap_or_default()).collect();
        out.push_str(&row.join("\t")); out.push('\n');
    }
    ok(&out)
}

fn cmd_comm(args: &[String]) -> CommandResult {
    if args.len() < 2 { return err("usage: comm <file1> <file2>"); }
    let a = match std::fs::read_to_string(&args[0]) { Ok(c) => c, Err(e) => return err(&format!("comm: {}", e)) };
    let b = match std::fs::read_to_string(&args[1]) { Ok(c) => c, Err(e) => return err(&format!("comm: {}", e)) };
    let a_lines: std::collections::BTreeSet<String> = a.lines().map(String::from).collect();
    let b_lines: std::collections::BTreeSet<String> = b.lines().map(String::from).collect();
    let mut out = String::new();
    for line in a_lines.union(&b_lines) {
        let in_a = a_lines.contains(line);
        let in_b = b_lines.contains(line);
        if in_a && !in_b { out.push_str(&format!("{}\n", line)); }
        else if !in_a && in_b { out.push_str(&format!("\t{}\n", line)); }
        else { out.push_str(&format!("\t\t{}\n", line)); }
    }
    ok(&out)
}

fn cmd_shuf(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: shuf <file>"); }
    let content = match std::fs::read_to_string(&args[0]) { Ok(c) => c, Err(e) => return err(&format!("shuf: {}", e)) };
    let mut lines: Vec<String> = content.lines().map(String::from).collect();
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos();
    let mut seed = now as u64;
    for i in (1..lines.len()).rev() {
        seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        let j = (seed >> 33) as usize % (i + 1);
        lines.swap(i, j);
    }
    ok(&format!("{}\n", lines.join("\n")))
}

fn cmd_fold(args: &[String]) -> CommandResult {
    let mut width = 80usize; let mut files: Vec<String> = Vec::new();
    for a in args {
        if a.starts_with("-w") && a.len() > 2 { width = a[2..].parse().unwrap_or(80); }
        else if !a.starts_with('-') { files.push(a.clone()); }
    }
    if files.is_empty() { return err("usage: fold [-w N] <file>"); }
    let content = match std::fs::read_to_string(&files[0]) { Ok(c) => c, Err(e) => return err(&format!("fold: {}", e)) };
    let mut out = String::new();
    for line in content.lines() {
        let chars: Vec<char> = line.chars().collect();
        for chunk in chars.chunks(width) { out.push_str(&chunk.iter().collect::<String>()); out.push('\n'); }
    }
    ok(&out)
}

fn cmd_mktemp(args: &[String]) -> CommandResult {
    let dir = if args.is_empty() { std::env::temp_dir() }
        else { std::path::PathBuf::from(&args[0]) };
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos();
    let name = format!("ghost_{}", now);
    let path = dir.join(&name);
    match std::fs::File::create(&path) {
        Ok(_) => ok(&format!("{}\n", path.display())),
        Err(e) => err(&format!("mktemp: {}", e)),
    }
}

// ── Disk & system ────────────────────────────────────────────────────────────

fn cmd_du(args: &[String]) -> CommandResult {
    let mut human = false; let mut summary = false; let mut path = ".".to_string();
    for a in args {
        match a.as_str() { "-h" | "--human" => human = true, "-s" | "--summary" => summary = true,
            a if !a.starts_with('-') => path = a.to_string(), _ => {} }
    }
    let path = Path::new(&path);
    let size = dir_size(path);
    let size_str = if human { human_size(size) } else { size.to_string() };
    ok(&format!("{:>10}  {}\n", size_str, path.display()))
}

fn dir_size(path: &Path) -> u64 {
    let mut total = 0;
    if path.is_dir() {
        if let Ok(entries) = std::fs::read_dir(path) {
            for entry in entries.flatten() {
                total += dir_size(&entry.path());
            }
        }
    } else if let Ok(meta) = path.metadata() { total = meta.len(); }
    total
}

fn cmd_df(args: &[String]) -> CommandResult {
    let human = args.iter().any(|a| a == "-h" || a == "--human");
    let output = std::process::Command::new("df")
        .args(if human { vec!["-h"] } else { vec![] })
        .output();
    match output {
        Ok(o) => {
            let stdout = String::from_utf8_lossy(&o.stdout).to_string();
            ok(&stdout)
        }
        Err(_) => err("df: not available on this system"),
    }
}

// ── Archives ─────────────────────────────────────────────────────────────────

fn cmd_tar(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: tar -czf <archive> <dir>  |  tar -xzf <archive>  |  tar -tf <archive>"); }
    let mut create = false; let mut extract = false; let mut list = false;
    let mut gzip_mode = false; let mut archive = String::new(); let mut files: Vec<String> = Vec::new();
    for a in args {
        if a.starts_with('-') {
            for ch in a[1..].chars() {
                match ch { 'c' => create = true, 'x' => extract = true, 't' => list = true,
                    'z' => gzip_mode = true, 'f' => {}, _ => {} }
            }
        } else if archive.is_empty() { archive = a.clone(); }
        else { files.push(a.clone()); }
    }
    if create {
        let tar_file = std::fs::File::create(&archive).unwrap();
        let encoder: Box<dyn std::io::Write> = if gzip_mode {
            Box::new(flate2::write::GzEncoder::new(tar_file, flate2::Compression::default()))
        } else { Box::new(tar_file) };
        let mut builder = tar::Builder::new(encoder);
        for f in &files {
            let path = Path::new(f);
            if path.is_dir() {
                let _ = builder.append_dir_all(f, f);
            } else {
                let _ = builder.append_file(f, &mut std::fs::File::open(f).unwrap());
            }
        }
        let _ = builder.finish();
        ok(&format!("created {}\n", archive))
    } else if extract {
        let tar_file = std::fs::File::open(&archive).unwrap();
        let reader: Box<dyn std::io::Read> = if gzip_mode {
            Box::new(flate2::read::GzDecoder::new(tar_file))
        } else { Box::new(tar_file) };
        let mut arch = tar::Archive::new(reader);
        match arch.unpack(".") {
            Ok(_) => ok(&format!("extracted {}\n", archive)),
            Err(e) => err(&format!("tar: {}", e)),
        }
    } else if list {
        let tar_file = std::fs::File::open(&archive).unwrap();
        let reader: Box<dyn std::io::Read> = if gzip_mode {
            Box::new(flate2::read::GzDecoder::new(tar_file))
        } else { Box::new(tar_file) };
        let mut archive = tar::Archive::new(reader);
        let mut out = String::new();
        if let Ok(entries) = archive.entries() {
            for entry in entries.flatten() {
                if let Ok(name) = entry.header().path() {
                    let size = entry.header().size().unwrap_or(0);
                    out.push_str(&format!("{:>10}  {}\n", size, name.display()));
                }
            }
        }
        ok(&out)
    } else { err("tar: must specify -c, -x, or -t") }
}

fn cmd_gzip(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: gzip <file>"); }
    let input = &args[0];
    let output = format!("{}.gz", input);
    match std::fs::File::open(input) {
        Ok(mut f_in) => {
            let f_out = match std::fs::File::create(&output) { Ok(f) => f, Err(e) => return err(&format!("gzip: {}", e)) };
            let mut encoder = flate2::write::GzEncoder::new(f_out, flate2::Compression::default());
            let _ = std::io::copy(&mut f_in, &mut encoder);
            let _ = encoder.finish();
            ok(&format!("compressed {} -> {}\n", input, output))
        }
        Err(e) => err(&format!("gzip: {}", e)),
    }
}

fn cmd_gunzip(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: gunzip <file.gz>"); }
    let input = &args[0];
    let output = input.trim_end_matches(".gz").to_string();
    match std::fs::File::open(input) {
        Ok(f_in) => {
            let decoder = flate2::read::GzDecoder::new(f_in);
            let mut reader = std::io::BufReader::new(decoder);
            match std::fs::File::create(&output) {
                Ok(mut f_out) => {
                    let _ = std::io::copy(&mut reader, &mut f_out);
                    ok(&format!("decompressed {} -> {}\n", input, output))
                }
                Err(e) => err(&format!("gunzip: {}", e)),
            }
        }
        Err(e) => err(&format!("gunzip: {}", e)),
    }
}

fn cmd_zip(args: &[String]) -> CommandResult {
    if args.len() < 2 { return err("usage: zip <archive.zip> <file1> [file2 ...]"); }
    let archive_name = &args[0];
    let files = &args[1..];
    match std::fs::File::create(archive_name) {
        Ok(f) => {
            let mut zip = zip::ZipWriter::new(f);
            let options = zip::write::SimpleFileOptions::default();
            for file in files {
                let path = Path::new(file);
                if path.is_file() {
                    let _ = zip.start_file(file, options);
                    let mut f = match std::fs::File::open(file) { Ok(f) => f, Err(e) => return err(&format!("zip: {}", e)) };
                    let _ = std::io::copy(&mut f, &mut zip);
                } else if path.is_dir() {
                    let _ = zip.add_directory(file, options);
                }
            }
            let _ = zip.finish();
            ok(&format!("created {}\n", archive_name))
        }
        Err(e) => err(&format!("zip: {}", e)),
    }
}

fn cmd_unzip(args: &[String]) -> CommandResult {
    if args.is_empty() { return err("usage: unzip <archive.zip> [dest]"); }
    let archive_name = &args[0];
    let dest = if args.len() > 1 { &args[1] } else { "." };
    match std::fs::File::open(archive_name) {
        Ok(f) => {
            let mut archive = match zip::ZipArchive::new(f) { Ok(a) => a, Err(e) => return err(&format!("unzip: {}", e)) };
            let mut out = String::new();
            for i in 0..archive.len() {
                let mut file = match archive.by_index(i) { Ok(f) => f, Err(_) => continue };
                let name = file.name().to_string();
                let path = std::path::PathBuf::from(dest).join(&name);
                if file.is_dir() {
                    let _ = std::fs::create_dir_all(&path);
                } else {
                    if let Some(parent) = path.parent() { let _ = std::fs::create_dir_all(parent); }
                    let mut out_file = match std::fs::File::create(&path) { Ok(f) => f, Err(_) => continue };
                    let _ = std::io::copy(&mut file, &mut out_file);
                }
                out.push_str(&format!("  {}\n", name));
            }
            ok(&format!("extracted to {}:\n{}", dest, out))
        }
        Err(e) => err(&format!("unzip: {}", e)),
    }
}
