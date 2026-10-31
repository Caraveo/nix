# Ghost upstream snapshot

Source: https://github.com/caraveo/ghost
Commit: `6f584f47abc5144d70169e401ea513759521b903` (v0.8.0)

The repository owner requested the Ghost port for NIX. The Rust sources and
manifest are retained unchanged here for provenance and further porting. The
upstream repository did not include a license file at this revision; this copy
does not assign a new license or expand third-party rights. Dependency licenses
still apply. macOS-specific build scripts and binary assets are not needed by Core.

The runnable Core adaptation is in `kernel/ghost/`. Its bounded tokenizer follows
`src/parser.rs`, and its command names/aliases follow the upstream built-ins.
The framebuffer palette uses `Theme::Light` from `src/app.rs`. The renderer
recreates the light canvas and tab toolbar from `src/gui.rs`, without egui or
macOS framework dependencies. Traffic lights and the tab toolbar are visual
chrome, not clickable controls in this MVP.

Core implements a limited built-in command engine. It cannot execute Ghost's
macOS binary or external processes. PTYs, full VT emulation, arbitrary file APIs,
pipelines, and the Swift settings panel need kernel/user-space services that
Core does not yet provide. Unsupported syntax fails explicitly.
