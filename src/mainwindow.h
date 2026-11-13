/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_MAINWINDOW_H
#define TMATRIX_MAINWINDOW_H

#include "json.h"

#include <tdemainwindow.h>
#include <tqguardedptr.h>
#include <tqmap.h>

class Composer;
class RoomList;
class TimelineView;
class Tray;
class VerificationDialog;
class TDEAction;
class TDESelectAction;
class TDEToggleAction;
class KPushButton;
class TQLabel;
class TQSplitter;
class TQTimer;
class TQWidgetStack;
class TQHBox;

class MainWindow : public TDEMainWindow
{
    TQ_OBJECT

public:
    MainWindow();
    ~MainWindow();

    /** Restores the last account or asks to log in. */
    void start( bool minimized );
    /** Opens a room by ID (command line / notifications). */
    void openRoom( const TQString &roomId );

protected:
    bool queryClose();
    bool queryExit();

private slots:
    // engine
    void slotLoggedInChanged();
    void slotSession( const TQString &userId, const TQString &session );
    void slotTokens( const TQString &access, const TQString &refresh );
    void slotRoomChanged( const JsonValue &room );
    void slotRoomRemoved( const TQString &roomId );
    void slotTimelineReset( const TQString &roomId, const JsonValue &items );
    void slotTimelineDiff( const TQString &roomId, const JsonValue &ops );
    void slotTimelinePaginated( const TQString &roomId, bool atStart );
    void slotNotification( const JsonValue &n );
    void slotSyncState( const TQString &state, const TQString &error );
    void slotEncryption( const JsonValue &status );
    void slotVerificationRequest( const JsonValue &request );
    void slotVerificationState( const JsonValue &state );
    void slotError( const TQString &cmd, const TQString &message );
    void slotRestored( const JsonValue &reply );
    void slotMembers( const JsonValue &reply );

    // user interface
    void slotRoomSelected( const TQString &roomId );
    void slotRoomAction( const TQString &roomId, const TQString &action );
    void slotSend();
    void slotSendReply( const JsonValue &reply );
    void slotEditLast();
    void slotCancelCompose();
    void slotTyping( bool typing );
    void slotPaginate();
    void slotReply( const TQString &eventId, const TQString &sender, const TQString &text );
    void slotEdit( const TQString &eventId, const TQString &body );
    void slotRedact( const TQString &eventId );
    void slotReact( const TQString &eventId, const TQString &key );
    void slotUserClicked( const TQString &userId );
    void slotVerifyUser( const TQString &userId );
    void slotUpload();
    void slotUploadFiles( const TQStringList &paths );
    void slotAcceptInvite();
    void slotDeclineInvite();
    void slotMarkRead();
    void slotUpdateTray();
    void slotMediaReady( const TQString &key );
    void slotBannerAction();
    void slotBannerDismiss();
    void slotRefreshEncryption();

    // actions
    void slotNewRoom();
    void slotStartChat();
    void slotExplore();
    void slotManageAccount();
    void slotEncryptionSettings();
    void slotVerifySession();
    void slotVerifyDevice( const TQString &deviceId );
    void slotVerificationStarted( const JsonValue &reply );
    void slotSwitchAccount( int index );
    void slotAddAccount();
    void slotLogout();
    void slotLoggedOut( const JsonValue &reply );
    void slotRoomSettings();
    void slotInvite();
    void slotLeave();
    void slotNextUnread();
    void slotToggleFavourite();
    void slotPreferences();
    void slotSettingsChanged();
    void slotQuit();
    void slotConfigureNotifications();
    void slotConfigureKeys();

private:
    void setupActions();
    void setupWidgets();
    void showLogin( bool addAccount );
    void restoreAccount( const TQString &userId );
    void updateAccountsMenu();
    void updateRoomHeader();
    void updateActions();
    void showRoomView( const JsonValue &room );
    void showBanner( const TQString &text, const TQString &action, const TQString &id );
    VerificationDialog *verificationDialog( const TQString &userId, const TQString &flowId, bool incoming, bool self );

    RoomList *m_roomList;
    TimelineView *m_timeline;
    Composer *m_composer;
    TQWidgetStack *m_stack;
    TQWidget *m_welcome, *m_roomPage, *m_invitePage;
    TQLabel *m_inviteText, *m_roomAvatar, *m_roomName, *m_roomTopic, *m_roomLock;
    TQHBox *m_replyBar;
    TQLabel *m_replyLabel;
    TQHBox *m_banner;
    TQLabel *m_bannerText;
    KPushButton *m_bannerButton;
    TQString m_bannerId;
    TQSplitter *m_splitter;
    TQGuardedPtr<Tray> m_tray;
    TQLabel *m_status;
    TQTimer *m_readTimer, *m_trayTimer;

    TDEAction *m_roomSettings, *m_invite, *m_leave, *m_upload, *m_markRead, *m_logout, *m_manage, *m_verify, *m_encryption;
    TDEToggleAction *m_favourite;
    TDESelectAction *m_accounts;

    TQString m_currentRoom, m_replyTo, m_editing;
    JsonValue m_lastEncryption;
    TQMap<TQString, TQGuardedPtr<VerificationDialog> > m_verifications;
    bool m_quitting, m_startMinimized, m_online, m_loginShown;
};

#endif
