/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_MEDIACACHE_H
#define TMATRIX_MEDIACACHE_H

#include "json.h"

#include <tqmap.h>
#include <tqobject.h>
#include <tqpixmap.h>
#include <tqstringlist.h>

/**
 * Downloads (and decrypts) avatars and images through the engine and keeps
 * them as pixmaps.  The images are also registered with the default
 * TQMimeSourceFactory under their key, so rich text can show them with
 * <img src="key">.
 */
class MediaCache : public TQObject
{
    TQ_OBJECT
public:
    static MediaCache *self();

    /** The key of an mxc:// URL (an avatar) scaled to @p size. */
    static TQString avatarKey( const TQString &mxc, int size );
    /** The key of an image message's media (source JSON) fitting in @p w x @p h. */
    static TQString imageKey( const JsonValue &source, int w, int h );

    /** True if the image is there; else it is fetched and ready() follows. */
    bool avatar( const TQString &mxc, int size );
    bool image( const JsonValue &source, int w, int h );

    TQPixmap pixmap( const TQString &key ) const;
    /** Path of the downloaded file of @p key (for opening it). */
    TQString path( const TQString &key ) const;

    /** A round avatar of @p size with the initial of @p name, coloured by @p id. */
    static TQPixmap placeholder( const TQString &name, const TQString &id, int size );

    void clear();

signals:
    void ready( const TQString &key );

private slots:
    void slotDone( const JsonValue &reply );

private:
    MediaCache();
    void fetch( const TQString &key, const JsonValue &args, int w, int h, bool round );

    TQMap<TQString, TQPixmap> m_pixmaps;
    TQMap<TQString, TQString> m_paths;
    TQMap<int, TQString> m_requests;
    TQMap<int, TQSize> m_sizes;
    TQMap<int, bool> m_round;
    TQStringList m_failed;
    TQStringList m_loading;
};

#endif
