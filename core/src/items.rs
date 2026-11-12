//! Converts timeline items of matrix-sdk-ui into the JSON the user interface
//! renders.
//
// SPDX-License-Identifier: GPL-2.0-or-later

use matrix_sdk::ruma::events::room::message::{MessageFormat, MessageType};
use matrix_sdk::ruma::events::room::MediaSource;
use matrix_sdk_ui::timeline::{
    EventSendState, EventTimelineItem, MembershipChange, MsgLikeKind, TimelineDetails,
    TimelineEventShieldState, TimelineItem, TimelineItemContent, TimelineItemKind,
    VirtualTimelineItem,
};
use serde_json::{json, Value};

pub fn media_source(source: &MediaSource) -> Value {
    serde_json::to_value(source).unwrap_or(Value::Null)
}

fn html_of(msgtype: &MessageType) -> Option<String> {
    let formatted = match msgtype {
        MessageType::Text(c) => c.formatted.as_ref(),
        MessageType::Notice(c) => c.formatted.as_ref(),
        MessageType::Emote(c) => c.formatted.as_ref(),
        MessageType::Image(c) => c.formatted_caption(),
        MessageType::File(c) => c.formatted_caption(),
        MessageType::Video(c) => c.formatted_caption(),
        MessageType::Audio(c) => c.formatted_caption(),
        _ => None,
    }?;
    (formatted.format == MessageFormat::Html).then(|| formatted.body.clone())
}

/// The content of an m.room.message.
pub fn message_json(msgtype: &MessageType) -> Value {
    let mut v = json!({ "body": msgtype.body() });
    if let Some(html) = html_of(msgtype) {
        v["html"] = html.into();
    }
    match msgtype {
        MessageType::Text(_) => v["type"] = "text".into(),
        MessageType::Notice(_) | MessageType::ServerNotice(_) => v["type"] = "notice".into(),
        MessageType::Emote(_) => v["type"] = "emote".into(),
        MessageType::Image(c) => {
            v["type"] = "image".into();
            v["filename"] = c.filename().into();
            v["caption"] = c.caption().map(|s| s.to_owned()).into();
            v["source"] = media_source(&c.source);
            if let Some(info) = &c.info {
                v["width"] = info.width.map(|n| u64::from(n)).into();
                v["height"] = info.height.map(|n| u64::from(n)).into();
                v["mimetype"] = info.mimetype.clone().into();
                v["size"] = info.size.map(|n| u64::from(n)).into();
                if let Some(t) = &info.thumbnail_source {
                    v["thumbnail"] = media_source(t);
                }
            }
        }
        MessageType::File(c) => {
            v["type"] = "file".into();
            v["filename"] = c.filename().into();
            v["caption"] = c.caption().map(|s| s.to_owned()).into();
            v["source"] = media_source(&c.source);
            if let Some(info) = &c.info {
                v["mimetype"] = info.mimetype.clone().into();
                v["size"] = info.size.map(|n| u64::from(n)).into();
            }
        }
        MessageType::Video(c) => {
            v["type"] = "video".into();
            v["filename"] = c.filename().into();
            v["caption"] = c.caption().map(|s| s.to_owned()).into();
            v["source"] = media_source(&c.source);
            if let Some(info) = &c.info {
                v["mimetype"] = info.mimetype.clone().into();
                v["size"] = info.size.map(|n| u64::from(n)).into();
                if let Some(t) = &info.thumbnail_source {
                    v["thumbnail"] = media_source(t);
                }
            }
        }
        MessageType::Audio(c) => {
            v["type"] = "audio".into();
            v["filename"] = c.filename().into();
            v["caption"] = c.caption().map(|s| s.to_owned()).into();
            v["source"] = media_source(&c.source);
            if let Some(info) = &c.info {
                v["mimetype"] = info.mimetype.clone().into();
                v["size"] = info.size.map(|n| u64::from(n)).into();
            }
        }
        MessageType::Location(c) => {
            v["type"] = "location".into();
            v["geo_uri"] = c.geo_uri.clone().into();
        }
        MessageType::VerificationRequest(_) => v["type"] = "verification".into(),
        _ => v["type"] = "text".into(),
    }
    v
}

fn membership_change(c: &MembershipChange) -> &'static str {
    match c {
        MembershipChange::Joined => "joined",
        MembershipChange::Left => "left",
        MembershipChange::Banned => "banned",
        MembershipChange::Unbanned => "unbanned",
        MembershipChange::Kicked => "kicked",
        MembershipChange::Invited => "invited",
        MembershipChange::KickedAndBanned => "kicked_and_banned",
        MembershipChange::InvitationAccepted => "invitation_accepted",
        MembershipChange::InvitationRejected => "invitation_rejected",
        MembershipChange::InvitationRevoked => "invitation_revoked",
        MembershipChange::Knocked => "knocked",
        MembershipChange::KnockAccepted => "knock_accepted",
        MembershipChange::KnockRetracted => "knock_retracted",
        MembershipChange::KnockDenied => "knock_denied",
        _ => "other",
    }
}

/// The content of any timeline event (also used for replies).
pub fn content_json(content: &TimelineItemContent) -> Value {
    match content {
        TimelineItemContent::MsgLike(m) => {
            let mut v = match &m.kind {
                MsgLikeKind::Message(msg) => {
                    let mut v = message_json(msg.msgtype());
                    v["edited"] = msg.is_edited().into();
                    v
                }
                MsgLikeKind::Sticker(s) => {
                    let c = s.content();
                    json!({ "type": "sticker", "body": c.body, "source": media_source(&c.source.clone().into()) })
                }
                MsgLikeKind::Poll(p) => {
                    let r = p.results();
                    let answers: Vec<Value> = r
                        .answers
                        .iter()
                        .map(|a| json!({ "id": a.id, "text": a.text, "votes": r.votes.get(&a.id).map(|v| v.len()).unwrap_or(0) }))
                        .collect();
                    json!({ "type": "poll", "body": r.question, "answers": answers, "ended": r.end_time.is_some() })
                }
                MsgLikeKind::Redacted => json!({ "type": "redacted", "body": "" }),
                MsgLikeKind::UnableToDecrypt(_) => json!({ "type": "utd", "body": "" }),
                MsgLikeKind::Other(o) => json!({ "type": "other", "body": "", "event_type": o.event_type().to_string() }),
                MsgLikeKind::LiveLocation(_) => json!({ "type": "location", "body": "" }),
            };
            let reactions: Vec<Value> = m
                .reactions
                .iter()
                .map(|(key, senders)| {
                    json!({
                        "key": key,
                        "count": senders.len(),
                        "senders": senders.keys().map(|u| u.to_string()).collect::<Vec<_>>(),
                    })
                })
                .collect();
            if !reactions.is_empty() {
                v["reactions"] = reactions.into();
            }
            if let Some(reply) = &m.in_reply_to {
                let mut r = json!({ "event_id": reply.event_id.to_string() });
                if let TimelineDetails::Ready(ev) = &reply.event {
                    r["sender"] = ev.sender.to_string().into();
                    r["sender_name"] = profile_name(&ev.sender_profile).into();
                    r["content"] = content_json(&ev.content);
                }
                v["reply_to"] = r;
            }
            if let Some(t) = &m.thread_summary {
                v["thread_replies"] = t.num_replies.into();
            }
            if m.thread_root.is_some() {
                v["in_thread"] = true.into();
            }
            v
        }
        TimelineItemContent::MembershipChange(c) => json!({
            "type": "membership",
            "body": "",
            "user_id": c.user_id().to_string(),
            "user_name": c.display_name(),
            "change": c.change().map(|c| membership_change(&c)).unwrap_or("other"),
        }),
        TimelineItemContent::ProfileChange(c) => {
            let mut v = json!({ "type": "profile", "body": "", "user_id": c.user_id().to_string() });
            if let Some(n) = c.displayname_change() {
                v["old_name"] = n.old.clone().into();
                v["new_name"] = n.new.clone().into();
            }
            if c.avatar_url_change().is_some() {
                v["avatar_changed"] = true.into();
            }
            v
        }
        TimelineItemContent::OtherState(s) => {
            use matrix_sdk_ui::timeline::AnyOtherStateEventContentChange as C;
            use matrix_sdk::ruma::events::StateEventContentChange as F;
            let mut v = json!({ "type": "state", "body": "", "event_type": s.content().event_type().to_string() });
            match s.content() {
                C::RoomName(F::Original { content, .. }) => v["value"] = content.name.clone().into(),
                C::RoomTopic(F::Original { content, .. }) => v["value"] = content.topic.clone().into(),
                C::RoomEncryption(_) => v["value"] = "".into(),
                C::RoomCreate(_) => v["value"] = "".into(),
                _ => {}
            }
            v
        }
        TimelineItemContent::CallInvite | TimelineItemContent::RtcNotification { .. } => {
            json!({ "type": "call", "body": "" })
        }
        _ => json!({ "type": "other", "body": "" }),
    }
}

pub fn profile_name(p: &TimelineDetails<matrix_sdk_ui::timeline::Profile>) -> Option<String> {
    match p {
        TimelineDetails::Ready(p) => p.display_name.clone(),
        _ => None,
    }
}

fn event_json(e: &EventTimelineItem) -> Value {
    let mut v = json!({
        "kind": "event",
        "sender": e.sender().to_string(),
        "ts": u64::from(e.timestamp().0),
        "own": e.is_own(),
        "editable": e.is_editable(),
        "highlighted": e.is_highlighted(),
        "can_reply": e.can_be_replied_to(),
        "content": content_json(e.content()),
    });
    if let Some(id) = e.event_id() {
        v["event_id"] = id.to_string().into();
    }
    if let Some(t) = e.transaction_id() {
        v["txn_id"] = t.to_string().into();
    }
    if let TimelineDetails::Ready(p) = e.sender_profile() {
        v["sender_name"] = p.display_name.clone().into();
        v["sender_avatar"] = p.avatar_url.as_ref().map(|u| u.to_string()).into();
    }
    v["send_state"] = match e.send_state() {
        Some(EventSendState::NotSentYet { .. }) => "sending",
        Some(EventSendState::SendingFailed { .. }) => "failed",
        _ => "sent",
    }
    .into();
    v["encrypted"] = e.encryption_info().is_some().into();
    v["shield"] = match e.get_shield(false) {
        TimelineEventShieldState::Red { .. } => "red",
        TimelineEventShieldState::Grey { .. } => "grey",
        _ => "none",
    }
    .into();
    v["read_by"] = e.read_receipts().keys().map(|u| u.to_string()).collect::<Vec<_>>().into();
    v
}

pub fn item_json(item: &TimelineItem) -> Value {
    let mut v = match item.kind() {
        TimelineItemKind::Event(e) => event_json(e),
        TimelineItemKind::Virtual(VirtualTimelineItem::DateDivider(ts)) => {
            json!({ "kind": "date", "ts": u64::from(ts.0) })
        }
        TimelineItemKind::Virtual(VirtualTimelineItem::ReadMarker) => json!({ "kind": "read_marker" }),
        TimelineItemKind::Virtual(VirtualTimelineItem::TimelineStart) => json!({ "kind": "start" }),
        #[allow(unreachable_patterns)]
        _ => json!({ "kind": "other" }),
    };
    v["uid"] = item.unique_id().0.clone().into();
    v
}
