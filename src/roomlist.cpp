/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "roomlist.h"
#include "mediacache.h"

#include <tqheader.h>
#include <tqpainter.h>

#include <kiconloader.h>
#include <tdelocale.h>
#include <tdepopupmenu.h>

static const int AVATAR = 24;

class SectionItem : public TDEListViewItem
{
public:
    SectionItem( TDEListView *parent, const TQString &title, int order )
        : TDEListViewItem( parent, title ), m_title( title ), m_order( order )
    {
        setSelectable( false );
        setOpen( true );
    }
    int compare( TQListViewItem *other, int, bool ) const
    {
        return m_order - static_cast<SectionItem *>( other )->m_order;
    }
    void paintCell( TQPainter *p, const TQColorGroup &cg, int column, int width, int align )
    {
        TQFont f = p->font();
        f.setBold( true );
        p->setFont( f );
        TQColorGroup g( cg );
        g.setColor( TQColorGroup::Text, cg.mid().dark( 160 ) );
        TDEListViewItem::paintCell( p, g, column, width, align );
    }
    TQString m_title;
    int m_order;
};

class RoomItem : public TDEListViewItem
{
public:
    RoomItem( TQListViewItem *parent, const JsonValue &room ) : TDEListViewItem( parent ) { update( room ); }

    void update( const JsonValue &room )
    {
        m_room = room;
        setText( 0, room.get( "name" ).toString() );
        updateAvatar();
        repaint();
    }
    void updateAvatar()
    {
        TQString mxc = m_room.get( "avatar" ).toString();
        if ( !mxc.isEmpty() && MediaCache::self()->avatar( mxc, AVATAR ) )
            setPixmap( 0, MediaCache::self()->pixmap( MediaCache::avatarKey( mxc, AVATAR ) ) );
        else
            setPixmap( 0, MediaCache::placeholder( m_room.get( "name" ).toString(), m_room.get( "room_id" ).toString(), AVATAR ) );
    }
    int unread() const { return m_room.get( "unread" ).toInt(); }
    int highlights() const { return m_room.get( "highlight" ).toInt(); }
    bool invite() const { return m_room.get( "membership" ).toString() == "invite"; }

    int compare( TQListViewItem *other, int, bool ) const
    {
        const RoomItem *o = static_cast<const RoomItem *>( other );
        double a = m_room.get( "last_ts" ).toDouble(), b = o->m_room.get( "last_ts" ).toDouble();
        if ( a != b )
            return a > b ? -1 : 1;  // newest first
        return text( 0 ).localeAwareCompare( o->text( 0 ) );
    }

    void paintCell( TQPainter *p, const TQColorGroup &cg, int column, int width, int align )
    {
        bool bold = unread() > 0 || invite() || m_room.get( "marked_unread" ).toBool();
        TQFont f = p->font();
        f.setBold( bold );
        p->setFont( f );
        int count = invite() ? 1 : unread();
        if ( count <= 0 && !m_room.get( "marked_unread" ).toBool() ) {
            TDEListViewItem::paintCell( p, cg, column, width, align );
            return;
        }
        // leave room for the badge
        TQString badge = invite() ? TQString( "!" ) : count > 0 ? TQString::number( count ) : TQString( " " );
        TQFont bf = f;
        bf.setPointSize( TQMAX( 6, f.pointSize() - 1 ) );
        TQFontMetrics fm( bf );
        int bw = TQMAX( fm.width( badge ) + 10, fm.height() + 2 );
        TDEListViewItem::paintCell( p, cg, column, width - bw - 6, align );
        p->fillRect( width - bw - 6, 0, bw + 6, height(), isSelected() ? cg.highlight() : backgroundColor( column ) );
        TQColor color = highlights() > 0 || invite() ? TQColor( "#d0453a" ) : TQColor( "#1d6fd1" );
        if ( count <= 0 )
            color = TQColor( "#909090" );
        p->setPen( TQt::NoPen );
        p->setBrush( color );
        int bh = fm.height() + 2;
        int y = ( height() - bh ) / 2;
        p->drawRoundRect( width - bw - 3, y, bw, bh, 99 * bh / bw, 99 );
        p->setFont( bf );
        p->setPen( TQt::white );
        p->drawText( width - bw - 3, y, bw, bh, TQt::AlignCenter, badge );
    }

    JsonValue m_room;
};

RoomList::RoomList( TQWidget *parent, const char *name ) : TDEListView( parent, name )
{
    addColumn( i18n( "Rooms" ) );
    header()->hide();
    setRootIsDecorated( false );
    setResizeMode( TQListView::LastColumn );
    setFullWidth( true );
    setItemMargin( 3 );
    setSorting( 0 );
    setShowToolTips( true );
    setTreeStepSize( 8 );
    m_invites = new SectionItem( this, i18n( "Invites" ), 0 );
    m_favourites = new SectionItem( this, i18n( "Favourites" ), 1 );
    m_people = new SectionItem( this, i18n( "People" ), 2 );
    m_rooms = new SectionItem( this, i18n( "Rooms" ), 3 );
    m_lowPriority = new SectionItem( this, i18n( "Low Priority" ), 4 );
    updateSectionTitles();
    connect( this, TQ_SIGNAL( selectionChanged( TQListViewItem * ) ), this, TQ_SLOT( slotSelection( TQListViewItem * ) ) );
    connect( this, TQ_SIGNAL( contextMenu( TDEListView *, TQListViewItem *, const TQPoint & ) ),
             this, TQ_SLOT( slotContextMenu( TDEListView *, TQListViewItem *, const TQPoint & ) ) );
    connect( MediaCache::self(), TQ_SIGNAL( ready( const TQString & ) ), this, TQ_SLOT( slotMediaReady( const TQString & ) ) );
}

TQListViewItem *RoomList::section( const JsonValue &room )
{
    if ( room.get( "membership" ).toString() == "invite" )
        return m_invites;
    if ( room.get( "favourite" ).toBool() )
        return m_favourites;
    if ( room.get( "low_priority" ).toBool() )
        return m_lowPriority;
    if ( room.get( "direct" ).toBool() )
        return m_people;
    return m_rooms;
}

void RoomList::updateRoom( const JsonValue &room )
{
    const TQString id = room.get( "room_id" ).toString();
    const TQString membership = room.get( "membership" ).toString();
    if ( membership != "join" && membership != "invite" ) {
        removeRoom( id );
        return;
    }
    if ( room.get( "space" ).toBool() )
        return;  // spaces are not chat rooms
    TQListViewItem *sec = section( room );
    RoomItem *item = m_items.contains( id ) ? m_items[id] : 0;
    if ( item && item->parent() != sec ) {
        bool selected = item->isSelected();
        blockSignals( true );
        item->parent()->takeItem( item );
        sec->insertItem( item );
        if ( selected )
            setSelected( item, true );
        blockSignals( false );
    }
    if ( item ) {
        item->update( room );
    } else {
        item = new RoomItem( sec, room );
        m_items.insert( id, item );
    }
    sort();
    updateSectionTitles();
}

void RoomList::removeRoom( const TQString &roomId )
{
    if ( !m_items.contains( roomId ) )
        return;
    delete m_items[roomId];
    m_items.remove( roomId );
    updateSectionTitles();
}

void RoomList::clearRooms()
{
    for ( TQMap<TQString, RoomItem *>::Iterator it = m_items.begin(); it != m_items.end(); ++it )
        delete it.data();
    m_items.clear();
    updateSectionTitles();
}

void RoomList::updateSectionTitles()
{
    TQListViewItem *secs[] = { m_invites, m_favourites, m_people, m_rooms, m_lowPriority };
    for ( int i = 0; i < 5; ++i ) {
        SectionItem *s = static_cast<SectionItem *>( secs[i] );
        s->setText( 0, s->m_title );
        s->setVisible( s->childCount() > 0 || s == m_rooms );
    }
}

JsonValue RoomList::room( const TQString &roomId ) const
{
    TQMap<TQString, RoomItem *>::ConstIterator it = m_items.find( roomId );
    return it == m_items.end() ? JsonValue() : it.data()->m_room;
}

TQString RoomList::currentRoomId() const
{
    TQListViewItem *i = selectedItem();
    if ( !i || !i->parent() )
        return TQString::null;
    return static_cast<RoomItem *>( i )->m_room.get( "room_id" ).toString();
}

void RoomList::selectRoom( const TQString &roomId )
{
    if ( !m_items.contains( roomId ) )
        return;
    RoomItem *item = m_items[roomId];
    setSelected( item, true );
    ensureItemVisible( item );
}

int RoomList::totalUnread() const
{
    int n = 0;
    for ( TQMap<TQString, RoomItem *>::ConstIterator it = m_items.begin(); it != m_items.end(); ++it )
        n += it.data()->invite() ? 0 : it.data()->unread();
    return n;
}

int RoomList::totalHighlights() const
{
    int n = 0;
    for ( TQMap<TQString, RoomItem *>::ConstIterator it = m_items.begin(); it != m_items.end(); ++it )
        n += it.data()->highlights();
    return n;
}

int RoomList::inviteCount() const
{
    return m_invites->childCount();
}

TQString RoomList::nextUnread() const
{
    TQString first;
    bool afterCurrent = false;
    TQString current = currentRoomId();
    for ( TQListViewItemIterator it( const_cast<RoomList *>( this ) ); it.current(); ++it ) {
        if ( !it.current()->parent() )
            continue;
        RoomItem *r = static_cast<RoomItem *>( it.current() );
        TQString id = r->m_room.get( "room_id" ).toString();
        if ( id == current ) {
            afterCurrent = true;
            continue;
        }
        if ( r->unread() > 0 || r->invite() ) {
            if ( afterCurrent )
                return id;
            if ( first.isEmpty() )
                first = id;
        }
    }
    return first;
}

void RoomList::slotSelection( TQListViewItem *item )
{
    if ( item && item->parent() )
        emit roomSelected( static_cast<RoomItem *>( item )->m_room.get( "room_id" ).toString() );
}

void RoomList::slotMediaReady( const TQString & )
{
    for ( TQMap<TQString, RoomItem *>::Iterator it = m_items.begin(); it != m_items.end(); ++it )
        if ( it.data()->m_room.has( "avatar" ) && !it.data()->m_room.get( "avatar" ).isNull() )
            it.data()->updateAvatar();
}

void RoomList::slotContextMenu( TDEListView *, TQListViewItem *item, const TQPoint &pos )
{
    if ( !item || !item->parent() )
        return;
    const JsonValue room = static_cast<RoomItem *>( item )->m_room;
    const TQString id = room.get( "room_id" ).toString();
    TDEPopupMenu menu( this );
    menu.insertTitle( room.get( "name" ).toString() );
    TQMap<int, TQString> actions;
    int n = 0;
    if ( room.get( "membership" ).toString() == "invite" ) {
        actions[menu.insertItem( SmallIcon( "button_ok" ), i18n( "&Accept Invitation" ), n++ )] = "accept";
        actions[menu.insertItem( SmallIcon( "button_cancel" ), i18n( "&Decline Invitation" ), n++ )] = "decline";
    } else {
        actions[menu.insertItem( SmallIcon( "apply" ), i18n( "Mark as &Read" ), n++ )] = "mark_read";
        int fav = menu.insertItem( SmallIcon( "bookmark" ), i18n( "&Favourite" ), n++ );
        menu.setItemChecked( fav, room.get( "favourite" ).toBool() );
        actions[fav] = "favourite";
        int low = menu.insertItem( i18n( "&Low Priority" ), n++ );
        menu.setItemChecked( low, room.get( "low_priority" ).toBool() );
        actions[low] = "low_priority";
        menu.insertSeparator();
        actions[menu.insertItem( SmallIcon( "configure" ), i18n( "Room &Settings…" ), n++ )] = "settings";
        actions[menu.insertItem( SmallIcon( "contact-new" ), i18n( "&Invite People…" ), n++ )] = "invite";
        actions[menu.insertItem( SmallIcon( "edit-copy" ), i18n( "Copy Room &Link" ), n++ )] = "copy_link";
        menu.insertSeparator();
        actions[menu.insertItem( SmallIcon( "system-log-out" ), i18n( "L&eave Room" ), n++ )] = "leave";
    }
    int r = menu.exec( pos );
    if ( actions.contains( r ) )
        emit roomAction( id, actions[r] );
}

#include "roomlist.moc"
