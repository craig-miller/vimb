/**
 * Floating cmdline popover — presentation-only layer for vimb prompts.
 *
 * Wraps vb.notebook in a GtkOverlay and hosts a center-screen popover
 * box (title label + inputbox slot + completion slot). Callers open the
 * popover on prompt-mode entry and close it on leave; the shared inputbox
 * is reparented up into the popover for the duration and returned to its
 * home container on close. A height-reserving placeholder occupies the
 * home slot while the popover is active so page content does not reflow.
 *
 * No trigger machinery lives here — this module only reshapes where the
 * existing inputbox is presented.
 */
#ifndef FLOATING_CMDLINE_H
#define FLOATING_CMDLINE_H

#include <gtk/gtk.h>

/**
 * Wrap @notebook in a GtkOverlay and construct the (initially hidden)
 * popover slot. Returns the GtkOverlay — pack it into the parent widget
 * in place of @notebook.
 */
GtkWidget *vb_floating_init(GtkWidget *notebook);

/**
 * Register the shared inputbox and the container it lives in. Must be
 * called after @inputbox has been packed into @home, before any open.
 */
void vb_floating_register_inputbox(GtkWidget *inputbox, GtkWidget *home);

/**
 * Show the popover with @title. Reparents the inputbox into the popover
 * and installs the placeholder in the home container. No-op if already
 * open, or if register has not been called.
 */
void vb_floating_open(const char *title);

/**
 * Hide the popover. Reparents the inputbox back to its home position
 * (immediately after its original prev-sibling) and removes the
 * placeholder. No-op if not open.
 */
void vb_floating_close(void);

/**
 * Grab keyboard focus onto the inputbox and re-grab on idle. Callers
 * whose mode needs typed text to accumulate in the buffer (ex mode)
 * invoke this after vb_floating_open. Modes whose keystrokes should
 * bubble to on_map_key_pressed for interpretation (pass save/fill,
 * hint-like pickers) MUST NOT invoke this — leaving focus on the
 * webview lets those keys reach the mode's keypress callback.
 */
void vb_floating_grab_focus(void);

/**
 * Container in which callers should pack a completion list while the
 * popover is open (appears below the inputbox in the popover). Returns
 * NULL when the popover is not open.
 */
GtkWidget *vb_floating_get_completion_slot(void);

/**
 * True if the popover is currently visible.
 */
gboolean vb_floating_is_open(void);

#endif /* FLOATING_CMDLINE_H */
