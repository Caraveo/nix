use portable_pty::{native_pty_system, CommandBuilder, MasterPty, PtySize};
use std::collections::HashMap;
use std::io::{Read, Write};
use std::sync::mpsc::{channel, Receiver};

pub struct PtySession {
    master: Box<dyn MasterPty + Send>,
    writer: Box<dyn Write + Send>,
    child: Box<dyn portable_pty::Child + Send>,
    receiver: Receiver<Vec<u8>>,
    pub parser: vt100::Parser,
    pub cols: u16,
    pub rows: u16,
    pub alive: bool,
    pub command: String,
}

impl PtySession {
    pub fn new(
        program: &str,
        args: &[String],
        cwd: &str,
        env: &HashMap<String, String>,
    ) -> Result<Self, String> {
        let pty_system = native_pty_system();
        let cols = 120u16;
        let rows = 40u16;
        let pair = pty_system
            .openpty(PtySize { rows, cols, pixel_width: 0, pixel_height: 0 })
            .map_err(|e| e.to_string())?;

        let mut cmd = CommandBuilder::new(program);
        for arg in args {
            cmd.arg(arg);
        }
        cmd.cwd(cwd);
        for (k, v) in env {
            if k != "?" {
                cmd.env(k, v);
            }
        }
        cmd.env("TERM", "xterm-256color");
        cmd.env("COLORTERM", "truecolor");

        let child = pair.slave.spawn_command(cmd).map_err(|e| e.to_string())?;
        drop(pair.slave);

        let reader = pair.master.try_clone_reader().map_err(|e| e.to_string())?;
        let writer = pair.master.take_writer().map_err(|e| e.to_string())?;

        let (tx, rx) = channel::<Vec<u8>>();
        std::thread::spawn(move || {
            let mut reader = reader;
            let mut buf = [0u8; 8192];
            loop {
                match reader.read(&mut buf) {
                    Ok(0) => break,
                    Ok(n) => {
                        if tx.send(buf[..n].to_vec()).is_err() {
                            break;
                        }
                    }
                    Err(_) => break,
                }
            }
            let _ = tx.send(Vec::new());
        });

        let parser = vt100::Parser::new(rows, cols, 0);

        let command = if args.is_empty() {
            program.to_string()
        } else {
            format!("{} {}", program, args.join(" "))
        };

        Ok(PtySession {
            master: pair.master,
            writer,
            child,
            receiver: rx,
            parser,
            cols,
            rows,
            alive: true,
            command,
        })
    }

    pub fn poll(&mut self) {
        while let Ok(data) = self.receiver.try_recv() {
            if data.is_empty() {
                self.alive = false;
            } else {
                self.parser.process(&data);
            }
        }

        match self.child.try_wait() {
            Ok(Some(_)) => self.alive = false,
            Ok(None) => {}
            Err(_) => self.alive = false,
        }
    }

    pub fn send(&mut self, data: &[u8]) {
        let _ = self.writer.write_all(data);
        let _ = self.writer.flush();
    }

    pub fn send_str(&mut self, s: &str) {
        self.send(s.as_bytes());
    }

    pub fn resize(&mut self, cols: u16, rows: u16) {
        if cols == self.cols && rows == self.rows {
            return;
        }
        self.cols = cols;
        self.rows = rows;
        self.parser.screen_mut().set_size(rows, cols);
        let _ = self.master.resize(PtySize { rows, cols, pixel_width: 0, pixel_height: 0 });
    }

    pub fn kill(&mut self) {
        let _ = self.child.kill();
    }
}
