//! End-to-end test of the engine against a real homeserver, through the same
//! JSON commands the user interface sends.
//!
//! Start a test server with tools/synapse/start-synapse.sh, then:
//!   TMATRIX_TEST_HOMESERVER=http://127.0.0.1:8008 cargo test --release -- --nocapture
//! Without TMATRIX_TEST_HOMESERVER the test does nothing.
//
// SPDX-License-Identifier: GPL-2.0-or-later

use std::time::{Duration, Instant};

use serde_json::{json, Value};
use tmatrix_core::Core;

struct Client {
    name: &'static str,
    core: Core,
    next_id: u64,
    seen: Vec<Value>,
    _dir: std::path::PathBuf,
}

impl Client {
    fn new(name: &'static str) -> Client {
        let dir = std::env::temp_dir().join(format!("tmatrix-test-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let core = Core::new(dir.to_str().unwrap()).unwrap();
        Client { name, core, next_id: 1, seen: Vec::new(), _dir: dir }
    }

    /// Sends a command and waits for its reply (other events are kept).
    fn call(&mut self, cmd: &str, mut args: Value) -> Value {
        let id = self.next_id;
        self.next_id += 1;
        args["id"] = id.into();
        args["cmd"] = cmd.into();
        self.core.command(&args.to_string());
        let reply = self.wait(&format!("reply to {cmd}"), 120, |e| e["type"] == "reply" && e["id"] == id);
        assert!(reply["ok"] == true, "{}: {cmd} failed: {}", self.name, reply["error"]);
        reply
    }

    /// Waits for an event matching `pred` (also looking at events seen before).
    fn wait(&mut self, what: &str, secs: u64, pred: impl Fn(&Value) -> bool) -> Value {
        if let Some(pos) = self.seen.iter().position(|e| pred(e)) {
            return self.seen.remove(pos);
        }
        let end = Instant::now() + Duration::from_secs(secs);
        while Instant::now() < end {
            if let Some(s) = self.core.wait_event(500) {
                let ev: Value = serde_json::from_str(&s).unwrap();
                if pred(&ev) {
                    return ev;
                }
                self.seen.push(ev);
                if self.seen.len() > 5000 {
                    self.seen.drain(..1000);
                }
            }
        }
        panic!("{}: timed out waiting for {what}", self.name);
    }
}

/// Registers a new user with Synapse's shared-secret registration (the
/// secret of tools/synapse/start-synapse.sh), so every run starts fresh.
fn register(hs: &str, user: &str) {
    let secret = std::env::var("TMATRIX_TEST_SECRET").unwrap_or_else(|_| "tmatrix-test-secret".into());
    let script = r#"
import hmac, hashlib, json, sys, urllib.request
hs, user, secret = sys.argv[1:4]
url = hs + "/_synapse/admin/v1/register"
nonce = json.load(urllib.request.urlopen(url))["nonce"]
mac = hmac.new(secret.encode(), b"\0".join([nonce.encode(), user.encode(), b"secret-password", b"notadmin"]), hashlib.sha1).hexdigest()
body = json.dumps({"nonce": nonce, "username": user, "password": "secret-password", "admin": False, "mac": mac}).encode()
urllib.request.urlopen(urllib.request.Request(url, body, {"Content-Type": "application/json"}))
"#;
    let ok = std::process::Command::new("python3")
        .args(["-c", script, hs, user, &secret])
        .status()
        .map(|s| s.success())
        .unwrap_or(false);
    assert!(ok, "could not register {user}");
}

fn timeline_has(ev: &Value, room: &str, pred: &dyn Fn(&Value) -> bool) -> bool {
    if ev["room_id"] != room {
        return false;
    }
    let mut items: Vec<&Value> = Vec::new();
    if ev["type"] == "timeline_reset" {
        items.extend(ev["items"].as_array().into_iter().flatten());
    } else if ev["type"] == "timeline_diff" {
        for op in ev["ops"].as_array().into_iter().flatten() {
            items.extend(op["items"].as_array().into_iter().flatten());
            if !op["item"].is_null() {
                items.push(&op["item"]);
            }
        }
    }
    items.iter().any(|i| pred(i))
}

#[test]
fn chat_encryption_and_verification() {
    let Ok(hs) = std::env::var("TMATRIX_TEST_HOMESERVER") else {
        eprintln!("TMATRIX_TEST_HOMESERVER is not set, skipping");
        return;
    };
    let mut alice = Client::new("alice");
    let mut bob = Client::new("bob");
    let run = std::process::id();
    let (a_name, b_name) = (format!("alice{run}"), format!("bob{run}"));
    register(&hs, &a_name);
    register(&hs, &b_name);
    let (a_id, b_id) = (format!("@{a_name}:example.org"), format!("@{b_name}:example.org"));

    // login, and the session is handed to the user interface
    let a = alice.call("login", json!({ "homeserver": hs, "user": a_name, "password": "secret-password" }));
    assert_eq!(a["user_id"], a_id.as_str());
    alice.wait("session", 10, |e| e["type"] == "session" && e["session"].as_str().is_some_and(|s| s.contains("store_passphrase")));
    bob.call("login", json!({ "homeserver": hs, "user": b_name, "password": "secret-password" }));
    alice.wait("first sync", 60, |e| e["type"] == "sync_state" && e["state"] == "running");
    bob.wait("first sync", 60, |e| e["type"] == "sync_state" && e["state"] == "running");

    // profile
    alice.call("set_display_name", json!({ "name": "Alice" }));
    let p = alice.call("profile", json!({}));
    assert_eq!(p["name"], "Alice");

    // encrypted room with an invitation
    let r = alice.call("create_room", json!({ "name": "Secret plans", "encrypted": true, "invite": [b_id] }));
    let room = r["room_id"].as_str().unwrap().to_owned();
    bob.wait("invitation", 60, |e| e["type"] == "room" && e["room_id"] == room.as_str() && e["membership"] == "invite");
    bob.call("accept_invite", json!({ "room_id": room }));
    let mut joined = false;
    for _ in 0..60 {
        let m = alice.call("members", json!({ "room_id": room }));
        if m["members"].as_array().unwrap().iter().filter(|m| m["membership"] == "join").count() == 2 {
            joined = true;
            break;
        }
        std::thread::sleep(Duration::from_secs(1));
    }
    assert!(joined, "bob did not join");
    let room_ev = bob.wait("joined room", 60, |e| e["type"] == "room" && e["room_id"] == room.as_str() && e["membership"] == "join" && e["encrypted"] == true);
    let _ = room_ev;

    // an encrypted message, Markdown turned into HTML
    alice.call("open_room", json!({ "room_id": room }));
    bob.call("open_room", json!({ "room_id": room }));
    alice.call("send", json!({ "room_id": room, "body": "Hello **Bob**!" }));
    let r2 = room.clone();
    let ev = bob.wait("decrypted message", 90, move |e| {
        timeline_has(e, &r2, &|i| i["content"]["body"] == "Hello **Bob**!" && i["encrypted"] == true)
    });
    let text = ev.to_string();
    assert!(text.contains("<strong>Bob</strong>"), "no HTML body: {text}");

    // a reply and a reaction
    bob.call("send", json!({ "room_id": room, "body": "Hi Alice" }));
    let r3 = room.clone();
    let b3 = b_id.clone();
    let ev = alice.wait("bob's reply", 90, move |e| timeline_has(e, &r3, &|i| i["content"]["body"] == "Hi Alice" && i["sender"] == b3.as_str()));
    let _ = ev;

    // public room directory and joining by alias
    let alias = format!("trinity-{}", std::process::id());
    let pr = bob.call("create_room", json!({ "name": "Trinity Users", "public": true, "alias": alias, "topic": "TDE" }));
    let public_room = pr["room_id"].as_str().unwrap().to_owned();
    let dir = alice.call("directory", json!({ "term": "Trinity Users" }));
    assert!(dir["rooms"].to_string().contains(&public_room), "not in directory: {dir}");
    let j = alice.call("join", json!({ "target": format!("#{alias}:example.org") }));
    assert_eq!(j["room_id"], public_room.as_str());
    let m = alice.call("members", json!({ "room_id": public_room }));
    assert_eq!(m["members"].as_array().unwrap().len(), 2);

    // media: upload an image and download it again
    let img = std::env::temp_dir().join(format!("tmatrix-test-{}.png", std::process::id()));
    std::fs::write(&img, b"\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\0\x01\0\0\0\x01\x08\x06\0\0\0\x1f\x15\xc4\x89\0\0\0\rIDATx\x9cc\xf8\xff\xff?\0\x05\xfe\x02\xfe\xa7\x35\x81\x84\0\0\0\0IEND\xaeB`\x82").unwrap();
    alice.call("upload", json!({ "room_id": room, "path": img.to_str().unwrap() }));
    let r4 = room.clone();
    let ev = bob.wait("image", 90, move |e| timeline_has(e, &r4, &|i| i["content"]["type"] == "image"));
    let src = ev.to_string();
    let v: Value = serde_json::from_str(&src).unwrap();
    let mut source = Value::Null;
    for op in v["ops"].as_array().into_iter().flatten().chain(std::iter::once(&json!({ "items": v["items"] }))) {
        for i in op["items"].as_array().into_iter().flatten().chain(op.get("item")) {
            if i["content"]["type"] == "image" {
                source = i["content"]["source"].clone();
            }
        }
    }
    assert!(source.to_string().contains("\"file\""), "media in an encrypted room must be encrypted: {source}");
    let media = bob.call("media", json!({ "source": source }));
    let data = std::fs::read(media["path"].as_str().unwrap()).unwrap();
    assert!(data.starts_with(b"\x89PNG"), "decrypted media differs");

    // interactive verification (SAS emoji) between alice and bob
    let req = alice.call("verify_user", json!({ "user_id": b_id }));
    let flow = req["flow_id"].as_str().unwrap().to_owned();
    // the request is sent in a direct chat, which matrix-sdk creates
    // (on a reused test server the direct chat exists already)
    for _ in 0..10 {
        let rooms = bob.call("rooms", json!({}));
        let invites: Vec<Value> = rooms["rooms"].as_array().unwrap().iter().filter(|r| r["membership"] == "invite").cloned().collect();
        for r in &invites {
            bob.call("accept_invite", json!({ "room_id": r["room_id"] }));
        }
        if !invites.is_empty() {
            break;
        }
        std::thread::sleep(Duration::from_secs(1));
    }
    let f2 = flow.clone();
    let incoming = bob.wait("verification request", 60, move |e| e["type"] == "verification_request" && e["flow_id"] == f2.as_str());
    assert_eq!(incoming["user_id"], a_id.as_str());
    bob.call("verification_accept", json!({ "user_id": a_id, "flow_id": flow }));
    let f3 = flow.clone();
    let ea = alice.wait("emoji (alice)", 60, move |e| e["type"] == "verification_state" && e["flow_id"] == f3.as_str() && e["state"] == "compare");
    let f4 = flow.clone();
    let eb = bob.wait("emoji (bob)", 60, move |e| e["type"] == "verification_state" && e["flow_id"] == f4.as_str() && e["state"] == "compare");
    assert_eq!(ea["emoji"].as_array().map(|a| a.len()), Some(7));
    assert_eq!(ea["emoji"], eb["emoji"], "both sides must show the same emoji");
    alice.call("verification_confirm", json!({ "user_id": b_id, "flow_id": flow }));
    bob.call("verification_confirm", json!({ "user_id": a_id, "flow_id": flow }));
    let f5 = flow.clone();
    alice.wait("verified (alice)", 60, move |e| e["type"] == "verification_state" && e["flow_id"] == f5.as_str() && e["state"] == "done");
    let f6 = flow.clone();
    bob.wait("verified (bob)", 60, move |e| e["type"] == "verification_state" && e["flow_id"] == f6.as_str() && e["state"] == "done");
    let mut verified = false;
    for _ in 0..30 {
        let t = alice.call("user_trust", json!({ "user_id": b_id }));
        if t["verified"] == true {
            verified = true;
            break;
        }
        std::thread::sleep(Duration::from_secs(1));
    }
    if !verified {
        eprintln!("alice: {}", alice.call("encryption_status", json!({})));
        eprintln!("bob: {}", bob.call("encryption_status", json!({})));
    }
    assert!(verified, "bob is not verified for alice");

    // a second session of alice is verified by the first one
    let mut alice2 = Client::new("alice2");
    alice2.call("login", json!({ "homeserver": hs, "user": a_name, "password": "secret-password" }));
    alice2.wait("first sync", 60, |e| e["type"] == "sync_state" && e["state"] == "running");
    let st = alice2.call("encryption_status", json!({}));
    assert_eq!(st["device_verified"], false, "a new session must not be verified: {st}");
    let req = alice2.call("verify_own_device", json!({}));
    let flow = req["flow_id"].as_str().unwrap().to_owned();
    let f = flow.clone();
    let incoming = alice.wait("self-verification request", 60, move |e| e["type"] == "verification_request" && e["flow_id"] == f.as_str());
    assert_eq!(incoming["self"], true);
    alice.call("verification_accept", json!({ "user_id": a_id, "flow_id": flow }));
    for c in [&mut alice, &mut alice2] {
        let f = flow.clone();
        c.wait("emoji", 60, move |e| e["type"] == "verification_state" && e["flow_id"] == f.as_str() && e["state"] == "compare");
    }
    alice.call("verification_confirm", json!({ "user_id": a_id, "flow_id": flow }));
    alice2.call("verification_confirm", json!({ "user_id": a_id, "flow_id": flow }));
    for c in [&mut alice, &mut alice2] {
        let f = flow.clone();
        c.wait("done", 60, move |e| e["type"] == "verification_state" && e["flow_id"] == f.as_str() && e["state"] == "done");
    }
    let mut ok = false;
    let mut st = Value::Null;
    for _ in 0..30 {
        st = alice2.call("encryption_status", json!({}));
        if st["device_verified"] == true && st["cross_signing"] == true {
            ok = true;
            break;
        }
        std::thread::sleep(Duration::from_secs(1));
    }
    assert!(ok, "the second session is not verified or has no cross-signing keys: {st}");
    // and it can read the history of the encrypted room through key sharing
    alice2.call("open_room", json!({ "room_id": room }));
    let r5 = room.clone();
    alice2.wait("old message decrypted", 90, move |e| {
        timeline_has(e, &r5, &|i| i["content"]["body"] == "Hello **Bob**!" && i["content"]["type"] == "text")
    });

    // recovery: back up the keys and get a recovery key
    let rec = alice.call("enable_recovery", json!({}));
    assert!(rec["key"].as_str().is_some_and(|k| k.len() > 40));
    let st = alice.call("encryption_status", json!({}));
    assert_eq!(st["recovery"], "enabled");
    assert_eq!(st["cross_signing"], true);

    // sessions
    let d = alice.call("devices", json!({}));
    assert!(d["devices"].as_array().unwrap().iter().any(|d| d["current"] == true));

    // leave
    bob.call("leave", json!({ "room_id": public_room }));
    eprintln!("all good");
}
