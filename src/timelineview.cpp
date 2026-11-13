/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "timelineview.h"
#include "matrixclient.h"
#include "mediacache.h"
#include "settings.h"

#include <tqapplication.h>
#include <tqbitmap.h>
#include <tqclipboard.h>
#include <tqcursor.h>
#include <tqdatetime.h>
#include <tqregexp.h>
#include <tqscrollbar.h>
#include <tqstylesheet.h>
#include <tqtimer.h>

#include <tdeapplication.h>
#include <tdefiledialog.h>
#include <tdeglobal.h>
#include <tdeglobalsettings.h>
#include <kiconloader.h>
#include <tdelocale.h>
#include <tdemessagebox.h>
#include <tdepopupmenu.h>
#include <krun.h>
#include <kurl.h>
#include <tdetempfile.h>
#include <kmimetype.h>
#include <tdeio/global.h>
#include <kstandarddirs.h>

static const int AVATAR = 32;
static const int IMAGE_W = 360;
static const int IMAGE_H = 280;
static const double GROUP_MS = 5 * 60 * 1000.0;

static const char *QUICK_REACTIONS[] = { "👍", "❤️", "😂", "🎉", "😮", "😢", 0 };

TimelineView::TimelineView( TQWidget *parent, const char *name )
    : KTextBrowser( parent, name, true ), m_atStart( false ), m_paginating( false ), m_canRedact( false )
{
    setTextFormat( TQt::RichText );
    setNotifyClick( true );
    setLinkUnderline( false );
    m_renderTimer = new TQTimer( this );
    connect( m_renderTimer, TQ_SIGNAL( timeout() ), this, TQ_SLOT( render() ) );
    connect( verticalScrollBar(), TQ_SIGNAL( valueChanged( int ) ), this, TQ_SLOT( slotScrolled( int ) ) );
    connect( MediaCache::self(), TQ_SIGNAL( ready( const TQString & ) ), this, TQ_SLOT( slotMediaReady( const TQString & ) ) );
}

void TimelineView::setRoom( const TQString &roomId, const TQString &ownUserId )
{
    m_roomId = roomId;
    m_ownUserId = ownUserId;
    m_items.clear();
    m_atStart = false;
    m_paginating = false;
    setText( TQString::null );
}

void TimelineView::clearView()
{
    setRoom( TQString::null, m_ownUserId );
}

void TimelineView::reset( const JsonValue &items )
{
    m_items.clear();
    for ( uint i = 0; i < items.count(); ++i )
        m_items.append( items.at( i ) );
    scheduleRender();
}

void TimelineView::applyDiff( const JsonValue &ops )
{
    for ( uint n = 0; n < ops.count(); ++n ) {
        const JsonValue &op = ops.at( n );
        const TQString kind = op.get( "op" ).toString();
        if ( kind == "append" ) {
            const JsonValue &items = op.get( "items" );
            for ( uint i = 0; i < items.count(); ++i )
                m_items.append( items.at( i ) );
        } else if ( kind == "insert" ) {
            uint idx = op.get( "index" ).toInt();
            if ( idx > m_items.size() )
                idx = m_items.size();
            m_items.insert( m_items.begin() + idx, op.get( "item" ) );
        } else if ( kind == "set" ) {
            uint idx = op.get( "index" ).toInt();
            if ( idx < m_items.size() )
                m_items[idx] = op.get( "item" );
        } else if ( kind == "remove" ) {
            uint idx = op.get( "index" ).toInt();
            if ( idx < m_items.size() )
                m_items.erase( m_items.begin() + idx );
        } else if ( kind == "pop_back" ) {
            if ( !m_items.isEmpty() )
                m_items.pop_back();
        } else if ( kind == "truncate" ) {
            uint len = op.get( "length" ).toInt();
            while ( m_items.size() > len )
                m_items.pop_back();
        } else if ( kind == "clear" ) {
            m_items.clear();
        } else if ( kind == "reset" ) {
            m_items.clear();
            const JsonValue &items = op.get( "items" );
            for ( uint i = 0; i < items.count(); ++i )
                m_items.append( items.at( i ) );
        }
    }
    scheduleRender();
}

void TimelineView::setAtStart( bool atStart )
{
    m_atStart = atStart;
    m_paginating = false;
    scheduleRender();
}

void TimelineView::scheduleRender()
{
    if ( !m_renderTimer->isActive() )
        m_renderTimer->start( 40, true );
}

void TimelineView::slotMediaReady( const TQString &key )
{
    if ( m_waitingKeys.contains( key ) )
        scheduleRender();
}

void TimelineView::slotScrolled( int value )
{
    if ( value <= verticalScrollBar()->minValue() + 4 && !m_atStart && !m_paginating && !m_roomId.isEmpty()
         && !m_items.isEmpty() ) {
        m_paginating = true;
        emit paginateRequested();
    }
}

JsonValue TimelineView::lastOwnEditable() const
{
    for ( int i = (int) m_items.size() - 1; i >= 0; --i ) {
        const JsonValue &it = m_items[i];
        if ( it.get( "kind" ).toString() == "event" && it.get( "own" ).toBool() && it.get( "editable" ).toBool()
             && it.has( "event_id" ) )
            return it;
    }
    return JsonValue();
}

// ---- formatting -------------------------------------------------------------

static bool isEmoji( const TQString &s, uint i )
{
    ushort c = s[i].unicode();
    if ( c >= 0xD83C && c <= 0xD83E )  // high surrogates of the emoji planes
        return true;
    if ( ( c >= 0xDC00 && c <= 0xDFFF ) && i > 0 && s[i - 1].unicode() >= 0xD83C && s[i - 1].unicode() <= 0xD83E )
        return true;
    return ( c >= 0x2600 && c <= 0x27BF ) || ( c >= 0x2B00 && c <= 0x2BFF ) || c == 0x200D || c == 0xFE0F || c == 0x2122;
}

TQString TimelineView::emojiFont( const TQString &html )
{
    // TQt does not fall back to an emoji font by itself: name one for the emoji
    TQString out;
    bool inTag = false, inEmoji = false;
    for ( uint i = 0; i < html.length(); ++i ) {
        TQChar ch = html[i];
        if ( !inTag && ch == '<' )
            inTag = true;
        bool emoji = !inTag && isEmoji( html, i );
        if ( emoji && !inEmoji ) {
            out += "<font face=\"Symbola\">";
            inEmoji = true;
        } else if ( !emoji && inEmoji ) {
            out += "</font>";
            inEmoji = false;
        }
        out += ch;
        if ( inTag && ch == '>' )
            inTag = false;
    }
    if ( inEmoji )
        out += "</font>";
    return out;
}

TQString TimelineView::formatPlain( const TQString &text )
{
    TQString s = TQStyleSheet::escape( text );
    TQRegExp url( "(https?://[^\\s<>\"]+)" );
    int pos = 0;
    while ( ( pos = url.search( s, pos ) ) != -1 ) {
        TQString u = url.cap( 1 );
        TQString link = "<a href=\"" + u + "\">" + u + "</a>";
        s.replace( pos, u.length(), link );
        pos += link.length();
    }
    s.replace( "\n", "<br>" );
    return s;
}

static TQString colorFor( const TQString &id )
{
    static const char *colors[] = { "#368bd6", "#ac3ba8", "#03b381", "#e64f7a", "#ff812d", "#2dc2c5", "#5c56f5", "#74d12c" };
    uint h = 0;
    for ( uint i = 0; i < id.length(); ++i )
        h = h * 31 + id[i].unicode();
    return colors[h % 8];
}

static TQString sizeText( double bytes )
{
    if ( bytes <= 0 )
        return TQString::null;
    return TDEIO::convertSize( (TDEIO::filesize_t) bytes );
}

TQString TimelineView::senderName( const JsonValue &item ) const
{
    TQString n = item.get( "sender_name" ).toString();
    return n.isEmpty() ? item.get( "sender" ).toString() : n;
}

TQString TimelineView::summary( const JsonValue &content, const TQString &sender )
{
    const TQString type = content.get( "type" ).toString();
    if ( type == "image" )
        return i18n( "sent an image" );
    if ( type == "file" )
        return i18n( "sent a file: %1" ).arg( content.get( "filename" ).toString() );
    if ( type == "video" )
        return i18n( "sent a video" );
    if ( type == "audio" )
        return i18n( "sent an audio file" );
    if ( type == "sticker" )
        return i18n( "sent a sticker" );
    if ( type == "utd" )
        return i18n( "Encrypted message" );
    if ( type == "redacted" )
        return i18n( "Message deleted" );
    if ( type == "emote" )
        return "* " + sender + " " + content.get( "body" ).toString();
    if ( type == "poll" )
        return i18n( "Poll: %1" ).arg( content.get( "body" ).toString() );
    if ( type == "membership" || type == "profile" || type == "state" )
        return TQStyleSheet::convertFromPlainText( stateText( content, sender ) ).remove( TQRegExp( "<[^>]*>" ) );
    return content.get( "body" ).toString();
}

TQString TimelineView::stateText( const JsonValue &c, const TQString &sender )
{
    const TQString type = c.get( "type" ).toString();
    if ( type == "membership" ) {
        TQString who = c.get( "user_name" ).toString();
        if ( who.isEmpty() )
            who = c.get( "user_id" ).toString();
        const TQString ch = c.get( "change" ).toString();
        if ( ch == "joined" ) return i18n( "%1 joined the room" ).arg( who );
        if ( ch == "left" ) return i18n( "%1 left the room" ).arg( who );
        if ( ch == "invited" ) return i18n( "%1 invited %2" ).arg( sender ).arg( who );
        if ( ch == "invitation_accepted" ) return i18n( "%1 accepted the invitation" ).arg( who );
        if ( ch == "invitation_rejected" ) return i18n( "%1 declined the invitation" ).arg( who );
        if ( ch == "invitation_revoked" ) return i18n( "%1 withdrew the invitation of %2" ).arg( sender ).arg( who );
        if ( ch == "kicked" ) return i18n( "%1 removed %2" ).arg( sender ).arg( who );
        if ( ch == "banned" || ch == "kicked_and_banned" ) return i18n( "%1 banned %2" ).arg( sender ).arg( who );
        if ( ch == "unbanned" ) return i18n( "%1 unbanned %2" ).arg( sender ).arg( who );
        if ( ch == "knocked" ) return i18n( "%1 asked to join" ).arg( who );
        return i18n( "%1 changed their membership" ).arg( who );
    }
    if ( type == "profile" ) {
        TQString oldName = c.get( "old_name" ).toString(), newName = c.get( "new_name" ).toString();
        if ( c.has( "new_name" ) ) {
            if ( oldName.isEmpty() )
                return i18n( "%1 set their name to %2" ).arg( c.get( "user_id" ).toString() ).arg( newName );
            if ( newName.isEmpty() )
                return i18n( "%1 removed their name" ).arg( oldName );
            return i18n( "%1 changed their name to %2" ).arg( oldName ).arg( newName );
        }
        return i18n( "%1 changed their avatar" ).arg( sender );
    }
    if ( type == "state" ) {
        const TQString et = c.get( "event_type" ).toString();
        const TQString value = c.get( "value" ).toString();
        if ( et == "m.room.name" ) return i18n( "%1 changed the room name to \"%2\"" ).arg( sender ).arg( value );
        if ( et == "m.room.topic" ) return i18n( "%1 changed the topic to \"%2\"" ).arg( sender ).arg( value );
        if ( et == "m.room.avatar" ) return i18n( "%1 changed the room avatar" ).arg( sender );
        if ( et == "m.room.encryption" ) return i18n( "%1 turned on end-to-end encryption" ).arg( sender );
        if ( et == "m.room.create" ) return i18n( "%1 created the room" ).arg( sender );
        if ( et == "m.room.join_rules" ) return i18n( "%1 changed who can join this room" ).arg( sender );
        if ( et == "m.room.history_visibility" ) return i18n( "%1 changed who can read the history" ).arg( sender );
        if ( et == "m.room.power_levels" ) return i18n( "%1 changed the permissions" ).arg( sender );
        if ( et == "m.room.canonical_alias" ) return i18n( "%1 changed the address of this room" ).arg( sender );
        if ( et == "m.room.pinned_events" ) return i18n( "%1 changed the pinned messages" ).arg( sender );
        return i18n( "%1 changed the room settings (%2)" ).arg( sender ).arg( et );
    }
    if ( type == "call" )
        return i18n( "%1 started a call (calls are not supported)" ).arg( sender );
    return TQString::null;
}

TQString TimelineView::bodyHtml( uint index, const JsonValue &c )
{
    const JsonValue &item = m_items[index];
    const TQString type = c.get( "type" ).toString();
    const TQString idx = TQString::number( index );
    TQString html;
    const TQString htmlBody = c.get( "html" ).toString();
    const TQString body = c.get( "body" ).toString();

    if ( type == "text" || type == "notice" || type == "emote" ) {
        TQString text = htmlBody.isEmpty() ? formatPlain( body ) : htmlBody;
        // custom emoji and inline images point to mxc:// URLs the text view cannot load
        text.replace( TQRegExp( "<img[^>]*>" ), "" );
        if ( type == "emote" )
            text = "* <b>" + TQStyleSheet::escape( senderName( item ) ) + "</b> " + text;
        if ( type == "notice" )
            text = "<font color=\"#808080\">" + text + "</font>";
        html = emojiFont( text );
    } else if ( type == "image" || type == "sticker" ) {
        JsonValue src = c.get( "thumbnail" ).isNull() ? c.get( "source" ) : c.get( "thumbnail" );
        if ( Settings::showImages() ) {
            TQString key = MediaCache::imageKey( src, IMAGE_W, IMAGE_H );
            if ( MediaCache::self()->image( src, IMAGE_W, IMAGE_H ) ) {
                html = "<a href=\"tmx-open:" + idx + "\"><img src=\"" + key + "\"></a>";
            } else {
                if ( !m_waitingKeys.contains( key ) )
                    m_waitingKeys.append( key );
                html = "<a href=\"tmx-open:" + idx + "\">" + i18n( "[Loading image %1…]" ).arg( TQStyleSheet::escape( c.get( "filename" ).toString() ) ) + "</a>";
            }
        } else {
            html = "<a href=\"tmx-open:" + idx + "\">" + i18n( "[Image: %1]" ).arg( TQStyleSheet::escape( c.get( "filename" ).toString() ) ) + "</a>";
        }
        TQString caption = c.get( "caption" ).toString();
        if ( !caption.isEmpty() )
            html += "<br>" + formatPlain( caption );
    } else if ( type == "file" || type == "video" || type == "audio" ) {
        TQString icon = type == "video" ? "video-x-generic" : type == "audio" ? "audio-x-generic" : "document";
        TQString size = sizeText( c.get( "size" ).toDouble() );
        html = "<table cellpadding=4 cellspacing=0 bgcolor=\"#eef0f4\"><tr><td><img src=\"icon:" + icon + "\"></td><td>"
               "<a href=\"tmx-open:" + idx + "\"><b>" + TQStyleSheet::escape( c.get( "filename" ).toString() ) + "</b></a>"
               + ( size.isEmpty() ? TQString() : " <font color=\"#808080\">(" + size + ")</font>" )
               + "<br><font size=\"-1\"><a href=\"tmx-open:" + idx + "\">" + i18n( "Open" ) + "</a> &middot; <a href=\"tmx-save:" + idx + "\">"
               + i18n( "Save as…" ) + "</a></font></td></tr></table>";
        TQString caption = c.get( "caption" ).toString();
        if ( !caption.isEmpty() )
            html += formatPlain( caption );
    } else if ( type == "utd" ) {
        html = "<i><font color=\"#a0522d\">" + i18n( "Unable to decrypt this message. TMatrix is waiting for the keys from your "
               "other sessions or the key backup." ) + "</font></i>";
    } else if ( type == "redacted" ) {
        html = "<i><font color=\"#808080\">" + i18n( "Message deleted" ) + "</font></i>";
    } else if ( type == "poll" ) {
        html = TQString::fromUtf8( "<b>📊 " ) + TQStyleSheet::escape( body ) + "</b><ul>";
        const JsonValue &answers = c.get( "answers" );
        for ( uint i = 0; i < answers.count(); ++i )
            html += "<li>" + TQStyleSheet::escape( answers.at( i ).get( "text" ).toString() ) + TQString::fromUtf8( " — " )
                    + i18n( "1 vote", "%n votes", answers.at( i ).get( "votes" ).toInt() ) + "</li>";
        html += "</ul>";
    } else if ( type == "location" ) {
        TQString geo = c.get( "geo_uri" ).toString();
        html = TQString::fromUtf8( "📍 " ) + formatPlain( body.isEmpty() ? geo : body );
    } else if ( type == "verification" ) {
        html = "<i>" + i18n( "Asked to verify" ) + "</i>";
    } else {
        html = "<i><font color=\"#808080\">" + i18n( "Unsupported message (%1)" ).arg( TQStyleSheet::escape( c.get( "event_type" ).toString() ) ) + "</font></i>";
    }

    if ( c.get( "edited" ).toBool() )
        html += " <font color=\"#808080\" size=\"-1\">" + i18n( "(edited)" ) + "</font>";
    return html;
}

TQString TimelineView::itemHtml( uint index )
{
    const JsonValue &item = m_items[index];
    const TQString kind = item.get( "kind" ).toString();
    const TQString idx = TQString::number( index );

    if ( kind == "date" ) {
        TQDateTime dt;
        dt.setTime_t( (uint) ( item.get( "ts" ).toDouble() / 1000 ) );
        return "<p align=\"center\"><font color=\"#606060\"><b>" + TDEGlobal::locale()->formatDate( dt.date(), false ) + "</b></font></p>";
    }
    if ( kind == "read_marker" ) {
        if ( index + 1 >= m_items.size() )
            return TQString::null;
        return "<p align=\"center\"><font color=\"#d0453a\" size=\"-1\">&mdash;&mdash;&mdash;&mdash;&nbsp;&nbsp;" + i18n( "New messages" ) + "&nbsp;&nbsp;&mdash;&mdash;&mdash;&mdash;</font></p>";
    }
    if ( kind == "start" ) {
        return "<p align=\"center\"><font color=\"#808080\" size=\"-1\">" + i18n( "This is the beginning of the room." ) + "</font></p>";
    }
    if ( kind != "event" )
        return TQString::null;

    const JsonValue &c = item.get( "content" );
    const TQString type = c.get( "type" ).toString();
    const TQString sender = item.get( "sender" ).toString();
    const TQString name = senderName( item );

    if ( type == "membership" || type == "profile" || type == "state" || type == "call" ) {
        if ( !Settings::showMembershipEvents() && type != "state" )
            return TQString::null;
        // technical settings most people do not care about
        const TQString et = c.get( "event_type" ).toString();
        if ( et == "m.room.guest_access" || et == "m.room.history_visibility" || et == "m.room.power_levels"
             || et == "m.room.server_acl" || et == "m.room.join_rules" )
            return TQString::null;
        return "<p align=\"center\"><font color=\"#808080\" size=\"-1\">" + TQStyleSheet::escape( stateText( c, name ) ) + "</font></p>";
    }

    // consecutive messages of the same sender are grouped
    bool grouped = false;
    for ( int p = (int) index - 1; p >= 0; --p ) {
        const JsonValue &prev = m_items[p];
        const TQString pk = prev.get( "kind" ).toString();
        if ( pk == "read_marker" )
            continue;
        if ( pk != "event" )
            break;
        const TQString pt = prev.get( "content" ).get( "type" ).toString();
        if ( pt == "membership" || pt == "profile" || pt == "state" || pt == "call" )
            break;
        grouped = prev.get( "sender" ).toString() == sender
                  && item.get( "ts" ).toDouble() - prev.get( "ts" ).toDouble() < GROUP_MS;
        break;
    }

    TQDateTime dt;
    dt.setTime_t( (uint) ( item.get( "ts" ).toDouble() / 1000 ) );
    TQString time = TDEGlobal::locale()->formatTime( dt.time() );

    TQString avatarCell = "<img src=\"tmx-spacer\" width=32 height=1>";
    if ( !grouped ) {
        TQString mxc = item.get( "sender_avatar" ).toString();
        TQString key = MediaCache::avatarKey( mxc, AVATAR );
        if ( !mxc.isEmpty() && MediaCache::self()->avatar( mxc, AVATAR ) ) {
            avatarCell = "<img src=\"" + key + "\" width=32 height=32>";
        } else {
            if ( !mxc.isEmpty() && !m_waitingKeys.contains( key ) )
                m_waitingKeys.append( key );
            TQString pkey = "placeholder-" + sender;
            if ( !TQMimeSourceFactory::defaultFactory()->data( pkey ) )
                TQMimeSourceFactory::defaultFactory()->setPixmap( pkey, MediaCache::placeholder( name, sender, AVATAR ) );
            avatarCell = "<img src=\"" + pkey + "\" width=32 height=32>";
        }
    }

    TQString status;
    if ( item.get( "encrypted" ).toBool() ) {
        TQString shield = item.get( "shield" ).toString();
        if ( shield == "red" )
            status += " <font color=\"#d0453a\" title=\"\">&#9888;</font>";
    }
    const TQString sendState = item.get( "send_state" ).toString();
    if ( sendState == "sending" )
        status += " <font color=\"#808080\">" + i18n( "sending…" ) + "</font>";
    else if ( sendState == "failed" )
        status += " <font color=\"#d0453a\">" + i18n( "not sent" ) + "</font>";

    TQString head;
    if ( !grouped )
        head = "<a href=\"tmx-user:" + sender + "\"><b><font color=\"" + colorFor( sender ) + "\">" + TQStyleSheet::escape( name )
               + "</font></b></a> ";
    head += "<font color=\"#909090\" size=\"-1\"><a href=\"tmx-menu:" + idx + "\"><font color=\"#909090\">" + time + "</font></a>"
            + status + "</font>";

    TQString reply;
    if ( c.has( "reply_to" ) ) {
        const JsonValue &r = c.get( "reply_to" );
        TQString rname = r.get( "sender_name" ).toString();
        if ( rname.isEmpty() )
            rname = r.get( "sender" ).toString();
        TQString rtext = r.has( "content" ) ? summary( r.get( "content" ), rname ) : i18n( "Loading…" );
        if ( rtext.length() > 160 )
            rtext = rtext.left( 160 ) + TQString::fromUtf8( "…" );
        reply = "<table cellpadding=3 cellspacing=0 width=\"100%\"><tr><td width=3 bgcolor=\"" + colorFor( r.get( "sender" ).toString() )
                + "\"></td><td bgcolor=\"#f2f3f5\"><font size=\"-1\"><b>" + TQStyleSheet::escape( rname ) + "</b><br>"
                + TQStyleSheet::escape( rtext ) + "</font></td></tr></table>";
    }

    TQString reactions;
    const JsonValue &rs = c.get( "reactions" );
    for ( uint i = 0; i < rs.count(); ++i ) {
        const JsonValue &r = rs.at( i );
        TQString key = r.get( "key" ).toString();
        bool mine = false;
        const JsonValue &senders = r.get( "senders" );
        for ( uint s = 0; s < senders.count(); ++s )
            if ( senders.at( s ).toString() == m_ownUserId )
                mine = true;
        reactions += TQString( "<a href=\"tmx-react:%1:%2\">" ).arg( idx ).arg( key )
                     + "<font color=\"" + ( mine ? "#1d6fd1" : "#606060" ) + "\">[" + emojiFont( TQStyleSheet::escape( key ) ) + " "
                     + TQString::number( r.get( "count" ).toInt() ) + "]</font></a> ";
    }

    TQString threads;
    if ( c.get( "thread_replies" ).toInt() > 0 )
        threads = "<br><font color=\"#606060\" size=\"-1\">" + i18n( "1 reply in thread", "%n replies in thread", c.get( "thread_replies" ).toInt() ) + "</font>";

    TQString bg = item.get( "highlighted" ).toBool() ? " bgcolor=\"#fff4d6\"" : "";
    return "<table width=\"100%\" cellspacing=0 cellpadding=1" + bg + "><tr><td width=40 valign=\"top\">" + avatarCell + "</td><td valign=\"top\">"
           + ( grouped ? TQString() : head + "<br>" ) + reply + bodyHtml( index, c ) + ( grouped ? " " + head : TQString() )
           + ( reactions.isEmpty() ? TQString() : "<br>" + reactions ) + threads + "</td></tr></table>";
}

void TimelineView::render()
{
    if ( m_roomId.isEmpty() ) {
        setText( TQString::null );
        return;
    }
    // icons used in file messages
    static bool icons = false;
    if ( !icons ) {
        icons = true;
        TQPixmap spacer( 32, 1 );
        spacer.fill( paletteBackgroundColor() );
        spacer.setMask( TQBitmap( 32, 1, true ) );
        TQMimeSourceFactory::defaultFactory()->setPixmap( "tmx-spacer", spacer );
        const char *names[] = { "document", "video-x-generic", "audio-x-generic", 0 };
        for ( int i = 0; names[i]; ++i )
            TQMimeSourceFactory::defaultFactory()->setPixmap( TQString( "icon:" ) + names[i],
                SmallIcon( names[i] == TQString( "document" ) ? "application-x-zerosize" : names[i], 32 ) );
    }

    int oldHeight = contentsHeight();
    int oldY = contentsY();
    bool atBottom = oldY + visibleHeight() >= oldHeight - 16;
    bool keepFromBottom = m_paginating || ( !atBottom && oldY < 40 );

    TQString html = "<qt>";
    if ( !m_atStart )
        html += "<p align=\"center\"><font size=\"-1\"><a href=\"tmx-more\">" + i18n( "Load older messages" ) + "</a></font></p>";
    for ( uint i = 0; i < m_items.size(); ++i )
        html += itemHtml( i );
    html += "</qt>";

    setUpdatesEnabled( false );
    setText( html );
    sync();
    if ( atBottom )
        setContentsPos( 0, contentsHeight() );
    else if ( keepFromBottom )
        setContentsPos( 0, TQMAX( 0, contentsHeight() - ( oldHeight - oldY ) ) );
    else
        setContentsPos( 0, oldY );
    setUpdatesEnabled( true );
    viewport()->update();
}

// ---- interaction ------------------------------------------------------------

void TimelineView::setSource( const TQString &name )
{
    if ( name.startsWith( "tmx-more" ) ) {
        if ( !m_paginating ) {
            m_paginating = true;
            emit paginateRequested();
        }
    } else if ( name.startsWith( "tmx-menu:" ) ) {
        showMenu( name.mid( 9 ).toUInt(), TQCursor::pos() );
    } else if ( name.startsWith( "tmx-user:" ) ) {
        emit userClicked( name.mid( 9 ) );
    } else if ( name.startsWith( "tmx-open:" ) ) {
        openMedia( name.mid( 9 ).toUInt(), false );
    } else if ( name.startsWith( "tmx-save:" ) ) {
        openMedia( name.mid( 9 ).toUInt(), true );
    } else if ( name.startsWith( "tmx-react:" ) ) {
        TQString rest = name.mid( 10 );
        uint idx = rest.section( ':', 0, 0 ).toUInt();
        TQString key = rest.section( ':', 1 );
        if ( idx < m_items.size() && m_items[idx].has( "event_id" ) )
            emit reactRequested( m_items[idx].get( "event_id" ).toString(), key );
    } else if ( name.startsWith( "http://" ) || name.startsWith( "https://" ) || name.startsWith( "mailto:" ) ) {
        kapp->invokeBrowser( name );
    } else if ( name.startsWith( "matrix:" ) || name.startsWith( "https://matrix.to/" ) ) {
        kapp->invokeBrowser( name );
    }
}

TQPopupMenu *TimelineView::createPopupMenu( const TQPoint &pos )
{
    TQString anchor = anchorAt( pos );
    if ( anchor.startsWith( "tmx-menu:" ) || anchor.startsWith( "tmx-open:" ) ) {
        uint idx = anchor.section( ':', 1, 1 ).toUInt();
        TQTimer::singleShot( 0, this, TQ_SLOT( scheduleRender() ) );
        showMenu( idx, viewport()->mapToGlobal( contentsToViewport( pos ) ) );
        return 0;
    }
    return KTextBrowser::createPopupMenu( pos );
}

void TimelineView::showMenu( uint index, const TQPoint &pos )
{
    if ( index >= m_items.size() )
        return;
    const JsonValue item = m_items[index];
    const JsonValue &c = item.get( "content" );
    const TQString eventId = item.get( "event_id" ).toString();
    const TQString type = c.get( "type" ).toString();
    bool sent = !eventId.isEmpty();

    TDEPopupMenu menu( this );
    menu.insertTitle( senderName( item ) );
    TQPopupMenu *react = new TQPopupMenu( &menu );
    for ( int i = 0; QUICK_REACTIONS[i]; ++i )
        react->insertItem( TQString::fromUtf8( QUICK_REACTIONS[i] ), 100 + i );
    enum { Reply = 1, React, Edit, Delete, Copy, Open, Save, CopyLink, Verify, Source };
    if ( item.get( "can_reply" ).toBool() )
        menu.insertItem( SmallIcon( "mail-reply-sender" ), i18n( "&Reply" ), Reply );
    if ( sent )
        menu.insertItem( i18n( "Re&act" ), react, React );
    if ( item.get( "editable" ).toBool() && sent )
        menu.insertItem( SmallIcon( "edit" ), i18n( "&Edit" ), Edit );
    if ( sent && type != "redacted" && ( item.get( "own" ).toBool() || m_canRedact ) )
        menu.insertItem( SmallIcon( "edit-delete" ), i18n( "&Delete" ), Delete );
    menu.insertSeparator();
    if ( !c.get( "body" ).toString().isEmpty() )
        menu.insertItem( SmallIcon( "edit-copy" ), i18n( "&Copy Text" ), Copy );
    if ( c.has( "source" ) ) {
        menu.insertItem( SmallIcon( "document-open" ), i18n( "&Open" ), Open );
        menu.insertItem( SmallIcon( "document-save-as" ), i18n( "&Save As…" ), Save );
    }
    if ( sent )
        menu.insertItem( SmallIcon( "edit-copy" ), i18n( "Copy &Link" ), CopyLink );
    if ( item.get( "encrypted" ).toBool() && !item.get( "own" ).toBool() )
        menu.insertItem( SmallIcon( "encrypted" ), i18n( "&Verify %1…" ).arg( senderName( item ) ), Verify );

    int r = menu.exec( pos );
    if ( r >= 100 && r < 100 + 6 ) {
        emit reactRequested( eventId, TQString::fromUtf8( QUICK_REACTIONS[r - 100] ) );
        return;
    }
    switch ( r ) {
    case Reply:
        emit replyRequested( eventId, senderName( item ), summary( c, senderName( item ) ) );
        break;
    case Edit:
        emit editRequested( eventId, c.get( "body" ).toString() );
        break;
    case Delete:
        emit redactRequested( eventId );
        break;
    case Copy:
        TQApplication::clipboard()->setText( c.get( "body" ).toString(), TQClipboard::Clipboard );
        break;
    case Open:
        openMedia( index, false );
        break;
    case Save:
        openMedia( index, true );
        break;
    case CopyLink:
        TQApplication::clipboard()->setText( "https://matrix.to/#/" + m_roomId + "/" + eventId, TQClipboard::Clipboard );
        break;
    case Verify:
        emit verifyUserRequested( item.get( "sender" ).toString() );
        break;
    default:
        break;
    }
}

void TimelineView::openMedia( uint index, bool save )
{
    if ( index >= m_items.size() )
        return;
    const JsonValue &c = m_items[index].get( "content" );
    const JsonValue source = c.get( "source" );
    if ( source.isNull() )
        return;
    TQString filename = c.get( "filename" ).toString();
    if ( filename.isEmpty() )
        filename = c.get( "body" ).toString();
    filename.replace( '/', '_' );
    TQString mimetype = c.get( "mimetype" ).toString();

    TQString target;
    if ( save ) {
        target = KFileDialog::getSaveFileName( TDEGlobalSettings::downloadPath() + "/" + filename, TQString::null, this,
                                               i18n( "Save Attachment" ) );
        if ( target.isEmpty() )
            return;
    } else {
        // decrypted files are written to a private temporary directory
        TQString dir = locateLocal( "tmp", "tde-tmatrix/" );
        target = dir + filename;
    }
    MatrixRequest *r = MatrixClient::self()->call( "save_media", MatrixClient::obj( "source", source, "path", target ) );
    // a null mimetype only reports errors; otherwise the file is opened once it is there
    MediaOpener *opener = new MediaOpener( this, save ? TQString::null : ( mimetype.isNull() ? TQString( "" ) : mimetype ) );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), opener, TQ_SLOT( open( const JsonValue & ) ) );
}

MediaOpener::MediaOpener( TQWidget *parent, const TQString &mimetype )
    : TQObject( parent ), m_parent( parent ), m_mimetype( mimetype )
{
}

void MediaOpener::open( const JsonValue &reply )
{
    if ( !reply.get( "ok" ).toBool() ) {
        KMessageBox::sorry( m_parent, i18n( "The file could not be downloaded:\n%1" ).arg( reply.get( "error" ).toString() ) );
    } else if ( !m_mimetype.isNull() ) {
        KURL url;
        url.setPath( reply.get( "path" ).toString() );
        TQString mt = m_mimetype.isEmpty() ? KMimeType::findByURL( url )->name() : m_mimetype;
        KRun::runURL( url, mt );
    }
    deleteLater();
}

#include "timelineview.moc"
