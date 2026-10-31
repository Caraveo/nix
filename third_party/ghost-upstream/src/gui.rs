use egui::{Color32, FontFamily, FontId, Key, Layout, Sense, Ui, Align};

use crate::app::{App, LineKind, Theme, ThemeColors};

pub fn setup(ctx: &egui::Context, theme: Theme) {
    let mut style: egui::Style = (*ctx.style()).clone();
    style.text_styles.insert(egui::TextStyle::Small, FontId::new(12.0, FontFamily::Monospace));
    style.text_styles.insert(egui::TextStyle::Body, FontId::new(14.0, FontFamily::Monospace));
    style.text_styles.insert(egui::TextStyle::Button, FontId::new(14.0, FontFamily::Monospace));
    style.spacing.item_spacing = egui::vec2(8.0, 4.0);
    style.spacing.window_margin = egui::Margin::same(8.0);
    style.spacing.button_padding = egui::vec2(12.0, 5.0);
    ctx.set_style(style);

    let c = ThemeColors::from(theme);
    let is_light = theme == crate::app::Theme::Light;
    let mut visuals = if is_light { egui::Visuals::light() } else { egui::Visuals::dark() };
    visuals.panel_fill = c.bg;
    visuals.window_fill = c.panel;
    visuals.extreme_bg_color = c.bg;
    visuals.faint_bg_color = c.bg;
    visuals.code_bg_color = c.panel;

    let control_rounding = egui::Rounding::same(5.0);
    visuals.window_rounding = egui::Rounding::same(10.0);
    visuals.widgets.noninteractive.rounding = control_rounding;
    visuals.widgets.inactive.rounding = control_rounding;
    visuals.widgets.hovered.rounding = control_rounding;
    visuals.widgets.active.rounding = control_rounding;
    visuals.widgets.open.rounding = control_rounding;

    let border_stroke = egui::Stroke::new(0.5_f32, c.border);
    let thin_stroke = egui::Stroke::new(0.3_f32, c.border);
    visuals.widgets.noninteractive.bg_fill = c.bg;
    visuals.widgets.noninteractive.fg_stroke = egui::Stroke::new(1.0_f32, c.white);
    visuals.widgets.noninteractive.bg_stroke = thin_stroke;
    visuals.widgets.inactive.bg_fill = c.panel;
    visuals.widgets.inactive.fg_stroke = egui::Stroke::new(1.0_f32, c.white);
    visuals.widgets.inactive.bg_stroke = border_stroke;
    visuals.widgets.hovered.bg_fill = c.dark;
    visuals.widgets.hovered.fg_stroke = egui::Stroke::new(1.0_f32, c.white);
    visuals.widgets.hovered.bg_stroke = egui::Stroke::new(1.0_f32, c.cyan);
    visuals.widgets.active.bg_fill = c.active;
    visuals.widgets.active.fg_stroke = egui::Stroke::new(1.0_f32, c.white);
    visuals.widgets.active.bg_stroke = egui::Stroke::new(1.5_f32, c.cyan);
    visuals.widgets.open.bg_fill = c.dark;
    visuals.widgets.open.fg_stroke = egui::Stroke::new(1.0_f32, c.white);
    visuals.selection.bg_fill = if is_light {
        egui::Color32::from_rgb(0, 120, 215)
    } else {
        c.active
    };
    visuals.selection.stroke = egui::Stroke::new(1.0_f32, c.cyan);
    ctx.set_visuals(visuals);
}

pub fn render(ctx: &egui::Context, app: &mut App) {
    let c = app.colors();
    let pty_active = app.pty().is_some();

    if pty_active {
        handle_pty_input(ctx, app);
    }

    render_tab_bar(ctx, app, &c);

    if !pty_active {
        egui::TopBottomPanel::bottom("bottom")
            .exact_height(54.0)
            .frame(egui::Frame::none()
                .fill(Color32::WHITE)
                .stroke(egui::Stroke::new(0.75_f32, c.border))
                .inner_margin(egui::Margin::symmetric(16.0, 10.0)))
            .show(ctx, |ui| {
                render_input(ui, app, &c);
            });
    }

    // Main output area / editor
    let scroll = app.scroll_to_bottom();
    let show_editor = app.show_editor && app.editor.is_some();
    let panel = egui::CentralPanel::default()
        .frame(egui::Frame::none().fill(c.bg).inner_margin(egui::Margin::symmetric(18.0, 12.0)))
        .show(ctx, |ui| {
            if show_editor {
                render_editor(ui, app, &c);
            } else if pty_active {
                let font_id = FontId::new(13.0, FontFamily::Monospace);
                let (cell_width, row_height) = ui.fonts(|fonts| {
                    (fonts.glyph_width(&font_id, 'M'), fonts.row_height(&font_id))
                });
                let cols = (ui.available_width() / cell_width).floor().max(20.0) as u16;
                let rows = (ui.available_height() / row_height).floor().max(5.0) as u16;

                app.pty_rect = ui.max_rect();
                app.pty_cell_w = cell_width;
                app.pty_cell_h = row_height;

                if let Some(pty) = app.pty_mut().as_mut() {
                    pty.resize(cols, rows);
                    let content = pty
                        .parser
                        .screen()
                        .rows(0, cols)
                        .collect::<Vec<_>>()
                        .join("\n");
                    ui.add(
                        egui::Label::new(
                            egui::RichText::new(content)
                                .color(c.white)
                                .font(font_id),
                        )
                        .extend(),
                    );
                }
            } else {
                render_output(ui, app, scroll, &c);
            }
        });

    // Right-click to copy output
    if panel.response.secondary_clicked() {
        let text = if app.pty().is_some() {
            app.pty().as_ref().map(|p| p.parser.screen().contents()).unwrap_or_default()
        } else {
            app.results().iter().map(|l| l.text.clone()).collect::<Vec<_>>().join("\n")
        };
        ctx.output_mut(|o| o.copied_text = text);
        app.status_message = "*copied*".into();
    }

    app.set_scroll(false);

    // Dialogs
    if app.confirm_mode { render_confirm(ctx, app, &c); }
    if app.show_help { render_help(ctx, app, &c); }
    if app.show_settings { render_settings(ctx, app, &c); }
    if app.show_completions && !app.completion_list.is_empty() { render_completions(ctx, app, &c); }

    // Drag & drop files
    handle_dropped_files(ctx, app);
}

fn handle_pty_input(ctx: &egui::Context, app: &mut App) {
    let events = ctx.input(|i| i.events.clone());
    let scroll_delta = ctx.input(|i| i.smooth_scroll_delta);
    let hover_pos = ctx.input(|i| i.pointer.hover_pos());

    let pty_rect = app.pty_rect;
    let cell_w = app.pty_cell_w;
    let cell_h = app.pty_cell_h;

    let Some(pty) = app.pty_mut().as_mut() else { return };
    let application_cursor = pty.parser.screen().application_cursor();
    let bracketed_paste = pty.parser.screen().bracketed_paste();
    let mouse_mode = pty.parser.screen().mouse_protocol_mode();
    let mouse_enc = pty.parser.screen().mouse_protocol_encoding();

    for event in events {
        match event {
            egui::Event::Text(text) => pty.send(text.as_bytes()),
            egui::Event::Paste(text) => {
                if bracketed_paste { pty.send(b"\x1b[200~"); }
                pty.send(text.as_bytes());
                if bracketed_paste { pty.send(b"\x1b[201~"); }
            }
            egui::Event::Key { key, pressed: true, modifiers, .. } if !modifiers.command => {
                let bytes: Option<&[u8]> = if modifiers.ctrl {
                    match key {
                        Key::A => Some(b"\x01"), Key::B => Some(b"\x02"),
                        Key::C => Some(b"\x03"), Key::D => Some(b"\x04"),
                        Key::E => Some(b"\x05"), Key::F => Some(b"\x06"),
                        Key::G => Some(b"\x07"), Key::H => Some(b"\x08"),
                        Key::I => Some(b"\x09"), Key::J => Some(b"\x0a"),
                        Key::K => Some(b"\x0b"), Key::L => Some(b"\x0c"),
                        Key::M => Some(b"\x0d"), Key::N => Some(b"\x0e"),
                        Key::O => Some(b"\x0f"), Key::P => Some(b"\x10"),
                        Key::Q => Some(b"\x11"), Key::R => Some(b"\x12"),
                        Key::S => Some(b"\x13"), Key::T => Some(b"\x14"),
                        Key::U => Some(b"\x15"), Key::V => Some(b"\x16"),
                        Key::W => Some(b"\x17"), Key::X => Some(b"\x18"),
                        Key::Y => Some(b"\x19"), Key::Z => Some(b"\x1a"),
                        _ => None,
                    }
                } else {
                    match key {
                        Key::Enter => Some(b"\r"), Key::Tab => Some(b"\t"),
                        Key::Backspace => Some(b"\x7f"), Key::Escape => Some(b"\x1b"),
                        Key::ArrowUp => Some(if application_cursor { b"\x1bOA" } else { b"\x1b[A" }),
                        Key::ArrowDown => Some(if application_cursor { b"\x1bOB" } else { b"\x1b[B" }),
                        Key::ArrowRight => Some(if application_cursor { b"\x1bOC" } else { b"\x1b[C" }),
                        Key::ArrowLeft => Some(if application_cursor { b"\x1bOD" } else { b"\x1b[D" }),
                        Key::Home => Some(b"\x1b[H"), Key::End => Some(b"\x1b[F"),
                        Key::Delete => Some(b"\x1b[3~"), Key::Insert => Some(b"\x1b[2~"),
                        Key::PageUp => Some(b"\x1b[5~"), Key::PageDown => Some(b"\x1b[6~"),
                        _ => None,
                    }
                };
                if let Some(bytes) = bytes { pty.send(bytes); }
            }
            egui::Event::PointerButton { pos, button, pressed, .. }
                if mouse_mode != vt100::MouseProtocolMode::None && pty_rect.contains(pos) =>
            {
                let col = ((pos.x - pty_rect.left()) / cell_w).floor().max(0.0) as u16;
                let row = ((pos.y - pty_rect.top()) / cell_h).floor().max(0.0) as u16;
                let btn_code = match button {
                    egui::PointerButton::Primary => 0u8,
                    egui::PointerButton::Middle => 1u8,
                    egui::PointerButton::Secondary => 2u8,
                    _ => continue,
                };
                let code = if pressed { btn_code } else { 3 };
                match mouse_enc {
                    vt100::MouseProtocolEncoding::Sgr => {
                        let suffix = if pressed { 'M' } else { 'm' };
                        let msg = format!("\x1b[<{};{};{}{}", code, col + 1, row + 1, suffix);
                        pty.send(msg.as_bytes());
                    }
                    _ => {
                        let mut buf = vec![0x1b, b'[', b'M'];
                        buf.push(32u8 + code);
                        buf.push((32u16 + col + 1).min(255) as u8);
                        buf.push((32u16 + row + 1).min(255) as u8);
                        pty.send(&buf);
                    }
                }
            }
            _ => {}
        }
    }

    if scroll_delta.y != 0.0 && hover_pos.map_or(false, |p| pty_rect.contains(p)) {
        let steps = ((scroll_delta.y.abs() / 30.0).round() as i32).max(1).min(10);
        if mouse_mode == vt100::MouseProtocolMode::None {
            let up: &[u8] = if application_cursor { b"\x1bOA" } else { b"\x1b[A" };
            let down: &[u8] = if application_cursor { b"\x1bOB" } else { b"\x1b[B" };
            for _ in 0..steps {
                if scroll_delta.y > 0.0 { pty.send(down); }
                else { pty.send(up); }
            }
        } else {
            let pos = hover_pos.unwrap();
            let col = ((pos.x - pty_rect.left()) / cell_w).floor().max(0.0) as u16;
            let row = ((pos.y - pty_rect.top()) / cell_h).floor().max(0.0) as u16;
            let button: u8 = if scroll_delta.y > 0.0 { 65 } else { 64 };
            for _ in 0..steps {
                match mouse_enc {
                    vt100::MouseProtocolEncoding::Sgr => {
                        let msg = format!("\x1b[<{};{};{}M", button, col + 1, row + 1);
                        pty.send(msg.as_bytes());
                    }
                    _ => {
                        let mut buf = vec![0x1b, b'[', b'M'];
                        buf.push(32u8 + button);
                        buf.push((32u16 + col + 1).min(255) as u8);
                        buf.push((32u16 + row + 1).min(255) as u8);
                        pty.send(&buf);
                    }
                }
            }
        }
    }
}

fn handle_dropped_files(ctx: &egui::Context, app: &mut App) {
    let dropped = ctx.input(|i| i.raw.dropped_files.clone());
    if !dropped.is_empty() {
        for file in &dropped {
            if let Some(path) = &file.path {
                if !app.input.is_empty() && !app.input.ends_with(' ') { app.input.push(' '); }
                app.input.push_str(&path.display().to_string());
            }
        }
        app.input_focused = true;
    }
}

fn render_tab_bar(ctx: &egui::Context, app: &mut App, c: &ThemeColors) {
    let active = app.active_tab;
    let mut switch_to = None;
    let mut close_active = false;
    let mut add_new = false;

    egui::TopBottomPanel::top("tabs")
        .exact_height(52.0)
        .frame(egui::Frame::none()
            .fill(c.bg)
            .stroke(egui::Stroke::new(0.75_f32, c.border))
            .inner_margin(egui::Margin {
                left: 14.0,
                right: 12.0,
                top: 9.0,
                bottom: 9.0,
            }))
        .show(ctx, |ui| {
            ui.horizontal(|ui| {
                render_window_controls(ui, ctx);
                ui.add_space(18.0);

                for (i, tab) in app.tabs.iter().enumerate() {
                    let is_active = i == active;
                    let running = tab.pty.is_some();
                    let name = &tab.name;
                    let label = if running { format!("● {}", name) } else { name.clone() };

                    let bg = c.bg;
                    let fg = if running || !is_active { c.green } else { c.white };
                    let stroke = egui::Stroke::new(if is_active { 1.0_f32 } else { 0.6_f32 }, c.border);

                    let btn = egui::Button::new(egui::RichText::new(&label).color(fg).size(14.0))
                        .fill(bg)
                        .stroke(stroke)
                        .rounding(5.0)
                        .min_size(egui::vec2(88.0, 34.0));
                    let resp = ui.add(btn);
                    if resp.clicked() { switch_to = Some(i); }
                }

                if ui.add(egui::Button::new(egui::RichText::new("+").color(c.white).strong().size(20.0))
                    .fill(Color32::TRANSPARENT).frame(false)).clicked() {
                    add_new = true;
                }

                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if ui.add(
                        egui::Button::new(egui::RichText::new("×").color(c.white).size(22.0))
                            .fill(Color32::TRANSPARENT)
                            .frame(false)
                    ).on_hover_text("Close active tab (⌘W)").clicked() {
                        if app.tabs.len() > 1 { close_active = true; }
                        else { ctx.send_viewport_cmd(egui::ViewportCommand::Close); }
                    }
                });
            });

        });

    if let Some(i) = switch_to { app.switch_tab(i); }
    if close_active { app.close_tab(active); }
    if add_new { app.new_shell_tab(); }
}

fn render_window_controls(ui: &mut Ui, ctx: &egui::Context) {
    let controls = [
        (Color32::from_rgb(255, 95, 87), "Close"),
        (Color32::from_rgb(255, 189, 46), "Minimize"),
        (Color32::from_rgb(40, 200, 64), "Zoom"),
    ];

    for (index, (color, label)) in controls.into_iter().enumerate() {
        let (rect, response) = ui.allocate_exact_size(egui::vec2(16.0, 34.0), Sense::click());
        ui.painter().circle_filled(rect.center(), 6.0, color);
        let response = response.on_hover_text(label);
        if response.clicked() {
            match index {
                0 => ctx.send_viewport_cmd(egui::ViewportCommand::Close),
                1 => ctx.send_viewport_cmd(egui::ViewportCommand::Minimized(true)),
                _ => {
                    let maximized = ctx.input(|i| i.viewport().maximized.unwrap_or(false));
                    ctx.send_viewport_cmd(egui::ViewportCommand::Maximized(!maximized));
                }
            }
        }
    }
}

fn render_output(ui: &mut Ui, app: &App, _scroll: bool, c: &ThemeColors) {
    egui::ScrollArea::vertical()
        .auto_shrink([false, false])
        .stick_to_bottom(true)
        .show(ui, |ui| {
            for line in app.results() {
                let color = line_color(&line.kind, c);
                if line.text.is_empty() { ui.add_space(4.0); }
                else { render_text_with_links(ui, &line.text, color, c); }
            }
        });
}

fn render_status_bar(ui: &mut Ui, app: &mut App, c: &ThemeColors) {
    ui.add_space(2.0);
    ui.horizontal(|ui| {
        if app.show_editor {
            let path = app.editor.as_ref().map(|e| e.path.as_str()).unwrap_or("");
            let modified = app.editor.as_ref().map(|e| e.modified()).unwrap_or(false);
            let lang = app.editor.as_ref().map(|e| e.language()).unwrap_or("");
            ui.label(egui::RichText::new(" EDIT ").color(c.cyan).size(11.0));
            ui.separator();
            ui.label(egui::RichText::new(path).color(c.white).size(11.0));
            if modified {
                ui.label(egui::RichText::new(" ●").color(c.yellow).size(11.0));
            }
            ui.separator();
            ui.label(egui::RichText::new(lang).color(c.gray).size(11.0));
            ui.separator();
            ui.label(egui::RichText::new("Ctrl+S: save  Esc: close").color(c.gray).size(11.0));
        } else {
            let st = if app.last_status == 0 { " OK ".to_string() } else { format!(" ERR({}) ", app.last_status) };
            let sc = if app.last_status == 0 { c.green } else { c.red };
            ui.label(egui::RichText::new(&st).color(sc).background_color(c.bg).size(11.0));
            ui.separator();
            let user = app.env.get("USER").or_else(|| app.env.get("USERNAME")).map(|s| s.as_str()).unwrap_or("user");
            ui.label(egui::RichText::new(user).color(c.yellow).size(11.0));
            ui.separator();
            ui.label(egui::RichText::new(&app.display_cwd()).color(c.cyan).size(11.0));

            if !app.git_branch.is_empty() {
                ui.separator();
                ui.label(egui::RichText::new(&app.git_branch).color(c.cyan).size(11.0));
                if app.git_dirty {
                    ui.label(egui::RichText::new("*").color(c.yellow).strong().size(11.0));
                }
            }
        }

        ui.separator();
        let bg = app.background_jobs.len();
        ui.label(egui::RichText::new(format!("jobs:{}", bg)).color(if bg > 0 { c.yellow } else { c.gray }).size(11.0));
        if !app.status_message.is_empty() {
            ui.separator();
            ui.label(egui::RichText::new(&app.status_message).color(c.green).size(11.0));
        }
        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            if ui.button(egui::RichText::new(app.theme.name()).color(c.cyan).size(11.0)).clicked() {
                app.theme = app.theme.next();
            }
            ui.separator();
            let n = now_str();
            ui.label(egui::RichText::new(&n).color(c.gray).size(11.0));
        });
    });
}

fn render_input(ui: &mut Ui, app: &mut App, c: &ThemeColors) {
    let pty_active = app.pty().is_some();
    ui.vertical(|ui| {
        ui.horizontal(|ui| {
            if pty_active {
                let cmd = app.pty().as_ref().map(|p| p.command.as_str()).unwrap_or("");
                ui.label(egui::RichText::new(format!("{} ▶", cmd)).color(c.yellow).strong().size(13.0));
            } else {
                ui.label(egui::RichText::new("→").color(c.white).strong().size(22.0));
            }

            let resp = ui.add(
                egui::TextEdit::singleline(&mut app.input)
                    .font(FontId::new(14.0, FontFamily::Monospace))
                    .text_color(c.white)
                    .desired_width(f32::MAX)
                    .frame(false)
                    .lock_focus(true)
            );

            if resp.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter)) {
                if pty_active {
                    let input = format!("{}\r", app.input);
                    if let Some(pty) = app.pty_mut().as_mut() {
                        pty.send(input.as_bytes());
                    }
                    app.reset_input();
                } else {
                    handle_enter(app);
                }
            }
            // Keep the shell ready for typing. A one-shot focus request can be
            // ignored while the native window is still activating, so retry
            // until egui confirms focus. Do not steal it from modal surfaces
            // or the built-in editor.
            let shell_can_focus = !pty_active
                && !app.show_editor
                && !app.show_settings
                && !app.show_help
                && !app.confirm_mode;
            let no_widget_focused = ui.ctx().memory(|memory| memory.focused().is_none());
            if shell_can_focus
                && !resp.has_focus()
                && (app.input_focused || no_widget_focused)
            {
                resp.request_focus();
                app.input_focused = true;
            } else if resp.has_focus() {
                app.input_focused = false;
            }

            if !pty_active && resp.has_focus() && ui.input(|i| i.key_pressed(Key::Tab)) { handle_tab(app); }
            if !pty_active && resp.has_focus() {
                if ui.input(|i| i.key_pressed(Key::ArrowUp)) { app.history_prev(); }
                if ui.input(|i| i.key_pressed(Key::ArrowDown)) { app.history_next(); }
            }
        });

        if !pty_active {
            if let Some(ref s) = app.suggestion {
                ui.add_space(1.0);
                let pad = " ".repeat(app.prompt().chars().count() + 1);
                ui.label(egui::RichText::new(format!("{}{}", pad, s)).color(c.dark).size(13.0).family(FontFamily::Monospace));
            }
        }
    });

    // Global shortcuts
    if pty_active {
        if ui.input(|i| i.key_pressed(Key::C) && i.modifiers.ctrl) {
            if let Some(pty) = app.pty_mut().as_mut() {
                pty.send(b"\x03");
            }
        }
        if ui.input(|i| i.key_pressed(Key::D) && i.modifiers.ctrl) {
            if let Some(pty) = app.pty_mut().as_mut() {
                pty.send(b"\x04");
            }
        }
        if ui.input(|i| i.key_pressed(Key::Escape)) {
            if let Some(mut pty) = app.pty_mut().take() {
                pty.kill();
            }
        }
    } else {
        if ui.input(|i| i.key_pressed(Key::L) && i.modifiers.ctrl) { app.clear_results(); }
        if ui.input(|i| i.key_pressed(Key::H) && i.modifiers.ctrl) { app.show_help = !app.show_help; }
        if ui.input(|i| i.key_pressed(Key::C) && i.modifiers.ctrl) {
            if app.confirm_mode { app.confirm_mode = false; app.pending_command.clear(); }
            else { app.reset_input(); }
        }
        if ui.input(|i| i.key_pressed(Key::D) && i.modifiers.ctrl) { app.should_quit = true; }
        if ui.input(|i| i.key_pressed(Key::Escape)) {
            if app.confirm_mode { app.confirm_mode = false; app.pending_command.clear(); }
            else if app.show_help { app.show_help = false; }
            else { app.should_quit = true; }
        }
    }
}

fn handle_enter(app: &mut App) {
    let input = app.input.trim().to_string();
    if input.is_empty() { return; }
    if app.safety_enabled {
        let safety = crate::safety::check_safety(&input);
        if safety.is_dangerous {
            app.confirm_mode = true;
            app.pending_command = input;
            app.pending_reason = safety.reason;
            app.pending_changes = safety.changes;
            app.reset_input();
        return;
        }
    }
    app.reset_input();
    app.selected_history = None;
    app.pending_execution = Some(input);
    app.input_focused = true;
}

fn handle_tab(app: &mut App) {
    let first = app.input.split_whitespace().next().unwrap_or(&app.input);
    let comps = crate::completion::get_completions(first, crate::BUILTINS);
    if comps.is_empty() { return; }
    if comps.len() == 1 {
        let c = &comps[0];
        if let Some(sp) = app.input.find(' ') { app.input = format!("{}{}", c, &app.input[sp..]); }
        else { app.input = format!("{} ", c); }
        app.suggestion = None;
    } else {
        if let Some(p) = crate::completion::common_prefix(&comps) {
            if p.len() > app.input.len() { app.input = p; app.suggestion = None; }
        }
        app.completion_list = comps;
        app.show_completions = true;
    }
}

fn render_confirm(ctx: &egui::Context, app: &mut App, c: &ThemeColors) {
    let mut open = true;
    let mut result = None;
    let has_changes = !app.pending_changes.is_empty();
    let window_height = if has_changes { 400.0 } else { 200.0 };
    egui::Window::new("ARE YOU SURE?")
        .open(&mut open).resizable(true).collapsible(false)
        .frame(egui::Frame::none().fill(Color32::from_rgba_premultiplied(40,10,10,200)).stroke(egui::Stroke::new(2.0_f32, c.red)).inner_margin(egui::Margin::same(12.0)))
        .min_width(500.0)
        .min_height(window_height)
        .show(ctx, |ui| {
            ui.label(egui::RichText::new(&app.pending_reason).color(c.yellow).strong().size(14.0));
            ui.add_space(4.0);
            ui.label(egui::RichText::new(format!("Command: {}", app.pending_command)).color(c.cyan).family(FontFamily::Monospace).size(13.0));
            ui.add_space(8.0);

            if has_changes {
                ui.label(egui::RichText::new("Changes that will occur:").color(c.white).strong().size(12.0));
                ui.add_space(2.0);
                egui::ScrollArea::vertical().max_height(250.0).show(ui, |ui| {
                    for change in &app.pending_changes {
                        let color = if change.contains("delete") { c.red }
                            else if change.contains("overwrite") { c.yellow }
                            else if change.contains("chmod") { c.cyan }
                            else { c.white };
                        ui.label(egui::RichText::new(format!("  {}", change)).color(color).family(FontFamily::Monospace).size(11.0));
                    }
                });
                if app.pending_changes.len() >= 50 {
                    ui.label(egui::RichText::new(format!("  ... and {} more (showing first 50)", app.pending_changes.len())).color(c.gray).size(11.0));
                }
                ui.add_space(4.0);
            }

            ui.add_space(4.0);
            ui.horizontal(|ui| {
                if ui.button(egui::RichText::new("  YES, I'm sure  ").color(c.green).strong()).clicked() { result = Some(true); }
                ui.add_space(8.0);
                if ui.button(egui::RichText::new("  Cancel  ").color(c.red)).clicked() { result = Some(false); }
            });
        });
    if !open { result = Some(false); }
    if let Some(conf) = result {
        if conf {
            let cmd = app.pending_command.clone();
            app.confirm_mode = false;
            app.pending_command.clear();
            app.pending_reason.clear();
            app.pending_changes.clear();
            app.pending_execution = Some(cmd);
            app.input_focused = true;
        } else {
            app.confirm_mode = false;
            app.pending_command.clear();
            app.pending_reason.clear();
            app.pending_changes.clear();
            app.status_message = "Command cancelled.".into();
        }
    }
}

fn render_help(ctx: &egui::Context, app: &mut App, c: &ThemeColors) {
    let mut open = true;
    egui::Window::new("Help").open(&mut open).resizable(true).collapsible(false)
        .min_width(520.0).min_height(400.0)
        .frame(egui::Frame::none().fill(c.panel).stroke(egui::Stroke::new(1.0_f32, c.cyan)).inner_margin(egui::Margin::same(12.0)))
        .show(ctx, |ui| {
            ui.heading(egui::RichText::new("Ghost Shell — Help").color(c.cyan).strong());
            ui.separator();
            ui.add_space(4.0);

            ui.label(egui::RichText::new("Keybindings").color(c.green).strong().size(14.0));
            for (key, desc) in &[
                ("Tab", "Auto-complete command"),
                ("Up/Down", "Navigate command history"),
                ("Enter", "Execute command"),
                ("Ctrl+L", "Clear output"),
                ("Ctrl+C", "Clear current input"),
                ("Ctrl+D", "Quit Ghost Shell"),
                ("Ctrl+H", "Toggle this help"),
                ("Esc", "Cancel dialog / Quit"),
            ] {
                ui.label(egui::RichText::new(format!("  {:<12} {}", key, desc)).color(c.white).size(12.0));
            }
            ui.add_space(8.0);

            ui.label(egui::RichText::new("Features").color(c.green).strong().size(14.0));
            for line in &[
                "  Themes: Click theme button in menu bar to cycle themes.",
                "  Safety: Destructive commands ask confirmation with file listing.",
                "  Git: Status bar shows current branch and dirty flag.",
                "  Drag & Drop: Drop files from Finder into the input box.",
                "  Clickable URLs: https:// links in output are clickable.",
                "  Tab completion: PATH scan + builtins.",
                "  Command history: Up/Down arrows to navigate.",
            ] {
                ui.label(egui::RichText::new(*line).color(c.white).size(12.0));
            }
            ui.add_space(8.0);

            ui.label(egui::RichText::new("Shell Syntax").color(c.green).strong().size(14.0));
            for (syntax, desc) in &[
                ("cmd1 | cmd2", "Pipe output"),
                ("cmd > file", "Redirect to file"),
                ("cmd >> file", "Append to file"),
                ("cmd < file", "Read from file"),
                ("cmd1 && cmd2", "Run if previous succeeds"),
                ("cmd1 || cmd2", "Run if previous fails"),
                ("cmd &", "Run in background"),
                ("$VAR / ${VAR}", "Environment variable"),
            ] {
                ui.label(egui::RichText::new(format!("  {:<16} {}", syntax, desc)).color(c.white).size(12.0));
            }
        });
    if !open { app.show_help = false; }
}

fn render_completions(ctx: &egui::Context, app: &mut App, c: &ThemeColors) {
    let comps = app.completion_list.clone();
    if comps.is_empty() { app.show_completions = false; return; }
    let screen = ctx.input(|i| i.screen_rect);
    let pos = egui::pos2(screen.left() + 250.0, screen.bottom() - 120.0);
    egui::Area::new(egui::Id::new("completions")).order(egui::Order::Foreground).fixed_pos(pos).show(ctx, |ui| {
        egui::Frame::popup(ui.style()).fill(c.bg).stroke(egui::Stroke::new(1.0_f32, c.cyan)).show(ui, |ui| {
            ui.set_max_width(300.0);
            ui.set_max_height(200.0);
            egui::ScrollArea::vertical().show(ui, |ui| {
                for comp in &comps {
                    let r = ui.add(egui::Label::new(egui::RichText::new(comp).color(c.white).size(12.0)).sense(Sense::click()));
                    if r.clicked() {
                        app.input = format!("{} ", comp);
                        app.suggestion = None;
                        app.show_completions = false;
                        app.completion_list.clear();
                        app.input_focused = true;
                    }
                }
            });
        });
    });
}

fn line_color(k: &LineKind, c: &ThemeColors) -> Color32 {
    match k {
        LineKind::Normal => c.white,
        LineKind::Prompt => c.green,
        LineKind::Stdout => c.white,
        LineKind::Stderr => c.red,
        LineKind::Info => c.cyan,
        LineKind::Warning => c.yellow,
        LineKind::Error => c.red,
    }
}

fn render_text_with_links(ui: &mut Ui, text: &str, color: Color32, c: &ThemeColors) {
    if !text.contains("https://") {
        ui.label(egui::RichText::new(text).color(color).family(FontFamily::Monospace).size(13.0));
        return;
    }

    ui.horizontal(|ui| {
        let mut remaining = text;
        loop {
            match remaining.find("https://") {
                Some(pos) => {
                    if pos > 0 {
                        ui.label(egui::RichText::new(&remaining[..pos]).color(color).family(FontFamily::Monospace).size(13.0));
                    }
                    let after = &remaining[pos..];
                    let url_end = after.find(|ch: char| ch.is_whitespace() || ch == '|' || ch == ')' || ch == ']')
                        .unwrap_or(after.len());
                    let url = &after[..url_end];
                    let clean_url = url.trim_end_matches(|ch: char| ch == '.' || ch == ',');
                    ui.hyperlink_to(
                        egui::RichText::new(clean_url).color(c.cyan).family(FontFamily::Monospace).size(13.0),
                        clean_url,
                    );
                    remaining = &after[url_end..];
                }
                None => {
                    if !remaining.is_empty() {
                        ui.label(egui::RichText::new(remaining).color(color).family(FontFamily::Monospace).size(13.0));
                    }
                    break;
                }
            }
        }
    });
}

fn render_settings(ctx: &egui::Context, app: &mut App, c: &ThemeColors) {
    let mut open = true;
    egui::Window::new("Settings")
        .open(&mut open).resizable(true).collapsible(false)
        .min_width(520.0).min_height(500.0)
        .frame(egui::Frame::none().fill(c.panel).stroke(egui::Stroke::new(1.0_f32, c.cyan)).inner_margin(egui::Margin::same(16.0)))
        .show(ctx, |ui| {
            // ── Appearance ──
            ui.label(egui::RichText::new("Appearance").color(c.green).strong().size(14.0));
            ui.add_space(4.0);

            ui.horizontal(|ui| {
                ui.label(egui::RichText::new("Theme:").color(c.white).size(12.0));
                for theme in Theme::all() {
                    let is_current = app.theme == *theme;
                    let btn = egui::Button::new(egui::RichText::new(theme.name())
                        .color(if is_current { c.bg } else { c.cyan })
                        .size(11.0))
                        .fill(if is_current { c.cyan } else { Color32::TRANSPARENT })
                        .stroke(if is_current { egui::Stroke::NONE } else { egui::Stroke::new(0.5_f32, c.border) });
                    if ui.add(btn).clicked() { app.theme = *theme; }
                    ui.add_space(4.0);
                }
            });
            ui.add_space(6.0);

            ui.horizontal(|ui| {
                ui.label(egui::RichText::new("Font size:").color(c.white).size(12.0));
                ui.add(egui::Slider::new(&mut app.font_size, 9.0..=20.0).text(""));
                ui.label(egui::RichText::new(format!("{:.0}px", app.font_size)).color(c.gray).size(11.0));
            });
            ui.add_space(10.0);

            // ── Terminal ──
            ui.label(egui::RichText::new("Terminal").color(c.green).strong().size(14.0));
            ui.add_space(4.0);

            ui.horizontal(|ui| {
                ui.label(egui::RichText::new("Terminal columns:").color(c.white).size(12.0));
                ui.add(egui::Slider::new(&mut app.pty_cols, 40..=240).text(""));
            });
            ui.add_space(2.0);
            ui.horizontal(|ui| {
                ui.label(egui::RichText::new("Terminal rows:").color(c.white).size(12.0));
                ui.add(egui::Slider::new(&mut app.pty_rows, 10..=80).text(""));
            });
            ui.add_space(6.0);

            ui.checkbox(&mut app.auto_switch_pty, egui::RichText::new("Auto-switch to new command tabs").color(c.white).size(12.0));
            ui.add_space(10.0);

            // ── Behavior ──
            ui.label(egui::RichText::new("Behavior").color(c.green).strong().size(14.0));
            ui.add_space(4.0);
            ui.checkbox(&mut app.show_startup_msg, egui::RichText::new("Show startup message").color(c.white).size(12.0));
            ui.add_space(2.0);
            ui.checkbox(&mut app.safety_enabled, egui::RichText::new("Safety checks for destructive commands").color(c.white).size(12.0));
            ui.add_space(10.0);

            // ── Environment ──
            ui.label(egui::RichText::new("Environment").color(c.green).strong().size(14.0));
            ui.add_space(4.0);

            egui::ScrollArea::vertical().max_height(120.0).show(ui, |ui| {
                let mut sorted: Vec<_> = app.env.iter().filter(|(k, _)| k.as_str() != "?").collect();
                sorted.sort_by(|a, b| a.0.cmp(b.0));
                for (k, v) in sorted {
                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new(k).color(c.cyan).size(11.0).family(FontFamily::Monospace));
                        ui.label(egui::RichText::new("=").color(c.gray).size(11.0));
                        ui.label(egui::RichText::new(v).color(c.white).size(11.0).family(FontFamily::Monospace));
                    });
                }
            });
            ui.add_space(10.0);

            // ── PATH ──
            ui.label(egui::RichText::new("PATH").color(c.green).strong().size(14.0));
            ui.add_space(4.0);
            if let Some(path) = app.env.get("PATH") {
                egui::ScrollArea::vertical().max_height(100.0).show(ui, |ui| {
                    for dir in path.split(':') {
                        if !dir.is_empty() {
                            ui.label(egui::RichText::new(dir).color(c.gray).size(11.0).family(FontFamily::Monospace));
                        }
                    }
                });
            }
            ui.add_space(8.0);

            // ── About ──
            ui.separator();
            ui.add_space(4.0);
            ui.label(egui::RichText::new("Ghost Shell v0.8.0").color(c.cyan).size(12.0));
            ui.label(egui::RichText::new("Standalone GUI shell built in Rust with egui").color(c.gray).size(11.0));
            ui.label(egui::RichText::new("Ctrl+T: new tab | Ctrl+L: clear | Ctrl+H: help | Ctrl+D: quit").color(c.gray).size(11.0));
        });
    if !open { app.show_settings = false; }
}

fn render_editor(ui: &mut Ui, app: &mut App, c: &ThemeColors) {
    let editor = app.editor.as_mut().unwrap();
    let modified = editor.modified();
    let path = editor.path.clone();
    let ext = editor.extension().to_string();
    let language = editor.language().to_string();
    let line_count = editor.content.lines().count().max(1);

    ui.horizontal_top(|ui| {
        egui::ScrollArea::vertical()
            .auto_shrink([false, false])
            .max_width(f32::INFINITY)
            .show(ui, |ui| {
                ui.horizontal_top(|ui| {
                    // Line numbers
                    ui.vertical(|ui| {
                        ui.spacing_mut().item_spacing.y = 0.0;
                        for i in 1..=line_count {
                            ui.label(egui::RichText::new(format!("{:>4} ", i))
                                .color(c.gray)
                                .font(FontId::new(13.0, FontFamily::Monospace))
                                .size(13.0));
                        }
                    });

                    ui.separator();

                    // Editor with syntax highlighting
                    let ctx = ui.ctx().clone();
                    let output = egui::TextEdit::multiline(&mut editor.content)
                        .font(FontId::new(13.0, FontFamily::Monospace))
                        .desired_width(f32::MAX)
                        .layouter(&mut move |_ui: &Ui, text: &str, _wrap: f32| {
                            let job = crate::editor::highlight(text, &language);
                            ctx.fonts(|f| f.layout_job(job))
                        })
                        .show(ui);

                    // Handle Tab to insert tab character instead of changing focus
                    if output.response.has_focus()
                        && ui.input(|i| i.key_pressed(Key::Tab) && !i.modifiers.shift)
                    {
                        if let Some(cursor) = &output.cursor_range {
                            let pos = cursor.primary.ccursor.index;
                            editor.content.insert(pos, '\t');
                            if let Some(mut state) =
                                egui::widgets::text_edit::TextEditState::load(ui.ctx(), output.response.id)
                            {
                                let new_pos = egui::text::CCursor::new(pos + 1);
                                state.cursor.set_char_range(Some(
                                    egui::text_selection::CCursorRange::one(new_pos),
                                ));
                                state.store(ui.ctx(), output.response.id);
                            }
                        }
                        ui.input_mut(|i| i.consume_key(egui::Modifiers::NONE, Key::Tab));
                    }
                });
            });
    });

    // Ctrl+S to save, Esc to close
    if ui.input(|i| i.key_pressed(Key::S) && i.modifiers.ctrl) {
        if let Some(editor) = app.editor.as_ref() {
            match std::fs::write(&editor.path, &editor.content) {
                Ok(_) => {
                    if let Some(editor) = app.editor.as_mut() {
                        editor.original = editor.content.clone();
                    }
                    app.status_message = "*saved*".into();
                }
                Err(e) => {
                    app.status_message = format!("save failed: {}", e);
                }
            }
        }
    }

    if ui.input(|i| i.key_pressed(Key::Escape)) {
        app.show_editor = false;
        app.editor = None;
        app.input_focused = true;
    }

    // Show file info in status bar area
    let _ = (modified, path, ext);
}

fn now_str() -> String {
    let now = std::time::SystemTime::now();
    let d = now.duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    let s = d.as_secs();
    format!("{:02}:{:02}:{:02}", (s/3600)%24, (s/60)%60, s%60)
}
