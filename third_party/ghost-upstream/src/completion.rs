use std::collections::HashSet;
use std::path::PathBuf;

pub fn get_completions(prefix: &str, builtins: &[&str]) -> Vec<String> {
    if prefix.is_empty() {
        return Vec::new();
    }

    let mut results: HashSet<String> = HashSet::new();

    for b in builtins {
        if b.starts_with(prefix) {
            results.insert(b.to_string());
        }
    }

    let path_dirs: Vec<PathBuf> = std::env::var("PATH")
        .unwrap_or_default()
        .split(if cfg!(windows) { ';' } else { ':' })
        .map(PathBuf::from)
        .collect();

    for dir in path_dirs {
        if let Ok(entries) = std::fs::read_dir(&dir) {
            for entry in entries.flatten() {
                if let Ok(name) = entry.file_name().into_string() {
                    if name.starts_with(prefix) {
                        let full_path = entry.path();
                        if is_executable(&full_path) {
                            results.insert(name);
                        }
                    }
                }
            }
        }
    }

    let mut sorted: Vec<String> = results.into_iter().collect();
    sorted.sort();
    sorted
}

#[cfg(unix)]
fn is_executable(path: &std::path::Path) -> bool {
    use std::os::unix::fs::PermissionsExt;
    if let Ok(meta) = std::fs::metadata(path) {
        meta.permissions().mode() & 0o111 != 0
    } else {
        false
    }
}

#[cfg(windows)]
fn is_executable(path: &std::path::Path) -> bool {
    path.extension()
        .map(|ext| ext == "exe" || ext == "bat" || ext == "cmd")
        .unwrap_or(false)
}

pub fn common_prefix(completions: &[String]) -> Option<String> {
    if completions.is_empty() {
        return None;
    }
    if completions.len() == 1 {
        return Some(completions[0].clone());
    }
    let first = &completions[0];
    let mut prefix_len = first.len();
    for c in &completions[1..] {
        prefix_len = prefix_len.min(c.len());
        for i in 0..prefix_len {
            if first.as_bytes()[i] != c.as_bytes()[i] {
                prefix_len = i;
                break;
            }
        }
    }
    if prefix_len > 0 {
        Some(first[..prefix_len].to_string())
    } else {
        None
    }
}
