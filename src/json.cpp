/*
 * tde-tmatrix - a Matrix chat client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-tmatrix contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "json.h"

#include <math.h>
#include <stdio.h>

static const JsonValue &nullValue()
{
    static JsonValue *v = 0;
    if ( !v )
        v = new JsonValue;
    return *v;
}

JsonValue::JsonValue( const JsonValue &o )
    : m_type( o.m_type ), m_bool( o.m_bool ), m_num( o.m_num ), m_str( o.m_str ),
      m_arr( o.m_arr ? new TQValueList<JsonValue>( *o.m_arr ) : 0 ),
      m_obj( o.m_obj ? new TQMap<TQString, JsonValue>( *o.m_obj ) : 0 )
{
}

JsonValue &JsonValue::operator=( const JsonValue &o )
{
    if ( this == &o )
        return *this;
    TQValueList<JsonValue> *arr = o.m_arr ? new TQValueList<JsonValue>( *o.m_arr ) : 0;
    TQMap<TQString, JsonValue> *obj = o.m_obj ? new TQMap<TQString, JsonValue>( *o.m_obj ) : 0;
    delete m_arr;
    delete m_obj;
    m_arr = arr;
    m_obj = obj;
    m_type = o.m_type;
    m_bool = o.m_bool;
    m_num = o.m_num;
    m_str = o.m_str;
    return *this;
}

JsonValue::~JsonValue()
{
    delete m_arr;
    delete m_obj;
}

TQString JsonValue::toString() const
{
    switch ( m_type ) {
    case String: return m_str;
    case Bool:   return m_bool ? "true" : "false";
    case Number:
        if ( m_num == floor( m_num ) && fabs( m_num ) < 1e15 )
            return TQString::number( m_num, 'f', 0 );
        return TQString::number( m_num, 'g', 12 );
    default:     return TQString();
    }
}

double JsonValue::toDouble() const
{
    switch ( m_type ) {
    case Number: return m_num;
    case Bool:   return m_bool ? 1 : 0;
    case String: return m_str.toDouble();
    default:     return 0;
    }
}

bool JsonValue::toBool() const
{
    switch ( m_type ) {
    case Bool:   return m_bool;
    case Number: return m_num != 0;
    case String: return m_str == "true" || m_str == "1";
    default:     return false;
    }
}

const JsonValue &JsonValue::at( uint i ) const
{
    if ( !m_arr || i >= m_arr->count() )
        return nullValue();
    return *m_arr->at( i );
}

JsonValue &JsonValue::append( const JsonValue &v )
{
    m_type = Array;
    if ( !m_arr )
        m_arr = new TQValueList<JsonValue>;
    m_arr->append( v );
    return *this;
}

const JsonValue &JsonValue::get( const TQString &key ) const
{
    if ( !m_obj )
        return nullValue();
    TQMap<TQString, JsonValue>::ConstIterator it = m_obj->find( key );
    if ( it == m_obj->end() )
        return nullValue();
    return it.data();
}

JsonValue &JsonValue::set( const TQString &key, const JsonValue &v )
{
    m_type = Object;
    if ( !m_obj )
        m_obj = new TQMap<TQString, JsonValue>;
    m_obj->replace( key, v );
    return *this;
}

TQStringList JsonValue::keys() const
{
    return m_obj ? m_obj->keys() : TQStringList();
}

/* ---------------------------------------------------------------- writer */

static void writeString( const TQString &s, TQString &out )
{
    out += '"';
    const uint len = s.length();
    const TQChar *u = s.unicode();
    uint start = 0;
    for ( uint i = 0; i < len; ++i ) {
        const ushort c = u[i].unicode();
        if ( c >= 0x20 && c != '"' && c != '\\' )
            continue;
        if ( i > start )
            out += TQString( u + start, i - start );
        start = i + 1;
        switch ( c ) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default: {
            char buf[8];
            snprintf( buf, sizeof( buf ), "\\u%04x", c );
            out += buf;
        }
        }
    }
    if ( start == 0 )
        out += s;
    else if ( len > start )
        out += TQString( u + start, len - start );
    out += '"';
}

void JsonValue::write( TQString &out ) const
{
    switch ( m_type ) {
    case Null:   out += "null"; break;
    case Bool:   out += m_bool ? "true" : "false"; break;
    case Number: out += toString(); break;
    case String: writeString( m_str, out ); break;
    case Array: {
        out += '[';
        bool first = true;
        for ( uint i = 0; i < count(); ++i ) {
            if ( !first )
                out += ',';
            first = false;
            at( i ).write( out );
        }
        out += ']';
        break;
    }
    case Object: {
        out += '{';
        bool first = true;
        if ( !m_obj ) {
            out += '}';
            break;
        }
        for ( TQMap<TQString, JsonValue>::ConstIterator it = m_obj->begin(); it != m_obj->end(); ++it ) {
            if ( !first )
                out += ',';
            first = false;
            writeString( it.key(), out );
            out += ':';
            it.data().write( out );
        }
        out += '}';
        break;
    }
    }
}

TQCString JsonValue::serialize() const
{
    TQString out;
    write( out );
    return out.utf8();
}

/* ---------------------------------------------------------------- parser */

namespace
{

class Parser
{
public:
    Parser( const TQString &text ) : m_u( text.unicode() ), m_len( text.length() ), m_pos( 0 ), m_ok( true ), m_depth( 0 ) {}

    JsonValue document()
    {
        JsonValue v = value();
        skipSpace();
        if ( m_pos != m_len )
            m_ok = false;
        return m_ok ? v : JsonValue();
    }

    bool ok() const { return m_ok; }

private:
    void skipSpace()
    {
        while ( m_pos < m_len ) {
            const ushort c = m_u[m_pos].unicode();
            if ( c != ' ' && c != '\n' && c != '\r' && c != '\t' )
                break;
            ++m_pos;
        }
    }

    bool literal( const char *word )
    {
        uint i = 0;
        for ( ; word[i]; ++i )
            if ( m_pos + i >= m_len || m_u[m_pos + i].unicode() != (ushort) word[i] )
                return false;
        m_pos += i;
        return true;
    }

    JsonValue fail()
    {
        m_ok = false;
        return JsonValue();
    }

    TQString string()
    {
        TQString out;
        ++m_pos;    // opening quote
        uint start = m_pos;
        while ( m_pos < m_len ) {
            const ushort c = m_u[m_pos].unicode();
            if ( c == '"' ) {
                out += TQString( m_u + start, m_pos - start );
                ++m_pos;
                return out;
            }
            if ( c != '\\' ) {
                ++m_pos;
                continue;
            }
            out += TQString( m_u + start, m_pos - start );
            if ( ++m_pos >= m_len )
                break;
            const ushort e = m_u[m_pos++].unicode();
            switch ( e ) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if ( m_pos + 4 > m_len ) {
                    m_ok = false;
                    return out;
                }
                bool hexOk = false;
                ushort code = TQString( m_u + m_pos, 4 ).toUShort( &hexOk, 16 );
                if ( !hexOk ) {
                    m_ok = false;
                    return out;
                }
                m_pos += 4;
                out += TQChar( code );  // surrogate pairs stay UTF-16 pairs
                break;
            }
            default: out += TQChar( e );
            }
            start = m_pos;
        }
        m_ok = false;
        return out;
    }

    JsonValue number()
    {
        uint start = m_pos;
        while ( m_pos < m_len ) {
            const ushort c = m_u[m_pos].unicode();
            if ( ( c >= '0' && c <= '9' ) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' )
                ++m_pos;
            else
                break;
        }
        bool numOk = false;
        double d = TQString( m_u + start, m_pos - start ).toDouble( &numOk );
        if ( !numOk )
            return fail();
        return JsonValue( d );
    }

    JsonValue value()
    {
        skipSpace();
        if ( m_pos >= m_len || !m_ok )
            return fail();
        const ushort c = m_u[m_pos].unicode();
        if ( c == '"' )
            return JsonValue( string() );
        if ( c == '{' || c == '[' ) {
            if ( ++m_depth > 200 )
                return fail();
            JsonValue v = ( c == '{' ) ? object() : array();
            --m_depth;
            return v;
        }
        if ( c == 't' )
            return literal( "true" ) ? JsonValue( true ) : fail();
        if ( c == 'f' )
            return literal( "false" ) ? JsonValue( false ) : fail();
        if ( c == 'n' )
            return literal( "null" ) ? JsonValue() : fail();
        return number();
    }

    JsonValue array()
    {
        JsonValue v = JsonValue::array();
        ++m_pos;
        skipSpace();
        if ( m_pos < m_len && m_u[m_pos] == ']' ) {
            ++m_pos;
            return v;
        }
        while ( m_ok ) {
            v.append( value() );
            skipSpace();
            if ( m_pos >= m_len )
                return fail();
            const ushort c = m_u[m_pos++].unicode();
            if ( c == ']' )
                return v;
            if ( c != ',' )
                return fail();
        }
        return v;
    }

    JsonValue object()
    {
        JsonValue v = JsonValue::object();
        ++m_pos;
        skipSpace();
        if ( m_pos < m_len && m_u[m_pos] == '}' ) {
            ++m_pos;
            return v;
        }
        while ( m_ok ) {
            skipSpace();
            if ( m_pos >= m_len || m_u[m_pos] != '"' )
                return fail();
            TQString key = string();
            skipSpace();
            if ( m_pos >= m_len || m_u[m_pos++] != ':' )
                return fail();
            v.set( key, value() );
            skipSpace();
            if ( m_pos >= m_len )
                return fail();
            const ushort c = m_u[m_pos++].unicode();
            if ( c == '}' )
                return v;
            if ( c != ',' )
                return fail();
        }
        return v;
    }

    const TQChar *m_u;
    uint          m_len;
    uint          m_pos;
    bool          m_ok;
    int           m_depth;
};

}

JsonValue JsonValue::parse( const TQString &text, bool *ok )
{
    Parser p( text );
    JsonValue v = p.document();
    if ( ok )
        *ok = p.ok();
    return v;
}

JsonValue JsonValue::parse( const TQByteArray &data, bool *ok )
{
    return parse( TQString::fromUtf8( data.data(), data.size() ), ok );
}
