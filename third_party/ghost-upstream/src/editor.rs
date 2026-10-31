use std::sync::OnceLock;

use syntect::easy::HighlightLines;
use syntect::highlighting::ThemeSet;
use syntect::parsing::SyntaxSet;
use syntect::util::LinesWithEndings;

static SYNTAX_SET: OnceLock<SyntaxSet> = OnceLock::new();
static THEME_SET: OnceLock<ThemeSet> = OnceLock::new();

fn syntax_set() -> &'static SyntaxSet {
    SYNTAX_SET.get_or_init(|| SyntaxSet::load_defaults_newlines())
}

fn theme_set() -> &'static ThemeSet {
    THEME_SET.get_or_init(|| ThemeSet::load_defaults())
}

pub struct EditorState {
    pub path: String,
    pub content: String,
    pub original: String,
}

impl EditorState {
    pub fn new(path: String, content: String) -> Self {
        let original = content.clone();
        Self { path, content, original }
    }

    pub fn modified(&self) -> bool {
        self.content != self.original
    }

    pub fn extension(&self) -> &str {
        self.path.rsplit('.').next().unwrap_or("")
    }

    pub fn language(&self) -> &str {
        detect_language(self.extension())
    }
}

pub fn detect_language(ext: &str) -> &'static str {
    match ext.to_lowercase().as_str() {
        "rs" => "Rust",
        "py" => "Python",
        "js" => "JavaScript",
        "jsx" => "JavaScript",
        "ts" => "TypeScript",
        "tsx" => "TypeScript",
        "go" => "Go",
        "c" | "h" => "C",
        "cpp" | "cc" | "cxx" | "hpp" | "hh" => "C++",
        "java" => "Java",
        "rb" => "Ruby",
        "sh" | "bash" | "zsh" => "Bash",
        "json" => "JSON",
        "html" | "htm" | "vue" => "HTML",
        "css" => "CSS",
        "xml" => "XML",
        "yaml" | "yml" => "YAML",
        "toml" => "TOML",
        "md" | "markdown" => "Markdown",
        "sql" => "SQL",
        "php" => "PHP",
        "swift" => "Swift",
        "kt" => "Kotlin",
        "scala" => "Scala",
        "lua" => "Lua",
        "dart" => "Dart",
        "perl" | "pl" => "Perl",
        "r" => "R",
        "haskell" | "hs" => "Haskell",
        "elm" => "Elm",
        "ex" | "exs" => "Elixir",
        "clj" | "cljs" => "Clojure",
        "diff" | "patch" => "Diff",
        "dockerfile" => "Dockerfile",
        "gitignore" | "gitconfig" => "Git Config",
        "vim" => "VimL",
        "txt" | "" => "Plain Text",
        _ => "Plain Text",
    }
}

pub fn highlight(text: &str, language: &str) -> egui::text::LayoutJob {
    let ss = syntax_set();
    let ts = theme_set();
    let theme = &ts.themes["base16-ocean.dark"];

    let mut job = egui::text::LayoutJob::default();
    job.wrap.max_width = f32::INFINITY;

    let syntax = ss
        .find_syntax_by_name(language)
        .or_else(|| ss.find_syntax_by_extension(language))
        .unwrap_or_else(|| ss.find_syntax_plain_text());

    let mut highlighter = HighlightLines::new(syntax, theme);

    for line in LinesWithEndings::from(text) {
        match highlighter.highlight_line(line, ss) {
            Ok(regions) => {
                for (style, text) in regions {
                    job.append(
                        &text,
                        0.0,
                        egui::text::TextFormat {
                            font_id: egui::FontId::monospace(13.0),
                            color: egui::Color32::from_rgb(
                                style.foreground.r,
                                style.foreground.g,
                                style.foreground.b,
                            ),
                            ..Default::default()
                        },
                    );
                }
            }
            Err(_) => {
                job.append(
                    line,
                    0.0,
                    egui::text::TextFormat {
                        font_id: egui::FontId::monospace(13.0),
                        color: egui::Color32::from_rgb(220, 220, 230),
                        ..Default::default()
                    },
                );
            }
        }
    }

    job
}
