#include "floating-cmdline.h"

/* Single-instance module state. */
static struct {
    GtkWidget *overlay;         /* wraps notebook; returned from _init */
    GtkWidget *popover_box;     /* overlay child; halign=center, valign=center */
    GtkWidget *title_label;
    GtkWidget *entry_slot;      /* reparent target for inputbox */
    GtkWidget *completion_slot; /* completion list packs here while open */

    GtkWidget *inputbox;        /* registered shared inputbox */
    GtkWidget *inputbox_home;   /* container inputbox lives in when idle */
    GtkWidget *saved_sibling;   /* prev-sibling of inputbox at open time */

    GtkWidget *placeholder;     /* holds home-slot height while open */

    gboolean is_open;
} state = { 0 };


static void
vb_floating_regrab_focus(gpointer unused)
{
    (void)unused;
    if (state.is_open && state.inputbox) {
        gtk_widget_grab_focus(state.inputbox);
    }
}

GtkWidget *
vb_floating_init(GtkWidget *notebook)
{
    state.popover_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(state.popover_box, "vimb-floating");
    gtk_widget_set_halign(state.popover_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(state.popover_box, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(state.popover_box, FALSE);

    state.title_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(state.title_label, "vimb-floating-title");
    gtk_widget_set_halign(state.title_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(state.popover_box), state.title_label);

    state.entry_slot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(state.entry_slot, "vimb-floating-entry-slot");
    gtk_box_append(GTK_BOX(state.popover_box), state.entry_slot);

    state.completion_slot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(state.completion_slot, "vimb-floating-completion-slot");
    gtk_box_append(GTK_BOX(state.popover_box), state.completion_slot);

    state.overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(state.overlay), notebook);
    gtk_overlay_add_overlay(GTK_OVERLAY(state.overlay), state.popover_box);

    state.placeholder = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));

    return state.overlay;
}

void
vb_floating_register_inputbox(GtkWidget *inputbox, GtkWidget *home)
{
    state.inputbox      = inputbox;
    state.inputbox_home = home;
}

void
vb_floating_open(const char *title)
{
    if (state.is_open || !state.inputbox || !state.inputbox_home) {
        return;
    }

    gtk_label_set_text(GTK_LABEL(state.title_label), title ? title : "");

    /* Reserve the home-slot height with the placeholder so page content
     * does not reflow when the inputbox leaves. */
    int height = gtk_widget_get_height(state.inputbox);
    if (height <= 0) {
        gtk_widget_measure(state.inputbox, GTK_ORIENTATION_VERTICAL, -1,
                           &height, NULL, NULL, NULL);
    }
    gtk_widget_set_size_request(state.placeholder, -1, height);

    /* Remember where the inputbox lived so we can put it back exactly. */
    state.saved_sibling = gtk_widget_get_prev_sibling(state.inputbox);

    /* Show the popover BEFORE the reparent. If we reparent first while
     * popover_box is still invisible, the inputbox is briefly unmapped;
     * GTK then reassigns focus away from it (to the notebook), and
     * WebKitWebView subsequently eats <Space> as page-scroll. Making
     * the popover visible first keeps the inputbox mapped throughout. */
    gtk_widget_set_visible(state.popover_box, TRUE);

    /* Reparent: home -> entry_slot. Refcount dance keeps the widget alive
     * across the transfer (gtk_box_remove drops the parent's ref; we hold
     * an extra ref until the new parent takes one). */
    g_object_ref(state.inputbox);
    gtk_box_remove(GTK_BOX(state.inputbox_home), state.inputbox);
    gtk_box_append(GTK_BOX(state.entry_slot), state.inputbox);
    g_object_unref(state.inputbox);

    /* Place placeholder where the inputbox used to live. */
    if (state.saved_sibling) {
        gtk_box_insert_child_after(GTK_BOX(state.inputbox_home),
                                    state.placeholder, state.saved_sibling);
    } else {
        gtk_box_prepend(GTK_BOX(state.inputbox_home), state.placeholder);
    }

    state.is_open = TRUE;
}

void
vb_floating_close(void)
{
    if (!state.is_open) {
        return;
    }

    gtk_widget_set_visible(state.popover_box, FALSE);

    gtk_box_remove(GTK_BOX(state.inputbox_home), state.placeholder);

    g_object_ref(state.inputbox);
    gtk_box_remove(GTK_BOX(state.entry_slot), state.inputbox);
    if (state.saved_sibling) {
        gtk_box_insert_child_after(GTK_BOX(state.inputbox_home),
                                    state.inputbox, state.saved_sibling);
    } else {
        gtk_box_prepend(GTK_BOX(state.inputbox_home), state.inputbox);
    }
    g_object_unref(state.inputbox);

    gtk_label_set_text(GTK_LABEL(state.title_label), "");
    state.saved_sibling = NULL;
    state.is_open       = FALSE;
}

GtkWidget *
vb_floating_get_completion_slot(void)
{
    return state.is_open ? state.completion_slot : NULL;
}

gboolean
vb_floating_is_open(void)
{
    return state.is_open;
}


void
vb_floating_grab_focus(void)
{
    if (!state.is_open || !state.inputbox) return;
    gtk_widget_grab_focus(state.inputbox);
    g_idle_add_once((GSourceOnceFunc)vb_floating_regrab_focus, NULL);
}
