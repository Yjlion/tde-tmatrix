//! Room summaries for the room list.
//
// SPDX-License-Identifier: GPL-2.0-or-later

use matrix_sdk::{Room, RoomState};
use serde_json::{json, Value};

pub async fn room_json(room: &Room) -> Value {
    let name = room
        .display_name()
        .await
        .map(|n| n.to_string())
        .unwrap_or_else(|_| room.room_id().to_string());
    let membership = match room.state() {
        RoomState::Joined => "join",
        RoomState::Invited => "invite",
        RoomState::Left => "leave",
        RoomState::Knocked => "knock",
        RoomState::Banned => "ban",
    };
    let counts = room.unread_notification_counts();
    // fetched from the server if the state was not synced yet (just joined)
    let encrypted = match room.state() {
        RoomState::Joined => room
            .latest_encryption_state()
            .await
            .map(|e| e.is_encrypted())
            .unwrap_or_else(|_| room.encryption_state().is_encrypted()),
        _ => room.encryption_state().is_encrypted(),
    };
    let mut v = json!({
        "room_id": room.room_id().to_string(),
        "name": name,
        "topic": room.topic(),
        "avatar": room.avatar_url().map(|u| u.to_string()),
        "alias": room.canonical_alias().map(|a| a.to_string()),
        "membership": membership,
        "encrypted": encrypted,
        "direct": room.is_direct().await.unwrap_or(false),
        "favourite": room.is_favourite(),
        "low_priority": room.is_low_priority(),
        "space": room.is_space(),
        "public": room.is_public(),
        "members": room.joined_members_count(),
        "unread": counts.notification_count,
        "highlight": counts.highlight_count,
        "marked_unread": room.is_marked_unread(),
        "last_ts": room.latest_event_timestamp().map(|t| u64::from(t.0)),
    });
    if room.state() == RoomState::Invited {
        if let Ok(invite) = room.invite_details().await {
            if let Some(inviter) = invite.inviter {
                v["inviter"] = inviter.user_id().to_string().into();
                v["inviter_name"] = inviter.display_name().map(|s| s.to_owned()).into();
            }
        }
    }
    v
}
