/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "logindialog.h"
#include "matrixclient.h"

#include <tqcursor.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqtimer.h>

#include <tdeapplication.h>
#include <kcombobox.h>
#include <kiconloader.h>
#include <klineedit.h>
#include <tdelocale.h>
#include <kpushbutton.h>

#include <unistd.h>

LoginDialog::LoginDialog( TQWidget *parent )
    : KDialogBase( parent, "login", true, i18n( "Log In to Matrix" ), Ok | Cancel, Ok, true ),
      m_busy( false ), m_ssoRunning( false )
{
    setButtonOK( KGuiItem( i18n( "&Log In" ), "go-next" ) );
    TQWidget *page = makeMainWidget();
    TQGridLayout *g = new TQGridLayout( page, 8, 2, 0, spacingHint() );

    TQLabel *logo = new TQLabel( page );
    logo->setPixmap( DesktopIcon( "tde-tmatrix", 48 ) );
    TQLabel *intro = new TQLabel( i18n( "<b>Welcome to TMatrix</b><br>Log in to your Matrix account. "
                                        "If you do not have one, create it on the web page of a homeserver, "
                                        "for example <i>app.element.io</i>, and come back." ), page );
    intro->setTextFormat( TQt::RichText );
    intro->setAlignment( TQt::WordBreak | TQt::AlignVCenter );
    g->addWidget( logo, 0, 0 );
    g->addWidget( intro, 0, 1 );

    m_server = new KComboBox( true, page );
    m_server->insertItem( "matrix.org" );
    m_server->setCurrentText( "matrix.org" );
    TQLabel *sl = new TQLabel( m_server, i18n( "&Homeserver:" ), page );
    g->addWidget( sl, 1, 0 );
    g->addWidget( m_server, 1, 1 );
    m_serverInfo = new TQLabel( page );
    m_serverInfo->setAlignment( TQt::WordBreak );
    TQFont small = m_serverInfo->font();
    small.setPointSize( TQMAX( 7, small.pointSize() - 1 ) );
    m_serverInfo->setFont( small );
    g->addWidget( m_serverInfo, 2, 1 );

    m_user = new KLineEdit( page );
    m_user->setTrapReturnKey( false );
    g->addWidget( new TQLabel( m_user, i18n( "&User name:" ), page ), 3, 0 );
    g->addWidget( m_user, 3, 1 );
    TQLabel *hint = new TQLabel( i18n( "Your user name (alice) or full Matrix ID (@alice:example.org)." ), page );
    hint->setFont( small );
    g->addWidget( hint, 4, 1 );

    m_password = new KLineEdit( page );
    m_password->setEchoMode( TQLineEdit::Password );
    g->addWidget( new TQLabel( m_password, i18n( "&Password:" ), page ), 5, 0 );
    g->addWidget( m_password, 5, 1 );

    m_ssoButton = new KPushButton( KGuiItem( i18n( "Log In in the &Web Browser (Single Sign-On)…" ), "network" ), page );
    g->addWidget( m_ssoButton, 6, 1, TQt::AlignLeft );

    m_status = new TQLabel( page );
    m_status->setAlignment( TQt::WordBreak );
    m_status->setMinimumWidth( 380 );
    g->addMultiCellWidget( m_status, 7, 7, 0, 1 );

    m_discoverTimer = new TQTimer( this );
    connect( m_discoverTimer, TQ_SIGNAL( timeout() ), this, TQ_SLOT( slotDiscover() ) );
    connect( m_server, TQ_SIGNAL( textChanged( const TQString & ) ), this, TQ_SLOT( slotServerChanged() ) );
    connect( m_user, TQ_SIGNAL( textChanged( const TQString & ) ), this, TQ_SLOT( slotUserChanged() ) );
    connect( m_ssoButton, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotSso() ) );
    connect( MatrixClient::self(), TQ_SIGNAL( openUrl( const TQString & ) ), this, TQ_SLOT( slotOpenUrl( const TQString & ) ) );
    connect( MatrixClient::self(), TQ_SIGNAL( ssoFinished( const JsonValue & ) ), this, TQ_SLOT( slotSsoFinished( const JsonValue & ) ) );
    m_user->setFocus();
    slotDiscover();
}

void LoginDialog::slotUserChanged()
{
    // a full Matrix ID names its server
    TQString u = m_user->text().stripWhiteSpace();
    if ( u.startsWith( "@" ) && u.contains( ':' ) ) {
        TQString server = u.section( ':', 1 );
        if ( !server.isEmpty() && server != m_server->currentText() )
            m_server->setCurrentText( server );
    }
}

void LoginDialog::slotServerChanged()
{
    m_discoverTimer->start( 700, true );
}

void LoginDialog::slotDiscover()
{
    TQString server = m_server->currentText().stripWhiteSpace();
    if ( server.isEmpty() )
        return;
    m_serverInfo->setText( i18n( "Checking %1…" ).arg( server ) );
    MatrixRequest *r = MatrixClient::self()->call( "discover", MatrixClient::obj( "homeserver", server ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotDiscovered( const JsonValue & ) ) );
}

void LoginDialog::slotDiscovered( const JsonValue &reply )
{
    if ( !reply.get( "ok" ).toBool() ) {
        m_serverInfo->setText( "<font color=\"#c0392b\">" + i18n( "This server cannot be reached or is not a Matrix homeserver." ) + "</font>" );
        return;
    }
    m_resolved = reply.get( "homeserver" ).toString();
    bool password = reply.get( "password" ).toBool(), sso = reply.get( "sso" ).toBool();
    TQString methods;
    if ( password && sso )
        methods = i18n( "password or single sign-on" );
    else if ( password )
        methods = i18n( "password" );
    else if ( sso )
        methods = i18n( "single sign-on only" );
    else
        methods = i18n( "no supported login method" );
    m_serverInfo->setText( i18n( "%1 — log in with %2" ).arg( m_resolved ).arg( methods ) );
    m_user->setEnabled( password );
    m_password->setEnabled( password );
    enableButtonOK( password && !m_busy );
    m_ssoButton->setEnabled( sso && !m_busy );
}

void LoginDialog::setBusy( bool busy, const TQString &message )
{
    m_busy = busy;
    enableButtonOK( !busy );
    m_ssoButton->setEnabled( !busy );
    m_server->setEnabled( !busy );
    m_user->setEnabled( !busy );
    m_password->setEnabled( !busy );
    m_status->setText( message );
    if ( busy )
        kapp->setOverrideCursor( TQCursor( TQt::WaitCursor ) );
    else
        kapp->restoreOverrideCursor();
}

void LoginDialog::showError( const TQString &message )
{
    setBusy( false );
    m_status->setText( "<font color=\"#c0392b\">" + message + "</font>" );
}

static TQString deviceName()
{
    char host[256];
    if ( gethostname( host, sizeof( host ) ) == 0 ) {
        host[sizeof( host ) - 1] = 0;
        return i18n( "TMatrix on %1" ).arg( TQString::fromLocal8Bit( host ) );
    }
    return "TMatrix";
}

void LoginDialog::slotOk()
{
    if ( m_busy )
        return;
    if ( m_user->text().stripWhiteSpace().isEmpty() || m_password->text().isEmpty() ) {
        showError( i18n( "Enter your user name and password." ) );
        return;
    }
    setBusy( true, i18n( "Logging in…" ) );
    JsonValue args = MatrixClient::obj( "homeserver", m_server->currentText().stripWhiteSpace(), "user", m_user->text().stripWhiteSpace(),
                                        "password", m_password->text(), "device_name", deviceName() );
    MatrixRequest *r = MatrixClient::self()->call( "login", args );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotLoggedIn( const JsonValue & ) ) );
}

void LoginDialog::slotLoggedIn( const JsonValue &reply )
{
    if ( !reply.get( "ok" ).toBool() ) {
        TQString e = reply.get( "error" ).toString();
        if ( e.contains( "M_FORBIDDEN" ) || e.contains( "Invalid username or password" ) )
            e = i18n( "The user name or password is wrong." );
        showError( i18n( "Login failed: %1" ).arg( e ) );
        return;
    }
    setBusy( false );
    m_userId = reply.get( "user_id" ).toString();
    KDialogBase::slotOk();
}

void LoginDialog::slotSso()
{
    setBusy( true, i18n( "Your web browser opens the login page of the homeserver. Log in there; TMatrix continues automatically." ) );
    m_ssoRunning = true;
    MatrixRequest *r = MatrixClient::self()->call( "login_sso", MatrixClient::obj( "homeserver", m_server->currentText().stripWhiteSpace(),
                                                                                  "device_name", deviceName() ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotSsoStarted( const JsonValue & ) ) );
}

void LoginDialog::slotSsoStarted( const JsonValue &reply )
{
    if ( !reply.get( "ok" ).toBool() ) {
        m_ssoRunning = false;
        showError( reply.get( "error" ).toString() );
    }
}

void LoginDialog::slotOpenUrl( const TQString &url )
{
    if ( m_ssoRunning )
        kapp->invokeBrowser( url );
}

void LoginDialog::slotSsoFinished( const JsonValue &result )
{
    if ( !m_ssoRunning )
        return;
    m_ssoRunning = false;
    if ( result.has( "error" ) ) {
        showError( i18n( "Login failed: %1" ).arg( result.get( "error" ).toString() ) );
        return;
    }
    setBusy( false );
    m_userId = result.get( "user_id" ).toString();
    KDialogBase::slotOk();
}

void LoginDialog::slotCancel()
{
    if ( m_ssoRunning ) {
        MatrixClient::self()->send( "cancel_sso" );
        m_ssoRunning = false;
    }
    if ( m_busy )
        setBusy( false );
    KDialogBase::slotCancel();
}

#include "logindialog.moc"
