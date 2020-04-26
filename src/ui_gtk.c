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

#include "ui.h"
#include "xmpp.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

struct xc_ui_gtk {
	GtkWidget       *uig_window;
	GtkWidget       *uig_view;
	GtkWidget       *uig_input;
	GtkSourceBuffer *uig_buffer;
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

static void ui_gtk_title(struct xc_ui_gtk *ui_gtk, const gchar *title)
{
	if (!ui_gtk->uig_done)
		gtk_window_set_title(GTK_WINDOW(ui_gtk->uig_window), title);
}

static int ui_gtk_init(struct xc_ui *ui)
{
	struct xc_ui_gtk         *ui_gtk;
	GtkSourceLanguageManager *lm;
	GtkSourceLanguage        *lang;
	GtkSourceBuffer          *buffer;
	GtkWidget                *window;
	GtkWidget                *view;
	GtkWidget                *input;
	GtkWidget                *paned;
	GtkWidget                *scrolled;
	gboolean                  check;

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

	paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_pack1(GTK_PANED(paned), scrolled, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(paned), input, TRUE, TRUE);
	gtk_paned_set_wide_handle(GTK_PANED(paned), TRUE);
	gtk_paned_set_position(GTK_PANED(paned), 400);

	gtk_container_add(GTK_CONTAINER(scrolled), view);
	gtk_container_add(GTK_CONTAINER(window), paned);

	ui_gtk->uig_window = window;
	ui_gtk->uig_view   = view;
	ui_gtk->uig_input  = input;
	ui_gtk->uig_buffer = buffer;
	ui_gtk->uig_done   = false;
	ui_gtk_title(ui_gtk, UI_GTK_TITLE_TEXT);

	ui->ui_priv = ui_gtk;

	return 0;
}

static void ui_gtk_fini(struct xc_ui *ui)
{
	free(ui->ui_priv);
	ui->ui_priv = NULL;
}

static void ui_gtk_inited(struct xc_ui *ui)
{
	struct xc_ui_gtk *ui_gtk = ui->ui_priv;

	g_signal_connect(G_OBJECT(ui_gtk->uig_window), "destroy",
			 G_CALLBACK(ui_gtk_quit_cb), ui);
	g_signal_connect(G_OBJECT(ui_gtk->uig_input), "key-press-event",
			 G_CALLBACK(ui_gtk_input_cb), ui);
	gtk_widget_show_all(ui_gtk->uig_window);
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
		ui_gtk_inited(ui);
		break;
	case XC_UI_CONNECTING:
		ui_gtk_title(ui_gtk, UI_GTK_TITLE_TEXT " (Connecting...)");
		break;
	case XC_UI_CONNECTED:
		ui_gtk_title(ui_gtk, UI_GTK_TITLE_TEXT " (Connected)");
		gtk_widget_set_sensitive(ui_gtk->uig_input, TRUE);
		break;
	case XC_UI_DISCONNECTING:
		ui_gtk_title(ui_gtk, UI_GTK_TITLE_TEXT " (Disconnecting...)");
		break;
	case XC_UI_DISCONNECTED:
		ui_gtk_title(ui_gtk, UI_GTK_TITLE_TEXT " (Disconnected)");
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
	.uio_init      = ui_gtk_init,
	.uio_fini      = ui_gtk_fini,
	.uio_state_set = ui_gtk_state_set,
	.uio_run       = ui_gtk_run,
	.uio_print     = ui_gtk_print,
	.uio_is_done   = ui_gtk_is_done,
	.uio_quit      = ui_gtk_quit,
};
