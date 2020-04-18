/*
 * XMPP Console - a tool for XMPP hackers
 *
 * Copyright (C) 2020 Dmitry Podgorny <pasis.ua@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * xmppconsole is an interactive tool that sends raw stanzas over an XMPP
 * connection and displays full XMPP streams.
 *
 * Main purpose of the tool is to study XEPs and debug servers behavior.
 *
 * For GTK UI, main priority is given to the GTK main loop, libstrophe
 * loop is executed in a timer callback. This is done in orer to improve
 * responsiveness of the UI.
 */

#include <assert.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <stdio.h>
#include <strophe.h>

struct _xmppconsole_ui {
	GtkWidget *window;
	GtkWidget *entry;
};
typedef struct _xmppconsole_ui xmppconsole_ui_t;

#define TITLE_TEXT "XMPP Console"
#define EVENT_LOOP_TIMEOUT 10

gboolean gtk_done = FALSE;
gboolean xmpp_done = FALSE;
gboolean connected = FALSE;

static void update_title(GtkWidget *window, const gchar *title)
{
	if (!gtk_done)
		gtk_window_set_title(GTK_WINDOW(window), title);
}

static void conn_handler(xmpp_conn_t         *conn,
			 xmpp_conn_event_t    status,
			 int                  error,
			 xmpp_stream_error_t *stream_error,
			 void                *userdata)
{
	xmppconsole_ui_t *ui = userdata;

	if (status == XMPP_CONN_CONNECT) {
		update_title(ui->window, TITLE_TEXT " (Connected)");
		connected = TRUE;
		if (gtk_done)
			xmpp_disconnect(conn);
		else
			gtk_widget_set_sensitive(ui->entry, TRUE);
	} else {
		update_title(ui->window, TITLE_TEXT " (Disconnected)");
		connected = FALSE;
		xmpp_done = TRUE;
		if (gtk_done)
			gtk_main_quit();
		else
			gtk_widget_set_sensitive(ui->entry, FALSE);
	}
}

static gboolean should_display(const char *msg)
{
	return strncmp(msg, "SENT:", 5) == 0 ||
	       strncmp(msg, "RECV:", 5) == 0;
}

static void xmppconsole_log_cb(void            *userdata,
			       xmpp_log_level_t level,
			       const char      *area,
			       const char      *msg)
{
	GtkSourceBuffer *buffer = userdata;
	GtkTextIter      end;
	GString         *text;

	if (!gtk_done && should_display(msg)) {
		text = g_string_new(msg);
		g_string_append(text, "\n");
		gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(buffer), &end);
		gtk_text_buffer_insert(GTK_TEXT_BUFFER(buffer), &end,
				       text->str, -1);
		g_string_free(text, TRUE);
	}
	printf("[%d] %s: %s\n", level, area, msg);
}

static gboolean gtk_quit_cb(GObject *obj, gpointer data)
{
	xmpp_conn_t *conn = data;

	gtk_done = TRUE;
	if (xmpp_done)
		gtk_main_quit();
	if (connected)
		xmpp_disconnect(conn);

	return FALSE;
}

static gboolean send_buffer_cb(GObject *obj, gpointer data)
{
	xmpp_conn_t *conn = data;
	const gchar *text = gtk_entry_get_text(GTK_ENTRY(obj));

	xmpp_send_raw_string(conn, "%s", text);
	gtk_entry_set_text(GTK_ENTRY(obj), "");

	return TRUE;
}

static gboolean timed_cb(gpointer data)
{
	xmpp_ctx_t *ctx = data;

	if (!xmpp_done) {
		xmpp_run_once(ctx, gtk_done ? EVENT_LOOP_TIMEOUT : 1);
	}
	return TRUE;
}

int main(int argc, char **argv)
{
	GtkWidget *window;
	GtkWidget *scrolled;
	GtkWidget *view;
	GtkWidget *entry;
	GtkWidget *box;
	GtkSourceBuffer *buffer;
	GtkSourceLanguageManager *lm;
	GtkSourceLanguage *lang;

	xmppconsole_ui_t ui;
	xmpp_ctx_t  *ctx;
	xmpp_conn_t *conn;
	xmpp_log_t log;
	const char *jid;
	const char *pass;

	if (argc < 3) {
		printf("Usage: xmppconsole <jid> <password>\n");
		return 1;
	}
	jid = argv[1];
	pass = argv[2];

	xmpp_initialize();
	gtk_init(&argc, &argv);

	lm = gtk_source_language_manager_get_default();
	lang = gtk_source_language_manager_get_language(lm, "xml");

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	update_title(window, TITLE_TEXT " (Connecting...)");
	gtk_window_set_default_size(GTK_WINDOW(window), 1000, 500);

	buffer = gtk_source_buffer_new(NULL);
	if (lang != NULL) {
		gtk_source_buffer_set_language(buffer, lang);
		gtk_source_buffer_set_highlight_syntax(buffer, TRUE);
	}
	view = gtk_source_view_new_with_buffer(buffer);
	gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(view), TRUE);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);

	entry = gtk_entry_new();
	gtk_widget_set_sensitive(entry, FALSE);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_hexpand(scrolled, FALSE);
	gtk_widget_set_vexpand(scrolled, TRUE);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(box), entry, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(scrolled), view);
	gtk_container_add(GTK_CONTAINER(window), box);
	gtk_widget_show_all(window);

	ui.window = window;
	ui.entry = entry;
	log = (xmpp_log_t){
		.handler = &xmppconsole_log_cb,
		.userdata = buffer,
	};
	ctx = xmpp_ctx_new(NULL, &log);
	assert(ctx != NULL);
	conn = xmpp_conn_new(ctx);
	assert(conn != NULL);
	xmpp_conn_set_flags(conn, XMPP_CONN_FLAG_MANDATORY_TLS);
	xmpp_conn_set_jid(conn, jid);
	xmpp_conn_set_pass(conn, pass);
	xmpp_connect_client(conn, NULL, 0, conn_handler, &ui);

	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_quit_cb), conn);
	g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(send_buffer_cb), conn);
	g_timeout_add(EVENT_LOOP_TIMEOUT, timed_cb, ctx);

	gtk_main();

	xmpp_conn_release(conn);
	xmpp_ctx_free(ctx);
	xmpp_shutdown();

	return 0;
}
