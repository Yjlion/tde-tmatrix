/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "verificationdialog.h"
#include "matrixclient.h"

#include <tqframe.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqobjectlist.h>

#include <kiconloader.h>
#include <tdelocale.h>

VerificationDialog::VerificationDialog( TQWidget *parent, const TQString &userId, const TQString &flowId, bool incoming, bool self )
    : KDialogBase( parent, "verification", false, self ? i18n( "Verify Session" ) : i18n( "Verify %1" ).arg( userId ),
                   User1 | User2 | Cancel, User1, true ),
      m_userId( userId ), m_flowId( flowId ), m_incoming( incoming ), m_comparing( false ), m_finished( false )
{
    setWFlags( getWFlags() | TQt::WDestructiveClose );
    TQWidget *page = makeMainWidget();
    TQVBoxLayout *v = new TQVBoxLayout( page, 0, spacingHint() );
    TQHBoxLayout *h = new TQHBoxLayout( v, spacingHint() );
    m_icon = new TQLabel( page );
    m_text = new TQLabel( page );
    m_text->setTextFormat( TQt::RichText );
    m_text->setAlignment( TQt::WordBreak | TQt::AlignVCenter );
    h->addWidget( m_icon, 0, TQt::AlignTop );
    h->addWidget( m_text, 1 );
    m_emojiBox = new TQFrame( page );
    m_emojiGrid = new TQGridLayout( m_emojiBox, 2, 7, 6, 10 );
    m_emojiBox->hide();
    v->addWidget( m_emojiBox );
    v->addStretch( 1 );
    showButton( User2, false );
    if ( incoming ) {
        setButtonText( User1, i18n( "&Accept" ) );
        showMessage( "encrypted", self ? i18n( "<b>Another of your sessions wants to verify this one.</b><p>Accept, then compare the emoji "
                                                   "shown on both devices.</p>" )
                                           : i18n( "<b>%1 wants to verify you.</b><p>Accept, then compare the emoji shown on both devices. "
                                                   "Do it in person or on a call so you know it is really them.</p>" ).arg( userId ) );
    } else {
        showButton( User1, false );
        showMessage( "encrypted", self ? i18n( "<b>Waiting for your other session…</b><p>Open TMatrix, Element or another Matrix app where "
                                                   "you are logged in and accept the verification request there.</p>" )
                                           : i18n( "<b>Waiting for %1 to accept…</b>" ).arg( userId ) );
    }
    setMinimumWidth( 540 );
}

void VerificationDialog::showMessage( const TQString &icon, const TQString &text )
{
    m_icon->setPixmap( DesktopIcon( icon, 48 ) );
    m_text->setText( text );
}

void VerificationDialog::stateChanged( const JsonValue &s )
{
    if ( !m_flowId.isEmpty() && s.get( "flow_id" ).toString() != m_flowId )
        return;
    const TQString state = s.get( "state" ).toString();
    if ( state == "ready" ) {
        showButton( User1, false );
        showMessage( "encrypted", i18n( "<b>Starting the comparison…</b>" ) );
    } else if ( state == "compare" ) {
        m_comparing = true;
        // drop old emoji
        TQObjectList *l = m_emojiBox->queryList( "TQLabel" );
        if ( l ) {
            for ( TQObjectListIt it( *l ); it.current(); ++it )
                delete it.current();
            delete l;
        }
        const JsonValue &emoji = s.get( "emoji" );
        TQFont big( "Symbola" );
        big.setPixelSize( 40 );
        for ( uint i = 0; i < emoji.count(); ++i ) {
            TQLabel *sym = new TQLabel( emoji.at( i ).get( "symbol" ).toString(), m_emojiBox );
            sym->setFont( big );
            sym->setAlignment( TQt::AlignCenter );
            TQLabel *desc = new TQLabel( emoji.at( i ).get( "description" ).toString(), m_emojiBox );
            desc->setAlignment( TQt::AlignCenter );
            m_emojiGrid->addWidget( sym, 0, i );
            m_emojiGrid->addWidget( desc, 1, i );
            sym->show();
            desc->show();
        }
        if ( emoji.count() == 0 ) {
            const JsonValue &d = s.get( "decimals" );
            TQLabel *num = new TQLabel( TQString( "%1  %2  %3" ).arg( d.at( 0 ).toInt() ).arg( d.at( 1 ).toInt() ).arg( d.at( 2 ).toInt() ), m_emojiBox );
            num->setFont( big );
            m_emojiGrid->addMultiCellWidget( num, 0, 0, 0, 6, TQt::AlignCenter );
            num->show();
        }
        m_emojiBox->show();
        showMessage( "encrypted", i18n( "<b>Compare the emoji.</b><p>Check that the same emoji appear in the same order on the other "
                                            "device.</p>" ) );
        setButtonText( User1, i18n( "&They Match" ) );
        setButtonText( User2, i18n( "They &Don't Match" ) );
        showButton( User1, true );
        showButton( User2, true );
        enableButton( User1, true );
        enableButton( User2, true );
        adjustSize();
    } else if ( state == "waiting" ) {
        enableButton( User1, false );
        enableButton( User2, false );
        showMessage( "encrypted", i18n( "<b>Waiting for the other device to confirm…</b>" ) );
    } else if ( state == "done" ) {
        m_finished = true;
        m_emojiBox->hide();
        showButton( User1, false );
        showButton( User2, false );
        setButtonText( Cancel, i18n( "&Close" ) );
        showMessage( "button_ok", i18n( "<b>Verified!</b><p>%1 is now trusted. Messages from this session will no longer show a warning.</p>" )
                                      .arg( m_userId == MatrixClient::self()->userId() ? i18n( "Your session" ) : m_userId ) );
        adjustSize();
    } else if ( state == "cancelled" ) {
        m_finished = true;
        m_emojiBox->hide();
        showButton( User1, false );
        showButton( User2, false );
        setButtonText( Cancel, i18n( "&Close" ) );
        showMessage( "messagebox_critical", i18n( "<b>The verification was cancelled.</b><p>%1</p>" ).arg( s.get( "reason" ).toString() ) );
        adjustSize();
    }
}

void VerificationDialog::slotUser1()
{
    JsonValue args = MatrixClient::obj( "user_id", m_userId, "flow_id", m_flowId );
    if ( m_comparing ) {
        MatrixClient::self()->send( "verification_confirm", args );
        enableButton( User1, false );
        enableButton( User2, false );
    } else if ( m_incoming ) {
        MatrixClient::self()->send( "verification_accept", args );
        showButton( User1, false );
        showMessage( "encrypted", i18n( "<b>Waiting for the other device…</b>" ) );
    }
}

void VerificationDialog::slotUser2()
{
    MatrixClient::self()->send( "verification_mismatch", MatrixClient::obj( "user_id", m_userId, "flow_id", m_flowId ) );
    m_finished = true;
}

void VerificationDialog::slotCancel()
{
    if ( !m_finished && !m_flowId.isEmpty() )
        MatrixClient::self()->send( "verification_cancel", MatrixClient::obj( "user_id", m_userId, "flow_id", m_flowId ) );
    KDialogBase::slotCancel();
}

#include "verificationdialog.moc"
