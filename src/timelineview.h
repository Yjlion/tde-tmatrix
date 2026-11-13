/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_TIMELINEVIEW_H
#define TMATRIX_TIMELINEVIEW_H

#include "json.h"

#include <ktextbrowser.h>
#include <tqvaluevector.h>

class TQTimer;

/**
 * The messages of one room, kept in sync with the engine's timeline (a list
 * of items changed by diffs) and shown as rich text.
 */
class TimelineView : public KTextBrowser
{
    TQ_OBJECT

public:
    TimelineView( TQWidget *parent, const char *name = 0 );

    void setRoom( const TQString &roomId, const TQString &ownUserId );
    TQString roomId() const { return m_roomId; }
    void setCanRedact( bool can ) { m_canRedact = can; }

    void reset( const JsonValue &items );
    void applyDiff( const JsonValue &ops );
    void setAtStart( bool atStart );
    void clearView();

    /** The newest event sent by the user that can be edited (for the Up key). */
    JsonValue lastOwnEditable() const;

    /** A one-line text of an event's content (replies, notifications). */
    static TQString summary( const JsonValue &content, const TQString &sender );
    /** Rich text of a state/membership event. */
    static TQString stateText( const JsonValue &content, const TQString &sender );
    static TQString formatPlain( const TQString &text );
    /** Wraps the emoji of rich text in a font that has them. */
    static TQString emojiFont( const TQString &html );

public slots:
    void setSource( const TQString &name );
    void scheduleRender();

signals:
    void paginateRequested();
    void replyRequested( const TQString &eventId, const TQString &sender, const TQString &text );
    void editRequested( const TQString &eventId, const TQString &body );
    void redactRequested( const TQString &eventId );
    void reactRequested( const TQString &eventId, const TQString &key );
    void userClicked( const TQString &userId );
    void verifyUserRequested( const TQString &userId );

protected:
    TQPopupMenu *createPopupMenu( const TQPoint &pos );

private slots:
    void render();
    void slotScrolled( int value );
    void slotMediaReady( const TQString &key );

private:
    TQString itemHtml( uint index );
    TQString bodyHtml( uint index, const JsonValue &content );
    TQString senderName( const JsonValue &item ) const;
    void showMenu( uint index, const TQPoint &pos );
    void openMedia( uint index, bool save );

    TQString m_roomId, m_ownUserId;
    TQValueVector<JsonValue> m_items;
    TQTimer *m_renderTimer;
    bool m_atStart, m_paginating, m_canRedact;
    TQStringList m_waitingKeys;
    TQString m_lastMenuAnchor;
};

/** Opens (or reports the failure of) a downloaded attachment. */
class MediaOpener : public TQObject
{
    TQ_OBJECT
public:
    MediaOpener( TQWidget *parent, const TQString &mimetype );
public slots:
    void open( const JsonValue &reply );
private:
    TQWidget *m_parent;
    TQString m_mimetype;
};

#endif
