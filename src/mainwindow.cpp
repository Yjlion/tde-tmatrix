/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mainwindow.h"
#include "accountdialog.h"
#include "accounts.h"
#include "composer.h"
#include "logindialog.h"
#include "matrixclient.h"
#include "mediacache.h"
#include "roomdialogs.h"
#include "roomlist.h"
#include "settings.h"
#include "settingsdialog.h"
#include "timelineview.h"
#include "tray.h"
#include "verificationdialog.h"

#include <tqapplication.h>
#include <tqclipboard.h>
#include <tqhbox.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqsplitter.h>
#include <tqstylesheet.h>
#include <tqtimer.h>
#include <tqtooltip.h>
#include <tqvbox.h>
#include <tqwidgetstack.h>

#include <tdeaction.h>
#include <tdeapplication.h>
#include <tdeconfig.h>
#include <tdefiledialog.h>
#include <tdeglobal.h>
#include <kiconloader.h>
#include <kkeydialog.h>
#include <tdelistviewsearchline.h>
#include <tdelocale.h>
#include <tdemessagebox.h>
#include <knotifyclient.h>
#include <knotifydialog.h>
#include <kpushbutton.h>
#include <kstatusbar.h>
#include <kstdaction.h>
#include <twin.h>
#include <tdepopupmenu.h>
#include <tqcursor.h>

MainWindow::MainWindow()
    : TDEMainWindow( 0, "MainWindow" ), m_quitting( false ), m_startMinimized( false ), m_online( false ), m_loginShown( false )
{
    setupWidgets();
    setupActions();
    setupGUI( Keys | StatusBar | Save | Create, "tde-tmatrixui.rc" );
    setAutoSaveSettings();
    statusBar()->addWidget( m_status = new TQLabel( statusBar() ), 1 );
    m_status->setText( i18n( "Not logged in" ) );

    MatrixClient *c = MatrixClient::self();
    connect( c, TQ_SIGNAL( loggedInChanged() ), this, TQ_SLOT( slotLoggedInChanged() ) );
    connect( c, TQ_SIGNAL( sessionCreated( const TQString &, const TQString & ) ), this, TQ_SLOT( slotSession( const TQString &, const TQString & ) ) );
    connect( c, TQ_SIGNAL( tokensRefreshed( const TQString &, const TQString & ) ), this, TQ_SLOT( slotTokens( const TQString &, const TQString & ) ) );
    connect( c, TQ_SIGNAL( roomChanged( const JsonValue & ) ), this, TQ_SLOT( slotRoomChanged( const JsonValue & ) ) );
    connect( c, TQ_SIGNAL( roomRemoved( const TQString & ) ), this, TQ_SLOT( slotRoomRemoved( const TQString & ) ) );
    connect( c, TQ_SIGNAL( timelineReset( const TQString &, const JsonValue & ) ), this, TQ_SLOT( slotTimelineReset( const TQString &, const JsonValue & ) ) );
    connect( c, TQ_SIGNAL( timelineDiff( const TQString &, const JsonValue & ) ), this, TQ_SLOT( slotTimelineDiff( const TQString &, const JsonValue & ) ) );
    connect( c, TQ_SIGNAL( timelinePaginated( const TQString &, bool ) ), this, TQ_SLOT( slotTimelinePaginated( const TQString &, bool ) ) );
    connect( c, TQ_SIGNAL( notification( const JsonValue & ) ), this, TQ_SLOT( slotNotification( const JsonValue & ) ) );
    connect( c, TQ_SIGNAL( syncState( const TQString &, const TQString & ) ), this, TQ_SLOT( slotSyncState( const TQString &, const TQString & ) ) );
    connect( c, TQ_SIGNAL( encryptionChanged( const JsonValue & ) ), this, TQ_SLOT( slotEncryption( const JsonValue & ) ) );
    connect( c, TQ_SIGNAL( verificationRequest( const JsonValue & ) ), this, TQ_SLOT( slotVerificationRequest( const JsonValue & ) ) );
    connect( c, TQ_SIGNAL( verificationState( const JsonValue & ) ), this, TQ_SLOT( slotVerificationState( const JsonValue & ) ) );
    connect( c, TQ_SIGNAL( error( const TQString &, const TQString & ) ), this, TQ_SLOT( slotError( const TQString &, const TQString & ) ) );
    connect( MediaCache::self(), TQ_SIGNAL( ready( const TQString & ) ), this, TQ_SLOT( slotMediaReady( const TQString & ) ) );

    slotSettingsChanged();
    updateActions();
}

MainWindow::~MainWindow()
{
}

// ---- construction -----------------------------------------------------------

void MainWindow::setupWidgets()
{
    TQVBox *central = new TQVBox( this );
    setCentralWidget( central );

    m_banner = new TQHBox( central );
    m_banner->setMargin( 6 );
    m_banner->setSpacing( 6 );
    m_banner->setPaletteBackgroundColor( TQColor( "#fff4d6" ) );
    TQLabel *bi = new TQLabel( m_banner );
    bi->setPixmap( SmallIcon( "messagebox_warning", 22 ) );
    m_bannerText = new TQLabel( m_banner );
    m_bannerText->setTextFormat( TQt::RichText );
    m_bannerText->setPaletteBackgroundColor( TQColor( "#fff4d6" ) );
    m_banner->setStretchFactor( m_bannerText, 1 );
    m_bannerButton = new KPushButton( m_banner );
    KPushButton *dismiss = new KPushButton( KGuiItem( i18n( "Later" ) ), m_banner );
    connect( m_bannerButton, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotBannerAction() ) );
    connect( dismiss, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotBannerDismiss() ) );
    m_banner->hide();

    m_splitter = new TQSplitter( TQt::Horizontal, central );
    central->setStretchFactor( m_splitter, 1 );

    TQVBox *left = new TQVBox( m_splitter );
    left->setSpacing( 3 );
    TDEListViewSearchLine *search = new TDEListViewSearchLine( left );
    m_roomList = new RoomList( left );
    search->setListView( m_roomList );
    search->setClickMessage( i18n( "Filter rooms" ) );
    connect( m_roomList, TQ_SIGNAL( roomSelected( const TQString & ) ), this, TQ_SLOT( slotRoomSelected( const TQString & ) ) );
    connect( m_roomList, TQ_SIGNAL( roomAction( const TQString &, const TQString & ) ), this, TQ_SLOT( slotRoomAction( const TQString &, const TQString & ) ) );

    m_stack = new TQWidgetStack( m_splitter );

    // welcome
    m_welcome = new TQWidget( m_stack );
    TQVBoxLayout *wl = new TQVBoxLayout( m_welcome, 20, 10 );
    wl->addStretch( 1 );
    TQLabel *logo = new TQLabel( m_welcome );
    logo->setPixmap( DesktopIcon( "tde-tmatrix", 128 ) );
    logo->setAlignment( TQt::AlignCenter );
    wl->addWidget( logo );
    TQLabel *wt = new TQLabel( i18n( "<h2>Welcome to TMatrix</h2><p>Choose a room on the left, or start a conversation:</p>" ), m_welcome );
    wt->setAlignment( TQt::AlignCenter | TQt::WordBreak );
    wl->addWidget( wt );
    TQHBox *wb = new TQHBox( m_welcome );
    wb->setSpacing( 6 );
    new TQWidget( wb );
    KPushButton *b1 = new KPushButton( KGuiItem( i18n( "Start a &Chat…" ), "contact-new" ), wb );
    KPushButton *b2 = new KPushButton( KGuiItem( i18n( "E&xplore Rooms…" ), "system-search" ), wb );
    KPushButton *b3 = new KPushButton( KGuiItem( i18n( "Create a &Room…" ), "list-add" ), wb );
    new TQWidget( wb );
    connect( b1, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotStartChat() ) );
    connect( b2, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotExplore() ) );
    connect( b3, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotNewRoom() ) );
    wl->addWidget( wb );
    wl->addStretch( 2 );
    m_stack->addWidget( m_welcome, 0 );

    // a room
    TQVBox *room = new TQVBox( m_stack );
    room->setSpacing( 4 );
    m_roomPage = room;
    TQHBox *header = new TQHBox( room );
    header->setSpacing( 8 );
    header->setMargin( 2 );
    m_roomAvatar = new TQLabel( header );
    m_roomAvatar->setFixedSize( 40, 40 );
    TQVBox *titles = new TQVBox( header );
    header->setStretchFactor( titles, 1 );
    m_roomName = new TQLabel( titles );
    TQFont bf = m_roomName->font();
    bf.setBold( true );
    bf.setPointSize( bf.pointSize() + 2 );
    m_roomName->setFont( bf );
    m_roomTopic = new TQLabel( titles );
    m_roomTopic->setTextFormat( TQt::PlainText );
    m_roomLock = new TQLabel( header );
    KPushButton *settingsButton = new KPushButton( header );
    settingsButton->setIconSet( SmallIconSet( "configure" ) );
    settingsButton->setFlat( true );
    TQToolTip::add( settingsButton, i18n( "Room settings" ) );
    connect( settingsButton, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotRoomSettings() ) );

    m_timeline = new TimelineView( room );
    room->setStretchFactor( m_timeline, 1 );
    connect( m_timeline, TQ_SIGNAL( paginateRequested() ), this, TQ_SLOT( slotPaginate() ) );
    connect( m_timeline, TQ_SIGNAL( replyRequested( const TQString &, const TQString &, const TQString & ) ),
             this, TQ_SLOT( slotReply( const TQString &, const TQString &, const TQString & ) ) );
    connect( m_timeline, TQ_SIGNAL( editRequested( const TQString &, const TQString & ) ), this, TQ_SLOT( slotEdit( const TQString &, const TQString & ) ) );
    connect( m_timeline, TQ_SIGNAL( redactRequested( const TQString & ) ), this, TQ_SLOT( slotRedact( const TQString & ) ) );
    connect( m_timeline, TQ_SIGNAL( reactRequested( const TQString &, const TQString & ) ), this, TQ_SLOT( slotReact( const TQString &, const TQString & ) ) );
    connect( m_timeline, TQ_SIGNAL( userClicked( const TQString & ) ), this, TQ_SLOT( slotUserClicked( const TQString & ) ) );
    connect( m_timeline, TQ_SIGNAL( verifyUserRequested( const TQString & ) ), this, TQ_SLOT( slotVerifyUser( const TQString & ) ) );

    m_replyBar = new TQHBox( room );
    m_replyBar->setSpacing( 6 );
    m_replyLabel = new TQLabel( m_replyBar );
    m_replyLabel->setTextFormat( TQt::RichText );
    m_replyBar->setStretchFactor( m_replyLabel, 1 );
    KPushButton *cancelReply = new KPushButton( m_replyBar );
    cancelReply->setIconSet( SmallIconSet( "button_cancel" ) );
    cancelReply->setFlat( true );
    connect( cancelReply, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotCancelCompose() ) );
    m_replyBar->hide();

    TQHBox *compose = new TQHBox( room );
    compose->setSpacing( 4 );
    KPushButton *attach = new KPushButton( compose );
    attach->setIconSet( SmallIconSet( "attach" ) );
    TQToolTip::add( attach, i18n( "Send a file or image" ) );
    attach->setSizePolicy( TQSizePolicy::Fixed, TQSizePolicy::Fixed );
    connect( attach, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotUpload() ) );
    m_composer = new Composer( compose );
    m_composer->setFixedHeight( 3 * m_composer->fontMetrics().lineSpacing() + 12 );
    compose->setStretchFactor( m_composer, 1 );
    KPushButton *send = new KPushButton( KGuiItem( i18n( "Send" ), "mail-send" ), compose );
    connect( send, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotSend() ) );
    connect( m_composer, TQ_SIGNAL( sendRequested() ), this, TQ_SLOT( slotSend() ) );
    connect( m_composer, TQ_SIGNAL( editLastRequested() ), this, TQ_SLOT( slotEditLast() ) );
    connect( m_composer, TQ_SIGNAL( cancelRequested() ), this, TQ_SLOT( slotCancelCompose() ) );
    connect( m_composer, TQ_SIGNAL( typing( bool ) ), this, TQ_SLOT( slotTyping( bool ) ) );
    connect( m_composer, TQ_SIGNAL( filesDropped( const TQStringList & ) ), this, TQ_SLOT( slotUploadFiles( const TQStringList & ) ) );
    m_stack->addWidget( room, 1 );

    // an invitation
    m_invitePage = new TQWidget( m_stack );
    TQVBoxLayout *il = new TQVBoxLayout( m_invitePage, 20, 10 );
    il->addStretch( 1 );
    m_inviteText = new TQLabel( m_invitePage );
    m_inviteText->setAlignment( TQt::AlignCenter | TQt::WordBreak );
    il->addWidget( m_inviteText );
    TQHBox *ib = new TQHBox( m_invitePage );
    ib->setSpacing( 6 );
    new TQWidget( ib );
    KPushButton *accept = new KPushButton( KGuiItem( i18n( "&Accept" ), "button_ok" ), ib );
    KPushButton *decline = new KPushButton( KGuiItem( i18n( "&Decline" ), "button_cancel" ), ib );
    new TQWidget( ib );
    connect( accept, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotAcceptInvite() ) );
    connect( decline, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotDeclineInvite() ) );
    il->addWidget( ib );
    il->addStretch( 2 );
    m_stack->addWidget( m_invitePage, 2 );
    m_stack->raiseWidget( 0 );

    TQValueList<int> sizes;
    sizes << 240 << 660;
    m_splitter->setSizes( sizes );
    m_splitter->setResizeMode( left, TQSplitter::KeepSize );

    m_readTimer = new TQTimer( this );
    connect( m_readTimer, TQ_SIGNAL( timeout() ), this, TQ_SLOT( slotMarkRead() ) );
    m_trayTimer = new TQTimer( this );
    connect( m_trayTimer, TQ_SIGNAL( timeout() ), this, TQ_SLOT( slotUpdateTray() ) );
    resize( 960, 640 );
}

void MainWindow::setupActions()
{
    TDEActionCollection *ac = actionCollection();
    new TDEAction( i18n( "&Start Chat…" ), "contact-new", CTRL + Key_N, this, TQ_SLOT( slotStartChat() ), ac, "start_chat" );
    new TDEAction( i18n( "&Create Room…" ), "list-add", CTRL + SHIFT + Key_N, this, TQ_SLOT( slotNewRoom() ), ac, "new_room" );
    new TDEAction( i18n( "E&xplore Rooms…" ), "system-search", CTRL + Key_J, this, TQ_SLOT( slotExplore() ), ac, "explore" );
    KStdAction::quit( this, TQ_SLOT( slotQuit() ), ac );

    m_manage = new TDEAction( i18n( "&Manage Account…" ), "identity", 0, this, TQ_SLOT( slotManageAccount() ), ac, "manage_account" );
    m_encryption = new TDEAction( i18n( "&Encryption and Recovery…" ), "encrypted", 0, this, TQ_SLOT( slotEncryptionSettings() ), ac, "encryption" );
    m_verify = new TDEAction( i18n( "&Verify This Session…" ), "messagebox_warning", 0, this, TQ_SLOT( slotVerifySession() ), ac, "verify_session" );
    m_accounts = new TDESelectAction( i18n( "S&witch Account" ), "switchuser", 0, ac, "switch_account" );
    connect( m_accounts, TQ_SIGNAL( activated( int ) ), this, TQ_SLOT( slotSwitchAccount( int ) ) );
    new TDEAction( i18n( "&Add Account…" ), "contact-new", 0, this, TQ_SLOT( slotAddAccount() ), ac, "add_account" );
    m_logout = new TDEAction( i18n( "&Log Out…" ), "system-log-out", 0, this, TQ_SLOT( slotLogout() ), ac, "logout" );

    m_roomSettings = new TDEAction( i18n( "Room &Settings…" ), "configure", CTRL + Key_I, this, TQ_SLOT( slotRoomSettings() ), ac, "room_settings" );
    m_invite = new TDEAction( i18n( "&Invite People…" ), "contact-new", 0, this, TQ_SLOT( slotInvite() ), ac, "invite" );
    m_upload = new TDEAction( i18n( "Send &File…" ), "attach", CTRL + Key_U, this, TQ_SLOT( slotUpload() ), ac, "upload" );
    m_favourite = new TDEToggleAction( i18n( "&Favourite" ), "bookmark", 0, this, TQ_SLOT( slotToggleFavourite() ), ac, "favourite" );
    m_markRead = new TDEAction( i18n( "&Mark as Read" ), "apply", 0, this, TQ_SLOT( slotMarkRead() ), ac, "mark_read" );
    new TDEAction( i18n( "&Next Unread Room" ), "go-down", ALT + Key_Down, this, TQ_SLOT( slotNextUnread() ), ac, "next_unread" );
    m_leave = new TDEAction( i18n( "&Leave Room…" ), "system-log-out", 0, this, TQ_SLOT( slotLeave() ), ac, "leave" );

    KStdAction::preferences( this, TQ_SLOT( slotPreferences() ), ac );
    KStdAction::configureNotifications( this, TQ_SLOT( slotConfigureNotifications() ), ac );
}

// ---- start-up and accounts ----------------------------------------------------

void MainWindow::start( bool minimized )
{
    m_startMinimized = minimized;
    TQString user = Accounts::current();
    if ( user.isEmpty() ) {
        if ( !minimized )
            show();
        showLogin( false );
        return;
    }
    if ( !minimized )
        show();
    restoreAccount( user );
}

void MainWindow::restoreAccount( const TQString &userId )
{
    TQString session = Accounts::loadSession( userId, winId() );
    if ( session.isEmpty() ) {
        KMessageBox::sorry( this, i18n( "The login of %1 could not be read (is the TDE Wallet closed?). Please log in again." ).arg( userId ) );
        showLogin( false );
        return;
    }
    m_status->setText( i18n( "Connecting as %1…" ).arg( userId ) );
    m_roomList->clearRooms();
    m_timeline->clearView();
    m_currentRoom = TQString::null;
    m_stack->raiseWidget( m_welcome );
    MediaCache::self()->clear();
    MatrixRequest *r = MatrixClient::self()->call( "restore", MatrixClient::obj( "session", session ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotRestored( const JsonValue & ) ) );
    Accounts::setCurrent( userId );
    updateAccountsMenu();
}

void MainWindow::slotRestored( const JsonValue &reply )
{
    if ( !reply.get( "ok" ).toBool() ) {
        m_status->setText( i18n( "Not logged in" ) );
        if ( !isVisible() )
            show();
        KMessageBox::detailedSorry( this, i18n( "TMatrix could not restore your login. Please log in again." ), reply.get( "error" ).toString() );
        showLogin( false );
    }
}

void MainWindow::showLogin( bool addAccount )
{
    if ( m_loginShown )
        return;
    m_loginShown = true;
    if ( !isVisible() )
        show();
    TQString previous = MatrixClient::self()->userId();
    LoginDialog dlg( this );
    int r = dlg.exec();
    m_loginShown = false;
    if ( r == TQDialog::Accepted ) {
        m_roomList->clearRooms();
        m_timeline->clearView();
        m_currentRoom = TQString::null;
        m_stack->raiseWidget( m_welcome );
        MediaCache::self()->clear();
        return;
    }
    if ( addAccount && !previous.isEmpty() && !MatrixClient::self()->loggedIn() )
        restoreAccount( previous );
}

void MainWindow::slotSession( const TQString &userId, const TQString &session )
{
    if ( !Accounts::storeSession( userId, session, winId() ) )
        KMessageBox::sorry( this, i18n( "Your login could not be saved; you will have to log in again next time." ) );
    updateAccountsMenu();
}

void MainWindow::slotTokens( const TQString &access, const TQString &refresh )
{
    if ( MatrixClient::self()->loggedIn() )
        Accounts::updateTokens( MatrixClient::self()->userId(), access, refresh );
}

void MainWindow::updateAccountsMenu()
{
    TQStringList l = Accounts::list();
    m_accounts->setItems( l );
    m_accounts->setCurrentItem( l.findIndex( Accounts::current() ) );
    m_accounts->setEnabled( l.count() > 1 );
}

void MainWindow::slotSwitchAccount( int index )
{
    TQStringList l = Accounts::list();
    if ( index < 0 || index >= (int) l.count() || l[index] == MatrixClient::self()->userId() )
        return;
    restoreAccount( l[index] );
}

void MainWindow::slotAddAccount()
{
    showLogin( true );
}

void MainWindow::slotLogout()
{
    TQString user = MatrixClient::self()->userId();
    if ( user.isEmpty() )
        return;
    if ( KMessageBox::warningContinueCancel( this, i18n( "<p>Log out %1 from this computer?</p><p>TMatrix deletes the local copy of your "
                                                         "messages and the encryption keys of this session. Without recovery or another "
                                                         "verified session you lose access to your encrypted messages.</p>" ).arg( user ),
                                             i18n( "Log Out" ), KGuiItem( i18n( "&Log Out" ), "system-log-out" ) ) != KMessageBox::Continue )
        return;
    MatrixRequest *r = MatrixClient::self()->call( "logout" );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotLoggedOut( const JsonValue & ) ) );
}

void MainWindow::slotLoggedOut( const JsonValue &reply )
{
    TQString user = reply.get( "user_id" ).toString();
    if ( user.isEmpty() )
        user = Accounts::current();
    Accounts::remove( user );
    m_roomList->clearRooms();
    m_timeline->clearView();
    m_currentRoom = TQString::null;
    m_stack->raiseWidget( m_welcome );
    m_banner->hide();
    updateAccountsMenu();
    TQString next = Accounts::current();
    if ( next.isEmpty() )
        showLogin( false );
    else
        restoreAccount( next );
}

void MainWindow::slotLoggedInChanged()
{
    MatrixClient *c = MatrixClient::self();
    if ( c->loggedIn() ) {
        m_status->setText( i18n( "Connecting as %1…" ).arg( c->userId() ) );
        setCaption( c->userId() );
    } else {
        m_status->setText( i18n( "Not logged in" ) );
        m_online = false;
    }
    updateActions();
    updateAccountsMenu();
    slotUpdateTray();
}

void MainWindow::slotSyncState( const TQString &state, const TQString &error )
{
    if ( state == "running" ) {
        m_online = true;
        m_status->setText( i18n( "Connected as %1" ).arg( MatrixClient::self()->userId() ) );
    } else if ( state == "error" ) {
        m_online = false;
        m_status->setText( i18n( "Connection problem, retrying: %1" ).arg( error ) );
    } else if ( state == "logged_out" ) {
        m_online = false;
        m_status->setText( i18n( "Logged out by the server" ) );
        KMessageBox::information( this, i18n( "The server ended this session (it was signed out from another device, or the password "
                                              "was changed). Please log in again." ) );
        Accounts::remove( Accounts::current() );
        showLogin( false );
    }
    slotUpdateTray();
}

// ---- rooms --------------------------------------------------------------------

void MainWindow::slotRoomChanged( const JsonValue &room )
{
    m_roomList->updateRoom( room );
    if ( room.get( "room_id" ).toString() == m_currentRoom ) {
        updateRoomHeader();
        if ( room.get( "unread" ).toInt() > 0 && isActiveWindow() && !m_readTimer->isActive() )
            m_readTimer->start( 1500, true );
    }
    if ( !m_trayTimer->isActive() )
        m_trayTimer->start( 300, true );
}

void MainWindow::slotRoomRemoved( const TQString &roomId )
{
    m_roomList->removeRoom( roomId );
    if ( roomId == m_currentRoom ) {
        m_currentRoom = TQString::null;
        m_timeline->clearView();
        m_stack->raiseWidget( m_welcome );
        updateActions();
    }
    slotUpdateTray();
}

void MainWindow::openRoom( const TQString &roomId )
{
    m_roomList->selectRoom( roomId );
}

void MainWindow::slotRoomSelected( const TQString &roomId )
{
    if ( roomId == m_currentRoom )
        return;
    if ( !m_currentRoom.isEmpty() )
        MatrixClient::self()->send( "close_room", MatrixClient::obj( "room_id", m_currentRoom ) );
    m_currentRoom = roomId;
    slotCancelCompose();
    m_composer->clear();
    JsonValue room = m_roomList->room( roomId );
    showRoomView( room );
    updateActions();
}

void MainWindow::showRoomView( const JsonValue &room )
{
    const TQString roomId = room.get( "room_id" ).toString();
    if ( room.get( "membership" ).toString() == "invite" ) {
        TQString inviter = room.get( "inviter_name" ).toString();
        if ( inviter.isEmpty() )
            inviter = room.get( "inviter" ).toString();
        TQString text = "<h2>" + TQStyleSheet::escape( room.get( "name" ).toString() ) + "</h2>";
        text += inviter.isEmpty() ? i18n( "<p>You were invited to this room.</p>" )
                                  : i18n( "<p><b>%1</b> invited you to %2.</p>" ).arg( TQStyleSheet::escape( inviter ) )
                                        .arg( room.get( "direct" ).toBool() ? i18n( "a direct chat" ) : i18n( "this room" ) );
        m_inviteText->setText( text );
        m_stack->raiseWidget( m_invitePage );
        setCaption( room.get( "name" ).toString() );
        return;
    }
    m_stack->raiseWidget( m_roomPage );
    m_timeline->setRoom( roomId, MatrixClient::self()->userId() );
    updateRoomHeader();
    MatrixClient::self()->send( "open_room", MatrixClient::obj( "room_id", roomId ) );
    MatrixRequest *r = MatrixClient::self()->call( "members", MatrixClient::obj( "room_id", roomId ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotMembers( const JsonValue & ) ) );
    m_readTimer->start( 1500, true );
    m_composer->setFocus();
}

void MainWindow::slotMembers( const JsonValue &reply )
{
    if ( reply.get( "room_id" ).toString() != m_currentRoom )
        return;
    m_timeline->setCanRedact( reply.get( "can_redact" ).toBool() );
    JsonValue room = m_roomList->room( m_currentRoom );
    if ( room.get( "topic" ).toString().stripWhiteSpace().isEmpty() ) {
        int joined = 0;
        const JsonValue &list = reply.get( "members" );
        for ( uint i = 0; i < list.count(); ++i )
            if ( list.at( i ).get( "membership" ).toString() == "join" )
                ++joined;
        m_roomTopic->setText( room.get( "direct" ).toBool() ? i18n( "Direct chat" )
                                                            : i18n( "1 member", "%n members", joined ) );
    }
}

void MainWindow::updateRoomHeader()
{
    JsonValue room = m_roomList->room( m_currentRoom );
    if ( room.isNull() )
        return;
    TQString name = room.get( "name" ).toString();
    m_roomName->setText( name );
    TQString topic = room.get( "topic" ).toString().simplifyWhiteSpace();
    if ( topic.length() > 160 )
        topic = topic.left( 160 ) + TQString::fromUtf8( "…" );
    if ( !topic.isEmpty() )
        m_roomTopic->setText( topic );
    else if ( room.get( "direct" ).toBool() )
        m_roomTopic->setText( i18n( "Direct chat" ) );
    else if ( room.get( "members" ).toInt() > 0 )
        m_roomTopic->setText( i18n( "1 member", "%n members", room.get( "members" ).toInt() ) );
    TQString mxc = room.get( "avatar" ).toString();
    if ( !mxc.isEmpty() && MediaCache::self()->avatar( mxc, 40 ) )
        m_roomAvatar->setPixmap( MediaCache::self()->pixmap( MediaCache::avatarKey( mxc, 40 ) ) );
    else
        m_roomAvatar->setPixmap( MediaCache::placeholder( name, m_currentRoom, 40 ) );
    bool enc = room.get( "encrypted" ).toBool();
    m_roomLock->setPixmap( SmallIcon( enc ? "encrypted" : "decrypted", 22 ) );
    TQToolTip::remove( m_roomLock );
    TQToolTip::add( m_roomLock, enc ? i18n( "Messages in this room are end-to-end encrypted" ) : i18n( "Messages in this room are not encrypted" ) );
    m_favourite->setChecked( room.get( "favourite" ).toBool() );
    setCaption( name );
}

void MainWindow::slotMediaReady( const TQString & )
{
    if ( !m_currentRoom.isEmpty() )
        updateRoomHeader();
}

void MainWindow::updateActions()
{
    bool in = MatrixClient::self()->loggedIn();
    JsonValue room = m_roomList->room( m_currentRoom );
    bool joined = in && !room.isNull() && room.get( "membership" ).toString() == "join";
    m_roomSettings->setEnabled( joined );
    m_invite->setEnabled( joined );
    m_upload->setEnabled( joined );
    m_leave->setEnabled( in && !room.isNull() );
    m_favourite->setEnabled( joined );
    m_markRead->setEnabled( joined );
    m_logout->setEnabled( in );
    m_manage->setEnabled( in );
    m_verify->setEnabled( in );
    m_encryption->setEnabled( in );
    actionCollection()->action( "start_chat" )->setEnabled( in );
    actionCollection()->action( "new_room" )->setEnabled( in );
    actionCollection()->action( "explore" )->setEnabled( in );
}

void MainWindow::slotRoomAction( const TQString &roomId, const TQString &action )
{
    JsonValue room = m_roomList->room( roomId );
    if ( action == "accept" ) {
        MatrixClient::self()->send( "accept_invite", MatrixClient::obj( "room_id", roomId ) );
    } else if ( action == "decline" ) {
        MatrixClient::self()->send( "decline_invite", MatrixClient::obj( "room_id", roomId ) );
    } else if ( action == "mark_read" ) {
        if ( roomId == m_currentRoom )
            slotMarkRead();
        else
            KMessageBox::information( this, i18n( "Open the room to mark it as read." ), TQString::null, "markReadHint" );
    } else if ( action == "favourite" ) {
        MatrixClient::self()->send( "set_favourite", MatrixClient::obj( "room_id", roomId, "value", !room.get( "favourite" ).toBool() ) );
    } else if ( action == "low_priority" ) {
        MatrixClient::self()->send( "set_low_priority", MatrixClient::obj( "room_id", roomId, "value", !room.get( "low_priority" ).toBool() ) );
    } else if ( action == "settings" ) {
        RoomSettingsDialog dlg( this, room );
        dlg.exec();
    } else if ( action == "invite" ) {
        m_roomList->selectRoom( roomId );
        slotInvite();
    } else if ( action == "copy_link" ) {
        TQString alias = room.get( "alias" ).toString();
        TQApplication::clipboard()->setText( "https://matrix.to/#/" + ( alias.isEmpty() ? roomId : alias ), TQClipboard::Clipboard );
    } else if ( action == "leave" ) {
        m_roomList->selectRoom( roomId );
        slotLeave();
    }
}

void MainWindow::slotAcceptInvite()
{
    if ( m_currentRoom.isEmpty() )
        return;
    MatrixRequest *r = MatrixClient::self()->call( "accept_invite", MatrixClient::obj( "room_id", m_currentRoom ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotSendReply( const JsonValue & ) ) );
}

void MainWindow::slotDeclineInvite()
{
    if ( !m_currentRoom.isEmpty() )
        MatrixClient::self()->send( "decline_invite", MatrixClient::obj( "room_id", m_currentRoom ) );
}

// ---- the timeline ------------------------------------------------------------

void MainWindow::slotTimelineReset( const TQString &roomId, const JsonValue &items )
{
    if ( roomId == m_currentRoom )
        m_timeline->reset( items );
}

void MainWindow::slotTimelineDiff( const TQString &roomId, const JsonValue &ops )
{
    if ( roomId != m_currentRoom )
        return;
    m_timeline->applyDiff( ops );
    if ( isActiveWindow() && !m_readTimer->isActive() )
        m_readTimer->start( 1500, true );
}

void MainWindow::slotTimelinePaginated( const TQString &roomId, bool atStart )
{
    if ( roomId == m_currentRoom )
        m_timeline->setAtStart( atStart );
}

void MainWindow::slotPaginate()
{
    if ( m_currentRoom.isEmpty() )
        return;
    MatrixRequest *r = MatrixClient::self()->call( "paginate", MatrixClient::obj( "room_id", m_currentRoom, "count", 30 ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotSendReply( const JsonValue & ) ) );
}

void MainWindow::slotMarkRead()
{
    JsonValue room = m_roomList->room( m_currentRoom );
    if ( room.isNull() || room.get( "membership" ).toString() != "join" )
        return;
    MatrixClient::self()->send( "mark_read", MatrixClient::obj( "room_id", m_currentRoom ) );
}

void MainWindow::slotSend()
{
    TQString text = m_composer->text();
    while ( text.endsWith( "\n" ) )
        text.truncate( text.length() - 1 );
    if ( text.stripWhiteSpace().isEmpty() || m_currentRoom.isEmpty() )
        return;
    JsonValue args = MatrixClient::obj( "room_id", m_currentRoom, "body", text );
    TQString cmd = "send";
    if ( !m_editing.isEmpty() ) {
        cmd = "edit";
        args.set( "event_id", m_editing );
    } else {
        if ( text.startsWith( "/me " ) ) {
            args.set( "body", text.mid( 4 ) );
            args.set( "emote", true );
        } else if ( text.startsWith( "/plain " ) ) {
            args.set( "body", text.mid( 7 ) );
            args.set( "plain", true );
        } else if ( text.startsWith( "//" ) ) {
            args.set( "body", text.mid( 1 ) );
        } else if ( text.startsWith( "/" ) ) {
            KMessageBox::sorry( this, i18n( "Unknown command %1. Commands: /me, /plain. Start the message with // to send a slash." )
                                          .arg( text.section( ' ', 0, 0 ) ) );
            return;
        }
        if ( !m_replyTo.isEmpty() )
            args.set( "reply_to", m_replyTo );
    }
    MatrixRequest *r = MatrixClient::self()->call( cmd, args );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotSendReply( const JsonValue & ) ) );
    m_composer->clear();
    slotCancelCompose();
}

void MainWindow::slotSendReply( const JsonValue &reply )
{
    if ( !reply.get( "ok" ).toBool() )
        slotError( reply.get( "cmd" ).toString(), reply.get( "error" ).toString() );
}

void MainWindow::slotReply( const TQString &eventId, const TQString &sender, const TQString &text )
{
    m_editing = TQString::null;
    m_replyTo = eventId;
    TQString t = text.length() > 120 ? text.left( 120 ) + TQString::fromUtf8( "…" ) : text;
    m_replyLabel->setText( i18n( "Replying to <b>%1</b>: %2" ).arg( TQStyleSheet::escape( sender ) ).arg( TQStyleSheet::escape( t ) ) );
    m_replyBar->show();
    m_composer->setFocus();
}

void MainWindow::slotEdit( const TQString &eventId, const TQString &body )
{
    m_replyTo = TQString::null;
    m_editing = eventId;
    m_replyLabel->setText( i18n( "<b>Editing</b> your message (Escape cancels)" ) );
    m_replyBar->show();
    m_composer->setText( body );
    m_composer->moveCursor( TQTextEdit::MoveEnd, false );
    m_composer->setFocus();
}

void MainWindow::slotEditLast()
{
    JsonValue last = m_timeline->lastOwnEditable();
    if ( !last.isNull() )
        slotEdit( last.get( "event_id" ).toString(), last.get( "content" ).get( "body" ).toString() );
}

void MainWindow::slotCancelCompose()
{
    if ( !m_editing.isEmpty() )
        m_composer->clear();
    m_replyTo = TQString::null;
    m_editing = TQString::null;
    m_replyBar->hide();
}

void MainWindow::slotTyping( bool typing )
{
    if ( Settings::sendTyping() && !m_currentRoom.isEmpty() )
        MatrixClient::self()->call( "typing", MatrixClient::obj( "room_id", m_currentRoom, "typing", typing ) );
}

void MainWindow::slotRedact( const TQString &eventId )
{
    if ( KMessageBox::warningContinueCancel( this, i18n( "Delete this message for everyone?" ), i18n( "Delete Message" ),
                                             KStdGuiItem::del() ) != KMessageBox::Continue )
        return;
    MatrixClient::self()->send( "redact", MatrixClient::obj( "room_id", m_currentRoom, "event_id", eventId ) );
}

void MainWindow::slotReact( const TQString &eventId, const TQString &key )
{
    MatrixClient::self()->send( "react", MatrixClient::obj( "room_id", m_currentRoom, "event_id", eventId, "key", key ) );
}

void MainWindow::slotUserClicked( const TQString &userId )
{
    if ( userId == MatrixClient::self()->userId() )
        return;
    TDEPopupMenu menu( this );
    menu.insertTitle( userId );
    menu.insertItem( SmallIcon( "mail-message-new" ), i18n( "Send a &Direct Message" ), 1 );
    menu.insertItem( SmallIcon( "encrypted" ), i18n( "&Verify…" ), 2 );
    menu.insertItem( SmallIcon( "edit-copy" ), i18n( "&Copy Matrix ID" ), 3 );
    int r = menu.exec( TQCursor::pos() );
    if ( r == 1 ) {
        MatrixRequest *req = MatrixClient::self()->call( "start_dm", MatrixClient::obj( "user_id", userId ) );
        connect( req, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotSendReply( const JsonValue & ) ) );
        connect( req, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotVerificationStarted( const JsonValue & ) ) );
    } else if ( r == 2 ) {
        slotVerifyUser( userId );
    } else if ( r == 3 ) {
        TQApplication::clipboard()->setText( userId, TQClipboard::Clipboard );
    }
}

void MainWindow::slotUpload()
{
    if ( m_currentRoom.isEmpty() )
        return;
    TQStringList files = KFileDialog::getOpenFileNames( TQString::null, TQString::null, this, i18n( "Send Files" ) );
    slotUploadFiles( files );
}

void MainWindow::slotUploadFiles( const TQStringList &paths )
{
    if ( m_currentRoom.isEmpty() || paths.isEmpty() )
        return;
    for ( TQStringList::ConstIterator it = paths.begin(); it != paths.end(); ++it ) {
        MatrixRequest *r = MatrixClient::self()->call( "upload", MatrixClient::obj( "room_id", m_currentRoom, "path", *it ) );
        connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotSendReply( const JsonValue & ) ) );
    }
    m_status->setText( i18n( "Sending 1 file…", "Sending %n files…", paths.count() ) );
}

// ---- notifications and the tray -----------------------------------------------

void MainWindow::slotNotification( const JsonValue &n )
{
    if ( !Settings::notifications() )
        return;
    const TQString roomId = n.get( "room_id" ).toString();
    if ( roomId == m_currentRoom && isActiveWindow() )
        return;
    TQString room = n.get( "room_name" ).toString();
    if ( n.get( "invite" ).toBool() ) {
        KNotifyClient::event( winId(), "invite", i18n( "You were invited to %1" ).arg( TQStyleSheet::escape( room ) ) );
        return;
    }
    TQString sender = n.get( "sender_name" ).toString();
    if ( sender.isEmpty() )
        sender = n.get( "sender" ).toString();
    TQString body = Settings::notificationPreview() ? TimelineView::summary( n.get( "content" ), sender ) : i18n( "New message" );
    if ( body.length() > 200 )
        body = body.left( 200 ) + TQString::fromUtf8( "…" );
    TQString title = n.get( "direct" ).toBool() ? sender : i18n( "%1 in %2" ).arg( sender ).arg( room );
    TQString text = "<b>" + TQStyleSheet::escape( title ) + "</b><br>" + TQStyleSheet::escape( body );
    KNotifyClient::event( winId(), n.get( "highlight" ).toBool() ? "mention" : "message", text );
}

void MainWindow::slotUpdateTray()
{
    if ( !m_tray )
        return;
    m_tray->setUnread( m_roomList->totalUnread(), m_roomList->totalHighlights(), m_roomList->inviteCount(),
                       !MatrixClient::self()->loggedIn() || !m_online );
}

void MainWindow::slotNextUnread()
{
    TQString id = m_roomList->nextUnread();
    if ( !id.isEmpty() ) {
        if ( !isVisible() )
            show();
        KWin::activateWindow( winId() );
        m_roomList->selectRoom( id );
    }
}

// ---- encryption -----------------------------------------------------------------

void MainWindow::slotEncryption( const JsonValue &s )
{
    if ( s.has( "ok" ) && !s.get( "ok" ).toBool() )
        return;
    m_lastEncryption = s;
    TQString recovery = s.get( "recovery" ).toString();
    bool verified = s.get( "device_verified" ).toBool();
    TDEConfigGroup g( TDEGlobal::config(), "Banners" );
    TQString user = MatrixClient::self()->userId();
    if ( !verified && s.get( "has_master_key" ).toBool() ) {
        if ( m_bannerId != "verify" && !g.readBoolEntry( "verify-" + user, false ) )
            showBanner( i18n( "<b>Verify this session</b> to read your earlier encrypted messages and to show others it is really you." ),
                        i18n( "&Verify…" ), "verify" );
    } else if ( recovery == "disabled" ) {
        if ( m_bannerId != "recovery" && !g.readBoolEntry( "recovery-" + user, false ) )
            showBanner( i18n( "<b>Set up recovery</b> so you do not lose your encrypted messages if you lose this computer." ),
                        i18n( "&Set Up…" ), "recovery" );
    } else if ( m_bannerId == "verify" || m_bannerId == "recovery" ) {
        m_banner->hide();
        m_bannerId = TQString::null;
    }
}

void MainWindow::slotRefreshEncryption()
{
    if ( !MatrixClient::self()->loggedIn() )
        return;
    MatrixClient::self()->refreshEncryption();
}

void MainWindow::showBanner( const TQString &text, const TQString &action, const TQString &id )
{
    m_bannerId = id;
    m_bannerText->setText( text );
    m_bannerButton->setText( action );
    m_banner->show();
}

void MainWindow::slotBannerAction()
{
    if ( m_bannerId == "verify" ) {
        AccountDialog dlg( this, AccountDialog::Security );
        connect( &dlg, TQ_SIGNAL( verifyOwnDevice() ), this, TQ_SLOT( slotVerifySession() ) );
        dlg.exec();
    } else if ( m_bannerId == "recovery" ) {
        slotEncryptionSettings();
    }
}

void MainWindow::slotBannerDismiss()
{
    TDEConfigGroup g( TDEGlobal::config(), "Banners" );
    g.writeEntry( m_bannerId + "-" + MatrixClient::self()->userId(), true );
    m_banner->hide();
}

VerificationDialog *MainWindow::verificationDialog( const TQString &userId, const TQString &flowId, bool incoming, bool self )
{
    if ( m_verifications.contains( flowId ) && m_verifications[flowId] )
        return m_verifications[flowId];
    VerificationDialog *d = new VerificationDialog( this, userId, flowId, incoming, self );
    m_verifications.insert( flowId, d );
    d->show();
    return d;
}

void MainWindow::slotVerificationRequest( const JsonValue &r )
{
    if ( r.get( "we_started" ).toBool() )
        return;
    const TQString flow = r.get( "flow_id" ).toString();
    if ( m_verifications.contains( flow ) && m_verifications[flow] )
        return;
    bool self = r.get( "self" ).toBool();
    if ( !isVisible() )
        show();
    KNotifyClient::event( winId(), "verification", self ? i18n( "Another of your sessions wants to verify this one." )
                                                        : i18n( "%1 wants to verify you." ).arg( r.get( "user_id" ).toString() ) );
    VerificationDialog *d = verificationDialog( r.get( "user_id" ).toString(), flow, true, self );
    d->raise();
}

void MainWindow::slotVerificationState( const JsonValue &s )
{
    const TQString flow = s.get( "flow_id" ).toString();
    if ( m_verifications.contains( flow ) && m_verifications[flow] )
        m_verifications[flow]->stateChanged( s );
    // the signatures and secrets of a finished verification arrive shortly after
    if ( s.get( "state" ).toString() == "done" ) {
        TQTimer::singleShot( 3000, this, TQ_SLOT( slotRefreshEncryption() ) );
        TQTimer::singleShot( 10000, this, TQ_SLOT( slotRefreshEncryption() ) );
    }
}

void MainWindow::slotVerifySession()
{
    MatrixRequest *r = MatrixClient::self()->call( "verify_own_device" );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotVerificationStarted( const JsonValue & ) ) );
}

void MainWindow::slotVerifyDevice( const TQString &deviceId )
{
    MatrixRequest *r = MatrixClient::self()->call( "verify_device", MatrixClient::obj( "user_id", MatrixClient::self()->userId(), "device_id", deviceId ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotVerificationStarted( const JsonValue & ) ) );
}

void MainWindow::slotVerifyUser( const TQString &userId )
{
    MatrixRequest *r = MatrixClient::self()->call( "verify_user", MatrixClient::obj( "user_id", userId ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotVerificationStarted( const JsonValue & ) ) );
}

void MainWindow::slotVerificationStarted( const JsonValue &reply )
{
    const TQString cmd = reply.get( "cmd" ).toString();
    if ( cmd == "start_dm" ) {
        if ( reply.get( "ok" ).toBool() ) {
            TQString id = reply.get( "room_id" ).toString();
            TQTimer::singleShot( 0, this, TQ_SLOT( slotUpdateTray() ) );
            m_roomList->selectRoom( id );
        }
        return;
    }
    if ( !reply.get( "ok" ).toBool() ) {
        KMessageBox::sorry( this, i18n( "The verification could not be started:\n%1" ).arg( reply.get( "error" ).toString() ) );
        return;
    }
    TQString user = reply.get( "user_id" ).toString();
    verificationDialog( user, reply.get( "flow_id" ).toString(), false, user == MatrixClient::self()->userId() );
}

// ---- dialogs ------------------------------------------------------------------

void MainWindow::slotNewRoom()
{
    CreateRoomDialog dlg( this );
    if ( dlg.exec() == TQDialog::Accepted )
        m_roomList->selectRoom( dlg.roomId() );
}

void MainWindow::slotStartChat()
{
    UserPickerDialog dlg( this, i18n( "Start Chat" ), i18n( "&Start Chat" ),
                          i18n( "Find the person you want to talk with by name or Matrix ID. Direct chats are end-to-end encrypted." ) );
    if ( dlg.exec() != TQDialog::Accepted || dlg.userId().isEmpty() )
        return;
    MatrixRequest *r = MatrixClient::self()->call( "start_dm", MatrixClient::obj( "user_id", dlg.userId() ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotSendReply( const JsonValue & ) ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotVerificationStarted( const JsonValue & ) ) );
}

void MainWindow::slotExplore()
{
    DirectoryDialog dlg( this );
    if ( dlg.exec() == TQDialog::Accepted )
        m_roomList->selectRoom( dlg.roomId() );
}

void MainWindow::slotManageAccount()
{
    AccountDialog dlg( this );
    connect( &dlg, TQ_SIGNAL( verifyOwnDevice() ), this, TQ_SLOT( slotVerifySession() ) );
    connect( &dlg, TQ_SIGNAL( verifyDevice( const TQString & ) ), this, TQ_SLOT( slotVerifyDevice( const TQString & ) ) );
    dlg.exec();
}

void MainWindow::slotEncryptionSettings()
{
    AccountDialog dlg( this, AccountDialog::Security );
    connect( &dlg, TQ_SIGNAL( verifyOwnDevice() ), this, TQ_SLOT( slotVerifySession() ) );
    connect( &dlg, TQ_SIGNAL( verifyDevice( const TQString & ) ), this, TQ_SLOT( slotVerifyDevice( const TQString & ) ) );
    dlg.exec();
}

void MainWindow::slotRoomSettings()
{
    JsonValue room = m_roomList->room( m_currentRoom );
    if ( room.isNull() )
        return;
    RoomSettingsDialog dlg( this, room );
    dlg.exec();
}

void MainWindow::slotInvite()
{
    JsonValue room = m_roomList->room( m_currentRoom );
    if ( room.isNull() )
        return;
    UserPickerDialog dlg( this, i18n( "Invite to %1" ).arg( room.get( "name" ).toString() ), i18n( "&Invite" ),
                          i18n( "Find the person to invite by name or Matrix ID." ) );
    if ( dlg.exec() == TQDialog::Accepted && !dlg.userId().isEmpty() )
        MatrixClient::self()->send( "invite", MatrixClient::obj( "room_id", m_currentRoom, "user_id", dlg.userId() ) );
}

void MainWindow::slotLeave()
{
    JsonValue room = m_roomList->room( m_currentRoom );
    if ( room.isNull() )
        return;
    if ( KMessageBox::warningContinueCancel( this, i18n( "Leave %1?" ).arg( room.get( "name" ).toString() ), i18n( "Leave Room" ),
                                             KGuiItem( i18n( "&Leave" ), "system-log-out" ) ) != KMessageBox::Continue )
        return;
    MatrixClient::self()->send( "leave", MatrixClient::obj( "room_id", m_currentRoom ) );
}

void MainWindow::slotToggleFavourite()
{
    if ( !m_currentRoom.isEmpty() )
        MatrixClient::self()->send( "set_favourite", MatrixClient::obj( "room_id", m_currentRoom, "value", m_favourite->isChecked() ) );
}

void MainWindow::slotPreferences()
{
    SettingsDialog dlg( this );
    connect( &dlg, TQ_SIGNAL( settingsChanged() ), this, TQ_SLOT( slotSettingsChanged() ) );
    dlg.exec();
}

void MainWindow::slotSettingsChanged()
{
    if ( Settings::trayIcon() && !m_tray ) {
        m_tray = new Tray( this );
        connect( m_tray, TQ_SIGNAL( nextUnreadRequested() ), this, TQ_SLOT( slotNextUnread() ) );
        connect( m_tray, TQ_SIGNAL( quitSelected() ), this, TQ_SLOT( slotQuit() ) );
        m_tray->show();
        slotUpdateTray();
    } else if ( !Settings::trayIcon() && m_tray ) {
        delete (Tray *) m_tray;
    }
    m_timeline->scheduleRender();
}

void MainWindow::slotConfigureNotifications()
{
    KNotifyDialog::configure( this );
}

void MainWindow::slotConfigureKeys()
{
    KKeyDialog::configure( actionCollection(), this );
}

void MainWindow::slotError( const TQString &cmd, const TQString &message )
{
    if ( cmd == "typing" || cmd == "mark_read" || cmd == "close_room" )
        return;
    m_status->setText( i18n( "Error: %1" ).arg( message ) );
    if ( cmd == "send" || cmd == "edit" || cmd == "upload" || cmd == "invite" || cmd == "leave" || cmd == "accept_invite"
         || cmd == "start_dm" || cmd == "redact" )
        KMessageBox::sorry( this, message, i18n( "TMatrix" ) );
}

// ---- closing --------------------------------------------------------------------

bool MainWindow::queryClose()
{
    if ( m_quitting || kapp->sessionSaving() || !m_tray )
        return true;
    hide();
    return false;
}

bool MainWindow::queryExit()
{
    return true;
}

void MainWindow::slotQuit()
{
    m_quitting = true;
    close();
    kapp->quit();
}

#include "mainwindow.moc"
