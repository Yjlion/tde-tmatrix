/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mediacache.h"
#include "matrixclient.h"

#include <tqbitmap.h>
#include <tqimage.h>
#include <tqmime.h>
#include <tqpainter.h>

#include <kmdcodec.h>

static MediaCache *s_self = 0;

MediaCache *MediaCache::self()
{
    if ( !s_self )
        s_self = new MediaCache;
    return s_self;
}

MediaCache::MediaCache() : TQObject( 0, "MediaCache" )
{
}

TQString MediaCache::avatarKey( const TQString &mxc, int size )
{
    return "avatar-" + TQString( KMD5( mxc.utf8() ).hexDigest() ) + "-" + TQString::number( size );
}

TQString MediaCache::imageKey( const JsonValue &source, int w, int h )
{
    return "img-" + TQString( KMD5( source.serialize() ).hexDigest() ) + "-" + TQString::number( w ) + "x" + TQString::number( h );
}

bool MediaCache::avatar( const TQString &mxc, int size )
{
    if ( mxc.isEmpty() )
        return false;
    TQString key = avatarKey( mxc, size );
    if ( m_pixmaps.contains( key ) )
        return true;
    if ( m_failed.contains( key ) || m_loading.contains( key ) )
        return false;
    fetch( key, MatrixClient::obj( "url", mxc, "width", size * 2, "height", size * 2 ), size, size, true );
    return false;
}

bool MediaCache::image( const JsonValue &source, int w, int h )
{
    if ( source.isNull() )
        return false;
    TQString key = imageKey( source, w, h );
    if ( m_pixmaps.contains( key ) )
        return true;
    if ( m_failed.contains( key ) || m_loading.contains( key ) )
        return false;
    // encrypted media cannot be thumbnailed by the server: get the file
    fetch( key, MatrixClient::obj( "source", source ), w, h, false );
    return false;
}

void MediaCache::fetch( const TQString &key, const JsonValue &args, int w, int h, bool round )
{
    m_loading.append( key );
    MatrixRequest *r = MatrixClient::self()->call( "media", args );
    m_requests.insert( r->id(), key );
    m_sizes.insert( r->id(), TQSize( w, h ) );
    m_round.insert( r->id(), round );
    connect( r, TQ_SIGNAL( done( const JsonValue & ) ), this, TQ_SLOT( slotDone( const JsonValue & ) ) );
}

static TQPixmap roundPixmap( const TQImage &img, int size )
{
    TQImage scaled = img.smoothScale( size, size, TQImage::ScaleMin );
    TQPixmap pm( size, size );
    pm.fill( TQt::white );
    TQPainter p( &pm );
    p.drawImage( ( size - scaled.width() ) / 2, ( size - scaled.height() ) / 2, scaled );
    p.end();
    TQBitmap mask( size, size );
    mask.fill( TQt::color0 );
    TQPainter m( &mask );
    m.setBrush( TQt::color1 );
    m.setPen( TQt::color1 );
    m.drawEllipse( 0, 0, size, size );
    m.end();
    pm.setMask( mask );
    return pm;
}

void MediaCache::slotDone( const JsonValue &reply )
{
    int id = reply.get( "id" ).toInt();
    TQString key = m_requests[id];
    TQSize size = m_sizes[id];
    bool round = m_round[id];
    m_requests.remove( id );
    m_sizes.remove( id );
    m_round.remove( id );
    m_loading.remove( key );
    if ( !reply.get( "ok" ).toBool() ) {
        m_failed.append( key );
        return;
    }
    TQString path = reply.get( "path" ).toString();
    TQImage img;
    if ( !img.load( path ) ) {
        m_failed.append( key );
        m_paths.insert( key, path );
        emit ready( key );
        return;
    }
    TQPixmap pm;
    if ( round ) {
        pm = roundPixmap( img, size.width() );
    } else {
        if ( img.width() > size.width() || img.height() > size.height() )
            img = img.smoothScale( size.width(), size.height(), TQImage::ScaleMin );
        pm.convertFromImage( img );
    }
    m_pixmaps.insert( key, pm );
    m_paths.insert( key, path );
    TQMimeSourceFactory::defaultFactory()->setPixmap( key, pm );
    emit ready( key );
}

TQPixmap MediaCache::pixmap( const TQString &key ) const
{
    TQMap<TQString, TQPixmap>::ConstIterator it = m_pixmaps.find( key );
    return it == m_pixmaps.end() ? TQPixmap() : it.data();
}

TQString MediaCache::path( const TQString &key ) const
{
    TQMap<TQString, TQString>::ConstIterator it = m_paths.find( key );
    return it == m_paths.end() ? TQString::null : it.data();
}

TQPixmap MediaCache::placeholder( const TQString &name, const TQString &id, int size )
{
    static const char *colors[] = { "#368bd6", "#ac3ba8", "#0dbd8b", "#e64f7a", "#ff812d", "#2dc2c5", "#5c56f5", "#74d12c" };
    uint h = 0;
    for ( uint i = 0; i < id.length(); ++i )
        h = h * 31 + id[i].unicode();
    TQString text = name;
    while ( !text.isEmpty() && ( text[0] == '@' || text[0] == '#' || text[0] == '!' ) )
        text = text.mid( 1 );
    TQString letter = text.isEmpty() ? TQString( "?" ) : TQString( text[0].upper() );

    TQPixmap pm( size, size );
    pm.fill( TQt::white );
    TQPainter p( &pm );
    p.setPen( TQt::NoPen );
    p.setBrush( TQColor( colors[h % 8] ) );
    p.drawEllipse( 0, 0, size, size );
    TQFont f = p.font();
    f.setPixelSize( size * 11 / 20 );
    f.setBold( true );
    p.setFont( f );
    p.setPen( TQt::white );
    p.drawText( 0, 0, size, size, TQt::AlignCenter, letter );
    p.end();
    TQBitmap mask( size, size );
    mask.fill( TQt::color0 );
    TQPainter m( &mask );
    m.setBrush( TQt::color1 );
    m.setPen( TQt::color1 );
    m.drawEllipse( 0, 0, size, size );
    m.end();
    pm.setMask( mask );
    return pm;
}

void MediaCache::clear()
{
    for ( TQMap<TQString, TQPixmap>::Iterator it = m_pixmaps.begin(); it != m_pixmaps.end(); ++it )
        TQMimeSourceFactory::defaultFactory()->setData( it.key(), 0 );
    m_pixmaps.clear();
    m_paths.clear();
    m_failed.clear();
}

#include "mediacache.moc"
