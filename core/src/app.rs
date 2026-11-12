//! The commands of the engine.
//
// SPDX-License-Identifier: GPL-2.0-or-later

use std::collections::{HashMap, HashSet};
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use futures_util::StreamExt;
use matrix_sdk::authentication::matrix::MatrixSession;
use matrix_sdk::config::SyncSettings;
use matrix_sdk::encryption::verification::{SasState, SasVerification, Verification, VerificationRequest, VerificationRequestState};
use matrix_sdk::encryption::{BackupDownloadStrategy, EncryptionSettings};
use matrix_sdk::media::{MediaFormat, MediaRequestParameters, MediaThumbnailSettings};
use matrix_sdk::notification_settings::RoomNotificationMode;
use matrix_sdk::ruma::api::client::directory::get_public_rooms_filtered;
use matrix_sdk::ruma::api::client::room::create_room::v3::Request as CreateRoomRequest;
use matrix_sdk::ruma::api::client::room::Visibility;
use matrix_sdk::ruma::api::client::uiaa;
use matrix_sdk::ruma::directory::Filter;
use matrix_sdk::ruma::events::key::verification::request::ToDeviceKeyVerificationRequestEvent;
use matrix_sdk::ruma::events::room::encryption::RoomEncryptionEventContent;
use matrix_sdk::ruma::events::room::message::{
    OriginalSyncRoomMessageEvent, RoomMessageEventContent, RoomMessageEventContentWithoutRelation,
};
use matrix_sdk::ruma::events::room::MediaSource;
use matrix_sdk::ruma::events::{AnySyncTimelineEvent, InitialStateEvent};
use matrix_sdk::ruma::{
    OwnedDeviceId, OwnedEventId, OwnedMxcUri, OwnedRoomId, OwnedRoomOrAliasId, OwnedServerName,
    OwnedUserId, RoomId, UInt, UserId,
};
use matrix_sdk::sync::SyncResponse;
use matrix_sdk::{Client, LoopCtrl, Room, RoomMemberships, RoomState, SessionMeta, SessionTokens};
use matrix_sdk_ui::timeline::{RoomExt, Timeline, TimelineEventItemId};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tokio::sync::Mutex;
use tokio::task::JoinHandle;

use crate::emitter::Emitter;
use crate::items;
use crate::rooms::room_json;

type Res<T> = Result<T, String>;

fn err<E: std::fmt::Display>(e: E) -> String {
    e.to_string()
}

/// What the user interface keeps (in TDEWallet) to restore a login.
#[derive(Serialize, Deserialize, Clone)]
struct StoredSession {
    homeserver: String,
    user_id: String,
    device_id: String,
    access_token: String,
    refresh_token: Option<String>,
    store_passphrase: String,
    /// Directory of the encrypted stores below the data directory.
    #[serde(default)]
    store_dir: Option<String>,
}

struct OpenTimeline {
    timeline: Arc<Timeline>,
    task: JoinHandle<()>,
}

#[derive(Default)]
struct State {
    client: Option<Client>,
    sync_task: Option<JoinHandle<()>>,
    side_tasks: Vec<JoinHandle<()>>,
    timelines: HashMap<OwnedRoomId, OpenTimeline>,
    /// SSO logins wait for the browser; this cancels them.
    sso_task: Option<JoinHandle<()>>,
    /// Store directory of the current session.
    store_dir: Option<String>,
}

pub struct App {
    data_dir: PathBuf,
    emitter: Emitter,
    state: Mutex<State>,
}

fn s<'a>(v: &'a Value, key: &str) -> Res<&'a str> {
    v.get(key).and_then(|x| x.as_str()).ok_or_else(|| format!("missing '{key}'"))
}

fn opt_s<'a>(v: &'a Value, key: &str) -> Option<&'a str> {
    v.get(key).and_then(|x| x.as_str()).filter(|x| !x.is_empty())
}

fn room_id(v: &Value) -> Res<OwnedRoomId> {
    RoomId::parse(s(v, "room_id")?).map_err(err)
}

fn user_id(v: &Value, key: &str) -> Res<OwnedUserId> {
    UserId::parse(s(v, key)?.trim()).map_err(err)
}

fn event_id(v: &Value) -> Res<OwnedEventId> {
    OwnedEventId::try_from(s(v, "event_id")?).map_err(err)
}

fn random_passphrase() -> String {
    use rand::RngCore;
    let mut bytes = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut bytes);
    hex::encode(bytes)
}

fn store_dir_name(user_id: &str) -> String {
    user_id.chars().map(|c| if c.is_ascii_alphanumeric() || c == '.' || c == '-' { c } else { '_' }).collect()
}

fn password_auth(user: &UserId, password: &str, session: Option<String>) -> uiaa::AuthData {
    let mut p = uiaa::Password::new(uiaa::UserIdentifier::Matrix(uiaa::MatrixUserIdentifier::new(user.to_string())), password.to_owned());
    p.session = session;
    uiaa::AuthData::Password(p)
}

impl App {
    pub fn new(data_dir: PathBuf, emitter: Emitter) -> App {
        App { data_dir, emitter, state: Mutex::new(State::default()) }
    }

    pub async fn shutdown(&self) {
        let mut st = self.state.lock().await;
        Self::stop_locked(&mut st);
    }

    fn stop_locked(st: &mut State) {
        if let Some(t) = st.sync_task.take() {
            t.abort();
        }
        for t in st.side_tasks.drain(..) {
            t.abort();
        }
        for (_, t) in st.timelines.drain() {
            t.task.abort();
        }
        if let Some(t) = st.sso_task.take() {
            t.abort();
        }
        st.client = None;
    }

    async fn client(&self) -> Res<Client> {
        self.state.lock().await.client.clone().ok_or_else(|| "not logged in".to_owned())
    }

    async fn room(&self, v: &Value) -> Res<Room> {
        let client = self.client().await?;
        let id = room_id(v)?;
        client.get_room(&id).ok_or_else(|| format!("unknown room {id}"))
    }

    async fn timeline(&self, v: &Value) -> Res<Arc<Timeline>> {
        let id = room_id(v)?;
        if let Some(t) = self.state.lock().await.timelines.get(&id) {
            return Ok(t.timeline.clone());
        }
        let room = self.room(v).await?;
        let t = room.timeline().await.map_err(err)?;
        Ok(Arc::new(t))
    }

    pub async fn handle(self: Arc<Self>, json: &str) {
        let v: Value = match serde_json::from_str(json) {
            Ok(v) => v,
            Err(e) => {
                self.emitter.emit("reply", json!({ "id": -1, "ok": false, "error": e.to_string() }));
                return;
            }
        };
        let id = v.get("id").cloned().unwrap_or(Value::Null);
        let cmd = v.get("cmd").and_then(|c| c.as_str()).unwrap_or("").to_owned();
        let result = self.clone().dispatch(&cmd, &v).await;
        let reply = match result {
            Ok(mut data) => {
                if !data.is_object() {
                    data = json!({ "data": data });
                }
                data["ok"] = true.into();
                data
            }
            Err(e) => json!({ "ok": false, "error": e }),
        };
        let mut reply = reply;
        reply["id"] = id;
        reply["cmd"] = cmd.into();
        self.emitter.emit("reply", reply);
    }

    async fn dispatch(self: Arc<Self>, cmd: &str, v: &Value) -> Res<Value> {
        match cmd {
            "discover" => self.discover(v).await,
            "login" => self.login(v).await,
            "login_sso" => self.login_sso(v).await,
            "cancel_sso" => {
                if let Some(t) = self.state.lock().await.sso_task.take() {
                    t.abort();
                }
                Ok(json!({}))
            }
            "restore" => self.restore(v).await,
            "logout" => self.logout().await,
            "close_session" => {
                Self::stop_locked(&mut *self.state.lock().await);
                Ok(json!({}))
            }
            "rooms" => self.all_rooms().await,
            "open_room" => self.open_room(v).await,
            "close_room" => {
                let id = room_id(v)?;
                if let Some(t) = self.state.lock().await.timelines.remove(&id) {
                    t.task.abort();
                }
                Ok(json!({}))
            }
            "paginate" => {
                let t = self.timeline(v).await?;
                let n = v.get("count").and_then(|c| c.as_u64()).unwrap_or(30) as u16;
                let at_start = t.paginate_backwards(n).await.map_err(err)?;
                Ok(json!({ "at_start": at_start, "room_id": s(v, "room_id")? }))
            }
            "send" => self.send(v).await,
            "edit" => {
                let t = self.timeline(v).await?;
                let eid = event_id(v)?;
                let body = s(v, "body")?;
                let content = RoomMessageEventContentWithoutRelation::text_markdown(body);
                t.edit(&TimelineEventItemId::EventId(eid), matrix_sdk::room::edit::EditedContent::RoomMessage(content))
                    .await
                    .map_err(err)?;
                Ok(json!({}))
            }
            "redact" => {
                let t = self.timeline(v).await?;
                let eid = event_id(v)?;
                t.redact(&TimelineEventItemId::EventId(eid), opt_s(v, "reason")).await.map_err(err)?;
                Ok(json!({}))
            }
            "react" => {
                let t = self.timeline(v).await?;
                let eid = event_id(v)?;
                t.toggle_reaction(&TimelineEventItemId::EventId(eid), s(v, "key")?).await.map_err(err)?;
                Ok(json!({}))
            }
            "mark_read" => {
                let t = self.timeline(v).await?;
                t.mark_as_read(matrix_sdk::ruma::api::client::receipt::create_receipt::v3::ReceiptType::Read)
                    .await
                    .map_err(err)?;
                Ok(json!({}))
            }
            "typing" => {
                let room = self.room(v).await?;
                let typing = v.get("typing").and_then(|t| t.as_bool()).unwrap_or(false);
                room.typing_notice(typing).await.map_err(err)?;
                Ok(json!({}))
            }
            "upload" => self.upload(v).await,
            "create_room" => self.create_room(v).await,
            "start_dm" => self.start_dm(v).await,
            "join" => {
                let client = self.client().await?;
                let target = OwnedRoomOrAliasId::try_from(s(v, "target")?.trim()).map_err(err)?;
                let servers: Vec<OwnedServerName> = target
                    .server_name()
                    .map(|s| vec![s.to_owned()])
                    .unwrap_or_default();
                let room = client.join_room_by_id_or_alias(&target, &servers).await.map_err(err)?;
                self.emit_room(&room).await;
                Ok(json!({ "room_id": room.room_id().to_string() }))
            }
            "accept_invite" => {
                let room = self.room(v).await?;
                room.join().await.map_err(err)?;
                self.emit_room(&room).await;
                Ok(json!({ "room_id": room.room_id().to_string() }))
            }
            "leave" | "decline_invite" => {
                let room = self.room(v).await?;
                room.leave().await.map_err(err)?;
                self.emitter.emit("room_removed", json!({ "room_id": room.room_id().to_string() }));
                Ok(json!({}))
            }
            "forget" => {
                let room = self.room(v).await?;
                room.forget().await.map_err(err)?;
                self.emitter.emit("room_removed", json!({ "room_id": room.room_id().to_string() }));
                Ok(json!({}))
            }
            "invite" => {
                let room = self.room(v).await?;
                room.invite_user_by_id(&user_id(v, "user_id")?).await.map_err(err)?;
                Ok(json!({}))
            }
            "kick" => {
                let room = self.room(v).await?;
                room.kick_user(&user_id(v, "user_id")?, opt_s(v, "reason")).await.map_err(err)?;
                Ok(json!({}))
            }
            "ban" => {
                let room = self.room(v).await?;
                room.ban_user(&user_id(v, "user_id")?, opt_s(v, "reason")).await.map_err(err)?;
                Ok(json!({}))
            }
            "members" => self.members(v).await,
            "set_room_name" => {
                let room = self.room(v).await?;
                room.set_name(s(v, "name")?.to_owned()).await.map_err(err)?;
                Ok(json!({}))
            }
            "set_room_topic" => {
                let room = self.room(v).await?;
                room.set_room_topic(s(v, "topic")?).await.map_err(err)?;
                Ok(json!({}))
            }
            "set_room_avatar" => {
                let room = self.room(v).await?;
                let path = s(v, "path")?;
                let data = tokio::fs::read(path).await.map_err(err)?;
                let mime = mime_guess::from_path(path).first_or_octet_stream();
                room.upload_avatar(&mime, data, None).await.map_err(err)?;
                Ok(json!({}))
            }
            "enable_encryption" => {
                let room = self.room(v).await?;
                room.enable_encryption().await.map_err(err)?;
                self.emit_room(&room).await;
                Ok(json!({}))
            }
            "set_favourite" | "set_low_priority" => {
                let room = self.room(v).await?;
                let on = v.get("value").and_then(|t| t.as_bool()).unwrap_or(false);
                if cmd == "set_favourite" {
                    room.set_is_favourite(on, None).await.map_err(err)?;
                } else {
                    room.set_is_low_priority(on, None).await.map_err(err)?;
                }
                Ok(json!({}))
            }
            "room_notifications" => self.room_notifications(v).await,
            "set_room_notifications" => self.set_room_notifications(v).await,
            "directory" => self.directory(v).await,
            "search_users" => {
                let client = self.client().await?;
                let r = client.search_users(s(v, "term")?, 30).await.map_err(err)?;
                let users: Vec<Value> = r
                    .results
                    .iter()
                    .map(|u| json!({ "user_id": u.user_id.to_string(), "name": u.display_name, "avatar": u.avatar_url.as_ref().map(|a| a.to_string()) }))
                    .collect();
                Ok(json!({ "users": users }))
            }
            "profile" => {
                let client = self.client().await?;
                let account = client.account();
                let name = account.get_display_name().await.map_err(err)?;
                let avatar = account.get_avatar_url().await.map_err(err)?;
                Ok(json!({
                    "user_id": client.user_id().map(|u| u.to_string()),
                    "device_id": client.device_id().map(|d| d.to_string()),
                    "homeserver": client.homeserver().to_string(),
                    "name": name,
                    "avatar": avatar.map(|a| a.to_string()),
                }))
            }
            "set_display_name" => {
                let client = self.client().await?;
                client.account().set_display_name(opt_s(v, "name")).await.map_err(err)?;
                Ok(json!({}))
            }
            "set_avatar" => {
                let client = self.client().await?;
                match opt_s(v, "path") {
                    Some(path) => {
                        let data = tokio::fs::read(path).await.map_err(err)?;
                        let mime = mime_guess::from_path(path).first_or_octet_stream();
                        client.account().upload_avatar(&mime, data).await.map_err(err)?;
                    }
                    None => client.account().set_avatar_url(None).await.map_err(err)?,
                }
                Ok(json!({}))
            }
            "change_password" => self.change_password(v).await,
            "devices" => self.devices().await,
            "rename_device" => {
                let client = self.client().await?;
                let dev: OwnedDeviceId = s(v, "device_id")?.into();
                client.rename_device(&dev, s(v, "name")?).await.map_err(err)?;
                Ok(json!({}))
            }
            "delete_devices" => self.delete_devices(v).await,
            "media" => self.media(v).await,
            "save_media" => self.save_media(v).await,
            "encryption_status" => self.encryption_status().await,
            "enable_recovery" => self.enable_recovery(v).await,
            "recover" => {
                let client = self.client().await?;
                client.encryption().recovery().recover(s(v, "key")?.trim()).await.map_err(err)?;
                self.emit_encryption_status().await;
                Ok(json!({}))
            }
            "reset_recovery_key" => {
                let client = self.client().await?;
                let key = client.encryption().recovery().reset_key().await.map_err(err)?;
                Ok(json!({ "key": key }))
            }
            "bootstrap_cross_signing" => {
                let client = self.client().await?;
                let auth = match opt_s(v, "password") {
                    Some(p) => Some(password_auth(client.user_id().ok_or("no user")?, p, None)),
                    None => None,
                };
                client.encryption().bootstrap_cross_signing_if_needed(auth).await.map_err(err)?;
                self.emit_encryption_status().await;
                Ok(json!({}))
            }
            "verify_own_device" | "verify_device" => {
                let client = self.client().await?;
                let request = if cmd == "verify_own_device" {
                    let own = client.user_id().ok_or("no user")?.to_owned();
                    let identity = client
                        .encryption()
                        .get_user_identity(&own)
                        .await
                        .map_err(err)?
                        .ok_or("cross-signing is not set up for this account")?;
                    identity.request_verification().await.map_err(err)?
                } else {
                    let uid = user_id(v, "user_id")?;
                    let dev: OwnedDeviceId = s(v, "device_id")?.into();
                    let device = client
                        .encryption()
                        .get_device(&uid, &dev)
                        .await
                        .map_err(err)?
                        .ok_or("unknown device")?;
                    device.request_verification().await.map_err(err)?
                };
                let flow = request.flow_id().to_owned();
                let other = request.other_user_id().to_string();
                self.clone().watch_request(request);
                Ok(json!({ "flow_id": flow, "user_id": other }))
            }
            "verify_user" => {
                let client = self.client().await?;
                let uid = user_id(v, "user_id")?;
                let identity = client
                    .encryption()
                    .get_user_identity(&uid)
                    .await
                    .map_err(err)?
                    .ok_or("this user has not set up cross-signing")?;
                let request = identity.request_verification().await.map_err(err)?;
                let flow = request.flow_id().to_owned();
                self.clone().watch_request(request);
                Ok(json!({ "flow_id": flow, "user_id": uid.to_string() }))
            }
            "verification_accept" => {
                let req = self.find_request(v).await?;
                req.accept().await.map_err(err)?;
                Ok(json!({}))
            }
            "verification_start_sas" => {
                let req = self.find_request(v).await?;
                let sas = req.start_sas().await.map_err(err)?.ok_or("the other side cannot verify with emoji")?;
                self.clone().watch_sas(sas, req.flow_id().to_owned());
                Ok(json!({}))
            }
            "verification_confirm" | "verification_mismatch" | "verification_cancel" => {
                let client = self.client().await?;
                let uid = user_id(v, "user_id")?;
                let flow = s(v, "flow_id")?;
                match client.encryption().get_verification(&uid, flow).await {
                    Some(Verification::SasV1(sas)) => match cmd {
                        "verification_confirm" => sas.confirm().await.map_err(err)?,
                        "verification_mismatch" => sas.mismatch().await.map_err(err)?,
                        _ => sas.cancel().await.map_err(err)?,
                    },
                    _ => {
                        if let Some(req) = client.encryption().get_verification_request(&uid, flow).await {
                            req.cancel().await.map_err(err)?;
                        }
                    }
                }
                Ok(json!({}))
            }
            "user_trust" => {
                let client = self.client().await?;
                let uid = user_id(v, "user_id")?;
                let identity = client.encryption().get_user_identity(&uid).await.map_err(err)?;
                Ok(json!({
                    "user_id": uid.to_string(),
                    "has_identity": identity.is_some(),
                    "verified": identity.as_ref().map(|i| i.is_verified()).unwrap_or(false),
                }))
            }
            _ => Err(format!("unknown command '{cmd}'")),
        }
    }

    // ---- login -------------------------------------------------------------

    async fn build_client(&self, homeserver: &str, user_dir: &str, passphrase: &str, fresh: bool) -> Res<Client> {
        let dir = self.data_dir.join(user_dir);
        if fresh && dir.exists() {
            let _ = tokio::fs::remove_dir_all(&dir).await;
        }
        tokio::fs::create_dir_all(&dir).await.map_err(err)?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let _ = std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o700));
        }
        Client::builder()
            .server_name_or_homeserver_url(homeserver.trim())
            .sqlite_store(&dir, Some(passphrase))
            .with_encryption_settings(EncryptionSettings {
                auto_enable_cross_signing: true,
                backup_download_strategy: BackupDownloadStrategy::AfterDecryptionFailure,
                auto_enable_backups: true,
            })
            .handle_refresh_tokens()
            .build()
            .await
            .map_err(err)
    }

    async fn discover(&self, v: &Value) -> Res<Value> {
        let client = Client::builder()
            .server_name_or_homeserver_url(s(v, "homeserver")?.trim())
            .build()
            .await
            .map_err(err)?;
        let types = client.matrix_auth().get_login_types().await.map_err(err)?;
        use matrix_sdk::ruma::api::client::session::get_login_types::v3::LoginType;
        let mut password = false;
        let mut sso = false;
        for t in &types.flows {
            match t {
                LoginType::Password(_) => password = true,
                LoginType::Sso(_) => sso = true,
                _ => {}
            }
        }
        Ok(json!({ "homeserver": client.homeserver().to_string(), "password": password, "sso": sso }))
    }

    async fn finish_login(&self, client: Client, passphrase: String, store_dir: String) -> Res<Value> {
        let session = client.matrix_auth().session().ok_or("no session")?;
        let stored = StoredSession {
            homeserver: client.homeserver().to_string(),
            user_id: session.meta.user_id.to_string(),
            device_id: session.meta.device_id.to_string(),
            access_token: session.tokens.access_token.clone(),
            refresh_token: session.tokens.refresh_token.clone(),
            store_passphrase: passphrase,
            store_dir: Some(store_dir.clone()),
        };
        self.state.lock().await.store_dir = Some(store_dir);
        self.emitter.emit("session", json!({ "user_id": stored.user_id, "session": serde_json::to_string(&stored).unwrap() }));
        self.start(client.clone()).await;
        Ok(json!({ "user_id": stored.user_id, "device_id": stored.device_id, "homeserver": stored.homeserver }))
    }

    fn device_name(v: &Value) -> String {
        opt_s(v, "device_name").map(|s| s.to_owned()).unwrap_or_else(|| "TMatrix (Trinity)".to_owned())
    }

    async fn login(&self, v: &Value) -> Res<Value> {
        Self::stop_locked(&mut *self.state.lock().await);
        let hs = s(v, "homeserver")?;
        let user = s(v, "user")?.trim().to_owned();
        let password = s(v, "password")?;
        // every login (device) gets its own store; the session remembers where
        let dir = format!("store-{}", &random_passphrase()[..16]);
        let passphrase = random_passphrase();
        let client = self.build_client(hs, &dir, &passphrase, true).await?;
        let res = client
            .matrix_auth()
            .login_username(&user, password)
            .initial_device_display_name(&Self::device_name(v))
            .send()
            .await;
        if let Err(e) = res {
            drop(client);
            let _ = tokio::fs::remove_dir_all(self.data_dir.join(&dir)).await;
            return Err(err(e));
        }
        self.finish_login(client, passphrase, dir).await
    }

    async fn login_sso(self: Arc<Self>, v: &Value) -> Res<Value> {
        Self::stop_locked(&mut *self.state.lock().await);
        let hs = s(v, "homeserver")?.to_owned();
        let name = Self::device_name(v);
        let request_id = v.get("id").cloned().unwrap_or(Value::Null);
        let this = self.clone();
        // the login waits for the browser, which may take minutes: answer at once
        // and send "sso_finished" when done
        let task = tokio::spawn(async move {
            let result: Res<Value> = async {
                let dir = format!("store-{}", &random_passphrase()[..16]);
                let passphrase = random_passphrase();
                let client = this.build_client(&hs, &dir, &passphrase, true).await?;
                let emitter = this.emitter.clone();
                let res = client
                    .matrix_auth()
                    .login_sso(move |url| {
                        let emitter = emitter.clone();
                        async move {
                            emitter.emit("open_url", json!({ "url": url }));
                            Ok(())
                        }
                    })
                    .initial_device_display_name(&name)
                    .server_builder(matrix_sdk::utils::local_server::LocalServerBuilder::new().response(
                        matrix_sdk::utils::local_server::LocalServerResponse::Html(
                            "<html><body><h2>TMatrix</h2><p>You are logged in. You can close this page.</p></body></html>".to_owned(),
                        ),
                    ))
                    .send()
                    .await;
                if let Err(e) = res {
                    drop(client);
                    let _ = tokio::fs::remove_dir_all(this.data_dir.join(&dir)).await;
                    return Err(err(e));
                }
                this.finish_login(client, passphrase, dir).await
            }
            .await;
            let mut ev = match result {
                Ok(v) => v,
                Err(e) => json!({ "error": e }),
            };
            ev["request_id"] = request_id;
            this.emitter.emit("sso_finished", ev);
        });
        self.state.lock().await.sso_task = Some(task);
        Ok(json!({ "pending": true }))
    }

    async fn restore(&self, v: &Value) -> Res<Value> {
        Self::stop_locked(&mut *self.state.lock().await);
        let stored: StoredSession = serde_json::from_str(s(v, "session")?).map_err(err)?;
        let dir = stored.store_dir.clone().unwrap_or_else(|| store_dir_name(&stored.user_id));
        let client = self.build_client(&stored.homeserver, &dir, &stored.store_passphrase, false).await?;
        client
            .matrix_auth()
            .restore_session(
                MatrixSession {
                    meta: SessionMeta {
                        user_id: UserId::parse(&stored.user_id).map_err(err)?,
                        device_id: stored.device_id.as_str().into(),
                    },
                    tokens: SessionTokens { access_token: stored.access_token.clone(), refresh_token: stored.refresh_token.clone() },
                },
                matrix_sdk::store::RoomLoadSettings::default(),
            )
            .await
            .map_err(err)?;
        self.start(client).await;
        self.state.lock().await.store_dir = Some(dir);
        Ok(json!({ "user_id": stored.user_id, "device_id": stored.device_id, "homeserver": stored.homeserver }))
    }

    async fn logout(&self) -> Res<Value> {
        let client = self.client().await?;
        let user = client.user_id().map(|u| u.to_string()).unwrap_or_default();
        let res = client.matrix_auth().logout().await;
        let dir = {
            let mut st = self.state.lock().await;
            Self::stop_locked(&mut st);
            st.store_dir.take()
        };
        drop(client);
        if let Some(dir) = dir {
            let _ = tokio::fs::remove_dir_all(self.data_dir.join(dir)).await;
        }
        let _ = tokio::fs::remove_dir_all(self.data_dir.join(store_dir_name(&user))).await;
        res.map_err(err)?;
        Ok(json!({ "user_id": user }))
    }

    // ---- sync --------------------------------------------------------------

    async fn start(&self, client: Client) {
        let emitter = self.emitter.clone();
        if let Err(e) = client.event_cache().subscribe() {
            tracing::warn!("event cache: {e}");
        }

        // tokens refreshed by the SDK must reach the wallet
        let mut side = Vec::new();
        {
            let c = client.clone();
            let emitter = emitter.clone();
            let passphrase_holder = self.data_dir.clone();
            let _ = passphrase_holder;
            side.push(tokio::spawn(async move {
                let mut changes = c.subscribe_to_session_changes();
                while let Ok(change) = changes.recv().await {
                    match change {
                        matrix_sdk::SessionChange::TokensRefreshed => {
                            if let Some(tokens) = c.session_tokens() {
                                emitter.emit(
                                    "tokens",
                                    json!({ "access_token": tokens.access_token, "refresh_token": tokens.refresh_token }),
                                );
                            }
                        }
                        matrix_sdk::SessionChange::UnknownToken(_) => {
                            emitter.emit("sync_state", json!({ "state": "logged_out", "error": "The server ended this session." }));
                        }
                    }
                }
            }));
        }

        // verification requests from other devices and users
        {
            let this_emitter = emitter.clone();
            client.add_event_handler(move |ev: ToDeviceKeyVerificationRequestEvent, c: Client| {
                let emitter = this_emitter.clone();
                async move {
                    if let Some(req) = c.encryption().get_verification_request(&ev.sender, &ev.content.transaction_id).await {
                        emit_request(&emitter, &req);
                        spawn_request_watch(emitter, req);
                    }
                }
            });
            let this_emitter = emitter.clone();
            client.add_event_handler(move |ev: OriginalSyncRoomMessageEvent, c: Client| {
                let emitter = this_emitter.clone();
                async move {
                    if let matrix_sdk::ruma::events::room::message::MessageType::VerificationRequest(_) = &ev.content.msgtype {
                        if Some(ev.sender.as_ref()) == c.user_id() {
                            return;
                        }
                        if let Some(req) = c.encryption().get_verification_request(&ev.sender, ev.event_id.as_str()).await {
                            emit_request(&emitter, &req);
                            spawn_request_watch(emitter, req);
                        }
                    }
                }
            });
        }

        // notifications by the push rules
        {
            let emitter = emitter.clone();
            client
                .register_notification_handler(move |n, room, c| {
                    let emitter = emitter.clone();
                    async move {
                        if !n.actions.iter().any(|a| a.should_notify()) {
                            return;
                        }
                        let raw = match &n.event {
                            matrix_sdk::deserialized_responses::RawAnySyncOrStrippedTimelineEvent::Sync(raw) => raw,
                            _ => {
                                if room.state() == RoomState::Invited {
                                    emitter.emit("notification", json!({ "room_id": room.room_id().to_string(), "invite": true, "room_name": room.display_name().await.map(|n| n.to_string()).unwrap_or_default() }));
                                }
                                return;
                            }
                        };
                        let Ok(ev) = raw.deserialize() else { return };
                        if Some(ev.sender()) == c.user_id() {
                            return;
                        }
                        let body = match &ev {
                            AnySyncTimelineEvent::MessageLike(m) => match m.original_content() {
                                Some(matrix_sdk::ruma::events::AnyMessageLikeEventContent::RoomMessage(c)) => {
                                    if c.relates_to.as_ref().is_some_and(|r| matches!(r, matrix_sdk::ruma::events::room::message::Relation::Replacement(_))) {
                                        return;
                                    }
                                    items::message_json(&c.msgtype)
                                }
                                Some(matrix_sdk::ruma::events::AnyMessageLikeEventContent::RoomEncrypted(_)) => json!({ "type": "utd", "body": "" }),
                                _ => return,
                            },
                            _ => return,
                        };
                        let sender_name = match room.get_member_no_sync(ev.sender()).await {
                            Ok(Some(m)) => m.display_name().map(|s| s.to_owned()),
                            _ => None,
                        };
                        emitter.emit(
                            "notification",
                            json!({
                                "room_id": room.room_id().to_string(),
                                "room_name": room.display_name().await.map(|n| n.to_string()).unwrap_or_default(),
                                "direct": room.is_direct().await.unwrap_or(false),
                                "sender": ev.sender().to_string(),
                                "sender_name": sender_name,
                                "event_id": ev.event_id().to_string(),
                                "content": body,
                                "highlight": n.actions.iter().any(|a| a.is_highlight()),
                            }),
                        );
                    }
                })
                .await;
        }

        // recovery state for the encryption banner
        {
            let c = client.clone();
            let emitter = emitter.clone();
            side.push(tokio::spawn(async move {
                let mut stream = c.encryption().recovery().state_stream();
                while let Some(_state) = stream.next().await {
                    emit_encryption(&emitter, &c).await;
                }
            }));
        }

        let user = client.user_id().map(|u| u.to_string()).unwrap_or_default();
        emitter.emit("logged_in", json!({ "user_id": user, "device_id": client.device_id().map(|d| d.to_string()), "homeserver": client.homeserver().to_string() }));

        // known rooms from the store first
        for room in client.rooms() {
            emitter.emit("room", room_json(&room).await);
        }

        let c = client.clone();
        let emitter2 = emitter.clone();
        let sync = tokio::spawn(async move {
            let first = Arc::new(std::sync::atomic::AtomicBool::new(true));
            let mut backoff = 1u64;
            loop {
                let em = emitter2.clone();
                let cc = c.clone();
                let settings = SyncSettings::default().timeout(Duration::from_secs(30));
                let result = c
                    .sync_with_result_callback(settings, |res: Result<SyncResponse, matrix_sdk::Error>| {
                        let em = em.clone();
                        let cc = cc.clone();
                        let was_first = first.swap(false, std::sync::atomic::Ordering::SeqCst);
                        async move {
                            let res = res?;
                            if was_first {
                                em.emit("sync_state", json!({ "state": "running" }));
                                for room in cc.rooms() {
                                    em.emit("room", room_json(&room).await);
                                }
                                emit_encryption(&em, &cc).await;
                            } else {
                                let mut ids: HashSet<OwnedRoomId> = HashSet::new();
                                ids.extend(res.rooms.joined.keys().cloned());
                                ids.extend(res.rooms.invited.keys().cloned());
                                ids.extend(res.rooms.left.keys().cloned());
                                for id in ids {
                                    if let Some(room) = cc.get_room(&id) {
                                        em.emit("room", room_json(&room).await);
                                    }
                                }
                            }
                            Ok(LoopCtrl::Continue)
                        }
                    })
                    .await;
                match result {
                    Ok(()) => break,
                    Err(e) => {
                        let msg = e.to_string();
                        let soft_logout = e.client_api_error_kind().is_some_and(|k| format!("{k:?}").starts_with("UnknownToken"));
                        emitter2.emit("sync_state", json!({ "state": if soft_logout { "logged_out" } else { "error" }, "error": msg }));
                        if soft_logout {
                            break;
                        }
                        tokio::time::sleep(Duration::from_secs(backoff)).await;
                        backoff = (backoff * 2).min(60);
                        first.store(true, std::sync::atomic::Ordering::SeqCst);
                    }
                }
            }
        });

        let mut st = self.state.lock().await;
        st.client = Some(client);
        st.sync_task = Some(sync);
        st.side_tasks = side;
    }

    async fn all_rooms(&self) -> Res<Value> {
        let client = self.client().await?;
        let mut rooms = Vec::new();
        for room in client.rooms() {
            rooms.push(room_json(&room).await);
        }
        Ok(json!({ "rooms": rooms }))
    }

    async fn emit_room(&self, room: &Room) {
        self.emitter.emit("room", room_json(room).await);
    }

    // ---- timeline ----------------------------------------------------------

    async fn open_room(&self, v: &Value) -> Res<Value> {
        let id = room_id(v)?;
        if let Some(t) = self.state.lock().await.timelines.remove(&id) {
            t.task.abort();
        }
        let room = self.room(v).await?;
        let timeline = Arc::new(room.timeline().await.map_err(err)?);
        let (items, mut stream) = timeline.subscribe().await;
        let rid = id.to_string();
        self.emitter.emit(
            "timeline_reset",
            json!({ "room_id": rid, "items": items.iter().map(|i| items::item_json(i)).collect::<Vec<_>>() }),
        );
        let emitter = self.emitter.clone();
        let rid2 = rid.clone();
        let task = tokio::spawn(async move {
            use eyeball_im::VectorDiff;
            while let Some(diffs) = stream.next().await {
                let mut ops = Vec::new();
                for d in diffs {
                    let op = match d {
                        VectorDiff::Append { values } => json!({ "op": "append", "items": values.iter().map(|i| items::item_json(i)).collect::<Vec<_>>() }),
                        VectorDiff::Clear => json!({ "op": "clear" }),
                        VectorDiff::PushFront { value } => json!({ "op": "insert", "index": 0, "item": items::item_json(&value) }),
                        VectorDiff::PushBack { value } => json!({ "op": "append", "items": [items::item_json(&value)] }),
                        VectorDiff::PopFront => json!({ "op": "remove", "index": 0 }),
                        VectorDiff::PopBack => json!({ "op": "pop_back" }),
                        VectorDiff::Insert { index, value } => json!({ "op": "insert", "index": index, "item": items::item_json(&value) }),
                        VectorDiff::Set { index, value } => json!({ "op": "set", "index": index, "item": items::item_json(&value) }),
                        VectorDiff::Remove { index } => json!({ "op": "remove", "index": index }),
                        VectorDiff::Truncate { length } => json!({ "op": "truncate", "length": length }),
                        VectorDiff::Reset { values } => json!({ "op": "reset", "items": values.iter().map(|i| items::item_json(i)).collect::<Vec<_>>() }),
                    };
                    ops.push(op);
                }
                emitter.emit("timeline_diff", json!({ "room_id": rid2, "ops": ops }));
            }
        });
        // load some history at once so the view is not empty
        let t2 = timeline.clone();
        let emitter = self.emitter.clone();
        let rid3 = rid.clone();
        tokio::spawn(async move {
            if let Ok(at_start) = t2.paginate_backwards(40).await {
                emitter.emit("timeline_paginated", json!({ "room_id": rid3, "at_start": at_start }));
            }
        });
        self.state.lock().await.timelines.insert(id, OpenTimeline { timeline, task });
        Ok(json!({ "room_id": rid }))
    }

    async fn send(&self, v: &Value) -> Res<Value> {
        let t = self.timeline(v).await?;
        let body = s(v, "body")?;
        let content = if v.get("emote").and_then(|e| e.as_bool()).unwrap_or(false) {
            RoomMessageEventContent::emote_markdown(body)
        } else if v.get("plain").and_then(|e| e.as_bool()).unwrap_or(false) {
            RoomMessageEventContent::text_plain(body)
        } else {
            RoomMessageEventContent::text_markdown(body)
        };
        if let Some(reply) = opt_s(v, "reply_to") {
            let eid = OwnedEventId::try_from(reply).map_err(err)?;
            t.send_reply(content.into(), eid).await.map_err(err)?;
        } else {
            t.send(content.into()).await.map_err(err)?;
        }
        Ok(json!({}))
    }

    async fn upload(&self, v: &Value) -> Res<Value> {
        let t = self.timeline(v).await?;
        let path = PathBuf::from(s(v, "path")?);
        let mime = mime_guess::from_path(&path).first_or_octet_stream();
        let mut config = matrix_sdk_ui::timeline::AttachmentConfig::default();
        if let Some(caption) = opt_s(v, "caption") {
            config.caption = Some(matrix_sdk::ruma::events::room::message::TextMessageEventContent::plain(caption));
        }
        t.send_attachment(path, mime, config).await.map_err(err)?;
        Ok(json!({}))
    }

    // ---- rooms -------------------------------------------------------------

    async fn create_room(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let mut req = CreateRoomRequest::new();
        req.name = opt_s(v, "name").map(|s| s.to_owned());
        req.topic = opt_s(v, "topic").map(|s| s.to_owned());
        let public = v.get("public").and_then(|p| p.as_bool()).unwrap_or(false);
        if public {
            req.visibility = Visibility::Public;
            req.preset = Some(matrix_sdk::ruma::api::client::room::create_room::v3::RoomPreset::PublicChat);
        } else {
            req.preset = Some(matrix_sdk::ruma::api::client::room::create_room::v3::RoomPreset::PrivateChat);
        }
        if let Some(alias) = opt_s(v, "alias") {
            req.room_alias_name = Some(alias.trim_start_matches('#').split(':').next().unwrap_or(alias).to_owned());
        }
        if v.get("encrypted").and_then(|p| p.as_bool()).unwrap_or(!public) {
            let content = RoomEncryptionEventContent::with_recommended_defaults();
            req.initial_state = vec![InitialStateEvent::with_empty_state_key(content).to_raw_any()];
        }
        if let Some(list) = v.get("invite").and_then(|l| l.as_array()) {
            req.invite = list.iter().filter_map(|u| u.as_str()).filter_map(|u| UserId::parse(u.trim()).ok()).collect();
        }
        let room = client.create_room(req).await.map_err(err)?;
        self.emit_room(&room).await;
        Ok(json!({ "room_id": room.room_id().to_string() }))
    }

    async fn start_dm(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let uid = user_id(v, "user_id")?;
        if let Some(room) = client.get_dm_room(&uid) {
            return Ok(json!({ "room_id": room.room_id().to_string(), "existing": true }));
        }
        let room = client.create_dm(&uid).await.map_err(err)?;
        self.emit_room(&room).await;
        Ok(json!({ "room_id": room.room_id().to_string() }))
    }

    async fn members(&self, v: &Value) -> Res<Value> {
        let room = self.room(v).await?;
        let members = room.members(RoomMemberships::JOIN | RoomMemberships::INVITE).await.map_err(err)?;
        let own = room.own_user_id().to_owned();
        let me = room.get_member_no_sync(&own).await.ok().flatten();
        let mut list = Vec::new();
        for m in members {
            list.push(json!({
                "user_id": m.user_id().to_string(),
                "name": m.display_name(),
                "avatar": m.avatar_url().map(|a| a.to_string()),
                "power": match m.power_level() { matrix_sdk::ruma::events::room::power_levels::UserPowerLevel::Int(i) => i64::from(i), _ => 100 },
                "membership": m.membership().to_string(),
            }));
        }
        let can = |action| me.as_ref().map(|m| m.can_do(action)).unwrap_or(false);
        use matrix_sdk::ruma::events::room::power_levels::PowerLevelAction as A;
        use matrix_sdk::ruma::events::{MessageLikeEventType, StateEventType};
        Ok(json!({
            "room_id": room.room_id().to_string(),
            "members": list,
            "can_invite": can(A::Invite),
            "can_kick": can(A::Kick),
            "can_ban": can(A::Ban),
            "can_set_name": can(A::SendState(StateEventType::RoomName)),
            "can_set_topic": can(A::SendState(StateEventType::RoomTopic)),
            "can_set_avatar": can(A::SendState(StateEventType::RoomAvatar)),
            "can_encrypt": can(A::SendState(StateEventType::RoomEncryption)),
            "can_redact": can(A::RedactOther),
            "can_send": can(A::SendMessage(MessageLikeEventType::RoomMessage)),
        }))
    }

    async fn room_notifications(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let id = room_id(v)?;
        let settings = client.notification_settings().await;
        let mode = settings.get_user_defined_room_notification_mode(&id).await;
        Ok(json!({ "mode": match mode {
            Some(RoomNotificationMode::AllMessages) => "all",
            Some(RoomNotificationMode::MentionsAndKeywordsOnly) => "mentions",
            Some(RoomNotificationMode::Mute) => "mute",
            None => "default",
        }}))
    }

    async fn set_room_notifications(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let id = room_id(v)?;
        let settings = client.notification_settings().await;
        match s(v, "mode")? {
            "all" => settings.set_room_notification_mode(&id, RoomNotificationMode::AllMessages).await,
            "mentions" => settings.set_room_notification_mode(&id, RoomNotificationMode::MentionsAndKeywordsOnly).await,
            "mute" => settings.set_room_notification_mode(&id, RoomNotificationMode::Mute).await,
            _ => settings.delete_user_defined_room_rules(&id).await,
        }
        .map_err(err)?;
        Ok(json!({}))
    }

    async fn directory(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let mut req = get_public_rooms_filtered::v3::Request::new();
        let mut filter = Filter::new();
        filter.generic_search_term = opt_s(v, "term").map(|s| s.to_owned());
        req.filter = filter;
        req.limit = Some(UInt::from(50u32));
        req.since = opt_s(v, "since").map(|s| s.to_owned());
        if let Some(server) = opt_s(v, "server") {
            req.server = Some(OwnedServerName::try_from(server.trim()).map_err(err)?);
        }
        let res = client.public_rooms_filtered(req).await.map_err(err)?;
        let rooms: Vec<Value> = res
            .chunk
            .iter()
            .map(|r| {
                json!({
                    "room_id": r.room_id.to_string(),
                    "name": r.name,
                    "topic": r.topic,
                    "alias": r.canonical_alias.as_ref().map(|a| a.to_string()),
                    "members": u64::from(r.num_joined_members),
                    "avatar": r.avatar_url.as_ref().map(|a| a.to_string()),
                    "joined": client.get_room(&r.room_id).map(|room| room.state() == RoomState::Joined).unwrap_or(false),
                })
            })
            .collect();
        Ok(json!({ "rooms": rooms, "next": res.next_batch, "total": res.total_room_count_estimate.map(u64::from) }))
    }

    // ---- account -----------------------------------------------------------

    async fn change_password(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let user = client.user_id().ok_or("no user")?.to_owned();
        let new = s(v, "new_password")?;
        let old = s(v, "old_password")?;
        match client.account().change_password(new, None).await {
            Ok(_) => Ok(json!({})),
            Err(e) => {
                let Some(info) = e.as_uiaa_response() else { return Err(err(e)) };
                client
                    .account()
                    .change_password(new, Some(password_auth(&user, old, info.session.clone())))
                    .await
                    .map_err(err)?;
                Ok(json!({}))
            }
        }
    }

    async fn devices(&self) -> Res<Value> {
        let client = self.client().await?;
        let own = client.device_id().map(|d| d.to_owned());
        let user = client.user_id().ok_or("no user")?.to_owned();
        let res = client.devices().await.map_err(err)?;
        let mut list = Vec::new();
        for d in res.devices {
            let verified = match client.encryption().get_device(&user, &d.device_id).await {
                Ok(Some(dev)) => dev.is_cross_signed_by_owner(),
                _ => false,
            };
            list.push(json!({
                "device_id": d.device_id.to_string(),
                "name": d.display_name,
                "last_seen_ip": d.last_seen_ip,
                "last_seen_ts": d.last_seen_ts.map(|t| u64::from(t.0)),
                "verified": verified,
                "current": Some(&d.device_id) == own.as_ref(),
            }));
        }
        Ok(json!({ "devices": list }))
    }

    async fn delete_devices(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let user = client.user_id().ok_or("no user")?.to_owned();
        let ids: Vec<OwnedDeviceId> = v
            .get("device_ids")
            .and_then(|l| l.as_array())
            .map(|l| l.iter().filter_map(|d| d.as_str()).map(|d| d.into()).collect())
            .unwrap_or_default();
        if ids.is_empty() {
            return Ok(json!({}));
        }
        match client.delete_devices(&ids, None).await {
            Ok(_) => Ok(json!({})),
            Err(e) => {
                let Some(info) = e.as_uiaa_response() else { return Err(err(e)) };
                let password = opt_s(v, "password").ok_or("password required")?;
                client
                    .delete_devices(&ids, Some(password_auth(&user, password, info.session.clone())))
                    .await
                    .map_err(err)?;
                Ok(json!({}))
            }
        }
    }

    // ---- media -------------------------------------------------------------

    fn media_source(v: &Value) -> Res<MediaSource> {
        if let Some(src) = v.get("source").filter(|s| !s.is_null()) {
            return serde_json::from_value(src.clone()).map_err(err);
        }
        let url = s(v, "url")?;
        Ok(MediaSource::Plain(OwnedMxcUri::from(url)))
    }

    /// Fetches media into the cache directory and answers with its path.
    async fn media(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let source = Self::media_source(v)?;
        let width = v.get("width").and_then(|w| w.as_u64()).unwrap_or(0) as u32;
        let height = v.get("height").and_then(|w| w.as_u64()).unwrap_or(0) as u32;
        let format = if width > 0 && height > 0 && matches!(source, MediaSource::Plain(_)) {
            MediaFormat::Thumbnail(MediaThumbnailSettings::new(UInt::from(width), UInt::from(height)))
        } else {
            MediaFormat::File
        };
        let key = {
            use sha2::{Digest, Sha256};
            let mut h = Sha256::new();
            h.update(serde_json::to_string(&source).unwrap_or_default());
            h.update(format!("{width}x{height}"));
            hex::encode(h.finalize())
        };
        let user = client.user_id().map(|u| store_dir_name(u.as_str())).unwrap_or_default();
        let dir = self.data_dir.join(&user).join("media-cache");
        let path = dir.join(&key);
        if !path.exists() {
            let data = client
                .media()
                .get_media_content(&MediaRequestParameters { source, format }, true)
                .await
                .map_err(err)?;
            tokio::fs::create_dir_all(&dir).await.map_err(err)?;
            let tmp = dir.join(format!("{key}.part"));
            tokio::fs::write(&tmp, data).await.map_err(err)?;
            tokio::fs::rename(&tmp, &path).await.map_err(err)?;
        }
        Ok(json!({ "path": path.to_string_lossy() }))
    }

    async fn save_media(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let source = Self::media_source(v)?;
        let target = s(v, "path")?;
        let data = client
            .media()
            .get_media_content(&MediaRequestParameters { source, format: MediaFormat::File }, true)
            .await
            .map_err(err)?;
        tokio::fs::write(target, data).await.map_err(err)?;
        Ok(json!({ "path": target }))
    }

    // ---- encryption --------------------------------------------------------

    async fn encryption_status(&self) -> Res<Value> {
        let client = self.client().await?;
        Ok(encryption_json(&client).await)
    }

    async fn emit_encryption_status(&self) {
        if let Ok(client) = self.client().await {
            emit_encryption(&self.emitter, &client).await;
        }
    }

    async fn enable_recovery(&self, v: &Value) -> Res<Value> {
        let client = self.client().await?;
        let recovery = client.encryption().recovery();
        let enable = recovery.enable().wait_for_backups_to_upload();
        let key = match opt_s(v, "passphrase") {
            Some(p) => enable.with_passphrase(p).await,
            None => enable.await,
        }
        .map_err(err)?;
        self.emit_encryption_status().await;
        Ok(json!({ "key": key }))
    }

    async fn find_request(&self, v: &Value) -> Res<VerificationRequest> {
        let client = self.client().await?;
        let uid = user_id(v, "user_id")?;
        client
            .encryption()
            .get_verification_request(&uid, s(v, "flow_id")?)
            .await
            .ok_or_else(|| "the verification request is gone".to_owned())
    }

    fn watch_request(self: Arc<Self>, req: VerificationRequest) {
        spawn_request_watch(self.emitter.clone(), req);
    }

    fn watch_sas(self: Arc<Self>, sas: SasVerification, flow_id: String) {
        spawn_sas_watch(self.emitter.clone(), sas, flow_id);
    }
}

async fn encryption_json(client: &Client) -> Value {
    use matrix_sdk::encryption::recovery::RecoveryState;
    let enc = client.encryption();
    let cs = enc.cross_signing_status().await;
    let own_verified = match enc.get_own_device().await {
        Ok(Some(d)) => d.is_cross_signed_by_owner(),
        _ => false,
    };
    let backup = enc.backups().state();
    json!({
        "recovery": match enc.recovery().state() {
            RecoveryState::Enabled => "enabled",
            RecoveryState::Disabled => "disabled",
            RecoveryState::Incomplete => "incomplete",
            RecoveryState::Unknown => "unknown",
        },
        "cross_signing": cs.as_ref().map(|c| c.is_complete()).unwrap_or(false),
        "has_master_key": cs.as_ref().map(|c| c.has_master).unwrap_or(false),
        "device_verified": own_verified,
        "backup": format!("{backup:?}").to_lowercase(),
        "ed25519": enc.ed25519_key().await,
        "curve25519": enc.curve25519_key().await.map(|k| k.to_base64()),
    })
}

async fn emit_encryption(emitter: &Emitter, client: &Client) {
    emitter.emit("encryption", encryption_json(client).await);
}

fn emit_request(emitter: &Emitter, req: &VerificationRequest) {
    emitter.emit(
        "verification_request",
        json!({
            "flow_id": req.flow_id(),
            "user_id": req.other_user_id().to_string(),
            "self": req.is_self_verification(),
            "we_started": req.we_started(),
            "room_id": req.room_id().map(|r| r.to_string()),
        }),
    );
}

fn spawn_request_watch(emitter: Emitter, req: VerificationRequest) {
    tokio::spawn(async move {
        let mut changes = req.changes();
        let base = json!({ "flow_id": req.flow_id(), "user_id": req.other_user_id().to_string() });
        while let Some(state) = changes.next().await {
            let mut ev = base.clone();
            match state {
                VerificationRequestState::Ready { .. } => {
                    ev["state"] = "ready".into();
                    emitter.emit("verification_state", ev);
                    // whoever started it begins the emoji comparison
                    if req.we_started() {
                        if let Ok(Some(sas)) = req.start_sas().await {
                            spawn_sas_watch(emitter.clone(), sas, req.flow_id().to_owned());
                        }
                    }
                }
                VerificationRequestState::Transitioned { verification, .. } => {
                    if let Verification::SasV1(sas) = verification {
                        if !sas.we_started() {
                            let _ = sas.accept().await;
                        }
                        spawn_sas_watch(emitter.clone(), sas, req.flow_id().to_owned());
                    }
                    break;
                }
                VerificationRequestState::Done => {
                    ev["state"] = "done".into();
                    emitter.emit("verification_state", ev);
                    break;
                }
                VerificationRequestState::Cancelled(info) => {
                    ev["state"] = "cancelled".into();
                    ev["reason"] = info.reason().into();
                    emitter.emit("verification_state", ev);
                    break;
                }
                _ => {}
            }
        }
    });
}

fn spawn_sas_watch(emitter: Emitter, sas: SasVerification, flow_id: String) {
    tokio::spawn(async move {
        let base = json!({ "flow_id": flow_id, "user_id": sas.other_user_id().to_string(), "device_id": sas.other_device().device_id().to_string() });
        let mut changes = sas.changes();
        while let Some(state) = changes.next().await {
            let mut ev = base.clone();
            match state {
                SasState::KeysExchanged { emojis, decimals } => {
                    ev["state"] = "compare".into();
                    if let Some(e) = emojis {
                        ev["emoji"] = e
                            .emojis
                            .iter()
                            .map(|e| json!({ "symbol": e.symbol, "description": e.description }))
                            .collect::<Vec<_>>()
                            .into();
                    }
                    ev["decimals"] = json!([decimals.0, decimals.1, decimals.2]);
                    emitter.emit("verification_state", ev);
                }
                SasState::Confirmed => {
                    ev["state"] = "waiting".into();
                    emitter.emit("verification_state", ev);
                }
                SasState::Done { .. } => {
                    ev["state"] = "done".into();
                    emitter.emit("verification_state", ev);
                    break;
                }
                SasState::Cancelled(info) => {
                    ev["state"] = "cancelled".into();
                    ev["reason"] = info.reason().into();
                    emitter.emit("verification_state", ev);
                    break;
                }
                _ => {}
            }
        }
    });
}
