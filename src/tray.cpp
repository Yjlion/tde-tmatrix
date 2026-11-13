/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "tray.h"

#include <tqimage.h>
#include <tqpainter.h>
#include <tqtooltip.h>

#include <kiconeffect.h>
#include <kiconloader.h>
#include <tdelocale.h>
#include <tdepopupmenu.h>

Tray::Tray( TQWidget *mainWindow ) : KSystemTray( mainWindow, "tray" )
{
    m_icon = loadIcon( "tde-tmatrix" );
    setPixmap( m_icon );
    contextMenu()->insertItem( SmallIcon( "go-next" ), i18n( "Show &Next Unread Room" ), this, TQ_SIGNAL( nextUnreadRequested() ), 0, -1, 1 );
    TQToolTip::add( this, i18n( "TMatrix" ) );
}

void Tray::setUnread( int unread, int highlights, int invites, bool offline )
{
    TQPixmap pm = m_icon;
    if ( offline ) {
        TQImage img = pm.convertToImage();
        TDEIconEffect::toGray( img, 0.9 );
        pm.convertFromImage( img );
    }
    int count = unread + invites;
    if ( count > 0 ) {
        TQPainter p( &pm );
        TQString text = count > 99 ? TQString( "99+" ) : TQString::number( count );
        TQFont f = font();
        f.setBold( true );
        f.setPixelSize( TQMAX( 7, pm.height() * 9 / 20 ) );
        p.setFont( f );
        TQFontMetrics fm( f );
        int w = TQMAX( fm.width( text ) + 4, fm.height() );
        int h = fm.height();
        TQRect r( pm.width() - w, pm.height() - h, w, h );
        p.setPen( TQt::NoPen );
        p.setBrush( highlights > 0 || invites > 0 ? TQColor( "#d0453a" ) : TQColor( "#1d6fd1" ) );
        p.drawRoundRect( r, 60, 60 );
        p.setPen( TQt::white );
        p.drawText( r, TQt::AlignCenter, text );
    }
    setPixmap( pm );
    TQString tip = i18n( "TMatrix" );
    if ( offline )
        tip += "\n" + i18n( "Not connected" );
    if ( unread > 0 )
        tip += "\n" + i18n( "1 unread message", "%n unread messages", unread );
    if ( highlights > 0 )
        tip += "\n" + i18n( "1 mention", "%n mentions", highlights );
    if ( invites > 0 )
        tip += "\n" + i18n( "1 invitation", "%n invitations", invites );
    TQToolTip::remove( this );
    TQToolTip::add( this, tip );
}

void Tray::mousePressEvent( TQMouseEvent *e )
{
    if ( e->button() == TQt::MidButton ) {
        emit nextUnreadRequested();
        return;
    }
    KSystemTray::mousePressEvent( e );
}

#include "tray.moc"
