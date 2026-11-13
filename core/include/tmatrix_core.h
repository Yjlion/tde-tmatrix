/*
 * tmatrix-core - the Matrix engine of TMatrix (Rust, matrix-rust-sdk)
 *
 * The user interface sends commands as JSON objects ({"id": n, "cmd": "...",
 * ...}) and receives events as JSON objects ({"type": "...", ...}).  Every
 * command is answered by a {"type": "reply", "id": n, "ok": true|false, ...}
 * event.  See core/src/app.rs for the commands and events.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TMATRIX_CORE_H
#define TMATRIX_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Core TmCore;

/* Creates the engine; data_dir holds the encrypted stores of all accounts. */
TmCore *tm_core_new( const char *data_dir );
void tm_core_free( TmCore *core );

/* Queues a command (UTF-8 JSON). */
void tm_core_command( TmCore *core, const char *json );

/* The next event (UTF-8 JSON, free with tm_string_free()) or NULL. */
char *tm_core_next_event( TmCore *core );

/* Readable while events are waiting; read 8 bytes to reset it. */
int tm_core_event_fd( TmCore *core );

void tm_string_free( char *s );
const char *tm_core_version( void );

#ifdef __cplusplus
}
#endif

#endif
