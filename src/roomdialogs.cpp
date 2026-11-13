/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "roomdialogs.h"
#include "matrixclient.h"
#include "mediacache.h"

#include <tqbuttongroup.h>
#include <tqcheckbox.h>
#include <tqheader.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqradiobutton.h>
#include <tqregexp.h>
#include <tqimage.h>
#include <tqtimer.h>
#include <tqvbox.h>
#include <tqhbox.h>

#include <tdeapplication.h>
#include <tdefiledialog.h>
#include <kiconloader.h>
#include <klineedit.h>
#include <tdelistview.h>
#include <tdelocale.h>
#include <tdemessagebox.h>
#include <kpushbutton.h>
#include <ktextedit.h>
#include <kinputdialog.h>
#include <tdepopupmenu.h>

static TQLabel *statusLabel( TQWidget *parent )
{
    TQLabel *l = new TQLabel( parent );
    l->setAlignment( TQt::WordBreak );
    return l;
}

static TQString errorText( const TQString &e )
{
    return "<font color=\"#c0392b\">" + e + "</font>";
}

// ---- UserPickerDialog ---------------------------------------------------------

UserPickerDialog::UserPickerDialog( TQWidget *parent, const TQString &caption, const TQString &okText, const TQString &intro )
    : KDialogBase( parent, "userpicker", true, caption, Ok | Cancel, Ok, true )
{
    setButtonOK( KGuiItem( okText, "go-next" ) );
    TQVBox *page = makeVBoxMainWidget();
    TQLabel *l = new TQLabel( intro, page );
    l->setAlignment( TQt::WordBreak );
    m_search = new KLineEdit( page );
    m_search->setClickMessage( i18n( "Name or Matrix ID, e.g. @alice:matrix.org" ) );
    m_list = new TDEListView( page );
    m_list->addColumn( i18n( "Name" ) );
    m_list->addColumn( i18n( "Matrix ID" ) );
    m_list->setAllColumnsShowFocus( true );
    m_list->setResizeMode( TQListView::AllColumns );
    m_status = statusLabel( page );
    m_timer = new TQTimer( this );
    connect( m_timer, TQ_SIGNAL( timeout() ), this, TQ_SLOT( slotSearch() ) );
    connect( m_search, TQ_SIGNAL( textChanged( const TQString & ) ), this, TQ_SLOT( slotChanged() ) );
    connect( m_list, TQ_SIGNAL( selectionChanged() ), this, TQ_SLOT( slotSelected() ) );
    connect( m_list, TQ_SIGNAL( doubleClicked( TQListViewItem *, const TQPoint &, int ) ), this, TQ_SLOT( slotOk() ) );
    enableButtonOK( false );
    resize( 460, 380 );
    m_search->setFocus();
}

TQString UserPickerDialog::userId() const
{
    if ( m_list->selectedItem() )
        return m_list->selectedItem()->text( 1 );
    TQString t = m_search->text().stripWhiteSpace();
    return ( t.startsWith( "@" ) && t.contains( ':' ) ) ? t : TQString::null;
}

void UserPickerDialog::slotChanged()
{
    m_list->clearSelection();
    enableButtonOK( !userId().isEmpty() );
    m_timer->start( 500, true );
}

void UserPickerDialog::slotSearch()
{
    TQString term = m_search->text().stripWhiteSpace();
    if ( term.length() < 2 )
        return;
    m_status->setText( i18n( "Searching…" ) );
    MatrixRequest *r = MatrixClient::self()->call( "search_users", MatrixClient::obj( "term", term ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotResults( const JsonValue & ) ) );
}

void UserPickerDialog::slotResults( const JsonValue &reply )
{
    m_list->clear();
    if ( !reply.get( "ok" ).toBool() ) {
        m_status->setText( errorText( reply.get( "error" ).toString() ) );
        return;
    }
    const JsonValue &users = reply.get( "users" );
    for ( uint i = 0; i < users.count(); ++i ) {
        const JsonValue &u = users.at( i );
        TQString name = u.get( "name" ).toString();
        TQListViewItem *item = new TQListViewItem( m_list, name.isEmpty() ? u.get( "user_id" ).toString() : name, u.get( "user_id" ).toString() );
        item->setPixmap( 0, MediaCache::placeholder( item->text( 0 ), item->text( 1 ), 22 ) );
    }
    m_status->setText( users.count() ? TQString::null : i18n( "Nobody found. You can still enter a full Matrix ID." ) );
}

void UserPickerDialog::slotSelected()
{
    enableButtonOK( !userId().isEmpty() );
}

// ---- CreateRoomDialog ---------------------------------------------------------

CreateRoomDialog::CreateRoomDialog( TQWidget *parent )
    : KDialogBase( parent, "createroom", true, i18n( "Create Room" ), Ok | Cancel, Ok, true )
{
    setButtonOK( KGuiItem( i18n( "&Create" ), "list-add" ) );
    TQWidget *page = makeMainWidget();
    TQGridLayout *g = new TQGridLayout( page, 8, 2, 0, spacingHint() );
    m_name = new KLineEdit( page );
    g->addWidget( new TQLabel( m_name, i18n( "&Name:" ), page ), 0, 0 );
    g->addWidget( m_name, 0, 1 );
    m_topic = new KLineEdit( page );
    g->addWidget( new TQLabel( m_topic, i18n( "&Topic:" ), page ), 1, 0 );
    g->addWidget( m_topic, 1, 1 );
    m_public = new TQCheckBox( i18n( "&Public room: anyone can find and join it" ), page );
    g->addWidget( m_public, 2, 1 );
    m_alias = new KLineEdit( page );
    m_alias->setEnabled( false );
    m_alias->setClickMessage( i18n( "e.g. trinity-users" ) );
    g->addWidget( new TQLabel( m_alias, i18n( "&Address:" ), page ), 3, 0 );
    g->addWidget( m_alias, 3, 1 );
    m_encrypted = new TQCheckBox( i18n( "&End-to-end encryption (cannot be turned off later)" ), page );
    m_encrypted->setChecked( true );
    g->addWidget( m_encrypted, 4, 1 );
    m_invite = new KLineEdit( page );
    m_invite->setClickMessage( i18n( "@bob:example.org, @carol:example.org" ) );
    g->addWidget( new TQLabel( m_invite, i18n( "&Invite:" ), page ), 5, 0 );
    g->addWidget( m_invite, 5, 1 );
    m_status = statusLabel( page );
    g->addMultiCellWidget( m_status, 6, 6, 0, 1 );
    g->setRowStretch( 7, 1 );
    connect( m_public, TQ_SIGNAL( toggled( bool ) ), this, TQ_SLOT( slotPublicToggled( bool ) ) );
    setMinimumWidth( 460 );
    m_name->setFocus();
}

void CreateRoomDialog::slotPublicToggled( bool on )
{
    m_alias->setEnabled( on );
    // public rooms are usually not encrypted: anyone can read them anyway
    m_encrypted->setChecked( !on );
}

void CreateRoomDialog::slotOk()
{
    if ( m_name->text().stripWhiteSpace().isEmpty() ) {
        m_status->setText( errorText( i18n( "Enter a name for the room." ) ) );
        return;
    }
    JsonValue args = MatrixClient::obj( "name", m_name->text().stripWhiteSpace(), "topic", m_topic->text().stripWhiteSpace(),
                                        "public", m_public->isChecked(), "encrypted", m_encrypted->isChecked() );
    if ( m_public->isChecked() && !m_alias->text().stripWhiteSpace().isEmpty() )
        args.set( "alias", m_alias->text().stripWhiteSpace() );
    JsonValue invite = JsonValue::array();
    TQStringList users = TQStringList::split( TQRegExp( "[,;\\s]+" ), m_invite->text() );
    for ( TQStringList::ConstIterator it = users.begin(); it != users.end(); ++it )
        invite.append( *it );
    args.set( "invite", invite );
    m_status->setText( i18n( "Creating the room…" ) );
    enableButtonOK( false );
    MatrixRequest *r = MatrixClient::self()->call( "create_room", args );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotCreated( const JsonValue & ) ) );
}

void CreateRoomDialog::slotCreated( const JsonValue &reply )
{
    enableButtonOK( true );
    if ( !reply.get( "ok" ).toBool() ) {
        m_status->setText( errorText( i18n( "The room could not be created: %1" ).arg( reply.get( "error" ).toString() ) ) );
        return;
    }
    m_roomId = reply.get( "room_id" ).toString();
    accept();
}

// ---- DirectoryDialog ----------------------------------------------------------

DirectoryDialog::DirectoryDialog( TQWidget *parent )
    : KDialogBase( parent, "directory", true, i18n( "Explore Rooms" ), User1 | Ok | Cancel, Ok, true ), m_append( false )
{
    setButtonOK( KGuiItem( i18n( "&Join" ), "go-next" ) );
    setButtonText( User1, i18n( "Load &More" ) );
    TQWidget *page = makeMainWidget();
    TQGridLayout *g = new TQGridLayout( page, 4, 2, 0, spacingHint() );
    m_search = new KLineEdit( page );
    m_search->setClickMessage( i18n( "Search rooms, or enter an address like #trinity:matrix.org" ) );
    g->addWidget( new TQLabel( m_search, i18n( "&Search:" ), page ), 0, 0 );
    g->addWidget( m_search, 0, 1 );
    m_server = new KLineEdit( page );
    m_server->setClickMessage( i18n( "your homeserver" ) );
    g->addWidget( new TQLabel( m_server, i18n( "Ser&ver:" ), page ), 1, 0 );
    g->addWidget( m_server, 1, 1 );
    m_list = new TDEListView( page );
    m_list->addColumn( i18n( "Room" ) );
    m_list->addColumn( i18n( "Members" ) );
    m_list->addColumn( i18n( "Topic" ) );
    m_list->addColumn( i18n( "Address" ) );
    m_list->setAllColumnsShowFocus( true );
    m_list->setColumnAlignment( 1, TQt::AlignRight );
    m_list->setColumnWidthMode( 2, TQListView::Manual );
    m_list->setColumnWidth( 2, 320 );
    m_list->setSorting( -1 );
    g->addMultiCellWidget( m_list, 2, 2, 0, 1 );
    m_status = statusLabel( page );
    g->addMultiCellWidget( m_status, 3, 3, 0, 1 );
    m_timer = new TQTimer( this );
    connect( m_timer, TQ_SIGNAL( timeout() ), this, TQ_SLOT( slotSearch() ) );
    connect( m_search, TQ_SIGNAL( textChanged( const TQString & ) ), m_timer, TQ_SLOT( start() ) );
    connect( m_server, TQ_SIGNAL( returnPressed() ), this, TQ_SLOT( slotSearch() ) );
    connect( m_list, TQ_SIGNAL( selectionChanged() ), this, TQ_SLOT( slotSelection() ) );
    connect( m_list, TQ_SIGNAL( doubleClicked( TQListViewItem *, const TQPoint &, int ) ), this, TQ_SLOT( slotOk() ) );
    connect( this, TQ_SIGNAL( user1Clicked() ), this, TQ_SLOT( slotMore() ) );
    enableButton( User1, false );
    resize( 720, 480 );
    m_search->setFocus();
    slotSearch();
}

void DirectoryDialog::slotSearch()
{
    m_timer->stop();
    TQString term = m_search->text().stripWhiteSpace();
    if ( term.startsWith( "#" ) || term.startsWith( "!" ) ) {
        m_status->setText( i18n( "Press Join to join %1." ).arg( term ) );
        enableButtonOK( true );
        return;
    }
    m_append = false;
    m_next = TQString::null;
    m_status->setText( i18n( "Loading the room directory…" ) );
    JsonValue args = MatrixClient::obj( "term", term, "server", m_server->text().stripWhiteSpace() );
    MatrixRequest *r = MatrixClient::self()->call( "directory", args );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotResults( const JsonValue & ) ) );
}

void DirectoryDialog::slotMore()
{
    if ( m_next.isEmpty() )
        return;
    m_append = true;
    JsonValue args = MatrixClient::obj( "term", m_search->text().stripWhiteSpace(), "server", m_server->text().stripWhiteSpace(),
                                        "since", m_next );
    MatrixRequest *r = MatrixClient::self()->call( "directory", args );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotResults( const JsonValue & ) ) );
}

void DirectoryDialog::slotResults( const JsonValue &reply )
{
    if ( !m_append )
        m_list->clear();
    if ( !reply.get( "ok" ).toBool() ) {
        m_status->setText( errorText( reply.get( "error" ).toString() ) );
        return;
    }
    const JsonValue &rooms = reply.get( "rooms" );
    TQListViewItem *last = m_list->lastItem();
    for ( uint i = 0; i < rooms.count(); ++i ) {
        const JsonValue &r = rooms.at( i );
        TQString name = r.get( "name" ).toString();
        if ( name.isEmpty() )
            name = r.get( "alias" ).toString();
        if ( name.isEmpty() )
            name = r.get( "room_id" ).toString();
        TQString topic = r.get( "topic" ).toString().simplifyWhiteSpace();
        last = new TQListViewItem( m_list, last, name, TQString::number( r.get( "members" ).toInt() ), topic, r.get( "alias" ).toString() );
        last->setText( 4, r.get( "room_id" ).toString() );
        last->setPixmap( 0, MediaCache::placeholder( name, r.get( "room_id" ).toString(), 16 ) );
        if ( r.get( "joined" ).toBool() )
            last->setText( 0, i18n( "%1 (joined)" ).arg( name ) );
    }
    m_next = reply.get( "next" ).toString();
    enableButton( User1, !m_next.isEmpty() );
    int total = reply.get( "total" ).toInt();
    m_status->setText( total > 0 ? i18n( "%1 of about %2 public rooms" ).arg( m_list->childCount() ).arg( total )
                                 : i18n( "%1 public rooms" ).arg( m_list->childCount() ) );
    enableButtonOK( m_list->selectedItem() != 0 );
}

void DirectoryDialog::slotSelection()
{
    enableButtonOK( m_list->selectedItem() != 0 );
}

void DirectoryDialog::slotOk()
{
    TQString term = m_search->text().stripWhiteSpace();
    if ( m_list->selectedItem() ) {
        TQString alias = m_list->selectedItem()->text( 3 );
        join( alias.isEmpty() ? m_list->selectedItem()->text( 4 ) : alias );
    } else if ( term.startsWith( "#" ) || term.startsWith( "!" ) ) {
        join( term );
    }
}

void DirectoryDialog::join( const TQString &target )
{
    m_status->setText( i18n( "Joining %1…" ).arg( target ) );
    enableButtonOK( false );
    MatrixRequest *r = MatrixClient::self()->call( "join", MatrixClient::obj( "target", target ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotJoined( const JsonValue & ) ) );
}

void DirectoryDialog::slotJoined( const JsonValue &reply )
{
    enableButtonOK( true );
    if ( !reply.get( "ok" ).toBool() ) {
        m_status->setText( errorText( i18n( "Could not join the room: %1" ).arg( reply.get( "error" ).toString() ) ) );
        return;
    }
    m_roomId = reply.get( "room_id" ).toString();
    accept();
}

// ---- RoomSettingsDialog -------------------------------------------------------

RoomSettingsDialog::RoomSettingsDialog( TQWidget *parent, const JsonValue &room, int page )
    : KDialogBase( IconList, i18n( "Room Settings — %1" ).arg( room.get( "name" ).toString() ), Ok | Apply | Cancel, Ok, parent,
                   "roomsettings", true, true ),
      m_room( room ), m_roomId( room.get( "room_id" ).toString() )
{
    // General
    TQFrame *general = addPage( i18n( "General" ), i18n( "Name, Topic and Picture" ), DesktopIcon( "configure", 32 ) );
    TQGridLayout *g = new TQGridLayout( general, 6, 3, 0, spacingHint() );
    m_avatar = new TQLabel( general );
    m_avatar->setFixedSize( 64, 64 );
    TQString mxc = room.get( "avatar" ).toString();
    if ( !mxc.isEmpty() && MediaCache::self()->avatar( mxc, 64 ) )
        m_avatar->setPixmap( MediaCache::self()->pixmap( MediaCache::avatarKey( mxc, 64 ) ) );
    else
        m_avatar->setPixmap( MediaCache::placeholder( room.get( "name" ).toString(), m_roomId, 64 ) );
    g->addMultiCellWidget( m_avatar, 0, 1, 0, 0 );
    KPushButton *avatarButton = new KPushButton( KGuiItem( i18n( "Change &Picture…" ), "image-x-generic" ), general );
    m_avatarButton = avatarButton;
    g->addWidget( avatarButton, 0, 2, TQt::AlignLeft );
    connect( avatarButton, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotAvatar() ) );
    m_name = new KLineEdit( room.get( "name" ).toString(), general );
    g->addWidget( new TQLabel( m_name, i18n( "&Name:" ), general ), 2, 0 );
    g->addMultiCellWidget( m_name, 2, 2, 1, 2 );
    m_topic = new KTextEdit( general );
    m_topic->setTextFormat( TQt::PlainText );
    m_topic->setText( room.get( "topic" ).toString() );
    m_topic->setFixedHeight( 80 );
    g->addWidget( new TQLabel( m_topic, i18n( "&Topic:" ), general ), 3, 0, TQt::AlignTop );
    g->addMultiCellWidget( m_topic, 3, 3, 1, 2 );
    m_address = new TQLabel( general );
    m_address->setTextFormat( TQt::RichText );
    TQString alias = room.get( "alias" ).toString();
    m_address->setText( i18n( "<b>Address:</b> %1<br><b>Room ID:</b> %2" ).arg( alias.isEmpty() ? i18n( "none" ) : alias ).arg( m_roomId ) );
    g->addMultiCellWidget( m_address, 4, 4, 0, 2 );
    g->setRowStretch( 5, 1 );

    // Members
    TQFrame *members = addPage( i18n( "Members" ), i18n( "People in This Room" ), DesktopIcon( "contact-new", 32 ) );
    TQVBoxLayout *mv = new TQVBoxLayout( members, 0, spacingHint() );
    m_members = new TDEListView( members );
    m_members->addColumn( i18n( "Name" ) );
    m_members->addColumn( i18n( "Matrix ID" ) );
    m_members->addColumn( i18n( "Role" ) );
    m_members->setAllColumnsShowFocus( true );
    mv->addWidget( m_members );
    TQHBox *mb = new TQHBox( members );
    mb->setSpacing( spacingHint() );
    KPushButton *inv = new KPushButton( KGuiItem( i18n( "&Invite…" ), "contact-new" ), mb );
    KPushButton *kick = new KPushButton( KGuiItem( i18n( "&Remove" ), "edit-delete" ), mb );
    KPushButton *ban = new KPushButton( KGuiItem( i18n( "&Ban" ), "button_cancel" ), mb );
    m_invite = inv;
    m_kick = kick;
    m_ban = ban;
    new TQWidget( mb );
    mv->addWidget( mb );
    connect( inv, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotInvite() ) );
    connect( kick, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotKick() ) );
    connect( ban, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotBan() ) );
    connect( m_members, TQ_SIGNAL( executed( TQListViewItem * ) ), this, TQ_SLOT( slotMemberMenu( TQListViewItem * ) ) );

    // Notifications
    TQFrame *notify = addPage( i18n( "Notifications" ), i18n( "When to Notify You" ), DesktopIcon( "knotify", 32 ) );
    TQVBoxLayout *nv = new TQVBoxLayout( notify, 0, spacingHint() );
    m_notify = new TQButtonGroup( 1, TQt::Horizontal, i18n( "Notify me about" ), notify );
    m_notify->setRadioButtonExclusive( true );
    m_notify->insert( new TQRadioButton( i18n( "The &default of the account (all messages in direct chats, mentions in rooms)" ), m_notify ), 0 );
    m_notify->insert( new TQRadioButton( i18n( "&All messages" ), m_notify ), 1 );
    m_notify->insert( new TQRadioButton( i18n( "&Mentions and keywords only" ), m_notify ), 2 );
    m_notify->insert( new TQRadioButton( i18n( "&Nothing (mute this room)" ), m_notify ), 3 );
    nv->addWidget( m_notify );
    nv->addStretch( 1 );

    // Security
    TQFrame *security = addPage( i18n( "Security" ), i18n( "Encryption" ), DesktopIcon( "encrypted", 32 ) );
    TQVBoxLayout *sv = new TQVBoxLayout( security, 0, spacingHint() );
    m_encryptionInfo = new TQLabel( security );
    m_encryptionInfo->setAlignment( TQt::WordBreak );
    m_encryptionInfo->setTextFormat( TQt::RichText );
    bool enc = room.get( "encrypted" ).toBool();
    m_encryptionInfo->setText( enc ? i18n( "<b>Messages in this room are end-to-end encrypted.</b><p>Only the members can read them; "
                                           "not even the servers can. Verify the sessions of the people you talk with to be sure "
                                           "nobody listens in.</p><p>Encryption cannot be turned off.</p>" )
                                   : i18n( "<b>Messages in this room are not encrypted.</b><p>The servers of all members can read them.</p>"
                                           "<p>Once turned on, encryption cannot be turned off again. Public rooms are usually not "
                                           "encrypted.</p>" ) );
    sv->addWidget( m_encryptionInfo );
    KPushButton *encrypt = new KPushButton( KGuiItem( i18n( "&Turn On Encryption" ), "encrypted" ), security );
    m_encrypt = encrypt;
    encrypt->setEnabled( false );
    encrypt->setShown( !enc );
    sv->addWidget( encrypt, 0, TQt::AlignLeft );
    sv->addStretch( 1 );
    connect( encrypt, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotEncrypt() ) );

    m_kick->setEnabled( false );
    m_ban->setEnabled( false );
    m_invite->setEnabled( false );
    m_avatarButton->setEnabled( false );
    m_name->setReadOnly( true );
    m_topic->setReadOnly( true );

    slotReloadMembers();
    MatrixRequest *n = MatrixClient::self()->call( "room_notifications", MatrixClient::obj( "room_id", m_roomId ) );
    connect( n, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotNotifications( const JsonValue & ) ) );

    showPage( page );
    resize( 640, 460 );
}

void RoomSettingsDialog::slotReloadMembers()
{
    MatrixRequest *r = MatrixClient::self()->call( "members", MatrixClient::obj( "room_id", m_roomId ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotMembers( const JsonValue & ) ) );
}

void RoomSettingsDialog::slotMembers( const JsonValue &reply )
{
    if ( !reply.get( "ok" ).toBool() )
        return;
    m_permissions = reply;
    m_members->clear();
    const JsonValue &list = reply.get( "members" );
    for ( uint i = 0; i < list.count(); ++i ) {
        const JsonValue &m = list.at( i );
        int power = m.get( "power" ).toInt();
        TQString role = power >= 100 ? i18n( "Administrator" ) : power >= 50 ? i18n( "Moderator" ) : TQString::null;
        if ( m.get( "membership" ).toString() == "invite" )
            role = i18n( "Invited" );
        TQString name = m.get( "name" ).toString();
        TQListViewItem *item = new TQListViewItem( m_members, name.isEmpty() ? m.get( "user_id" ).toString() : name,
                                                   m.get( "user_id" ).toString(), role );
        item->setPixmap( 0, MediaCache::placeholder( item->text( 0 ), item->text( 1 ), 20 ) );
    }
    m_invite->setEnabled( reply.get( "can_invite" ).toBool() );
    m_kick->setEnabled( reply.get( "can_kick" ).toBool() );
    m_ban->setEnabled( reply.get( "can_ban" ).toBool() );
    m_name->setReadOnly( !reply.get( "can_set_name" ).toBool() );
    m_topic->setReadOnly( !reply.get( "can_set_topic" ).toBool() );
    m_avatarButton->setEnabled( reply.get( "can_set_avatar" ).toBool() );
    m_encrypt->setEnabled( reply.get( "can_encrypt" ).toBool() );
}

void RoomSettingsDialog::slotNotifications( const JsonValue &reply )
{
    m_notifyMode = reply.get( "mode" ).toString();
    int id = m_notifyMode == "all" ? 1 : m_notifyMode == "mentions" ? 2 : m_notifyMode == "mute" ? 3 : 0;
    m_notify->setButton( id );
}

void RoomSettingsDialog::slotAvatar()
{
    TQString path = KFileDialog::getOpenFileName( TQString::null, "image/png image/jpeg image/gif image/webp", this,
                                                  i18n( "Choose a Room Picture" ) );
    if ( path.isEmpty() )
        return;
    MatrixClient::self()->send( "set_room_avatar", MatrixClient::obj( "room_id", m_roomId, "path", path ) );
    TQPixmap pm( path );
    if ( !pm.isNull() )
        m_avatar->setPixmap( TQPixmap( pm.convertToImage().smoothScale( 64, 64, TQImage::ScaleMin ) ) );
}

void RoomSettingsDialog::slotInvite()
{
    UserPickerDialog dlg( this, i18n( "Invite to %1" ).arg( m_room.get( "name" ).toString() ), i18n( "&Invite" ),
                          i18n( "Find the person to invite by name or Matrix ID." ) );
    if ( dlg.exec() != TQDialog::Accepted || dlg.userId().isEmpty() )
        return;
    MatrixRequest *r = MatrixClient::self()->call( "invite", MatrixClient::obj( "room_id", m_roomId, "user_id", dlg.userId() ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotReloadMembers() ) );
}

void RoomSettingsDialog::slotKick()
{
    TQListViewItem *item = m_members->selectedItem();
    if ( !item )
        return;
    bool ok;
    TQString reason = KInputDialog::getText( i18n( "Remove Member" ), i18n( "Remove %1 from the room? Reason (optional):" ).arg( item->text( 0 ) ),
                                            TQString::null, &ok, this );
    if ( !ok )
        return;
    MatrixRequest *r = MatrixClient::self()->call( "kick", MatrixClient::obj( "room_id", m_roomId, "user_id", item->text( 1 ), "reason", reason ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotReloadMembers() ) );
}

void RoomSettingsDialog::slotBan()
{
    TQListViewItem *item = m_members->selectedItem();
    if ( !item )
        return;
    bool ok;
    TQString reason = KInputDialog::getText( i18n( "Ban Member" ), i18n( "Ban %1 from the room? Reason (optional):" ).arg( item->text( 0 ) ),
                                            TQString::null, &ok, this );
    if ( !ok )
        return;
    MatrixRequest *r = MatrixClient::self()->call( "ban", MatrixClient::obj( "room_id", m_roomId, "user_id", item->text( 1 ), "reason", reason ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotReloadMembers() ) );
}

void RoomSettingsDialog::slotMemberMenu( TQListViewItem * )
{
}

void RoomSettingsDialog::slotEncrypt()
{
    if ( KMessageBox::warningContinueCancel( this, i18n( "Turn on end-to-end encryption for this room? It cannot be turned off again, "
                                                         "and bots or bridges that do not support encryption stop working." ),
                                             i18n( "Turn On Encryption" ), KGuiItem( i18n( "Turn On" ), "encrypted" ) ) != KMessageBox::Continue )
        return;
    MatrixClient::self()->send( "enable_encryption", MatrixClient::obj( "room_id", m_roomId ) );
    m_encrypt->setEnabled( false );
    m_encryptionInfo->setText( i18n( "<b>Messages in this room are end-to-end encrypted.</b>" ) );
}

void RoomSettingsDialog::apply()
{
    if ( !m_name->isReadOnly() && m_name->text().stripWhiteSpace() != m_room.get( "name" ).toString() ) {
        MatrixClient::self()->send( "set_room_name", MatrixClient::obj( "room_id", m_roomId, "name", m_name->text().stripWhiteSpace() ) );
        m_room.set( "name", m_name->text().stripWhiteSpace() );
    }
    if ( !m_topic->isReadOnly() && m_topic->text() != m_room.get( "topic" ).toString() ) {
        MatrixClient::self()->send( "set_room_topic", MatrixClient::obj( "room_id", m_roomId, "topic", m_topic->text() ) );
        m_room.set( "topic", m_topic->text() );
    }
    static const char *modes[] = { "default", "all", "mentions", "mute" };
    int id = m_notify->selectedId();
    if ( id >= 0 && id < 4 && m_notifyMode != modes[id] ) {
        MatrixClient::self()->send( "set_room_notifications", MatrixClient::obj( "room_id", m_roomId, "mode", modes[id] ) );
        m_notifyMode = modes[id];
    }
}

void RoomSettingsDialog::slotApply()
{
    apply();
}

void RoomSettingsDialog::slotOk()
{
    apply();
    accept();
}

#include "roomdialogs.moc"
