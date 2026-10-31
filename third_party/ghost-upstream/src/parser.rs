use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq)]
pub enum Token {
    Word(String),
    Pipe,
    And,
    Or,
    Semi,
    RedirectIn,
    RedirectOut,
    RedirectAppend,
    Background,
}

pub fn tokenize(input: &str) -> Result<Vec<Token>, String> {
    let chars: Vec<char> = input.chars().collect();
    let mut tokens = Vec::new();
    let mut i = 0;

    while i < chars.len() {
        let c = chars[i];

        if c.is_whitespace() {
            i += 1;
            continue;
        }

        match c {
            '|' => {
                if i + 1 < chars.len() && chars[i + 1] == '|' {
                    tokens.push(Token::Or);
                    i += 2;
                } else {
                    tokens.push(Token::Pipe);
                    i += 1;
                }
            }
            '&' => {
                if i + 1 < chars.len() && chars[i + 1] == '&' {
                    tokens.push(Token::And);
                    i += 2;
                } else {
                    tokens.push(Token::Background);
                    i += 1;
                }
            }
            ';' => {
                tokens.push(Token::Semi);
                i += 1;
            }
            '>' => {
                if i + 1 < chars.len() && chars[i + 1] == '>' {
                    tokens.push(Token::RedirectAppend);
                    i += 2;
                } else {
                    tokens.push(Token::RedirectOut);
                    i += 1;
                }
            }
            '<' => {
                tokens.push(Token::RedirectIn);
                i += 1;
            }
            '"' | '\'' => {
                let quote = c;
                i += 1;
                let start = i;
                while i < chars.len() && chars[i] != quote {
                    i += 1;
                }
                if i >= chars.len() {
                    return Err(format!("unterminated quote: {}", quote));
                }
                tokens.push(Token::Word(chars[start..i].iter().collect()));
                i += 1;
            }
            _ => {
                let start = i;
                while i < chars.len()
                    && !chars[i].is_whitespace()
                    && chars[i] != '|'
                    && chars[i] != '&'
                    && chars[i] != ';'
                    && chars[i] != '>'
                    && chars[i] != '<'
                {
                    i += 1;
                }
                tokens.push(Token::Word(chars[start..i].iter().collect()));
            }
        }
    }

    Ok(tokens)
}

#[derive(Debug, Clone)]
pub struct ParsedCommand {
    pub program: String,
    pub args: Vec<String>,
    pub stdin_file: Option<String>,
    pub stdout_file: Option<String>,
    pub append: bool,
}

#[derive(Debug, Clone)]
pub struct Pipeline {
    pub commands: Vec<ParsedCommand>,
    pub background: bool,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ChainOp {
    None,
    And,
    Or,
    Semi,
}

#[derive(Debug, Clone)]
pub struct Statement {
    pub pipeline: Pipeline,
    pub op: ChainOp,
}

#[derive(Debug)]
pub struct Program {
    pub statements: Vec<Statement>,
}

fn parse_command(tokens: &[Token], start: &mut usize) -> Result<ParsedCommand, String> {
    let mut program: Option<String> = None;
    let mut args = Vec::new();
    let mut stdin_file = None;
    let mut stdout_file = None;
    let mut append = false;

    while *start < tokens.len() {
        match &tokens[*start] {
            Token::Word(w) => {
                if program.is_none() {
                    program = Some(w.clone());
                } else {
                    args.push(w.clone());
                }
                *start += 1;
            }
            Token::RedirectIn => {
                *start += 1;
                if *start >= tokens.len() {
                    return Err("expected filename after <".into());
                }
                if let Token::Word(w) = &tokens[*start] {
                    stdin_file = Some(w.clone());
                    *start += 1;
                } else {
                    return Err("expected filename after <".into());
                }
            }
            Token::RedirectOut => {
                *start += 1;
                if *start >= tokens.len() {
                    return Err("expected filename after >".into());
                }
                if let Token::Word(w) = &tokens[*start] {
                    stdout_file = Some(w.clone());
                    append = false;
                    *start += 1;
                } else {
                    return Err("expected filename after >".into());
                }
            }
            Token::RedirectAppend => {
                *start += 1;
                if *start >= tokens.len() {
                    return Err("expected filename after >>".into());
                }
                if let Token::Word(w) = &tokens[*start] {
                    stdout_file = Some(w.clone());
                    append = true;
                    *start += 1;
                } else {
                    return Err("expected filename after >>".into());
                }
            }
            _ => break,
        }
    }

    let program = program.ok_or_else(|| "empty command".to_string())?;
    Ok(ParsedCommand {
        program,
        args,
        stdin_file,
        stdout_file,
        append,
    })
}

fn parse_pipeline(tokens: &[Token], start: &mut usize) -> Result<Pipeline, String> {
    let mut commands = Vec::new();
    let mut background = false;

    loop {
        let cmd = parse_command(tokens, start)?;
        commands.push(cmd);

        if *start >= tokens.len() {
            break;
        }

        match &tokens[*start] {
            Token::Pipe => {
                *start += 1;
            }
            Token::Background => {
                background = true;
                *start += 1;
                break;
            }
            _ => break,
        }
    }

    Ok(Pipeline {
        commands,
        background,
    })
}

pub fn parse(input: &str) -> Result<Program, String> {
    let tokens = tokenize(input)?;
    let mut statements = Vec::new();
    let mut i = 0;

    while i < tokens.len() {
        let pipeline = parse_pipeline(&tokens, &mut i)?;
        let op = if i < tokens.len() {
            match &tokens[i] {
                Token::And => {
                    i += 1;
                    ChainOp::And
                }
                Token::Or => {
                    i += 1;
                    ChainOp::Or
                }
                Token::Semi => {
                    i += 1;
                    ChainOp::Semi
                }
                _ => ChainOp::None,
            }
        } else {
            ChainOp::None
        };

        statements.push(Statement { pipeline, op });
    }

    Ok(Program { statements })
}

pub fn expand_env(arg: &str, env: &HashMap<String, String>) -> String {
    let mut result = String::new();
    let chars: Vec<char> = arg.chars().collect();
    let mut i = 0;

    while i < chars.len() {
        if chars[i] == '$' && i + 1 < chars.len() {
            if chars[i + 1] == '{' {
                let mut j = i + 2;
                while j < chars.len() && chars[j] != '}' {
                    j += 1;
                }
                if j < chars.len() {
                    let var_name: String = chars[i + 2..j].iter().collect();
                    if let Some(val) = env.get(&var_name) {
                        result.push_str(val);
                    }
                    i = j + 1;
                    continue;
                }
            } else if chars[i + 1].is_alphabetic() || chars[i + 1] == '_' {
                let mut j = i + 1;
                while j < chars.len() && (chars[j].is_alphanumeric() || chars[j] == '_') {
                    j += 1;
                }
                let var_name: String = chars[i + 1..j].iter().collect();
                if let Some(val) = env.get(&var_name) {
                    result.push_str(val);
                }
                i = j;
                continue;
            } else if chars[i + 1] == '?' {
                if let Some(val) = env.get("?") {
                    result.push_str(val);
                }
                i += 2;
                continue;
            }
        }
        result.push(chars[i]);
        i += 1;
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_tokenize_simple() {
        let tokens = tokenize("echo hello world").unwrap();
        assert_eq!(tokens.len(), 3);
        assert_eq!(tokens[0], Token::Word("echo".into()));
        assert_eq!(tokens[1], Token::Word("hello".into()));
        assert_eq!(tokens[2], Token::Word("world".into()));
    }

    #[test]
    fn test_tokenize_pipe() {
        let tokens = tokenize("ls | grep rust").unwrap();
        assert_eq!(tokens[1], Token::Pipe);
    }

    #[test]
    fn test_tokenize_redirect() {
        let tokens = tokenize("echo hi > file.txt").unwrap();
        assert_eq!(tokens[2], Token::RedirectOut);
        assert_eq!(tokens[3], Token::Word("file.txt".into()));
    }

    #[test]
    fn test_tokenize_append() {
        let tokens = tokenize("echo hi >> file.txt").unwrap();
        assert_eq!(tokens[2], Token::RedirectAppend);
        assert_eq!(tokens[3], Token::Word("file.txt".into()));
    }

    #[test]
    fn test_tokenize_and() {
        let tokens = tokenize("cmd1 && cmd2").unwrap();
        assert_eq!(tokens[1], Token::And);
    }

    #[test]
    fn test_tokenize_or() {
        let tokens = tokenize("cmd1 || cmd2").unwrap();
        assert_eq!(tokens[1], Token::Or);
    }

    #[test]
    fn test_tokenize_background() {
        let tokens = tokenize("sleep 10 &").unwrap();
        assert_eq!(tokens[2], Token::Background);
    }

    #[test]
    fn test_tokenize_quotes() {
        let tokens = tokenize("echo \"hello world\"").unwrap();
        assert_eq!(tokens[1], Token::Word("hello world".into()));
    }

    #[test]
    fn test_parse_pipeline() {
        let prog = parse("ls -la | grep rust").unwrap();
        assert_eq!(prog.statements.len(), 1);
        assert_eq!(prog.statements[0].pipeline.commands.len(), 2);
        assert_eq!(prog.statements[0].pipeline.commands[0].program, "ls");
        assert_eq!(prog.statements[0].pipeline.commands[1].program, "grep");
    }

    #[test]
    fn test_parse_chaining() {
        let prog = parse("cmd1 && cmd2 || cmd3 ; cmd4").unwrap();
        assert_eq!(prog.statements.len(), 4);
        assert_eq!(prog.statements[0].op, ChainOp::And);
        assert_eq!(prog.statements[1].op, ChainOp::Or);
        assert_eq!(prog.statements[2].op, ChainOp::Semi);
        assert_eq!(prog.statements[3].op, ChainOp::None);
    }

    #[test]
    fn test_parse_redirect() {
        let prog = parse("echo hello > out.txt").unwrap();
        let cmd = &prog.statements[0].pipeline.commands[0];
        assert_eq!(cmd.stdout_file.as_deref(), Some("out.txt"));
        assert!(!cmd.append);
    }

    #[test]
    fn test_parse_append() {
        let prog = parse("echo hello >> out.txt").unwrap();
        let cmd = &prog.statements[0].pipeline.commands[0];
        assert_eq!(cmd.stdout_file.as_deref(), Some("out.txt"));
        assert!(cmd.append);
    }

    #[test]
    fn test_parse_background() {
        let prog = parse("sleep 10 &").unwrap();
        assert!(prog.statements[0].pipeline.background);
    }

    #[test]
    fn test_expand_env() {
        let mut env = HashMap::new();
        env.insert("HOME".into(), "/Users/test".into());
        assert_eq!(expand_env("$HOME", &env), "/Users/test");
        assert_eq!(expand_env("${HOME}/dir", &env), "/Users/test/dir");
        assert_eq!(expand_env("no vars here", &env), "no vars here");
    }
}
