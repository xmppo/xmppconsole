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

#ifdef BUILD_UI_GTK

#include "ui.h"
#include "xmpp.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

struct xc_ui_gtk {
	GtkWidget       *uig_window;
	GtkWidget       *uig_view;
	GtkWidget       *uig_input;
	GtkWidget       *uig_status_jid;
	GtkWidget       *uig_status_tls;
	GtkWidget       *uig_status_conn;
	GtkWidget       *uig_status_spinner;
	GtkSourceBuffer *uig_buffer;
	GtkTextMark     *uig_mark;
	bool             uig_done;
};

#define UI_GTK_TITLE_TEXT "XMPP Console"
#define UI_GTK_EVENT_LOOP_TIMEOUT 10

static gboolean ui_gtk_quit_cb(GObject *obj, gpointer data)
{
	struct xc_ui     *ui = data;
	struct xc_ui_gtk *ui_gtk = ui->ui_priv;

	ui_gtk->uig_done = true;
	xc_quit(ui->ui_ctx);

	return FALSE;
}

static gboolean ui_gtk_input_cb(GObject *obj, GdkEventKey *event, gpointer data)
{
	struct xc_ui  *ui = data;
	GtkTextBuffer *buffer;
	GtkTextIter    start;
	GtkTextIter    end;
	gchar         *text;

	if (event->keyval == GDK_KEY_Return) {
		if (event->state & GDK_SHIFT_MASK ||
		    event->state & GDK_CONTROL_MASK) {
			/* Shift+Enter and Crtl+Enter add new line. */
			return FALSE;
		}

		buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(obj));
		gtk_text_buffer_get_bounds (buffer, &start, &end);
		text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
		xc_send(ui->ui_ctx, text);
		g_free(text);
		gtk_text_buffer_set_text(buffer, "", -1);
		return TRUE;
	}
	return FALSE;
}

static gboolean ui_gtk_timed_cb(gpointer data)
{
	struct xc_ui     *ui = data;
	struct xc_ui_gtk *ui_gtk = ui->ui_priv;
	xmpp_ctx_t       *xmpp_ctx = ui->ui_ctx->c_ctx;

	xmpp_run_once(xmpp_ctx,
		      ui_gtk->uig_done ? UI_GTK_EVENT_LOOP_TIMEOUT : 1);

	return TRUE;
}

static void ui_gtk_status_set(struct xc_ui *ui, const gchar *status)
{
	struct xc_ui_gtk *ui_gtk = ui->ui_priv;
	struct xc_ctx    *ctx    = ui->ui_ctx;
	const char       *jid;
	bool              is_tls;

	if (!ui_gtk->uig_done) {
		gtk_label_set_text(GTK_LABEL(ui_gtk->uig_status_conn), status);
		if (ctx != NULL && ctx->c_conn != NULL) {
			jid = xmpp_conn_get_bound_jid(ctx->c_conn) ?:
			      xmpp_conn_get_jid(ctx->c_conn);
			gtk_label_set_text(GTK_LABEL(ui_gtk->uig_status_jid),
					   jid != NULL ? jid : "");
			is_tls = xmpp_conn_is_connected(ctx->c_conn) &&
				 xmpp_conn_is_secured(ctx->c_conn);
			gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(ui_gtk->uig_status_tls),
				is_tls);
			if (strstr(status, "ing") != NULL) {
				gtk_spinner_start(
					GTK_SPINNER(ui_gtk->uig_status_spinner));
			} else {
				gtk_spinner_stop(
					GTK_SPINNER(ui_gtk->uig_status_spinner));
			}
		}
	}
}

static gboolean ui_gtk_dialog_input_cb(GObject *obj,
				       GdkEventKey *event,
				       gpointer data)
{
	GtkWidget *dialog = data;

	if (event->keyval == GDK_KEY_Return) {
		gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
		return TRUE;
	}
	return FALSE;
}

static char *ui_gtk_dialog_password(struct xc_ui *ui)
{
	struct xc_ui_gtk *ui_gtk = ui->ui_priv;
	GtkWidget        *window = ui_gtk->uig_window;
	GtkWidget        *dialog;
	GtkWidget        *dialog_entry;
	GtkWidget        *dialog_box;
	char             *password = NULL;
	gint              response;

	dialog = gtk_message_dialog_new(GTK_WINDOW(window),
					GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
					"Enter password");
	dialog_box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	dialog_entry = gtk_entry_new();
	gtk_window_set_title(GTK_WINDOW(dialog), "Enter password");
	gtk_widget_set_size_request(dialog_entry, 250, 0);
	gtk_entry_set_input_purpose(GTK_ENTRY(dialog_entry),
				    GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_visibility(GTK_ENTRY(dialog_entry), FALSE);
	gtk_entry_set_invisible_char(GTK_ENTRY(dialog_entry), '*');
	gtk_box_pack_end(GTK_BOX(dialog_box), dialog_entry, FALSE, FALSE, 0);
	gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	g_signal_connect(G_OBJECT(dialog_entry), "key-press-event",
			 G_CALLBACK(ui_gtk_dialog_input_cb), dialog);
	gtk_widget_show_all(dialog);
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_ACCEPT) {
		password = strdup(gtk_entry_get_text(GTK_ENTRY(dialog_entry)));
	}
	gtk_widget_destroy(dialog);

	return password;
}

/*
 *  <-----Box for status bar---->
 *
 * +-----------------------------+        ^
 * | Status bar with JID and TLS |        |
 * +-----------------------------+        |
 * |                             |  ^     |
 * |  SourceView for displaying  |  |     |
 * |       the XMPP stream       |  |     |
 * |                             |  |    Box
 * |                             | Paned  |
 * +-----------------------------+  |     |
 * |                             |  |     |
 * | TextView for typing stanzas |  |     |
 * |                             |  v     |
 * +-----------------------------+        v
 *
 *
 * Status bar:
 * +-Frame-----------------------+
 * |         JID      |TLS|STATUS|
 * +-----------------------------+
 */

static int ui_gtk_init(struct xc_ui *ui)
{
	struct xc_ui_gtk         *ui_gtk;
	GtkSourceLanguageManager *lm;
	GtkSourceLanguage        *lang;
	GtkSourceBuffer          *buffer;
	GtkTextIter               buffer_end;
	GtkTextMark              *buffer_mark;
	GtkWidget                *window;
	GtkWidget                *view;
	GtkWidget                *input;
	GtkWidget                *paned;
	GtkWidget                *scrolled;
	GtkWidget                *scrolled_input;
	GtkWidget                *box;
	gboolean                  check;
	/* Status bar. */
	GtkWidget                *status_frame;
	GtkWidget                *status_box;
	GtkWidget                *status_jid;
	GtkWidget                *status_tls;
	GtkWidget                *status_conn;
	GtkWidget                *status_spinner;

	check = gtk_init_check(NULL, NULL);
	if (!check)
		return -ENODEV;

	ui_gtk = malloc(sizeof(*ui_gtk));
	if (ui_gtk == NULL)
		return -ENOMEM;

	lm = gtk_source_language_manager_get_default();
	lang = gtk_source_language_manager_get_language(lm, "xml");

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 1000, 500);

	buffer = gtk_source_buffer_new(NULL);
	if (lang != NULL) {
		gtk_source_buffer_set_language(buffer, lang);
		gtk_source_buffer_set_highlight_syntax(buffer, TRUE);
	}
	gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(buffer), &buffer_end);
	buffer_mark = gtk_text_buffer_create_mark (GTK_TEXT_BUFFER(buffer),
						NULL, &buffer_end, FALSE);
	view = gtk_source_view_new_with_buffer(buffer);
	gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(view), TRUE);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);

	input = gtk_text_view_new();
	gtk_widget_set_sensitive(input, FALSE);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_hexpand(scrolled, FALSE);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_container_add(GTK_CONTAINER(scrolled), view);

	scrolled_input = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_hexpand(scrolled_input, FALSE);
	gtk_widget_set_vexpand(scrolled_input, TRUE);
	gtk_container_add(GTK_CONTAINER(scrolled_input), input);
	gtk_widget_set_size_request(scrolled_input, -1, 100);

	paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_pack1(GTK_PANED(paned), scrolled, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(paned), scrolled_input, FALSE, FALSE);
	gtk_paned_set_wide_handle(GTK_PANED(paned), TRUE);

	status_jid = gtk_label_new(NULL);
	status_tls = gtk_check_button_new_with_label("TLS");
	gtk_widget_set_sensitive(status_tls, FALSE);
	status_conn = gtk_label_new(NULL);
	status_spinner = gtk_spinner_new();
	status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_box_pack_start(GTK_BOX(status_box), status_jid, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(status_box), status_spinner, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(status_box), status_conn, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(status_box), status_tls, FALSE, FALSE, 0);
	status_frame = gtk_frame_new(NULL);
	gtk_container_add(GTK_CONTAINER(status_frame), status_box);
	gtk_container_set_border_width(GTK_CONTAINER(status_frame), 10);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_end(GTK_BOX(box), paned, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), status_frame, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(window), box);
	gtk_window_set_title(GTK_WINDOW(window), UI_GTK_TITLE_TEXT);

	ui_gtk->uig_window = window;
	ui_gtk->uig_view   = view;
	ui_gtk->uig_input  = input;
	ui_gtk->uig_buffer = buffer;
	ui_gtk->uig_mark   = buffer_mark;
	ui_gtk->uig_done   = false;

	ui_gtk->uig_status_jid     = status_jid;
	ui_gtk->uig_status_tls     = status_tls;
	ui_gtk->uig_status_conn    = status_conn;
	ui_gtk->uig_status_spinner = status_spinner;

	ui->ui_priv = ui_gtk;

	g_signal_connect(G_OBJECT(window), "destroy",
			 G_CALLBACK(ui_gtk_quit_cb), ui);
	g_signal_connect(G_OBJECT(input), "key-press-event",
			 G_CALLBACK(ui_gtk_input_cb), ui);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_widget_show_all(window);

	return 0;
}

static void ui_gtk_fini(struct xc_ui *ui)
{
	free(ui->ui_priv);
	ui->ui_priv = NULL;
}

static int ui_gtk_get_passwd(struct xc_ui *ui, char **out)
{
	/* TODO Handle errors. */
	*out = ui_gtk_dialog_password(ui);
	return 0;
}

static void ui_gtk_state_set(struct xc_ui *ui, xc_ui_state_t state)
{
	struct xc_ui_gtk *ui_gtk = ui->ui_priv;

	/* Don't touch destroyed gtk objects. */
	if (ui_gtk->uig_done)
		return;

	switch (state) {
	case XC_UI_UNKNOWN:
		/* Must not happen. */
		break;
	case XC_UI_INITED:
		ui_gtk_status_set(ui, "[offline]");
		break;
	case XC_UI_CONNECTING:
		ui_gtk_status_set(ui, "[connecting...]");
		break;
	case XC_UI_CONNECTED:
		ui_gtk_status_set(ui, "[online]");
		gtk_widget_set_sensitive(ui_gtk->uig_input, TRUE);
		break;
	case XC_UI_DISCONNECTING:
		ui_gtk_status_set(ui, "[disconnecting...]");
		break;
	case XC_UI_DISCONNECTED:
		ui_gtk_status_set(ui, "[offline]");
		gtk_widget_set_sensitive(ui_gtk->uig_input, FALSE);
		break;
	}
}

static void ui_gtk_run(struct xc_ui *ui)
{
	g_timeout_add(UI_GTK_EVENT_LOOP_TIMEOUT, ui_gtk_timed_cb, ui);
	gtk_main();
}

static void ui_gtk_print(struct xc_ui *ui, const char *msg)
{
	struct xc_ui_gtk *ui_gtk = ui->ui_priv;
	GtkSourceBuffer  *buffer = ui_gtk->uig_buffer;
	GtkTextIter       end;
	GString          *text;

	if (!ui_gtk->uig_done) {
		text = g_string_new(msg);
		g_string_append(text, "\n");
		gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(buffer), &end);
		gtk_text_buffer_insert(GTK_TEXT_BUFFER(buffer), &end,
				       text->str, -1);
		g_string_free(text, TRUE);
		/* TODO Scroll only when it is at the bottom. */
		gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(ui_gtk->uig_view),
					     ui_gtk->uig_mark, 0, FALSE, 0, 0);
	}
}

static bool ui_gtk_is_done(struct xc_ui *ui)
{
	struct xc_ui_gtk *ui_gtk = ui->ui_priv;

	return ui_gtk->uig_done;
}

static void ui_gtk_quit(struct xc_ui *ui)
{
	gtk_main_quit();
}

struct xc_ui_ops xc_ui_ops_gtk = {
	.uio_init       = ui_gtk_init,
	.uio_fini       = ui_gtk_fini,
	.uio_get_passwd = ui_gtk_get_passwd,
	.uio_state_set  = ui_gtk_state_set,
	.uio_run        = ui_gtk_run,
	.uio_print      = ui_gtk_print,
	.uio_is_done    = ui_gtk_is_done,
	.uio_quit       = ui_gtk_quit,
};

#endif /* BUILD_UI_GTK */
