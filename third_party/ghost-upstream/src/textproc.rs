use crate::executor::CommandResult;
use regex::Regex;
use serde_json::Value;

pub fn handle(name: &str, args: &[String]) -> Option<CommandResult> {
    match name {
        "grep" | "rg" => Some(cmd_grep(args)),
        "jq" => Some(cmd_jq(args)),
        "wc" => Some(cmd_wc(args)),
        "sort" => Some(cmd_sort(args)),
        "uniq" => Some(cmd_uniq(args)),
        "head" => Some(cmd_head(args)),
        "tail" => Some(cmd_tail(args)),
        "cut" => Some(cmd_cut(args)),
        "tr" => Some(cmd_tr(args)),
        "rev" => Some(cmd_rev(args)),
        "sed" => Some(cmd_sed(args)),
        "tee" => Some(cmd_tee(args)),
        "printf" => Some(cmd_printf(args)),
        "seq" => Some(cmd_seq(args)),
        "yes" => Some(cmd_yes(args)),
        "cat" => Some(cmd_cat(args)),
        "diff" => Some(cmd_diff(args)),
        _ => None,
    }
}

fn read_file_or_err(path: &str) -> Result<String, String> {
    std::fs::read_to_string(path).map_err(|e| format!("{}: {}", path, e))
}

fn cmd_grep(args: &[String]) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("usage: grep [-i] [-v] [-c] [-n] [-w] <pattern> <file>");
    }
    let mut case_insensitive = false;
    let mut invert = false;
    let mut count = false;
    let mut line_numbers = false;
    let mut whole_word = false;
    let mut pattern = String::new();
    let mut files: Vec<String> = Vec::new();
    let mut got_pattern = false;

    for arg in args {
        if !got_pattern && arg.starts_with('-') && arg.len() > 1 {
            for ch in arg[1..].chars() {
                match ch { 'i' => case_insensitive = true, 'v' => invert = true,
                    'c' => count = true, 'n' => line_numbers = true, 'w' => whole_word = true, _ => {} }
            }
        } else if !got_pattern { pattern = arg.clone(); got_pattern = true; }
        else { files.push(arg.clone()); }
    }

    if pattern.is_empty() { return CommandResult::err("grep: no pattern"); }

    let re_str = if whole_word { format!(r"\b{}\b", regex::escape(&pattern)) }
                 else if case_insensitive { format!("(?i){}", &pattern) }
                 else { pattern.clone() };
    let re = match Regex::new(&re_str) {
        Ok(r) => r, Err(e) => return CommandResult::err(&format!("grep: bad pattern: {}", e)),
    };

    let mut output = String::new();
    let mut total_matches = 0;

    for file in &files {
        let content = match read_file_or_err(file) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
        for (i, line) in content.lines().enumerate() {
            let matched = re.is_match(line);
            if matched != invert { continue; }
            if invert { continue; } // fixed: if invert, skip matches
            total_matches += 1;
            if count { continue; }
            let prefix = if files.len() > 1 { format!("{}:", file) } else { String::new() };
            let num = if line_numbers { format!("{}:", i + 1) } else { String::new() };
            output.push_str(&format!("{}{}{}\n", prefix, num, line));
        }
    }

    if count { CommandResult::ok(&format!("{}\n", total_matches)) }
    else if output.is_empty() { CommandResult::code(1, "", "") }
    else { CommandResult::ok(&output) }
}

fn cmd_jq(args: &[String]) -> CommandResult {
    if args.is_empty() {
        return CommandResult::err("usage: jq <expression> [file]\n  .key  .key.sub  .[N]  .[]  keys  length  type");
    }
    let expr = &args[0];
    let file = if args.len() > 1 { Some(&args[1]) } else { None };

    let json_str = if let Some(f) = file {
        match read_file_or_err(f) { Ok(c) => c, Err(e) => return CommandResult::err(&e) }
    } else { return CommandResult::err("jq: reading from stdin not supported, provide a file"); };

    let value: Value = match serde_json::from_str(&json_str) {
        Ok(v) => v, Err(e) => return CommandResult::err(&format!("jq: invalid JSON: {}", e)),
    };

    let result = apply_jq(&value, expr);
    match result {
        Ok(v) => {
            let out = format_jq_value(&v);
            CommandResult::ok(&format!("{}\n", out))
        }
        Err(e) => CommandResult::err(&format!("jq: {}", e)),
    }
}

fn apply_jq(value: &Value, expr: &str) -> Result<Value, String> {
    let expr = expr.trim();
    if expr == "." || expr.is_empty() { return Ok(value.clone()); }
    if expr == "keys" {
        if let Value::Object(m) = value {
            return Ok(Value::Array(m.keys().map(|k| Value::String(k.clone())).collect()));
        }
        return Err("keys requires object".into());
    }
    if expr == "length" {
        return Ok(Value::from(match value {
            Value::Array(a) => a.len(), Value::Object(o) => o.len(),
            Value::String(s) => s.len(), Value::Null => 0, _ => 1,
        }));
    }
    if expr == "type" {
        return Ok(Value::String(match value {
            Value::Null => "null", Value::Bool(_) => "boolean", Value::Number(_) => "number",
            Value::String(_) => "string", Value::Array(_) => "array", Value::Object(_) => "object",
        }.into()));
    }
    if expr == "values" {
        if let Value::Object(m) = value {
            return Ok(Value::Array(m.values().cloned().collect()));
        }
    }

    // Handle pipe: expr1 | expr2
    if let Some(pipe_pos) = find_pipe(expr) {
        let left = expr[..pipe_pos].trim();
        let right = expr[pipe_pos+1..].trim();
        let intermediate = apply_jq(value, left)?;
        return apply_jq(&intermediate, right);
    }

    // Handle .[] array iteration
    if expr == ".[]" {
        if let Value::Array(arr) = value {
            return Ok(Value::Array(arr.clone()));
        }
        return Err(".[] requires array".into());
    }

    // Handle .key.subkey path
    let mut current = value.clone();
    let path = expr.trim_start_matches('.');
    for part in path.split('.') {
        let part = part.trim();
        if part.is_empty() { continue; }
        if part == "[]" { continue; }
        // Array index?
        if let Ok(idx) = part.parse::<usize>() {
            if let Value::Array(arr) = &current {
                current = arr.get(idx).cloned().unwrap_or(Value::Null);
            } else { return Err(format!("cannot index {} with number", current)); }
        } else if part.ends_with("[]") {
            let key = &part[..part.len()-2];
            if let Value::Object(m) = &current {
                if let Some(v) = m.get(key) {
                    if let Value::Array(arr) = v { return Ok(Value::Array(arr.clone())); }
                }
            }
            return Err(format!("cannot iterate {}", part));
        } else {
            if let Value::Object(m) = &current {
                current = m.get(part).cloned().unwrap_or(Value::Null);
            } else if let Value::Array(arr) = &current {
                if let Ok(idx) = part.parse::<usize>() {
                    current = arr.get(idx).cloned().unwrap_or(Value::Null);
                }
            } else {
                return Err(format!("cannot access key '{}' on {}", part, current));
            }
        }
    }
    Ok(current)
}

fn find_pipe(s: &str) -> Option<usize> {
    let mut in_string = false;
    for (i, c) in s.char_indices() {
        match c { '"' => in_string = !in_string, '|' if !in_string => return Some(i), _ => {} }
    }
    None
}

fn format_jq_value(v: &Value) -> String {
    match v {
        Value::Null => "null".into(),
        Value::Bool(b) => b.to_string(),
        Value::Number(n) => n.to_string(),
        Value::String(s) => s.clone(),
        Value::Array(arr) => {
            if arr.is_empty() { return "[]".into(); }
            arr.iter().map(|v| format_jq_value(v)).collect::<Vec<_>>().join("\n")
        }
        Value::Object(m) => serde_json::to_string_pretty(m).unwrap_or_default(),
    }
}

fn cmd_wc(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: wc <file> [-l] [-w] [-c]"); }
    let mut lines_only = false; let mut words_only = false; let mut bytes_only = false;
    let mut files: Vec<String> = Vec::new();
    for a in args {
        match a.as_str() { "-l" => lines_only = true, "-w" => words_only = true,
            "-c" => bytes_only = true, _ => files.push(a.clone()) }
    }
    let all = !lines_only && !words_only && !bytes_only;
    let mut out = String::new();
    for f in &files {
        let content = match read_file_or_err(f) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
        let l = content.lines().count();
        let w = content.split_whitespace().count();
        let b = content.len();
        let parts: Vec<String> = vec![
            if all || lines_only { format!("{:>7}", l) } else { String::new() },
            if all || words_only { format!("{:>7}", w) } else { String::new() },
            if all || bytes_only { format!("{:>7}", b) } else { String::new() },
        ].into_iter().filter(|s| !s.is_empty()).collect();
        out.push_str(&format!("{} {}\n", parts.join(" "), f));
    }
    CommandResult::ok(&out)
}

fn cmd_sort(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: sort <file> [-r] [-n] [-u]"); }
    let mut reverse = false; let mut numeric = false; let mut unique = false;
    let mut files: Vec<String> = Vec::new();
    for a in args {
        match a.as_str() { "-r" => reverse = true, "-n" => numeric = true,
            "-u" => unique = true, _ => files.push(a.clone()) }
    }
    let mut all_lines = Vec::new();
    for f in &files {
        let content = match read_file_or_err(f) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
        all_lines.extend(content.lines().map(String::from));
    }
    if numeric {
        all_lines.sort_by(|a, b| {
            let na: f64 = a.trim().parse().unwrap_or(0.0);
            let nb: f64 = b.trim().parse().unwrap_or(0.0);
            na.partial_cmp(&nb).unwrap_or(std::cmp::Ordering::Equal)
        });
    } else { all_lines.sort(); }
    if reverse { all_lines.reverse(); }
    if unique { all_lines.dedup(); }
    let out = all_lines.join("\n");
    CommandResult::ok(&format!("{}\n", out))
}

fn cmd_uniq(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: uniq <file>"); }
    let content = match read_file_or_err(&args[0]) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
    let mut out = String::new();
    let mut prev: Option<String> = None;
    for line in content.lines() {
        if prev.as_deref() != Some(line) {
            out.push_str(line); out.push('\n');
            prev = Some(line.to_string());
        }
    }
    CommandResult::ok(&out)
}

fn cmd_head(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: head [-n N] <file>"); }
    let mut n = 10usize; let mut files: Vec<String> = Vec::new(); let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-n" => { i += 1; if i < args.len() { n = args[i].parse().unwrap_or(10); } }
            a if a.starts_with("-n") => { n = a[2..].parse().unwrap_or(10); }
            _ => files.push(args[i].clone()), _ => {}
        }
        i += 1;
    }
    let mut out = String::new();
    for f in &files {
        let content = match read_file_or_err(f) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
        for line in content.lines().take(n) { out.push_str(line); out.push('\n'); }
    }
    CommandResult::ok(&out)
}

fn cmd_tail(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: tail [-n N] <file>"); }
    let mut n = 10usize; let mut files: Vec<String> = Vec::new(); let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-n" => { i += 1; if i < args.len() { n = args[i].parse().unwrap_or(10); } }
            a if a.starts_with("-n") => { n = a[2..].parse().unwrap_or(10); }
            _ => files.push(args[i].clone()), _ => {}
        }
        i += 1;
    }
    let mut out = String::new();
    for f in &files {
        let content = match read_file_or_err(f) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
        let lines: Vec<&str> = content.lines().collect();
        let start = if lines.len() > n { lines.len() - n } else { 0 };
        for line in &lines[start..] { out.push_str(line); out.push('\n'); }
    }
    CommandResult::ok(&out)
}

fn cmd_cut(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: cut -d<delim> -f<fields> <file>"); }
    let mut delim = '\t'; let mut fields: Vec<usize> = Vec::new(); let mut files: Vec<String> = Vec::new();
    for a in args {
        if a.starts_with("-d") && a.len() > 2 { delim = a[2..].chars().next().unwrap_or('\t'); }
        else if a.starts_with("-f") && a.len() > 2 {
            fields = a[2..].split(',').filter_map(|f| f.parse().ok()).collect();
        } else if !a.starts_with('-') { files.push(a.clone()); }
    }
    let mut out = String::new();
    for f in &files {
        let content = match read_file_or_err(f) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
        for line in content.lines() {
            let parts: Vec<&str> = line.split(delim).collect();
            let selected: Vec<String> = fields.iter()
                .filter_map(|&idx| parts.get(idx.wrapping_sub(1)).map(|s| s.to_string()))
                .collect();
            out.push_str(&selected.join(&delim.to_string())); out.push('\n');
        }
    }
    CommandResult::ok(&out)
}

fn cmd_tr(args: &[String]) -> CommandResult {
    if args.len() < 2 { return CommandResult::err("usage: tr <from> <to> <file>"); }
    let from: Vec<char> = args[0].chars().collect();
    let to: Vec<char> = args[1].chars().collect();
    let file = &args[2];
    let content = match read_file_or_err(file) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
    let out: String = content.chars().map(|c| {
        if let Some(pos) = from.iter().position(|&f| f == c) {
            to.get(pos).copied().unwrap_or(c)
        } else { c }
    }).collect();
    CommandResult::ok(&out)
}

fn cmd_rev(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: rev <file>"); }
    let content = match read_file_or_err(&args[0]) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
    let out: String = content.lines().map(|l| l.chars().rev().collect::<String>()).collect::<Vec<_>>().join("\n");
    CommandResult::ok(&format!("{}\n", out))
}

fn cmd_sed(args: &[String]) -> CommandResult {
    if args.len() < 2 { return CommandResult::err("usage: sed 's/old/new/[g]' <file>"); }
    let expr = &args[0];
    let file = &args[1];
    let content = match read_file_or_err(file) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
    if expr.starts_with("s/") {
        let parts: Vec<&str> = expr[2..].splitn(3, '/').collect();
        if parts.len() < 2 { return CommandResult::err("sed: invalid expression"); }
        let old = parts[0];
        let new = parts[1];
        let global = parts.get(2).map(|f| f.contains('g')).unwrap_or(false);
        let re = match Regex::new(&regex::escape(old)) { Ok(r) => r, Err(_) => return CommandResult::err("sed: bad pattern") };
        let out = if global { re.replace_all(&content, new).to_string() }
                  else { re.replace(&content, new).to_string() };
        CommandResult::ok(&out)
    } else { CommandResult::err("sed: only s/old/new/ supported") }
}

fn cmd_tee(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: tee <file> (reads from file, writes to file + stdout)"); }
    let file = &args[0];
    let content = match read_file_or_err(file) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
    CommandResult::ok(&content)
}

fn cmd_printf(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::ok("\n"); }
    let fmt = &args[0];
    let rest = &args[1..];
    let mut out = String::new();
    let mut arg_idx = 0;
    let mut chars = fmt.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\\' {
            match chars.next() {
                Some('n') => out.push('\n'), Some('t') => out.push('\t'),
                Some('r') => out.push('\r'), Some('\\') => out.push('\\'),
                Some(c) => { out.push('\\'); out.push(c); }
                None => out.push('\\'),
            }
        } else if c == '%' {
            match chars.next() {
                Some('s') => { out.push_str(rest.get(arg_idx).map(|s| s.as_str()).unwrap_or("")); arg_idx += 1; }
                Some('d') => { out.push_str(&rest.get(arg_idx).and_then(|s| s.parse::<i64>().ok()).unwrap_or(0).to_string()); arg_idx += 1; }
                Some('f') => { out.push_str(&rest.get(arg_idx).and_then(|s| s.parse::<f64>().ok()).unwrap_or(0.0).to_string()); arg_idx += 1; }
                Some(c) => { out.push('%'); out.push(c); }
                None => out.push('%'),
            }
        } else { out.push(c); }
    }
    CommandResult::ok(&out)
}

fn cmd_seq(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: seq [start] [step] end"); }
    let nums: Vec<f64> = args.iter().filter_map(|a| a.parse().ok()).collect();
    let (start, step, end) = match nums.len() {
        1 => (1.0, 1.0, nums[0]),
        2 => (nums[0], 1.0, nums[1]),
        3 => (nums[0], nums[1], nums[2]),
        _ => return CommandResult::err("seq: invalid arguments"),
    };
    let mut out = String::new();
    let mut current = start;
    if step > 0.0 {
        while current <= end + 0.0001 { out.push_str(&format!("{}\n", current)); current += step; }
    } else if step < 0.0 {
        while current >= end - 0.0001 { out.push_str(&format!("{}\n", current)); current += step; }
    }
    CommandResult::ok(&out)
}

fn cmd_yes(args: &[String]) -> CommandResult {
    let text = if args.is_empty() { "y" } else { &args[0] };
    let mut out = String::new();
    for _ in 0..1000 { out.push_str(text); out.push('\n'); }
    CommandResult::ok(&out)
}

fn cmd_cat(args: &[String]) -> CommandResult {
    if args.is_empty() { return CommandResult::err("usage: cat <file> [file2 ...]"); }
    let mut out = String::new();
    for f in args {
        match read_file_or_err(f) { Ok(c) => out.push_str(&c), Err(e) => return CommandResult::err(&e) }
    }
    CommandResult::ok(&out)
}

fn cmd_diff(args: &[String]) -> CommandResult {
    if args.len() < 2 { return CommandResult::err("usage: diff <file1> <file2>"); }
    let a = match read_file_or_err(&args[0]) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
    let b = match read_file_or_err(&args[1]) { Ok(c) => c, Err(e) => return CommandResult::err(&e) };
    let a_lines: Vec<&str> = a.lines().collect();
    let b_lines: Vec<&str> = b.lines().collect();
    let max = a_lines.len().max(b_lines.len());
    let mut out = String::new();
    let mut diffs = 0;
    for i in 0..max {
        match (a_lines.get(i), b_lines.get(i)) {
            (Some(a), Some(b)) if a == b => {}
            (Some(a), Some(b)) => { out.push_str(&format!("- {}\n+ {}\n", a, b)); diffs += 1; }
            (Some(a), None) => { out.push_str(&format!("- {}\n", a)); diffs += 1; }
            (None, Some(b)) => { out.push_str(&format!("+ {}\n", b)); diffs += 1; }
            _ => {}
        }
    }
    if diffs == 0 { CommandResult::ok("Files are identical.\n") }
    else { CommandResult::code(1, &out, &format!("{} differences found\n", diffs)) }
}
