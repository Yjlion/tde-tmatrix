/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_ROOMLIST_H
#define TMATRIX_ROOMLIST_H

#include "json.h"

#include <tdelistview.h>
#include <tqmap.h>

class RoomItem;

/**
 * The rooms of the account, grouped into invites, favourites, people
 * (direct chats), rooms and low priority, newest activity first.
 */
class RoomList : public TDEListView
{
    TQ_OBJECT

public:
    RoomList( TQWidget *parent, const char *name = 0 );

    void updateRoom( const JsonValue &room );
    void removeRoom( const TQString &roomId );
    void clearRooms();

    JsonValue room( const TQString &roomId ) const;
    TQString currentRoomId() const;
    void selectRoom( const TQString &roomId );
    /** Sum of the unread notification counts (for the tray). */
    int totalUnread() const;
    int totalHighlights() const;
    int inviteCount() const;
    /** The next room with unread messages after the current one. */
    TQString nextUnread() const;

signals:
    void roomSelected( const TQString &roomId );
    void roomAction( const TQString &roomId, const TQString &action );

private slots:
    void slotSelection( TQListViewItem *item );
    void slotContextMenu( TDEListView *, TQListViewItem *item, const TQPoint &pos );
    void slotMediaReady( const TQString &key );

private:
    TQListViewItem *section( const JsonValue &room );
    void updateSectionTitles();

    TQListViewItem *m_invites, *m_favourites, *m_people, *m_rooms, *m_lowPriority;
    TQMap<TQString, RoomItem *> m_items;
};

#endif
