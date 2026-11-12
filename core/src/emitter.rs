//! The event queue shared with the user interface.
//
// SPDX-License-Identifier: GPL-2.0-or-later

use std::collections::VecDeque;
use std::os::raw::c_int;
use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;

use serde_json::Value;

struct Inner {
    queue: Mutex<VecDeque<String>>,
    cond: Condvar,
    fd: c_int,
}

impl Drop for Inner {
    fn drop(&mut self) {
        unsafe { libc::close(self.fd) };
    }
}

#[derive(Clone)]
pub struct Emitter(Arc<Inner>);

impl Emitter {
    pub fn new() -> std::io::Result<Emitter> {
        let fd = unsafe { libc::eventfd(0, libc::EFD_CLOEXEC | libc::EFD_NONBLOCK) };
        if fd < 0 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(Emitter(Arc::new(Inner { queue: Mutex::new(VecDeque::new()), cond: Condvar::new(), fd })))
    }

    pub fn fd(&self) -> c_int {
        self.0.fd
    }

    /// Queues an event of the given type; `fields` must be a JSON object.
    pub fn emit(&self, kind: &str, mut fields: Value) {
        if !fields.is_object() {
            fields = Value::Object(Default::default());
        }
        fields["type"] = Value::String(kind.into());
        self.push(fields.to_string());
    }

    pub fn push(&self, s: String) {
        self.0.queue.lock().unwrap().push_back(s);
        self.0.cond.notify_all();
        let one: u64 = 1;
        unsafe { libc::write(self.0.fd, &one as *const u64 as *const libc::c_void, 8) };
    }

    pub fn pop(&self) -> Option<String> {
        self.0.queue.lock().unwrap().pop_front()
    }

    pub fn wait_pop(&self, timeout: Duration) -> Option<String> {
        let q = self.0.queue.lock().unwrap();
        let (mut q, _) = self.0.cond.wait_timeout_while(q, timeout, |q| q.is_empty()).unwrap();
        q.pop_front()
    }
}
