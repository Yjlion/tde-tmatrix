/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TMATRIX_JSON_H
#define TMATRIX_JSON_H

#include <tqcstring.h>
#include <tqmap.h>
#include <tqstring.h>
#include <tqstringlist.h>
#include <tqvaluelist.h>

/** A small JSON document model with a parser and a writer. */
class JsonValue
{
public:
    enum Type { Null = 0, Bool, Number, String, Array, Object };

    JsonValue() : m_type( Null ), m_bool( false ), m_num( 0 ), m_arr( 0 ), m_obj( 0 ) {}
    JsonValue( bool b ) : m_type( Bool ), m_bool( b ), m_num( 0 ), m_arr( 0 ), m_obj( 0 ) {}
    JsonValue( int n ) : m_type( Number ), m_bool( false ), m_num( n ), m_arr( 0 ), m_obj( 0 ) {}
    JsonValue( double n ) : m_type( Number ), m_bool( false ), m_num( n ), m_arr( 0 ), m_obj( 0 ) {}
    JsonValue( const TQString &s ) : m_type( String ), m_bool( false ), m_num( 0 ), m_str( s ), m_arr( 0 ), m_obj( 0 ) {}
    JsonValue( const char *s ) : m_type( String ), m_bool( false ), m_num( 0 ), m_str( TQString::fromUtf8( s ) ), m_arr( 0 ), m_obj( 0 ) {}
    JsonValue( const JsonValue &other );
    JsonValue &operator=( const JsonValue &other );
    ~JsonValue();

    static JsonValue array()  { JsonValue v; v.m_type = Array;  return v; }
    static JsonValue object() { JsonValue v; v.m_type = Object; return v; }

    Type type() const     { return m_type; }
    bool isNull() const   { return m_type == Null; }
    bool isString() const { return m_type == String; }
    bool isArray() const  { return m_type == Array; }
    bool isObject() const { return m_type == Object; }

    /** Strings as they are, numbers and booleans converted, anything else empty. */
    TQString toString() const;
    double toDouble() const;
    int toInt() const { return (int) toDouble(); }
    bool toBool() const;

    /* arrays */
    uint count() const { return m_arr ? m_arr->count() : 0; }
    const JsonValue &at( uint i ) const;
    JsonValue &append( const JsonValue &v );

    /* objects */
    bool has( const TQString &key ) const { return m_obj && m_obj->contains( key ); }
    const JsonValue &get( const TQString &key ) const;
    JsonValue &set( const TQString &key, const JsonValue &v );
    TQStringList keys() const;

    /** Compact UTF-8 JSON text. */
    TQCString serialize() const;

    /** Parses UTF-8 JSON text. @p ok is set to false on a syntax error. */
    static JsonValue parse( const TQByteArray &data, bool *ok = 0 );
    static JsonValue parse( const TQString &text, bool *ok = 0 );

private:
    void write( TQString &out ) const;

    Type                       m_type;
    bool                       m_bool;
    double                     m_num;
    TQString                   m_str;
    /* allocated on demand: the TQt containers hold a value of their own,
       so a JsonValue cannot contain them directly */
    TQValueList<JsonValue>     *m_arr;
    TQMap<TQString, JsonValue> *m_obj;
};

#endif
