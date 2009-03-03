/* the Music Player Daemon (MPD)
 * Copyright (C) 2003-2007 by Warren Dukes (warren.dukes@gmail.com)
 * This project's homepage is: http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ls.h"
#include "uri.h"
#include "client.h"
#include "config.h"

#include <assert.h>
#include <string.h>


/**
  * file:// is not included in remoteUrlPrefixes, the connection method
  * is detected at runtime and displayed as a urlhandler if the client is
  * connected by IPC socket.
  */
static const char *remoteUrlPrefixes[] = {
#ifdef HAVE_CURL
	"http://",
#endif
#ifdef ENABLE_LASTFM
	"lastfm://",
#endif
#ifdef ENABLE_MMS
	"mms://",
	"mmsh://",
	"mmst://",
	"mmsu://",
#endif
	NULL
};

void print_supported_uri_schemes_to_fp(FILE *fp)
{
	const char **prefixes = remoteUrlPrefixes;

#ifdef HAVE_UN
	fprintf(fp, "file:// ");
#endif
	while (*prefixes) {
		fprintf(fp, "%s ", *prefixes);
		prefixes++;
	}
	puts("\n");
}

void print_supported_uri_schemes(struct client *client)
{
	const char **prefixes = remoteUrlPrefixes;

	while (*prefixes) {
		client_printf(client, "handler: %s\n", *prefixes);
		prefixes++;
	}
}

bool uri_supported_scheme(const char *uri)
{
	const char **urlPrefixes = remoteUrlPrefixes;

	assert(uri_has_scheme(uri));

	while (*urlPrefixes) {
		if (g_str_has_prefix(uri, *urlPrefixes))
			return true;
		urlPrefixes++;
	}

	return false;
}
