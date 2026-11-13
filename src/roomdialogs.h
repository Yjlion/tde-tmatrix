/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_ROOMDIALOGS_H
#define TMATRIX_ROOMDIALOGS_H

#include "json.h"

#include <kdialogbase.h>

class KLineEdit;
class TDEListView;
class KTextEdit;
class TQButtonGroup;
class TQCheckBox;
class TQLabel;
class TQListViewItem;
class TQRadioButton;
class TQTimer;

/** Finds people by name or Matrix ID (for direct chats and invitations). */
class UserPickerDialog : public KDialogBase
{
    TQ_OBJECT
public:
    UserPickerDialog( TQWidget *parent, const TQString &caption, const TQString &okText, const TQString &intro );
    /** The selected Matrix ID (or the text typed if it is one). */
    TQString userId() const;

private slots:
    void slotChanged();
    void slotSearch();
    void slotResults( const JsonValue &reply );
    void slotSelected();

private:
    KLineEdit *m_search;
    TDEListView *m_list;
    TQLabel *m_status;
    TQTimer *m_timer;
};

/** Creates a room. */
class CreateRoomDialog : public KDialogBase
{
    TQ_OBJECT
public:
    CreateRoomDialog( TQWidget *parent );
    TQString roomId() const { return m_roomId; }
protected slots:
    void slotOk();
private slots:
    void slotCreated( const JsonValue &reply );
    void slotPublicToggled( bool on );
private:
    KLineEdit *m_name, *m_topic, *m_alias, *m_invite;
    TQCheckBox *m_public, *m_encrypted;
    TQLabel *m_status;
    TQString m_roomId;
};

/** Lists the public rooms of a server and joins rooms by address. */
class DirectoryDialog : public KDialogBase
{
    TQ_OBJECT
public:
    DirectoryDialog( TQWidget *parent );
    TQString roomId() const { return m_roomId; }
protected slots:
    void slotOk();
private slots:
    void slotSearch();
    void slotResults( const JsonValue &reply );
    void slotJoined( const JsonValue &reply );
    void slotSelection();
    void slotMore();
private:
    void join( const TQString &target );
    KLineEdit *m_search, *m_server;
    TDEListView *m_list;
    TQLabel *m_status;
    TQTimer *m_timer;
    TQString m_next, m_roomId;
    bool m_append;
};

/** Name, topic, avatar, members, notifications and encryption of a room. */
class RoomSettingsDialog : public KDialogBase
{
    TQ_OBJECT
public:
    RoomSettingsDialog( TQWidget *parent, const JsonValue &room, int page = 0 );
    enum { General = 0, Members, Notifications, Security };
protected slots:
    void slotApply();
    void slotOk();
private slots:
    void slotMembers( const JsonValue &reply );
    void slotNotifications( const JsonValue &reply );
    void slotAvatar();
    void slotInvite();
    void slotKick();
    void slotBan();
    void slotEncrypt();
    void slotMemberMenu( TQListViewItem *item );
    void slotReloadMembers();
private:
    void apply();
    JsonValue m_room;
    TQString m_roomId;
    KLineEdit *m_name;
    KTextEdit *m_topic;
    TQLabel *m_avatar, *m_address, *m_encryptionInfo;
    TDEListView *m_members;
    TQButtonGroup *m_notify;
    TQString m_notifyMode;
    TQWidget *m_kick, *m_ban, *m_invite, *m_encrypt, *m_avatarButton;
    JsonValue m_permissions;
};

#endif
