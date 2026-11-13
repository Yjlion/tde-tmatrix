/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "accountdialog.h"
#include "matrixclient.h"
#include "mediacache.h"

#include <tqapplication.h>
#include <tqclipboard.h>
#include <tqdatetime.h>
#include <tqfile.h>
#include <tqimage.h>
#include <tqhbox.h>
#include <tqcursor.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqtextstream.h>

#include <tdeapplication.h>
#include <tdefiledialog.h>
#include <tdeglobal.h>
#include <tdeglobalsettings.h>
#include <kiconloader.h>
#include <kinputdialog.h>
#include <klineedit.h>
#include <tdelistview.h>
#include <tdelocale.h>
#include <tdemessagebox.h>
#include <kpassdlg.h>
#include <kpushbutton.h>

AccountDialog::AccountDialog( TQWidget *parent, int page )
    : KDialogBase( IconList, i18n( "Manage Account" ), Ok | Apply | Cancel, Ok, parent, "account", true, true )
{
    // Profile
    TQFrame *profile = addPage( i18n( "Profile" ), i18n( "Your Name and Picture" ), DesktopIcon( "identity", 32 ) );
    TQGridLayout *g = new TQGridLayout( profile, 5, 3, 0, spacingHint() );
    m_avatar = new TQLabel( profile );
    m_avatar->setFixedSize( 72, 72 );
    m_avatar->setPixmap( MediaCache::placeholder( MatrixClient::self()->userId(), MatrixClient::self()->userId(), 72 ) );
    g->addMultiCellWidget( m_avatar, 0, 1, 0, 0 );
    KPushButton *pic = new KPushButton( KGuiItem( i18n( "Change &Picture…" ), "image-x-generic" ), profile );
    g->addWidget( pic, 0, 2, TQt::AlignLeft );
    connect( pic, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotAvatar() ) );
    m_displayName = new KLineEdit( profile );
    g->addWidget( new TQLabel( m_displayName, i18n( "&Display name:" ), profile ), 2, 0 );
    g->addMultiCellWidget( m_displayName, 2, 2, 1, 2 );
    m_userInfo = new TQLabel( profile );
    m_userInfo->setTextFormat( TQt::RichText );
    m_userInfo->setText( i18n( "<b>Matrix ID:</b> %1<br><b>Homeserver:</b> %2<br><b>Session:</b> %3" )
                             .arg( MatrixClient::self()->userId() ).arg( MatrixClient::self()->homeserver() )
                             .arg( MatrixClient::self()->deviceId() ) );
    g->addMultiCellWidget( m_userInfo, 3, 3, 0, 2 );
    g->setRowStretch( 4, 1 );

    // Sessions
    TQFrame *sessions = addPage( i18n( "Sessions" ), i18n( "Where You Are Logged In" ), DesktopIcon( "computer", 32 ) );
    TQVBoxLayout *sv = new TQVBoxLayout( sessions, 0, spacingHint() );
    m_devices = new TDEListView( sessions );
    m_devices->addColumn( i18n( "Session" ) );
    m_devices->addColumn( i18n( "ID" ) );
    m_devices->addColumn( i18n( "Last Seen" ) );
    m_devices->addColumn( i18n( "Verified" ) );
    m_devices->setAllColumnsShowFocus( true );
    m_devices->setSelectionModeExt( TDEListView::Extended );
    sv->addWidget( m_devices );
    TQHBox *db = new TQHBox( sessions );
    db->setSpacing( spacingHint() );
    m_rename = new KPushButton( KGuiItem( i18n( "Re&name…" ), "edit" ), db );
    m_verifyDevice = new KPushButton( KGuiItem( i18n( "&Verify…" ), "encrypted" ), db );
    m_signOut = new KPushButton( KGuiItem( i18n( "&Sign Out…" ), "system-log-out" ), db );
    new TQWidget( db );
    sv->addWidget( db );
    connect( m_rename, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotRename() ) );
    connect( m_signOut, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotSignOut() ) );
    connect( m_verifyDevice, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotVerify() ) );
    connect( m_devices, TQ_SIGNAL( selectionChanged() ), this, TQ_SLOT( slotDeviceSelection() ) );
    slotDeviceSelection();

    // Security
    TQFrame *security = addPage( i18n( "Encryption" ), i18n( "Encryption, Verification and Recovery" ), DesktopIcon( "encrypted", 32 ) );
    TQVBoxLayout *ev = new TQVBoxLayout( security, 0, spacingHint() );
    m_securityInfo = new TQLabel( i18n( "Checking…" ), security );
    m_securityInfo->setTextFormat( TQt::RichText );
    m_securityInfo->setAlignment( TQt::WordBreak | TQt::AlignTop );
    ev->addWidget( m_securityInfo );
    m_verifySession = new KPushButton( KGuiItem( i18n( "Verify This Session with &Another Device…" ), "encrypted" ), security );
    m_enterKey = new KPushButton( KGuiItem( i18n( "&Enter Recovery Key…" ), "password" ), security );
    m_setupRecovery = new KPushButton( KGuiItem( i18n( "Set Up &Recovery…" ), "document-save" ), security );
    ev->addWidget( m_verifySession, 0, TQt::AlignLeft );
    ev->addWidget( m_enterKey, 0, TQt::AlignLeft );
    ev->addWidget( m_setupRecovery, 0, TQt::AlignLeft );
    ev->addStretch( 1 );
    connect( m_verifySession, TQ_SIGNAL( clicked() ), this, TQ_SIGNAL( verifyOwnDevice() ) );
    connect( m_verifySession, TQ_SIGNAL( clicked() ), this, TQ_SLOT( reject() ) );
    connect( m_enterKey, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotEnterKey() ) );
    connect( m_setupRecovery, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotSetupRecovery() ) );

    // Password
    TQFrame *pw = addPage( i18n( "Password" ), i18n( "Change Your Password" ), DesktopIcon( "password", 32 ) );
    TQGridLayout *pg = new TQGridLayout( pw, 6, 2, 0, spacingHint() );
    m_oldPassword = new KLineEdit( pw );
    m_oldPassword->setEchoMode( TQLineEdit::Password );
    m_newPassword = new KLineEdit( pw );
    m_newPassword->setEchoMode( TQLineEdit::Password );
    m_newPassword2 = new KLineEdit( pw );
    m_newPassword2->setEchoMode( TQLineEdit::Password );
    pg->addWidget( new TQLabel( m_oldPassword, i18n( "C&urrent password:" ), pw ), 0, 0 );
    pg->addWidget( m_oldPassword, 0, 1 );
    pg->addWidget( new TQLabel( m_newPassword, i18n( "&New password:" ), pw ), 1, 0 );
    pg->addWidget( m_newPassword, 1, 1 );
    pg->addWidget( new TQLabel( m_newPassword2, i18n( "&Repeat new password:" ), pw ), 2, 0 );
    pg->addWidget( m_newPassword2, 2, 1 );
    KPushButton *change = new KPushButton( KGuiItem( i18n( "&Change Password" ), "button_ok" ), pw );
    pg->addWidget( change, 3, 1, TQt::AlignLeft );
    m_passwordStatus = new TQLabel( pw );
    m_passwordStatus->setAlignment( TQt::WordBreak );
    pg->addMultiCellWidget( m_passwordStatus, 4, 4, 0, 1 );
    pg->setRowStretch( 5, 1 );
    connect( change, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotChangePassword() ) );

    MatrixRequest *r = MatrixClient::self()->call( "profile" );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotProfile( const JsonValue & ) ) );
    slotReloadDevices();
    MatrixRequest *e = MatrixClient::self()->call( "encryption_status" );
    connect( e, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotEncryption( const JsonValue & ) ) );
    connect( MatrixClient::self(), TQ_SIGNAL( encryptionChanged( const JsonValue & ) ), this, TQ_SLOT( slotEncryption( const JsonValue & ) ) );

    showPage( page );
    resize( 660, 470 );
}

void AccountDialog::slotProfile( const JsonValue &reply )
{
    if ( !reply.get( "ok" ).toBool() )
        return;
    m_originalName = reply.get( "name" ).toString();
    m_displayName->setText( m_originalName );
    TQString mxc = reply.get( "avatar" ).toString();
    if ( !mxc.isEmpty() ) {
        if ( MediaCache::self()->avatar( mxc, 72 ) )
            m_avatar->setPixmap( MediaCache::self()->pixmap( MediaCache::avatarKey( mxc, 72 ) ) );
    } else {
        m_avatar->setPixmap( MediaCache::placeholder( m_originalName.isEmpty() ? MatrixClient::self()->userId() : m_originalName,
                                                      MatrixClient::self()->userId(), 72 ) );
    }
}

void AccountDialog::slotReloadDevices()
{
    MatrixRequest *r = MatrixClient::self()->call( "devices" );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotDevices( const JsonValue & ) ) );
}

void AccountDialog::slotDevices( const JsonValue &reply )
{
    m_devices->clear();
    if ( !reply.get( "ok" ).toBool() )
        return;
    const JsonValue &list = reply.get( "devices" );
    for ( uint i = 0; i < list.count(); ++i ) {
        const JsonValue &d = list.at( i );
        TQString name = d.get( "name" ).toString();
        if ( name.isEmpty() )
            name = d.get( "device_id" ).toString();
        TQString seen;
        double ts = d.get( "last_seen_ts" ).toDouble();
        if ( ts > 0 ) {
            TQDateTime dt;
            dt.setTime_t( (uint) ( ts / 1000 ) );
            seen = TDEGlobal::locale()->formatDateTime( dt, true );
            TQString ip = d.get( "last_seen_ip" ).toString();
            if ( !ip.isEmpty() )
                seen += " (" + ip + ")";
        }
        bool current = d.get( "current" ).toBool();
        TQListViewItem *item = new TQListViewItem( m_devices, current ? i18n( "%1 (this session)" ).arg( name ) : name,
                                                   d.get( "device_id" ).toString(), seen,
                                                   d.get( "verified" ).toBool() ? i18n( "Yes" ) : i18n( "No" ) );
        item->setPixmap( 0, SmallIcon( d.get( "verified" ).toBool() ? "encrypted" : "decrypted" ) );
        item->setText( 4, current ? "1" : "0" );
    }
}

void AccountDialog::slotDeviceSelection()
{
    int n = 0;
    bool current = false;
    for ( TQListViewItemIterator it( m_devices, TQListViewItemIterator::Selected ); it.current(); ++it ) {
        ++n;
        current |= it.current()->text( 4 ) == "1";
    }
    m_rename->setEnabled( n == 1 );
    m_verifyDevice->setEnabled( n == 1 && !current );
    m_signOut->setEnabled( n > 0 && !current );
}

void AccountDialog::slotRename()
{
    TQListViewItem *item = m_devices->selectedItem();
    if ( !item )
        return;
    bool ok;
    TQString name = KInputDialog::getText( i18n( "Rename Session" ), i18n( "New name of the session %1:" ).arg( item->text( 1 ) ),
                                          item->text( 0 ), &ok, this );
    if ( !ok || name.isEmpty() )
        return;
    MatrixRequest *r = MatrixClient::self()->call( "rename_device", MatrixClient::obj( "device_id", item->text( 1 ), "name", name ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotReloadDevices() ) );
}

void AccountDialog::slotSignOut()
{
    JsonValue ids = JsonValue::array();
    TQStringList names;
    for ( TQListViewItemIterator it( m_devices, TQListViewItemIterator::Selected ); it.current(); ++it ) {
        ids.append( it.current()->text( 1 ) );
        names.append( it.current()->text( 0 ) );
    }
    if ( !ids.count() )
        return;
    TQString password;
    TQString prompt = i18n( "Sign out of these sessions?\n%1\n\nEnter your password to confirm." ).arg( names.join( "\n" ) );
    if ( KPasswordDialog::getPassword( password, prompt ) != KPasswordDialog::Accepted )
        return;
    JsonValue args = MatrixClient::obj( "device_ids", ids, "password", password );
    MatrixRequest *r = MatrixClient::self()->call( "delete_devices", args );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotReloadDevices() ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotPasswordChanged( const JsonValue & ) ) );
}

void AccountDialog::slotVerify()
{
    TQListViewItem *item = m_devices->selectedItem();
    if ( !item )
        return;
    emit verifyDevice( item->text( 1 ) );
    reject();
}

void AccountDialog::slotEncryption( const JsonValue &s )
{
    if ( s.has( "ok" ) && !s.get( "ok" ).toBool() )
        return;
    TQString recovery = s.get( "recovery" ).toString();
    bool verified = s.get( "device_verified" ).toBool();
    bool crossSigning = s.get( "cross_signing" ).toBool();
    TQString text;
    text += "<p>" + ( verified ? i18n( "<b>✔ This session is verified.</b> Other people and your other sessions trust it." )
                               : i18n( "<b>⚠ This session is not verified.</b> Verify it with another session, or enter your recovery key, "
                                       "to read your older encrypted messages and to be trusted by others." ) ) + "</p>";
    text += "<p>" + ( crossSigning ? i18n( "Cross-signing is set up." ) : i18n( "Cross-signing is not set up yet." ) ) + "</p>";
    if ( recovery == "enabled" )
        text += "<p>" + i18n( "<b>Recovery is set up:</b> your message keys are backed up on the server, encrypted with your recovery key. "
                              "If you lose all your sessions, the recovery key restores your encrypted history." ) + "</p>";
    else if ( recovery == "incomplete" )
        text += "<p>" + i18n( "<b>Recovery is set up, but this session does not have the keys yet.</b> Enter your recovery key." ) + "</p>";
    else
        text += "<p>" + i18n( "<b>Recovery is not set up.</b> If you lose all your sessions, you lose access to your encrypted messages. "
                              "Set up recovery to back up your message keys." ) + "</p>";
    text += "<p><font size=\"-1\">" + i18n( "Session key: %1" ).arg( s.get( "ed25519" ).toString() ) + "</font></p>";
    m_securityInfo->setText( text );
    m_verifySession->setEnabled( !verified );
    m_enterKey->setEnabled( recovery == "incomplete" || ( recovery == "enabled" && !verified ) );
    m_setupRecovery->setText( recovery == "disabled" || recovery == "unknown" ? i18n( "Set Up &Recovery…" ) : i18n( "&Reset Recovery Key…" ) );
}

void AccountDialog::slotSetupRecovery()
{
    bool reset = m_setupRecovery->text().contains( i18n( "Reset" ) );
    if ( reset && KMessageBox::warningContinueCancel( this, i18n( "Create a new recovery key? The old one stops working." ),
                                                       i18n( "Reset Recovery Key" ) ) != KMessageBox::Continue )
        return;
    m_setupRecovery->setEnabled( false );
    kapp->setOverrideCursor( TQCursor( TQt::WaitCursor ) );
    MatrixRequest *r = MatrixClient::self()->call( reset ? "reset_recovery_key" : "enable_recovery" );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotRecoveryReady( const JsonValue & ) ) );
}

void AccountDialog::slotRecoveryReady( const JsonValue &reply )
{
    kapp->restoreOverrideCursor();
    m_setupRecovery->setEnabled( true );
    if ( !reply.get( "ok" ).toBool() ) {
        KMessageBox::sorry( this, i18n( "Recovery could not be set up:\n%1" ).arg( reply.get( "error" ).toString() ) );
        return;
    }
    RecoveryKeyDialog dlg( this, reply.get( "key" ).toString() );
    dlg.exec();
}

void AccountDialog::slotEnterKey()
{
    bool ok;
    TQString key = KInputDialog::getText( i18n( "Enter Recovery Key" ),
                                         i18n( "Enter the recovery key you saved when you set up recovery\n(48 characters in groups of four):" ),
                                         TQString::null, &ok, this );
    if ( !ok || key.stripWhiteSpace().isEmpty() )
        return;
    kapp->setOverrideCursor( TQCursor( TQt::WaitCursor ) );
    MatrixRequest *r = MatrixClient::self()->call( "recover", MatrixClient::obj( "key", key ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotRecovered( const JsonValue & ) ) );
}

void AccountDialog::slotRecovered( const JsonValue &reply )
{
    kapp->restoreOverrideCursor();
    if ( !reply.get( "ok" ).toBool() )
        KMessageBox::sorry( this, i18n( "The recovery key did not work:\n%1" ).arg( reply.get( "error" ).toString() ) );
    else
        KMessageBox::information( this, i18n( "This session is now verified and is downloading your message keys." ) );
}

void AccountDialog::slotAvatar()
{
    TQString path = KFileDialog::getOpenFileName( TQString::null, "image/png image/jpeg image/gif image/webp", this,
                                                  i18n( "Choose Your Picture" ) );
    if ( path.isEmpty() )
        return;
    MatrixClient::self()->send( "set_avatar", MatrixClient::obj( "path", path ) );
    TQImage img( path );
    if ( !img.isNull() )
        m_avatar->setPixmap( TQPixmap( img.smoothScale( 72, 72, TQImage::ScaleMin ) ) );
}

void AccountDialog::slotChangePassword()
{
    if ( m_newPassword->text().isEmpty() || m_newPassword->text() != m_newPassword2->text() ) {
        m_passwordStatus->setText( "<font color=\"#c0392b\">" + i18n( "The new passwords are empty or do not match." ) + "</font>" );
        return;
    }
    m_passwordStatus->setText( i18n( "Changing the password…" ) );
    JsonValue args = MatrixClient::obj( "old_password", m_oldPassword->text(), "new_password", m_newPassword->text() );
    MatrixRequest *r = MatrixClient::self()->call( "change_password", args );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotPasswordChanged( const JsonValue & ) ) );
}

void AccountDialog::slotPasswordChanged( const JsonValue &reply )
{
    bool ok = reply.get( "ok" ).toBool();
    if ( reply.get( "cmd" ).toString() == "delete_devices" ) {
        if ( !ok )
            KMessageBox::sorry( this, i18n( "Signing out failed:\n%1" ).arg( reply.get( "error" ).toString() ) );
        return;
    }
    if ( ok ) {
        m_oldPassword->clear();
        m_newPassword->clear();
        m_newPassword2->clear();
        m_passwordStatus->setText( i18n( "Your password was changed." ) );
    } else {
        m_passwordStatus->setText( "<font color=\"#c0392b\">" + reply.get( "error" ).toString() + "</font>" );
    }
}

void AccountDialog::applyProfile()
{
    if ( m_displayName->text().stripWhiteSpace() != m_originalName ) {
        m_originalName = m_displayName->text().stripWhiteSpace();
        MatrixClient::self()->send( "set_display_name", MatrixClient::obj( "name", m_originalName ) );
    }
}

void AccountDialog::slotApply()
{
    applyProfile();
}

void AccountDialog::slotOk()
{
    applyProfile();
    accept();
}

// ---- RecoveryKeyDialog --------------------------------------------------------

RecoveryKeyDialog::RecoveryKeyDialog( TQWidget *parent, const TQString &key )
    : KDialogBase( parent, "recoverykey", true, i18n( "Your Recovery Key" ), User1 | User2 | Ok, Ok, true ), m_key( key )
{
    setButtonText( User1, i18n( "&Copy" ) );
    setButtonText( User2, i18n( "&Save As…" ) );
    setButtonOK( KGuiItem( i18n( "I Have Saved It" ), "button_ok" ) );
    TQWidget *page = makeMainWidget();
    TQVBoxLayout *v = new TQVBoxLayout( page, 0, spacingHint() );
    TQLabel *intro = new TQLabel( i18n( "<p>Store this key somewhere safe, for example in a password manager or printed in a drawer. "
                                        "You need it to read your encrypted messages if you lose all your sessions.</p>"
                                        "<p><b>Anyone with this key can read your encrypted messages.</b> TMatrix does not keep a copy.</p>" ),
                                  page );
    intro->setTextFormat( TQt::RichText );
    intro->setAlignment( TQt::WordBreak );
    v->addWidget( intro );
    TQLabel *k = new TQLabel( key, page );
    TQFont f = TDEGlobalSettings::fixedFont();
    f.setPointSize( f.pointSize() + 2 );
    f.setBold( true );
    k->setFont( f );
    k->setFrameStyle( TQFrame::StyledPanel | TQFrame::Sunken );
    k->setMargin( 10 );
    k->setAlignment( TQt::AlignCenter | TQt::WordBreak );
    v->addWidget( k );
    connect( this, TQ_SIGNAL( user1Clicked() ), this, TQ_SLOT( slotCopy() ) );
    connect( this, TQ_SIGNAL( user2Clicked() ), this, TQ_SLOT( slotSave() ) );
    setMinimumWidth( 520 );
}

void RecoveryKeyDialog::slotCopy()
{
    TQApplication::clipboard()->setText( m_key, TQClipboard::Clipboard );
}

void RecoveryKeyDialog::slotSave()
{
    TQString path = KFileDialog::getSaveFileName( "matrix-recovery-key.txt", "*.txt", this, i18n( "Save Recovery Key" ) );
    if ( path.isEmpty() )
        return;
    TQFile f( path );
    if ( f.open( IO_WriteOnly ) ) {
        TQTextStream s( &f );
        s << m_key << "\n";
    }
}

#include "accountdialog.moc"
