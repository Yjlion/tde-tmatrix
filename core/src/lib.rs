#![recursion_limit = "256"]
//! tmatrix-core: the Matrix engine of TMatrix (tde-tmatrix).
//!
//! The Trinity user interface talks to this library through a tiny C ABI
//! (see `tmatrix_core.h`): it sends commands as JSON strings and reads
//! events as JSON strings.  An eventfd becomes readable whenever an event is
//! waiting, so the TQt event loop can watch it with a TQSocketNotifier.
//!
//! Copyright (C) 2026 tde-tmatrix contributors
//! SPDX-License-Identifier: GPL-2.0-or-later

mod app;
mod emitter;
mod items;
mod rooms;

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::sync::Arc;

pub use emitter::Emitter;

/// The engine: a tokio runtime and the application state.
pub struct Core {
    rt: tokio::runtime::Runtime,
    app: Arc<app::App>,
    emitter: Emitter,
}

impl Core {
    pub fn new(data_dir: &str) -> std::io::Result<Core> {
        let rt = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(4)
            .thread_name("tmatrix-core")
            .enable_all()
            .build()?;
        let emitter = Emitter::new()?;
        let app = Arc::new(app::App::new(data_dir.into(), emitter.clone()));
        Ok(Core { rt, app, emitter })
    }

    /// Runs one command (a JSON object with at least "cmd") in the background.
    pub fn command(&self, json: &str) {
        let app = self.app.clone();
        let json = json.to_owned();
        self.rt.spawn(async move { app.handle(&json).await });
    }

    pub fn next_event(&self) -> Option<String> {
        self.emitter.pop()
    }

    pub fn event_fd(&self) -> c_int {
        self.emitter.fd()
    }

    /// Blocks until an event is waiting or `timeout_ms` passed (used by tests).
    pub fn wait_event(&self, timeout_ms: u64) -> Option<String> {
        self.emitter.wait_pop(std::time::Duration::from_millis(timeout_ms))
    }
}

impl Drop for Core {
    fn drop(&mut self) {
        let app = self.app.clone();
        self.rt.block_on(async move { app.shutdown().await });
    }
}

fn init_logging() {
    use tracing_subscriber::EnvFilter;
    let filter = std::env::var("TMATRIX_LOG").unwrap_or_else(|_| "warn".into());
    let _ = tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::new(filter))
        .with_writer(std::io::stderr)
        .try_init();
}

/// Creates the engine.  `data_dir` holds the stores of all accounts.
/// Returns NULL on failure.
#[no_mangle]
pub extern "C" fn tm_core_new(data_dir: *const c_char) -> *mut Core {
    if data_dir.is_null() {
        return std::ptr::null_mut();
    }
    init_logging();
    let dir = unsafe { CStr::from_ptr(data_dir) }.to_string_lossy().into_owned();
    match Core::new(&dir) {
        Ok(core) => Box::into_raw(Box::new(core)),
        Err(_) => std::ptr::null_mut(),
    }
}

/// Stops all work and frees the engine.
#[no_mangle]
pub extern "C" fn tm_core_free(core: *mut Core) {
    if !core.is_null() {
        drop(unsafe { Box::from_raw(core) });
    }
}

/// Queues a command (UTF-8 JSON).  The answer arrives as a "reply" event with
/// the command's "id".
#[no_mangle]
pub extern "C" fn tm_core_command(core: *mut Core, json: *const c_char) {
    if core.is_null() || json.is_null() {
        return;
    }
    let json = unsafe { CStr::from_ptr(json) }.to_string_lossy();
    unsafe { &*core }.command(&json);
}

/// Takes the next event (UTF-8 JSON) or returns NULL if there is none.
/// Free the string with tm_string_free().
#[no_mangle]
pub extern "C" fn tm_core_next_event(core: *mut Core) -> *mut c_char {
    if core.is_null() {
        return std::ptr::null_mut();
    }
    match unsafe { &*core }.next_event() {
        Some(s) => CString::new(s).map(|c| c.into_raw()).unwrap_or(std::ptr::null_mut()),
        None => std::ptr::null_mut(),
    }
}

/// A file descriptor that becomes readable when events are waiting.
/// Read 8 bytes from it to reset it, then call tm_core_next_event() until it
/// returns NULL.
#[no_mangle]
pub extern "C" fn tm_core_event_fd(core: *mut Core) -> c_int {
    if core.is_null() {
        return -1;
    }
    unsafe { &*core }.event_fd()
}

#[no_mangle]
pub extern "C" fn tm_string_free(s: *mut c_char) {
    if !s.is_null() {
        drop(unsafe { CString::from_raw(s) });
    }
}

/// The library version.  The string is static.
#[no_mangle]
pub extern "C" fn tm_core_version() -> *const c_char {
    concat!(env!("CARGO_PKG_VERSION"), "\0").as_ptr() as *const c_char
}
