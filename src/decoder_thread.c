/* the Music Player Daemon (MPD)
 * Copyright (C) 2003-2007 by Warren Dukes (warren.dukes@gmail.com)
 * Copyright (C) 2008 Max Kellermann <max@duempel.org>
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

#include "decoder_thread.h"
#include "decoder_control.h"
#include "decoder_internal.h"
#include "player_control.h"
#include "pipe.h"
#include "song.h"
#include "mapper.h"
#include "path.h"
#include "log.h"
#include "ls.h"

static bool
decoder_try_decode(const struct decoder_plugin *plugin,
		   struct input_stream *input_stream)
{
	bool ret;

	if (plugin->try_decode == NULL)
		return true;

	ret = plugin->try_decode(input_stream);

	/* rewind the stream, so the next reader gets a fresh start */
	input_stream_seek(input_stream, 0, SEEK_SET);

	return ret;
}

static void decoder_run(void)
{
	struct song *song = dc.next_song;
	char buffer[MPD_PATH_MAX];
	const char *uri;
	struct decoder decoder;
	int ret;
	bool close_instream = true;
	struct input_stream input_stream;
	const struct decoder_plugin *plugin;

	if (song_is_file(song))
		uri = map_song_fs(song, buffer);
	else
		uri = song_get_url(song, buffer);
	if (uri == NULL) {
		dc.error = DECODE_ERROR_FILE;
		return;
	}

	dc.current_song = dc.next_song; /* NEED LOCK */
	if (!input_stream_open(&input_stream, uri)) {
		dc.error = DECODE_ERROR_FILE;
		return;
	}

	decoder.seeking = false;
	decoder.stream_tag_sent = false;

	dc.state = DECODE_STATE_START;
	dc.command = DECODE_COMMAND_NONE;
	notify_signal(&pc.notify);

	/* wait for the input stream to become ready; its metadata
	   will be available then */

	while (!input_stream.ready) {
		if (dc.command != DECODE_COMMAND_NONE) {
			input_stream_close(&input_stream);
			return;
		}

		ret = input_stream_buffer(&input_stream);
		if (ret < 0) {
			input_stream_close(&input_stream);
			dc.error = DECODE_ERROR_FILE;
			return;
		}
	}

	if (dc.command == DECODE_COMMAND_STOP) {
		input_stream_close(&input_stream);
		return;
	}

	ret = false;
	if (!song_is_file(song)) {
		unsigned int next = 0;

		/* first we try mime types: */
		while ((plugin = decoder_plugin_from_mime_type(input_stream.mime, next++))) {
			if (plugin->stream_decode == NULL)
				continue;
			if (!decoder_try_decode(plugin, &input_stream))
				continue;
			ret = plugin->stream_decode(&decoder, &input_stream);
			break;
		}

		/* if that fails, try suffix matching the URL: */
		if (plugin == NULL) {
			const char *s = getSuffix(uri);
			next = 0;
			while ((plugin = decoder_plugin_from_suffix(s, next++))) {
				if (plugin->stream_decode == NULL)
					continue;
				if (!decoder_try_decode(plugin, &input_stream))
					continue;
				ret = plugin->stream_decode(&decoder,
							    &input_stream);
				break;
			}
		}
		/* fallback to mp3: */
		/* this is needed for bastard streams that don't have a suffix
		   or set the mimeType */
		if (plugin == NULL) {
			/* we already know our mp3Plugin supports streams, no
			 * need to check for stream{Types,DecodeFunc} */
			if ((plugin = decoder_plugin_from_name("mp3"))) {
				ret = plugin->stream_decode(&decoder,
							    &input_stream);
			}
		}
	} else {
		unsigned int next = 0;
		const char *s = getSuffix(uri);
		while ((plugin = decoder_plugin_from_suffix(s, next++))) {
			if (!decoder_try_decode(plugin, &input_stream))
				continue;

			if (plugin->file_decode != NULL) {
				input_stream_close(&input_stream);
				close_instream = false;
				ret = plugin->file_decode(&decoder, uri);
				break;
			} else if (plugin->stream_decode != NULL) {
				ret = plugin->stream_decode(&decoder,
							    &input_stream);
				break;
			}
		}
	}

	music_pipe_flush();

	if (!ret) {
		dc.error = plugin == NULL
			? DECODE_ERROR_UNKTYPE
			: DECODE_ERROR_FILE;
	}

	if (close_instream)
		input_stream_close(&input_stream);
}

static void * decoder_task(mpd_unused void *arg)
{
	while (1) {
		assert(dc.state == DECODE_STATE_STOP);

		switch (dc.command) {
		case DECODE_COMMAND_START:
		case DECODE_COMMAND_SEEK:
			decoder_run();

			dc.state = DECODE_STATE_STOP;
			dc.command = DECODE_COMMAND_NONE;
			notify_signal(&pc.notify);
			break;

		case DECODE_COMMAND_STOP:
			dc.command = DECODE_COMMAND_NONE;
			notify_signal(&pc.notify);
			break;

		case DECODE_COMMAND_NONE:
			notify_wait(&dc.notify);
			break;
		}
	}

	return NULL;
}

void decoder_thread_start(void)
{
	pthread_attr_t attr;
	pthread_t decoder_thread;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&decoder_thread, &attr, decoder_task, NULL))
		FATAL("Failed to spawn decoder task: %s\n", strerror(errno));
}
