/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "composer.h"
#include "settings.h"

#include <tqdragobject.h>
#include <tqtimer.h>

#include <kurl.h>
#include <kurldrag.h>

Composer::Composer( TQWidget *parent, const char *name ) : KTextEdit( parent, name ), m_typing( false )
{
    setTextFormat( TQt::PlainText );
    setWordWrap( TQTextEdit::WidgetWidth );
    setCheckSpellingEnabled( false );
    setAcceptDrops( true );
    m_typingTimer = new TQTimer( this );
    connect( m_typingTimer, TQ_SIGNAL( timeout() ), this, TQ_SLOT( slotStopTyping() ) );
    connect( this, TQ_SIGNAL( textChanged() ), this, TQ_SLOT( slotChanged() ) );
}

void Composer::keyPressEvent( TQKeyEvent *e )
{
    bool enter = e->key() == TQt::Key_Return || e->key() == TQt::Key_Enter;
    if ( enter ) {
        bool shift = e->state() & TQt::ShiftButton;
        bool ctrl = e->state() & TQt::ControlButton;
        if ( ( Settings::enterSends() && !shift ) || ( !Settings::enterSends() && ctrl ) ) {
            slotStopTyping();
            emit sendRequested();
            return;
        }
    }
    if ( e->key() == TQt::Key_Up && text().isEmpty() ) {
        emit editLastRequested();
        return;
    }
    if ( e->key() == TQt::Key_Escape ) {
        emit cancelRequested();
        return;
    }
    KTextEdit::keyPressEvent( e );
}

void Composer::slotChanged()
{
    if ( text().isEmpty() ) {
        slotStopTyping();
        return;
    }
    if ( !m_typing ) {
        m_typing = true;
        emit typing( true );
    }
    m_typingTimer->start( 5000, true );
}

void Composer::slotStopTyping()
{
    m_typingTimer->stop();
    if ( m_typing ) {
        m_typing = false;
        emit typing( false );
    }
}

void Composer::contentsDragEnterEvent( TQDragEnterEvent *e )
{
    if ( KURLDrag::canDecode( e ) )
        e->accept( true );
    else
        KTextEdit::contentsDragEnterEvent( e );
}

void Composer::contentsDropEvent( TQDropEvent *e )
{
    KURL::List urls;
    if ( KURLDrag::decode( e, urls ) && !urls.isEmpty() ) {
        TQStringList paths;
        for ( KURL::List::ConstIterator it = urls.begin(); it != urls.end(); ++it )
            if ( ( *it ).isLocalFile() )
                paths.append( ( *it ).path() );
        if ( !paths.isEmpty() ) {
            emit filesDropped( paths );
            return;
        }
    }
    KTextEdit::contentsDropEvent( e );
}

#include "composer.moc"
