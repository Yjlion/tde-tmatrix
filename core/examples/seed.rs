//! Fills the test homeserver with conversations for the screenshots, using
//! the engine itself (so direct chats and private rooms are really
//! encrypted).  The users bob, carol and a second session of alice ("Alice's
//! phone") take part; the sessions are kept in a state directory so the
//! phases can run as separate processes.
//!
//!   seed <homeserver> <state dir> setup      # names, avatars, rooms, invites
//!   seed <homeserver> <state dir> messages   # the conversations
//!   seed <homeserver> <state dir> verify     # alice's phone answers a verification
//!                                            # and confirms once <state dir>/confirm exists
//
// SPDX-License-Identifier: GPL-2.0-or-later

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use serde_json::{json, Value};
use tmatrix_core::Core;

struct User {
    name: String,
    core: Core,
    next_id: u64,
    seen: Vec<Value>,
}

impl User {
    fn open(hs: &str, state: &Path, name: &str, login: &str, device: &str) -> User {
        let dir = state.join(name);
        std::fs::create_dir_all(&dir).unwrap();
        let core = Core::new(dir.to_str().unwrap()).unwrap();
        let mut u = User { name: name.into(), core, next_id: 1, seen: Vec::new() };
        let session_file = state.join(format!("{name}.session"));
        if let Ok(s) = std::fs::read_to_string(&session_file) {
            u.call("restore", json!({ "session": s }));
        } else {
            u.call("login", json!({ "homeserver": hs, "user": login, "password": "secret-password", "device_name": device }));
            let ev = u.wait("session", 30, |e| e["type"] == "session");
            std::fs::write(&session_file, ev["session"].as_str().unwrap()).unwrap();
        }
        u.wait("sync", 60, |e| e["type"] == "sync_state" && e["state"] == "running");
        u
    }

    fn call(&mut self, cmd: &str, mut args: Value) -> Value {
        let id = self.next_id;
        self.next_id += 1;
        args["id"] = id.into();
        args["cmd"] = cmd.into();
        self.core.command(&args.to_string());
        let r = self.wait(cmd, 120, |e| e["type"] == "reply" && e["id"] == id);
        if r["ok"] != true {
            eprintln!("{}: {cmd} failed: {}", self.name, r["error"]);
        }
        r
    }

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
            }
        }
        panic!("{}: timed out waiting for {what}", self.name);
    }

    fn room_by_name(&mut self, name: &str) -> String {
        let r = self.call("rooms", json!({}));
        for room in r["rooms"].as_array().unwrap() {
            if room["name"] == name {
                return room["room_id"].as_str().unwrap().to_owned();
            }
        }
        // a direct chat is named after the other person, which may still be loading
        for room in r["rooms"].as_array().unwrap() {
            if name.starts_with("Alice") && room["direct"] == true {
                return room["room_id"].as_str().unwrap().to_owned();
            }
        }
        panic!("{}: no room {name}", self.name);
    }

    fn send(&mut self, room: &str, body: &str) {
        self.call("send", json!({ "room_id": room, "body": body }));
        std::thread::sleep(Duration::from_millis(700));
    }

    fn settle(&mut self, ms: u64) {
        let end = Instant::now() + Duration::from_millis(ms);
        while Instant::now() < end {
            if let Some(s) = self.core.wait_event(100) {
                self.seen.push(serde_json::from_str(&s).unwrap());
            }
        }
        self.seen.clear();
    }
}

fn accept_all_invites(u: &mut User) {
    let r = u.call("rooms", json!({}));
    let ids: Vec<String> = r["rooms"]
        .as_array()
        .unwrap()
        .iter()
        .filter(|r| r["membership"] == "invite")
        .map(|r| r["room_id"].as_str().unwrap().to_owned())
        .collect();
    for id in ids {
        u.call("accept_invite", json!({ "room_id": id }));
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let hs = args.get(1).expect("homeserver").clone();
    let state = PathBuf::from(args.get(2).expect("state dir"));
    let phase = args.get(3).map(|s| s.as_str()).unwrap_or("setup");
    std::fs::create_dir_all(&state).unwrap();
    let avatars = state.join("avatars");

    match phase {
        "setup" => {
            let mut bob = User::open(&hs, &state, "bob", "bob", "Bob's laptop");
            let mut carol = User::open(&hs, &state, "carol", "carol", "Carol's desktop");
            let mut phone = User::open(&hs, &state, "alice-phone", "alice", "Alice's phone");
            bob.call("set_display_name", json!({ "name": "Bob Martin" }));
            carol.call("set_display_name", json!({ "name": "Carol Nguyen" }));
            phone.call("set_display_name", json!({ "name": "Alice Walker" }));
            for (u, f) in [(&mut bob, "bob.png"), (&mut carol, "carol.png"), (&mut phone, "alice.png")] {
                let p = avatars.join(f);
                if p.exists() {
                    u.call("set_avatar", json!({ "path": p.to_str().unwrap() }));
                }
            }
            // shared-secret registration leaves an unnamed session behind
            let d = phone.call("devices", json!({}));
            let extra: Vec<Value> = d["devices"]
                .as_array()
                .unwrap()
                .iter()
                .filter(|d| d["current"] != true && d["name"].is_null())
                .map(|d| d["device_id"].clone())
                .collect();
            if !extra.is_empty() {
                phone.call("delete_devices", json!({ "device_ids": extra, "password": "secret-password" }));
            }
            // alice's phone sets up cross-signing and recovery, as a first app would
            let _ = phone.call("enable_recovery", json!({}));

            // an encrypted direct chat bob <-> alice
            bob.call("start_dm", json!({ "user_id": "@alice:example.org" }));
            // a private encrypted room
            let r = carol.call("create_room", json!({ "name": "Trinity Developers", "topic": "Coordinating the next R14 release",
                "encrypted": true, "invite": ["@alice:example.org", "@bob:example.org"] }));
            let dev = r["room_id"].as_str().unwrap().to_owned();
            let p = avatars.join("room-dev.png");
            if p.exists() {
                carol.call("set_room_avatar", json!({ "room_id": dev, "path": p.to_str().unwrap() }));
            }
            // public rooms for the directory
            let r = bob.call("create_room", json!({ "name": "TDE Users", "topic": "Help and chat for users of the Trinity Desktop Environment",
                "public": true, "alias": "tde-users" }));
            let users = r["room_id"].as_str().unwrap().to_owned();
            let p = avatars.join("room-users.png");
            if p.exists() {
                bob.call("set_room_avatar", json!({ "room_id": users, "path": p.to_str().unwrap() }));
            }
            carol.call("create_room", json!({ "name": "Retro Computing", "topic": "Amiga, Atari, C64 and everything with a blinking cursor",
                "public": true, "alias": "retro" }));
            carol.call("create_room", json!({ "name": "Linux Audio", "topic": "PipeWire, JACK and making music on Linux",
                "public": true, "alias": "linux-audio" }));
            bob.call("create_room", json!({ "name": "Debian Packaging", "topic": "Questions about building .deb packages",
                "public": true, "alias": "debian-packaging" }));
            carol.call("join", json!({ "target": "#tde-users:example.org" }));
            bob.settle(3000);
            accept_all_invites(&mut bob);
            accept_all_invites(&mut phone);
            phone.call("join", json!({ "target": "#tde-users:example.org" }));
            phone.call("join", json!({ "target": "#retro:example.org" }));
            phone.call("set_favourite", json!({ "room_id": dev, "value": true }));
            let _ = users;
            println!("setup done");
        }
        "messages" => {
            let mut bob = User::open(&hs, &state, "bob", "bob", "Bob's laptop");
            let mut carol = User::open(&hs, &state, "carol", "carol", "Carol's desktop");
            let mut phone = User::open(&hs, &state, "alice-phone", "alice", "Alice's phone");
            bob.settle(3000);
            let dm = bob.room_by_name("Alice Walker");
            let dev = carol.room_by_name("Trinity Developers");
            let users = bob.room_by_name("TDE Users");
            let retro = carol.room_by_name("Retro Computing");

            carol.send(&retro, "Found a boxed Amiga 1200 at a flea market today 🎉");
            phone.send(&retro, "Nice! Does it still boot?");

            bob.send(&users, "Welcome to **TDE Users**! Ask anything about Trinity here.");
            carol.send(&users, "Is there a way to get the new mixer into the panel?");
            bob.send(&users, "Yes: install `tde-mixer-trinity`, it replaces KMix in the tray.");

            carol.send(&dev, "Release checklist for R14.1.8:\n\n- [x] tdelibs builds on trixie\n- [ ] update the translations\n- [ ] test on bookworm");
            bob.send(&dev, "I can take the bookworm tests this weekend.");
            phone.send(&dev, "I'll do the translations. Who has the *Weblate* login?");
            carol.send(&dev, "I'll send it to you in a DM 👍");

            bob.send(&dm, "Hi Alice! Did you try the new Matrix client for Trinity?");
            phone.send(&dm, "Yes, I'm writing this from TMatrix 😀 It does end-to-end encryption, so this chat is private.");
            bob.send(&dm, "Great. Can you send me the photo from the **Trinity meetup**?");
            let img = avatars.join("meetup.jpg");
            if img.exists() {
                phone.call("upload", json!({ "room_id": dm, "path": img.to_str().unwrap() }));
                std::thread::sleep(Duration::from_millis(1500));
            }
            phone.send(&dm, "Here it is. Next one is in March, are you coming?");
            // a reply and a reaction by bob
            let ev = bob.call("open_room", json!({ "room_id": dm }));
            let _ = ev;
            bob.settle(4000);
            let r = bob.call("paginate", json!({ "room_id": dm, "count": 20 }));
            let _ = r;
            bob.send(&dm, "> Next one is in March, are you coming?\n\nWouldn't miss it! I'll bring the old **ThinkPad** running R14 🙂");
            bob.settle(1500);

            // an invitation that stays open
            let r = carol.call("create_room", json!({ "name": "Book Club", "topic": "This month: The Cathedral and the Bazaar",
                "encrypted": true, "invite": ["@alice:example.org"] }));
            let _ = r;
            bob.settle(3000);
            let ids = json!({ "dm": dm, "dev": dev, "users": users, "retro": retro });
            std::fs::write(state.join("rooms.json"), ids.to_string()).unwrap();
            println!("messages done");
        }
        "react" => {
            let mut bob = User::open(&hs, &state, "bob", "bob", "Bob's laptop");
            bob.settle(2000);
            let dm = bob.room_by_name("Alice Walker");
            let event = args.get(4).cloned().unwrap_or_default();
            bob.call("react", json!({ "room_id": dm, "event_id": event, "key": "❤️" }));
            bob.settle(2000);
        }
        "verify" => {
            let mut phone = User::open(&hs, &state, "alice-phone", "alice", "Alice's phone");
            let req = phone.wait("verification request", 300, |e| e["type"] == "verification_request");
            let flow = req["flow_id"].as_str().unwrap().to_owned();
            let user = req["user_id"].as_str().unwrap().to_owned();
            phone.call("verification_accept", json!({ "user_id": user, "flow_id": flow }));
            let f = flow.clone();
            phone.wait("compare", 120, move |e| e["type"] == "verification_state" && e["flow_id"] == f.as_str() && e["state"] == "compare");
            println!("comparing");
            let flag = state.join("confirm");
            let end = Instant::now() + Duration::from_secs(300);
            while !flag.exists() && Instant::now() < end {
                phone.settle(500);
            }
            phone.call("verification_confirm", json!({ "user_id": user, "flow_id": flow }));
            let f = flow.clone();
            phone.wait("done", 120, move |e| e["type"] == "verification_state" && e["flow_id"] == f.as_str() && e["state"] == "done");
            // the signatures and the secrets for the other session are sent
            // by the sync loop: stay a while
            phone.settle(20000);
            println!("verified");
        }
        _ => panic!("unknown phase {phase}"),
    }
}
