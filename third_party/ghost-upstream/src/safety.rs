const DANGEROUS_PATTERNS: &[&str] = &[
    "rm -rf /", "rm -rf ~", "rm -rf /*", "rm -rf *", "rm -rf .",
    "rm -rf ..", "rm -rf ~/", "dd if=/dev/zero", "dd if=/dev/random",
    "dd of=/dev/sd", "dd of=/dev/nvme", "dd of=/dev/disk",
    "mkfs", "shutdown", "reboot", "halt", "poweroff",
    ":(){:|:&};:", "fork bomb", "> /dev/sd", "> /dev/nvme",
    "chmod -R 777 /", "chmod -R 000 /", "chown -R",
    "kill -9 -1", "iptables -F",
];

pub struct SafetyCheck {
    pub is_dangerous: bool,
    pub reason: String,
    pub changes: Vec<String>,
}

pub fn check_safety(input: &str) -> SafetyCheck {
    let normalized = input.trim().to_lowercase();
    let mut changes: Vec<String> = Vec::new();

    for pattern in DANGEROUS_PATTERNS {
        if normalized.contains(pattern) {
            return SafetyCheck {
                is_dangerous: true,
                reason: format!("Dangerous pattern: '{}'", pattern),
                changes,
            };
        }
    }

    // rm with -rf and broad paths
    if normalized.contains("rm ") && (normalized.contains("-rf") || normalized.contains("-r -f") || normalized.contains("-fr")) {
        let parts: Vec<&str> = input.split_whitespace().collect();
        let mut targets: Vec<&str> = Vec::new();
        let mut skip_flags = true;
        for p in &parts[1..] {
            if skip_flags && p.starts_with('-') { continue; }
            skip_flags = false;
            targets.push(p);
        }
        for target in &targets {
            let path = std::path::Path::new(target);
            if path.exists() {
                if path.is_dir() {
                    enumerate_dir_deletions(path, &mut changes, 50);
                } else {
                    changes.push(format!("delete: {}", target));
                }
            } else {
                changes.push(format!("delete (not found): {}", target));
            }
        }
        if !changes.is_empty() {
            let total = changes.len();
            return SafetyCheck {
                is_dangerous: true,
                reason: format!("Will delete {} item(s) — {} shown", total, changes.len().min(50)),
                changes,
            };
        }
    }

    // rm without -rf but targeting files
    if normalized.starts_with("rm ") && !normalized.contains("-rf") {
        let parts: Vec<&str> = input.split_whitespace().collect();
        for p in &parts[1..] {
            if p.starts_with('-') { continue; }
            let path = std::path::Path::new(p);
            if path.exists() && !path.is_dir() {
                changes.push(format!("delete: {}", p));
            }
        }
        if !changes.is_empty() {
            return SafetyCheck {
                is_dangerous: true,
                reason: format!("Will delete {} file(s)", changes.len()),
                changes,
            };
        }
    }

    // mv that would overwrite
    if normalized.starts_with("mv ") {
        let parts: Vec<&str> = input.split_whitespace().collect();
        let files: Vec<&str> = parts[1..].iter().filter(|p| !p.starts_with('-')).copied().collect();
        if files.len() >= 2 {
            let dest = std::path::Path::new(files[files.len() - 1]);
            if dest.exists() {
                let sources = &files[..files.len() - 1];
                for src in sources {
                    changes.push(format!("move: {} -> {} (overwrites)", src, dest.display()));
                }
                return SafetyCheck {
                    is_dangerous: true,
                    reason: format!("Will overwrite {} with {} file(s)", dest.display(), sources.len()),
                    changes,
                };
            }
        }
    }

    // cp that would overwrite
    if normalized.starts_with("cp ") || normalized.starts_with("copy ") {
        let parts: Vec<&str> = input.split_whitespace().collect();
        let files: Vec<&str> = parts[1..].iter().filter(|p| !p.starts_with('-')).copied().collect();
        if files.len() >= 2 {
            let dest = std::path::Path::new(files[files.len() - 1]);
            if dest.exists() && dest.is_file() {
                changes.push(format!("copy: {} -> {} (overwrites)", files[0], dest.display()));
                return SafetyCheck {
                    is_dangerous: true,
                    reason: format!("Will overwrite {}", dest.display()),
                    changes,
                };
            }
        }
    }

    // chmod -R (recursive permission change)
    if normalized.starts_with("chmod ") && normalized.contains("-r") {
        let parts: Vec<&str> = input.split_whitespace().collect();
        let mode = parts.get(1).unwrap_or(&"?");
        for p in &parts[2..] {
            if p.starts_with('-') { continue; }
            let path = std::path::Path::new(p);
            if path.exists() {
                changes.push(format!("chmod {} {}", mode, p));
                if path.is_dir() {
                    enumerate_dir_chmod(path, mode, &mut changes, 50);
                }
            }
        }
        if !changes.is_empty() {
            let total = changes.len();
            return SafetyCheck {
                is_dangerous: true,
                reason: format!("Will change permissions on {} item(s) — {} shown", total, changes.len().min(50)),
                changes,
            };
        }
    }

    // sudo
    if normalized.contains("sudo ") {
        return SafetyCheck {
            is_dangerous: true,
            reason: "Running with elevated privileges (sudo)".to_string(),
            changes,
        };
    }

    SafetyCheck { is_dangerous: false, reason: String::new(), changes }
}

fn enumerate_dir_deletions(path: &std::path::Path, changes: &mut Vec<String>, limit: usize) {
    if changes.len() >= limit { return; }
    if let Ok(entries) = std::fs::read_dir(path) {
        for entry in entries.flatten() {
            if changes.len() >= limit { break; }
            let p = entry.path();
            let p_str = p.display().to_string();
            if p.is_dir() {
                changes.push(format!("delete dir: {}/", p_str));
                enumerate_dir_deletions(&p, changes, limit);
            } else {
                changes.push(format!("delete: {}", p_str));
            }
        }
    }
}

fn enumerate_dir_chmod(path: &std::path::Path, mode: &str, changes: &mut Vec<String>, limit: usize) {
    if changes.len() >= limit { return; }
    if let Ok(entries) = std::fs::read_dir(path) {
        for entry in entries.flatten() {
            if changes.len() >= limit { break; }
            let p = entry.path();
            changes.push(format!("chmod {} {}", mode, p.display()));
            if p.is_dir() {
                enumerate_dir_chmod(&p, mode, changes, limit);
            }
        }
    }
}
