/* gr-edit-page.c:
 *
 * Copyright (C) 2016 Matthias Clasen <mclasen@redhat.com>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#include "config.h"

#include <stdlib.h>
#include <math.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#ifdef ENABLE_GSPELL
#include <gspell/gspell.h>
#endif

#include "gr-edit-page.h"
#include "gr-recipe-store.h"
#include "gr-recipe.h"
#include "gr-utils.h"
#include "gr-ingredients-list.h"
#include "gr-cuisine.h"
#include "gr-meal.h"
#include "gr-season.h"
#include "gr-image.h"
#include "gr-image-viewer.h"
#include "gr-ingredient-row.h"
#include "gr-ingredient.h"
#include "gr-number.h"
#include "gr-unit.h"
#include "gr-chef-dialog.h"
#include "gr-cooking-view.h"
#include "gr-window.h"
#include "gr-account.h"
#include "gr-recipe-tile.h"
#include "gr-recipe-formatter.h"
#include "gr-ingredients-viewer.h"


struct _GrEditPage
{
        GtkBox parent_instance;

        GrRecipe *recipe;

        GtkWidget *main_content;
        GtkWidget *error_revealer;
        GtkWidget *error_label;
        GtkWidget *name_label;
        GtkWidget *name_entry;
        GtkWidget *cuisine_combo;
        GtkWidget *category_combo;
        GtkWidget *season_combo;
        GtkWidget *spiciness_combo;
        GtkWidget *prep_time_combo;
        GtkWidget *cook_time_combo;
        GtkWidget *description_field;
        GtkWidget *instructions_field;
        GtkWidget *serves_spin;
        GtkWidget *gluten_free_check;
        GtkWidget *nut_free_check;
        GtkWidget *vegan_check;
        GtkWidget *vegetarian_check;
        GtkWidget *milk_free_check;
        GtkWidget *images;
        GtkWidget *add_image_button;
        GtkWidget *remove_image_button;
        GtkWidget *default_image_button;
        GtkWidget *default_image_image;
        GtkWidget *rotate_image_right_button;
        GtkWidget *rotate_image_left_button;
        GtkWidget *author_label;
        GtkWidget *ingredients_box;
        GtkWidget *cooking_view;

        GtkWidget *recipe_popover;
        GtkWidget *recipe_list;
        GtkWidget *recipe_filter_entry;
        GtkWidget *add_step_button;
        GtkWidget *link_image_button;
        GtkWidget *timer_button;
        GtkWidget *temperature_button;
        GtkWidget *prev_step_button;
        GtkWidget *next_step_button;
        GtkWidget *image_popover;
        GtkWidget *image_flowbox;
        GtkWidget *timer_popover;
        GtkWidget *temperature_popover;
        GtkWidget *temperature_spin;
        GtkWidget *celsius_button;
        GtkWidget *timer_spin;
        GtkWidget *timer_title;
        GtkWidget *preview_stack;

        GrRecipeSearch *search;

        GtkSizeGroup *group;

        guint account_response_signal_id;

        GList *segments;
        GtkWidget *active_row;

        char *ing_term;
        char *unit_term;
        char *recipe_term;

        guint index_handler_id;

        char *author;
        gboolean unsaved;

        GCancellable *cancellable;
};

G_DEFINE_TYPE (GrEditPage, gr_edit_page, GTK_TYPE_BOX)

enum {
        PROP_0,
        PROP_UNSAVED,
        N_PROPS
};

static GParamSpec *props [N_PROPS];

static char *get_text_view_text (GtkTextView *textview);
static void  set_text_view_text (GtkTextView *textview,
                                 const char  *text);

static void
dismiss_error (GrEditPage *page)
{
        gtk_revealer_set_reveal_child (GTK_REVEALER (page->error_revealer), FALSE);
}

static void add_image_cb (GrEditPage *page);
static void set_unsaved  (GrEditPage *page);
static void add_list     (GrEditPage *page);

static void
populate_image_flowbox (GrEditPage *page)
{
        int i;
        GPtrArray *images;
        GtkWidget *button;

        g_cancellable_cancel (page->cancellable);
        g_clear_object (&page->cancellable);
        page->cancellable = g_cancellable_new ();

        images = gr_image_viewer_get_images (GR_IMAGE_VIEWER (page->images));

        container_remove_all (GTK_CONTAINER (page->image_flowbox));

        for (i = 0; i < images->len; i++) {
                GrImage *ri = g_ptr_array_index (images, i);
                GtkWidget *image;
                GtkWidget *child;

                image = gtk_image_new ();
                gtk_widget_show (image);
                gtk_container_add (GTK_CONTAINER (page->image_flowbox), image);
                child = gtk_widget_get_parent (image);
                g_object_set_data (G_OBJECT (child), "image-idx", GINT_TO_POINTER (i));

                gr_image_load (ri, 60, 40, FALSE, page->cancellable, gr_image_set_pixbuf, image);
        }

        button = gtk_button_new ();
        gtk_container_add (GTK_CONTAINER (button), gtk_image_new_from_icon_name ("list-add-symbolic", 1));
        gtk_widget_show_all (button);
        gtk_container_add (GTK_CONTAINER (page->image_flowbox), button);
        g_signal_connect_swapped (button, "clicked", G_CALLBACK (add_image_cb), page);
}

static void
update_image_button_sensitivity (GrEditPage *page)
{
        GPtrArray *images;
        int length;

        images = gr_image_viewer_get_images (GR_IMAGE_VIEWER (page->images));
        length = images->len;
        gtk_widget_set_sensitive (page->add_image_button, TRUE);
        gtk_widget_set_sensitive (page->remove_image_button, length > 0);
        gtk_widget_set_sensitive (page->rotate_image_left_button, length > 0);
        gtk_widget_set_sensitive (page->rotate_image_right_button, length > 0);
        gtk_widget_set_sensitive (page->default_image_button, length > 0);
}

static void
update_default_image_button (GrEditPage *page)
{
        int index;

        g_object_get (page->images, "index", &index, NULL);

        if (page->recipe && gr_recipe_get_default_image (page->recipe) == index) {
                gtk_widget_set_state_flags (page->default_image_button, GTK_STATE_FLAG_CHECKED, FALSE);
                gtk_image_set_from_icon_name (GTK_IMAGE (page->default_image_image), "starred-symbolic", 1);
        }
        else {
                gtk_widget_unset_state_flags (page->default_image_button, GTK_STATE_FLAG_CHECKED);
                gtk_image_set_from_icon_name (GTK_IMAGE (page->default_image_image), "non-starred-symbolic", 1);
        }
}

static void
set_default_image_cb (GrEditPage *page)
{
        int index;

        g_object_get (page->images, "index", &index, NULL);

        if (page->recipe)
                g_object_set (page->recipe, "default-image", index, NULL);
        set_unsaved (page);
}

static void
images_changed (GrEditPage *page)
{
        update_image_button_sensitivity (page);
        populate_image_flowbox (page);
        update_default_image_button (page);
}

static void
index_changed (GrEditPage *page)
{
        update_default_image_button (page);
}

static void
add_image_cb (GrEditPage *page)
{
        gr_image_viewer_add_image (GR_IMAGE_VIEWER (page->images));
        set_unsaved (page);
}

static char *
rewrite_instructions_for_removed_image (const char *instructions,
                                        int         index)
{
        g_autoptr(GPtrArray) steps = NULL;
        GString *s;
        int i;

        steps = gr_recipe_parse_instructions (instructions, FALSE);

        for (i = 0; i < steps->len; i++) {
                GrRecipeStep *step = g_ptr_array_index (steps, i);

                if (step->image == index) {
                        step->image = -1;
                }
                else if (step->image > index) {
                        step->image -= 1;
                }
        }

        s = g_string_new ("");

        for (i = 0; i < steps->len; i++) {
                GrRecipeStep *step = g_ptr_array_index (steps, i);

                if (i > 0)
                        g_string_append (s, "\n\n");

                if (step->timer != 0) {
                        int seconds;
                        int minutes;
                        int hours;

                        seconds = (int)(step->timer / G_TIME_SPAN_SECOND);
                        minutes = seconds / 60;
                        seconds = seconds - 60 * minutes;
                        hours = minutes / 60;
                        minutes = minutes - 60 * hours;

                        g_string_append_printf (s, "[timer:%02d:%02d:%02d]", hours, minutes, seconds);
                }
                else if (step->image != -1) {
                        g_string_append_printf (s, "[image:%d]", step->image);
                }

                g_string_append (s, step->text);
        }

        return g_string_free (s, FALSE);
}

static void
remove_image_cb (GrEditPage *page)
{
        int index;
        g_autofree char *instructions = NULL;
        g_autofree char *rewritten = NULL;

        index = gr_image_viewer_get_index (GR_IMAGE_VIEWER (page->images));
        gr_image_viewer_remove_image (GR_IMAGE_VIEWER (page->images));

        instructions = get_text_view_text (GTK_TEXT_VIEW (page->instructions_field));
        rewritten = rewrite_instructions_for_removed_image (instructions, index);
        set_text_view_text (GTK_TEXT_VIEW (page->instructions_field), rewritten);
        set_unsaved (page);
}

static void
rotate_image_left_cb (GrEditPage *page)
{
        gr_image_viewer_rotate_image (GR_IMAGE_VIEWER (page->images), 90);
        set_unsaved (page);
}

static void
rotate_image_right_cb (GrEditPage *page)
{
        gr_image_viewer_rotate_image (GR_IMAGE_VIEWER (page->images), 270);
        set_unsaved (page);
}

static void
edit_page_finalize (GObject *object)
{
        GrEditPage *self = GR_EDIT_PAGE (object);

        g_cancellable_cancel (self->cancellable);
        g_clear_object (&self->cancellable);

        if (self->index_handler_id)
                g_signal_handler_disconnect (self->recipe, self->index_handler_id);
        g_clear_object (&self->recipe);
        g_clear_object (&self->group);
        g_list_free (self->segments);

        g_free (self->ing_term);
        g_free (self->unit_term);
        g_free (self->recipe_term);

        g_clear_object (&self->search);

        g_free (self->author);

        G_OBJECT_CLASS (gr_edit_page_parent_class)->finalize (object);
}

static void
populate_cuisine_combo (GrEditPage *page)
{
        g_autofree char **names = NULL;
        guint length;
        int i;
        GrRecipeStore *store;

        store = gr_recipe_store_get ();
        names = gr_recipe_store_get_all_cuisines (store, &length);
        gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (page->cuisine_combo));

        for (i = 0; i < length; ++i) {
                g_autofree char *temp_cuisine_name = NULL;

                temp_cuisine_name = g_strstrip (g_strdup (names[i]));
                if (strcmp (temp_cuisine_name, "") != 0) {
                        const char *title;

                        gr_cuisine_get_data (temp_cuisine_name, &title, NULL, NULL);
                        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (page->cuisine_combo),
                                                                       temp_cuisine_name, title);
                }
        }
}

static void
populate_category_combo (GrEditPage *page)
{
        const char **names;
        int length;
        int i;

        names = gr_meal_get_names (&length);
        for (i = 0; i < length; i++) {
                gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (page->category_combo),
                                           names[i],
                                           gr_meal_get_title (names[i]));
        }
}

static void
populate_season_combo (GrEditPage *page)
{
        const char **names;
        int length;
        int i;

        names = gr_season_get_names (&length);
        for (i = 0; i < length; i++) {
                gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (page->season_combo),
                                           names[i],
                                           gr_season_get_title (names[i]));
        }
}

static char *
collect_ingredients (GrEditPage *page)
{
        GString *s;
        GtkWidget *segment;
        GtkWidget *list;
        GtkWidget *entry;
        GList *children, *l, *k;

        s = g_string_new ("");
        for (k = page->segments; k; k = k->next) {
                segment = k->data;
                list = GTK_WIDGET (g_object_get_data (G_OBJECT (segment), "list"));
                entry = GTK_WIDGET (g_object_get_data (G_OBJECT (segment), "entry"));
                children = gtk_container_get_children (GTK_CONTAINER (list));
                for (l = children; l; l = l->next) {
                        GtkWidget *row = l->data;
                        const char *amount;
                        const char *unit;
                        const char *ingredient;
                        const char *id;

                        amount = (const char *)g_object_get_data (G_OBJECT (row), "amount");
                        unit = (const char *)g_object_get_data (G_OBJECT (row), "unit");
                        ingredient = (const char *)g_object_get_data (G_OBJECT (row), "ingredient");
                        id = gr_ingredient_get_id (ingredient);
                        if (s->len > 0)
                                g_string_append (s, "\n");
                        g_string_append (s, amount);
                        g_string_append (s, "\t");
                        g_string_append (s, unit);
                        g_string_append (s, "\t");
                        g_string_append (s, id ? id : ingredient);
                        g_string_append (s, "\t");
                        g_string_append (s, gtk_entry_get_text (GTK_ENTRY (entry)));
                }
                g_list_free (children);
        }

        return g_string_free (s, FALSE);
}

static void
remove_tag_if_found (GtkTextBuffer *buffer)
{
        GtkTextIter start, end;
        g_autofree char *text = NULL;

        gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
        gtk_text_iter_set_line_offset (&start, 0);
        end = start;
        gtk_text_iter_forward_to_line_end (&end);
        text = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);
        if (g_str_has_prefix (text, "[image:") ||
            g_str_has_prefix (text, "[timer:")) {
                char *p = strstr (text, "]");
                gtk_text_iter_set_line_index (&end, p - text + 1);
                gtk_text_buffer_delete (buffer, &start, &end);
        }
}

static void
add_tag_to_step (GrEditPage *self,
                 const char *tag)
{
        GtkTextBuffer *buffer;
        GtkTextIter start;

        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->instructions_field));
        remove_tag_if_found (buffer);

        gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
        gtk_text_iter_set_line_offset (&start, 0);

        gtk_text_buffer_insert (buffer, &start, tag, -1);
}

static void
add_tag_at_insert (GrEditPage *self,
                   const char *tag)
{
        GtkTextBuffer *buffer;
        GtkTextIter start;

        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->instructions_field));
        gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
        gtk_text_buffer_insert (buffer, &start, tag, -1);
}

static void
image_activated (GtkFlowBox *flowbox,
                 GtkFlowBoxChild *child,
                 GrEditPage *self)
{
        int idx;
        g_autofree char *text = NULL;

        idx = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (child), "image-idx"));
        text = g_strdup_printf ("[image:%d]", idx);
        add_tag_to_step (self, text);

        gtk_popover_popdown (GTK_POPOVER (self->image_popover));
        gtk_widget_grab_focus (self->instructions_field);
}

static void
add_image_link (GtkButton *button, GrEditPage *page)
{
        gtk_popover_popup (GTK_POPOVER (page->image_popover));
}

static int
time_spin_input (GtkSpinButton *spin_button,
                 double        *new_val)
{
        const char *text;
        gboolean found = FALSE;

        text = gtk_entry_get_text (GTK_ENTRY (spin_button));
        if (!strchr (text, ':')) {
                g_auto(GStrv) str = NULL;
                int num;
                char *endn;

                str = g_strsplit (text, " ", 2);
                num = strtol (str[0], &endn, 10);
                if (!*endn) {
                        if (str[1] == NULL) {
                                *new_val = num; /* minutes */
                                found = TRUE;
                        }
                        else if (strcmp (str[1], _("hour")) == 0 ||
                                 strcmp (str[1], _("hours")) == 0 ||
                                 strcmp (str[1], C_("hour abbreviation", "h")) == 0) {
                                *new_val = num * 3600; /* hours */
                                found = TRUE;
                        }
                        else if (strcmp (str[1], _("minute")) == 0 ||
                                 strcmp (str[1], _("minutes")) == 0 ||
                                 strcmp (str[1], C_("minute abbreviation", "min")) == 0 ||
                                 strcmp (str[1], C_("minute abbreviation", "m")) == 0) {
                                *new_val = num * 60;
                                found = TRUE;
                        }
                        else if (strcmp (str[1], _("second")) == 0 ||
                                 strcmp (str[1], _("seconds")) == 0 ||
                                 strcmp (str[1], C_("second abbreviation", "sec")) == 0 ||
                                 strcmp (str[1], C_("second abbreviation", "s")) == 0) {
                                *new_val = num;
                                found = TRUE;
                        }
                }
        }
        else {
                g_auto(GStrv) str = NULL;
                int hours;
                int minutes;
                int seconds;
                char *endh;
                char *endm;
                char *ends;

                str = g_strsplit (text, ":", 3);

                if (g_strv_length (str) == 3) {
                        hours = strtol (str[0], &endh,10);
                        minutes = strtol (str[1], &endm, 10);
                        seconds = strtol (str[2], &ends, 10);
                        if (!*endh && !*endm && !*ends &&
                            0 <= hours && hours < 24 &&
                            0 <=  minutes && minutes < 60 &&
                            0 <= seconds && seconds < 60) {
                                *new_val = (hours * 60 + minutes) * 60 + seconds;
                                found = TRUE;
                        }
                }
                else if (g_strv_length (str) == 2) {
                        hours = strtol (str[0], &endh, 10);
                        minutes = strtol (str[1], &endm, 10);
                        if (!*endh && !*endm &&
                            0 <= hours && hours < 24 &&
                            0 <=  minutes && minutes < 60) {
                                *new_val = (hours * 60 + minutes) * 60;
                                found = TRUE;
                        }
                }
        }

        if (!found) {
                *new_val = 0.0;
                return GTK_INPUT_ERROR;
        }

        return TRUE;
}

static int
time_spin_output (GtkSpinButton *spin_button)
{
        GtkAdjustment *adjustment;
        double hours;
        double minutes;
        double seconds;
        g_autofree char *buf = NULL;

        adjustment = gtk_spin_button_get_adjustment (spin_button);
        hours = gtk_adjustment_get_value (adjustment) / 3600.0;
        minutes = (hours - floor (hours)) * 60.0;
        seconds = (minutes - floor (minutes)) * 60.0;
        buf = g_strdup_printf ("%02.0f:%02.0f:%02.0f", floor (hours), floor (minutes), floor (seconds + 0.5));
        if (strcmp (buf, gtk_entry_get_text (GTK_ENTRY (spin_button))))
                gtk_entry_set_text (GTK_ENTRY (spin_button), buf);

        return TRUE;
}

static void
time_spin_activate (GtkEntry *entry, GrEditPage *self)
{
        GtkAdjustment *adjustment;
        double hours;
        double minutes;
        double seconds;
        g_autofree char *text = NULL;
        const char *title;

        gtk_spin_button_update (GTK_SPIN_BUTTON (self->timer_spin));

        adjustment = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (self->timer_spin));
        hours = gtk_adjustment_get_value (adjustment) / 3600.0;
        minutes = (hours - floor (hours)) * 60.0;
        seconds = (minutes - floor (minutes)) * 60.0;

        title = gtk_entry_get_text (GTK_ENTRY (self->timer_title));

        if (title && title[0])
                text = g_strdup_printf ("[timer:%02.0f:%02.0f:%02.0f,%s]", floor (hours), floor (minutes), floor (seconds + 0.5), title);
        else
                text = g_strdup_printf ("[timer:%02.0f:%02.0f:%02.0f]", floor (hours), floor (minutes), floor (seconds + 0.5));

        add_tag_to_step (self, text);

        gtk_popover_popdown (GTK_POPOVER (self->timer_popover));
        gtk_widget_grab_focus (self->instructions_field);
}

static void
add_timer (GtkButton *button, GrEditPage *page)
{
        gtk_popover_popup (GTK_POPOVER (page->timer_popover));
}

static void
temperature_spin_activate (GtkEntry *entry, GrEditPage *self)
{
        int value;
        g_autofree char *text = NULL;
        const char *unit;

        gtk_spin_button_update (GTK_SPIN_BUTTON (entry));

        value = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (entry));
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->celsius_button)))
                unit = "C";
        else
                unit = "F";

        text = g_strdup_printf ("[temperature:%d%s]", value, unit);
        add_tag_at_insert (self, text);

        gtk_popover_popdown (GTK_POPOVER (self->temperature_popover));
        gtk_widget_grab_focus (self->instructions_field);
}
static void
add_temperature (GtkButton *button, GrEditPage *page)
{
        gtk_popover_popup (GTK_POPOVER (page->temperature_popover));
}

static void
add_step (GtkButton *button, GrEditPage *self)
{
        GtkTextBuffer *buffer;
        GtkTextIter end;

        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->instructions_field));
        gtk_text_buffer_get_end_iter (buffer, &end);
        gtk_text_buffer_place_cursor (buffer, &end);
        gtk_text_buffer_insert_at_cursor (buffer, "\n\n", 2);
        set_unsaved (self);
}

static void update_author_label (GrEditPage *page,
                                 GrChef     *chef);

static void
chef_done (GrChefDialog *dialog, GrChef *chef, GrEditPage *page)
{
        if (chef) {
                g_free (page->author);
                page->author = g_strdup (gr_chef_get_id (chef));
                update_author_label (page, chef);
        }
        gtk_widget_destroy (GTK_WIDGET (dialog));
}

static gboolean
edit_chef (GrEditPage *page)
{
        GrChefDialog *dialog;
        GtkWidget *win;
        GrRecipeStore *store;
        g_autoptr(GrChef) chef = NULL;

        win = gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_APPLICATION_WINDOW);

        store = gr_recipe_store_get ();

        chef = gr_recipe_store_get_chef (store, page->author ? page->author : "");

        dialog = gr_chef_dialog_new (chef, TRUE);
        g_signal_connect (dialog, "done", G_CALLBACK (chef_done), page);

        gtk_window_set_title (GTK_WINDOW (dialog), _("Recipe Author"));
        gr_window_present_dialog (GR_WINDOW (win), GTK_WINDOW (dialog));

        return TRUE;
}



static void
gr_edit_page_init (GrEditPage *page)
{
        gtk_widget_set_has_window (GTK_WIDGET (page), FALSE);
        gtk_widget_init_template (GTK_WIDGET (page));

        page->group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

        populate_cuisine_combo (page);
        populate_category_combo (page);
        populate_season_combo (page);

#ifdef ENABLE_GSPELL
        {
                GspellTextView *gspell_view;

                gspell_view = gspell_text_view_get_from_gtk_text_view (GTK_TEXT_VIEW (page->description_field));
                gspell_text_view_basic_setup (gspell_view);

                gspell_view = gspell_text_view_get_from_gtk_text_view (GTK_TEXT_VIEW (page->instructions_field));
                gspell_text_view_basic_setup (gspell_view);
        }
#endif
}

static void
update_steppers (GrEditPage *page)
{
        int step;
        int n_steps;

        step = gr_cooking_view_get_step (GR_COOKING_VIEW (page->cooking_view));
        n_steps = gr_cooking_view_get_n_steps (GR_COOKING_VIEW (page->cooking_view));

        gtk_widget_set_sensitive (page->prev_step_button, step - 1 >= 0);
        gtk_widget_set_sensitive (page->next_step_button, step + 1 <= n_steps - 1);
}

static void
preview_visible_changed (GrEditPage *page)
{
        const char *visible;

        visible = gtk_stack_get_visible_child_name (GTK_STACK (page->preview_stack));

        if (strcmp (visible, "edit") == 0) {
                gtk_widget_set_sensitive (page->add_step_button, TRUE);
                gtk_widget_set_sensitive (page->link_image_button, TRUE);
                gtk_widget_set_sensitive (page->timer_button, TRUE);
                gtk_widget_set_sensitive (page->temperature_button, TRUE);
                gtk_widget_set_visible (page->prev_step_button, FALSE);
                gtk_widget_set_visible (page->next_step_button, FALSE);
                gr_cooking_view_stop (GR_COOKING_VIEW (page->cooking_view), TRUE);
        }
        else {
                GPtrArray *images;
                g_autofree char *instructions = NULL;

                gtk_widget_set_sensitive (page->add_step_button, FALSE);
                gtk_widget_set_sensitive (page->link_image_button, FALSE);
                gtk_widget_set_sensitive (page->timer_button, FALSE);
                gtk_widget_set_sensitive (page->temperature_button, FALSE);
                gtk_widget_set_visible (page->prev_step_button, TRUE);
                gtk_widget_set_visible (page->next_step_button, TRUE);

                images = gr_image_viewer_get_images (GR_IMAGE_VIEWER (page->images));
                instructions = get_text_view_text (GTK_TEXT_VIEW (page->instructions_field));

                gr_cooking_view_set_data (GR_COOKING_VIEW (page->cooking_view), NULL, instructions, images);
                gr_cooking_view_start (GR_COOKING_VIEW (page->cooking_view));

                update_steppers (page);
        }
}

static void
prev_step (GrEditPage *page)
{
        gr_cooking_view_prev_step (GR_COOKING_VIEW (page->cooking_view));
        update_steppers (page);
}

static void
next_step (GrEditPage *page)
{
        gr_cooking_view_next_step (GR_COOKING_VIEW (page->cooking_view));
        update_steppers (page);
}

static void
gr_edit_page_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{    
        GrEditPage *self = GR_EDIT_PAGE (object);
        switch (prop_id) {
        case PROP_UNSAVED:
                self->unsaved = g_value_get_boolean (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}


static void
gr_edit_page_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
        GrEditPage *self = GR_EDIT_PAGE (object);

        switch (prop_id) {
        case PROP_UNSAVED:
                g_value_set_boolean (value, self->unsaved);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
set_unsaved (GrEditPage *page)
{
        g_object_set (G_OBJECT (page), "unsaved", TRUE, NULL);
}

static void
gr_edit_page_grab_focus (GtkWidget *widget)
{
        GrEditPage *self = GR_EDIT_PAGE (widget);

        gtk_widget_grab_focus (self->name_entry);
}

static void
gr_edit_page_class_init (GrEditPageClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->finalize = edit_page_finalize;
        object_class->set_property = gr_edit_page_set_property;
        object_class->get_property = gr_edit_page_get_property;

        widget_class->grab_focus = gr_edit_page_grab_focus;

        props [PROP_UNSAVED] = g_param_spec_boolean ("unsaved",
                                                      NULL, NULL,
                                                      TRUE, G_PARAM_READWRITE);
        g_object_class_install_properties (object_class, N_PROPS, props);

        gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Recipes/gr-edit-page.ui");

        gtk_widget_class_bind_template_child (widget_class, GrEditPage, main_content);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, error_revealer);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, error_label);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, name_label);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, name_entry);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, cuisine_combo);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, category_combo);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, season_combo);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, prep_time_combo);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, cook_time_combo);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, serves_spin);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, spiciness_combo);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, description_field);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, instructions_field);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, gluten_free_check);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, nut_free_check);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, vegan_check);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, vegetarian_check);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, milk_free_check);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, images);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, add_image_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, remove_image_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, default_image_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, default_image_image);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, rotate_image_left_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, rotate_image_right_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, author_label);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, ingredients_box);

        gtk_widget_class_bind_template_child (widget_class, GrEditPage, add_step_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, link_image_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, timer_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, temperature_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, prev_step_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, next_step_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, image_popover);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, image_flowbox);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, timer_popover);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, temperature_popover);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, temperature_spin);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, celsius_button);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, timer_spin);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, timer_title);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, preview_stack);
        gtk_widget_class_bind_template_child (widget_class, GrEditPage, cooking_view);

        gtk_widget_class_bind_template_callback (widget_class, dismiss_error);
        gtk_widget_class_bind_template_callback (widget_class, add_image_cb);
        gtk_widget_class_bind_template_callback (widget_class, remove_image_cb);
        gtk_widget_class_bind_template_callback (widget_class, rotate_image_left_cb);
        gtk_widget_class_bind_template_callback (widget_class, rotate_image_right_cb);
        gtk_widget_class_bind_template_callback (widget_class, images_changed);
        gtk_widget_class_bind_template_callback (widget_class, index_changed);

        gtk_widget_class_bind_template_callback (widget_class, add_image_link);
        gtk_widget_class_bind_template_callback (widget_class, add_timer);
        gtk_widget_class_bind_template_callback (widget_class, add_step);
        gtk_widget_class_bind_template_callback (widget_class, add_temperature);
        gtk_widget_class_bind_template_callback (widget_class, image_activated);

        gtk_widget_class_bind_template_callback (widget_class, set_default_image_cb);
        gtk_widget_class_bind_template_callback (widget_class, edit_chef);

        gtk_widget_class_bind_template_callback (widget_class, time_spin_input);
        gtk_widget_class_bind_template_callback (widget_class, time_spin_output);
        gtk_widget_class_bind_template_callback (widget_class, time_spin_activate);
        gtk_widget_class_bind_template_callback (widget_class, temperature_spin_activate);
        gtk_widget_class_bind_template_callback (widget_class, preview_visible_changed);
        gtk_widget_class_bind_template_callback (widget_class, prev_step);
        gtk_widget_class_bind_template_callback (widget_class, next_step);
        gtk_widget_class_bind_template_callback (widget_class, set_unsaved);

        gtk_widget_class_bind_template_callback (widget_class, add_list);
}

GtkWidget *
gr_edit_page_new (void)
{
        GrEditPage *page;

        page = g_object_new (GR_TYPE_EDIT_PAGE, NULL);

        return GTK_WIDGET (page);
}

static char *
get_combo_value (GtkComboBox *combo)
{
        const char *id;
        char *value;

        id = gtk_combo_box_get_active_id (combo);
        if (id)
                value = g_strdup (id);
        else
                value = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));

        return value;
}

static void
set_combo_value (GtkComboBox *combo,
                 const char  *value)
{
        if (!gtk_combo_box_set_active_id (combo, value)) {
                GtkWidget *entry;

                entry = gtk_bin_get_child (GTK_BIN (combo));
                gtk_entry_set_text (GTK_ENTRY (entry), value);
        }
}

static char *
get_text_view_text (GtkTextView *textview)
{
        GtkTextBuffer *buffer;
        GtkTextIter start, end;

        buffer = gtk_text_view_get_buffer (textview);
        gtk_text_buffer_get_bounds (buffer, &start, &end);
        return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}

static void
set_text_view_text (GtkTextView *textview,
                    const char  *text)
{
        GtkTextBuffer *buffer;

        buffer = gtk_text_view_get_buffer (textview);
        gtk_text_buffer_set_text (buffer, text ? text : "", -1);
}

static void
set_spiciness (GrEditPage *page,
               int         spiciness)
{
        if (spiciness < 25)
                gtk_combo_box_set_active_id (GTK_COMBO_BOX (page->spiciness_combo), "mild");
        else if (spiciness < 50)
                gtk_combo_box_set_active_id (GTK_COMBO_BOX (page->spiciness_combo), "spicy");
        else if (spiciness < 75)
                gtk_combo_box_set_active_id (GTK_COMBO_BOX (page->spiciness_combo), "hot");
        else
                gtk_combo_box_set_active_id (GTK_COMBO_BOX (page->spiciness_combo), "extreme");
}

static int
get_spiciness (GrEditPage *page)
{
        const char *s;

        s = gtk_combo_box_get_active_id (GTK_COMBO_BOX (page->spiciness_combo));

        if (g_strcmp0 (s, "mild") == 0)
                return 15;
        else if (g_strcmp0 (s, "spicy") == 0)
                return 40;
        else if (g_strcmp0 (s, "hot") == 0)
                return 65;
        else if (g_strcmp0 (s, "extreme") == 0)
                return 90;
        else
                return 0;
}

static void
remove_list2 (GrIngredientsViewer *viewer, GrEditPage *page)
{
        gtk_widget_destroy (GTK_WIDGET (viewer));
}

static void
active_changed (GrIngredientsViewer *viewer,
                GParamSpec          *pspec,
                GrEditPage          *page)
{
        gboolean active;
        GList *children, *l;

        g_object_get (viewer, "active", &active, NULL);

        if (!active)
                return;

        children = gtk_container_get_children (GTK_CONTAINER (page->ingredients_box));
        for (l = children; l; l = l->next) {
                GrIngredientsViewer *list = l->data;
                if (list != viewer)
                        g_object_set (list, "active", FALSE, NULL);
        }
        g_list_free (children);
}

static void
add_list_full (GrEditPage *page,
               const char *title,
               const char *ingredients,
               gboolean    editable_title)
{
        GtkWidget *list;

        list = g_object_new (GR_TYPE_INGREDIENTS_VIEWER,
                             "title", title,
                             "ingredients", ingredients,
                             "editable-title", editable_title,
                             "editable", TRUE,
                             NULL);

        g_signal_connect_swapped (list, "notify::title", G_CALLBACK (set_unsaved), page);
        g_signal_connect_swapped (list, "notify::ingredients", G_CALLBACK (set_unsaved), page);
        g_signal_connect (list, "notify::active", G_CALLBACK (active_changed), page);
        g_signal_connect (list, "delete", G_CALLBACK (remove_list2), page);

        gtk_container_add (GTK_CONTAINER (page->ingredients_box), list);
}

static void
add_list (GrEditPage *page)
{
        add_list_full (page, "", "", TRUE);
}

static void
populate_ingredients (GrEditPage *page,
                      const char *text)
{
        container_remove_all (GTK_CONTAINER (page->ingredients_box));

        if (strcmp (text, "") == 0) {
                add_list_full (page, "", "", FALSE);
        }
        else {
                g_autoptr(GrIngredientsList) ingredients = NULL;
                g_autofree char **segs = NULL;
                gboolean editable_title;
                int j;

                ingredients = gr_ingredients_list_new (text);
                segs = gr_ingredients_list_get_segments (ingredients);
                editable_title = g_strv_length (segs) > 1;
                for (j = 0; segs[j]; j++) {
                        add_list_full (page, segs[j], text, editable_title);
                }
        }
}

static void
scroll_up (GrEditPage *page)
{
        GtkAdjustment *adj;

        adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (page->main_content));
        gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
}

void
gr_edit_page_clear (GrEditPage *page)
{
        g_autoptr(GPtrArray) images = NULL;
        GrRecipeStore *store;

        gr_image_viewer_revert_changes (GR_IMAGE_VIEWER (page->images));

        store = gr_recipe_store_get ();

        gtk_label_set_label (GTK_LABEL (page->name_label), _("Name Your Recipe"));
        gtk_entry_set_text (GTK_ENTRY (page->name_entry), "");
        set_combo_value (GTK_COMBO_BOX (page->cuisine_combo), "");
        set_combo_value (GTK_COMBO_BOX (page->category_combo), "");
        set_combo_value (GTK_COMBO_BOX (page->season_combo), "");
        set_combo_value (GTK_COMBO_BOX (page->prep_time_combo), "");
        set_combo_value (GTK_COMBO_BOX (page->cook_time_combo), "");
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (page->serves_spin), 1);
        set_spiciness (page, 0);
        populate_ingredients (page, "");
        set_text_view_text (GTK_TEXT_VIEW (page->description_field), "");
        set_text_view_text (GTK_TEXT_VIEW (page->instructions_field), "");
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->gluten_free_check), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->nut_free_check), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->vegan_check), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->vegetarian_check), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->milk_free_check), FALSE);
        gtk_widget_hide (page->author_label);

        images = gr_image_array_new ();
        g_object_set (page->images, "images", images, NULL);

        gr_cooking_view_set_data (GR_COOKING_VIEW (page->cooking_view), NULL, "", images);
        gr_cooking_view_set_step (GR_COOKING_VIEW (page->cooking_view), 0);
        gtk_stack_set_visible_child_name (GTK_STACK (page->preview_stack), "edit");
        preview_visible_changed (page);

        if (page->index_handler_id) {
                g_signal_handler_disconnect (page->recipe, page->index_handler_id);
                page->index_handler_id = 0;
        }

        g_clear_object (&page->recipe);

        g_free (page->author);
        page->author = g_strdup (gr_recipe_store_get_user_key (store));

        scroll_up (page);
}

static void
set_instructions (GtkTextView *text_view,
                  const char  *text)
{
        GtkTextBuffer *buffer;
        GtkTextIter iter;
        GtkTextTag *tag;
        const char *p;
        const char *p1, *p2, *q1, *q2, *r1, *r2;
        GdkRGBA color;

        gdk_rgba_parse (&color, "blue");
        buffer = gtk_text_view_get_buffer (text_view);
        gtk_text_buffer_set_text (buffer, "", -1);

        p = text;
        while (*p) {
                g_autofree char *recipe_id = NULL;
                g_autofree char *url = NULL;
                int image_idx;

                q1 = NULL;
                r1 = NULL;
                p1 = strstr (p, "<a href=\"");
                if (!p1)
                        break;
                p2 = p1 + strlen ("<a href=\"");
                if (strncmp (p2, "recipe:", strlen ("recipe:")) == 0) {
                        p2 = p2 + strlen ("recipe:");
                        q1 = strstr (p2, "\">");
                        recipe_id = g_strndup (p2, q1 - p2);
                        url = g_strconcat ("recipe:", recipe_id, NULL);
                }
                else if (strncmp (p2, "image:", strlen ("image:")) == 0) {
                        p2 = p2 + strlen ("image:");
                        q1 = strstr (p2, "\">");
                        image_idx = (int)g_ascii_strtoll (p2, NULL, 10);
                        url = g_strdup_printf ("image:%d", image_idx);
                }
                else {
                        p = p2;
                        continue;
                }

                if (!q1)
                        break;

                q2 = q1 + strlen ("\">");
                r1 = strstr (q2, "</a>");

                if (!r1)
                        break;

                r2 = r1 + strlen ("</a>");

                gtk_text_buffer_get_end_iter (buffer, &iter);
                gtk_text_buffer_insert (buffer, &iter, p, p1 - p);

                tag = gtk_text_buffer_create_tag (buffer, NULL,
                                                  "foreground-rgba", &color,
                                                  "underline", PANGO_UNDERLINE_SINGLE,
                                                  NULL);
                g_object_set_data_full (G_OBJECT (tag), "href", g_strdup (url), g_free);

                gtk_text_buffer_get_end_iter (buffer, &iter);
                gtk_text_buffer_insert_with_tags (buffer, &iter, q2, r1 - q2, tag, NULL);

                p = r2;
        }

        gtk_text_buffer_get_end_iter (buffer, &iter);
        gtk_text_buffer_insert (buffer, &iter, p, -1);
}

static void
update_author_label (GrEditPage *page,
                     GrChef     *chef)
{
        if (chef) {
                g_autofree char *tmp = NULL;
                g_autofree char *link = NULL;

                link = g_strdup_printf ("<a href=\"edit\">%s</a>", gr_chef_get_name (chef));
                tmp = g_strdup_printf (_("Recipe by %s"), link);
                gtk_label_set_label (GTK_LABEL (page->author_label), tmp);
                gtk_widget_show (page->author_label);
        }
        else {
                gtk_widget_hide (page->author_label);
        }
}

void
gr_edit_page_edit (GrEditPage *page,
                   GrRecipe   *recipe)
{
        const char *name;
        const char *cuisine;
        const char *category;
        const char *season;
        const char *prep_time;
        const char *cook_time;
        const char *author;
        int serves;
        int spiciness;
        const char *description;
        const char *instructions;
        const char *ingredients;
        GrDiets diets;
        int index;
        GPtrArray *images;
        g_autoptr(GrChef) chef = NULL;
        GrRecipeStore *store;

        gr_image_viewer_revert_changes (GR_IMAGE_VIEWER (page->images));

        store = gr_recipe_store_get ();

        name = gr_recipe_get_name (recipe);
        serves = gr_recipe_get_serves (recipe);
        spiciness = gr_recipe_get_spiciness (recipe);
        cuisine = gr_recipe_get_cuisine (recipe);
        category = gr_recipe_get_category (recipe);
        season = gr_recipe_get_season (recipe);
        prep_time = gr_recipe_get_prep_time (recipe);
        cook_time = gr_recipe_get_cook_time (recipe);
        diets = gr_recipe_get_diets (recipe);
        description = gr_recipe_get_description (recipe);
        instructions = gr_recipe_get_instructions (recipe);
        ingredients = gr_recipe_get_ingredients (recipe);
        author = gr_recipe_get_author (recipe);
        index = gr_recipe_get_default_image (recipe);
        images = gr_recipe_get_images (recipe);

        g_free (page->author);
        page->author = g_strdup (author);

        chef = gr_recipe_store_get_chef (store, author ? author : "");

        gtk_label_set_label (GTK_LABEL (page->name_label), _("Name"));

        gtk_entry_set_text (GTK_ENTRY (page->name_entry), name);
        set_combo_value (GTK_COMBO_BOX (page->cuisine_combo), cuisine);
        set_combo_value (GTK_COMBO_BOX (page->category_combo), category);
        set_combo_value (GTK_COMBO_BOX (page->season_combo), season);
        set_combo_value (GTK_COMBO_BOX (page->prep_time_combo), prep_time);
        set_combo_value (GTK_COMBO_BOX (page->cook_time_combo), cook_time);
        set_spiciness (page, spiciness);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (page->serves_spin), serves);
        set_text_view_text (GTK_TEXT_VIEW (page->description_field), description);
        set_instructions (GTK_TEXT_VIEW (page->instructions_field), instructions);
        gtk_stack_set_visible_child_name (GTK_STACK (page->preview_stack), "edit");

        populate_ingredients (page, ingredients);

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->gluten_free_check), (diets & GR_DIET_GLUTEN_FREE) != 0);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->nut_free_check), (diets & GR_DIET_NUT_FREE) != 0);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->vegan_check), (diets & GR_DIET_VEGAN) != 0);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->vegetarian_check), (diets & GR_DIET_VEGETARIAN) != 0);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->milk_free_check), (diets & GR_DIET_MILK_FREE) != 0);

        gr_image_viewer_set_images (GR_IMAGE_VIEWER (page->images), images, index);

        update_author_label (page, chef);

        if (page->index_handler_id) {
                g_signal_handler_disconnect (page->recipe, page->index_handler_id);
                page->index_handler_id = 0;
        }

        g_set_object (&page->recipe, recipe);

        if (recipe) {
                page->index_handler_id = g_signal_connect_swapped (recipe, "notify::default-image",
                                                                   G_CALLBACK (update_default_image_button), page);
        }

        update_default_image_button (page);

        scroll_up (page);
}

gboolean
gr_edit_page_save (GrEditPage *page)
{
        const char *name;
        g_autofree char *cuisine = NULL;
        g_autofree char *category = NULL;
        g_autofree char *season = NULL;
        g_autofree char *prep_time = NULL;
        g_autofree char *cook_time = NULL;
        int serves;
        int spiciness;
        g_autofree char *description = NULL;
        g_autofree char *ingredients = NULL;
        g_autofree char *instructions = NULL;
        GrRecipeStore *store;
        g_autoptr(GError) error = NULL;
        gboolean ret = TRUE;
        GrDiets diets;
        GPtrArray *images;

        store = gr_recipe_store_get ();

        name = gtk_entry_get_text (GTK_ENTRY (page->name_entry));
        cuisine = get_combo_value (GTK_COMBO_BOX (page->cuisine_combo));
        category = get_combo_value (GTK_COMBO_BOX (page->category_combo));
        season = get_combo_value (GTK_COMBO_BOX (page->season_combo));
        prep_time = get_combo_value (GTK_COMBO_BOX (page->prep_time_combo));
        cook_time = get_combo_value (GTK_COMBO_BOX (page->cook_time_combo));
        spiciness = get_spiciness (page);
        serves = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (page->serves_spin));
        ingredients = collect_ingredients (page);
        description = get_text_view_text (GTK_TEXT_VIEW (page->description_field));
        instructions = get_text_view_text (GTK_TEXT_VIEW (page->instructions_field));
        diets = (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->gluten_free_check)) ? GR_DIET_GLUTEN_FREE : 0) |
                (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->nut_free_check)) ? GR_DIET_NUT_FREE : 0) |
                (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->vegan_check)) ? GR_DIET_VEGAN : 0) |
               (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->vegetarian_check)) ? GR_DIET_VEGETARIAN : 0) |
                (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->milk_free_check)) ? GR_DIET_MILK_FREE : 0);

        images = gr_image_viewer_get_images (GR_IMAGE_VIEWER (page->images));

        if (page->recipe) {
                g_autofree char *old_id = NULL;
                g_autofree char *id = NULL;

                id = generate_id ("R_", name, "_by_", page->author, NULL);
                old_id = g_strdup (gr_recipe_get_id (page->recipe));
                g_object_set (page->recipe,
                              "id", id,
                              "author", page->author,
                              "name", name,
                              "cuisine", cuisine,
                              "category", category,
                              "season", season,
                              "prep-time", prep_time,
                              "cook-time", cook_time,
                              "serves", serves,
                              "spiciness", spiciness,
                              "description", description,
                              "ingredients", ingredients,
                              "instructions", instructions,
                              "diets", diets,
                              "images", images,
                              "mtime", g_date_time_new_now_utc (),
                              NULL);
                ret = gr_recipe_store_update_recipe (store, page->recipe, old_id, &error);
        }
        else if (name == NULL || name[0] == '\0') {
                g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             _("You need to provide a name for the recipe"));
                ret = FALSE;
        }
        else {
                g_autoptr(GrRecipe) recipe = NULL;
                g_autofree char *id = NULL;
                GtkWidget *window;

                window = gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_APPLICATION_WINDOW);
                gr_ensure_user_chef (GTK_WINDOW (window), NULL, NULL);
                id = generate_id ("R_", name, "_by_", page->author, NULL);
                recipe = g_object_new (GR_TYPE_RECIPE,
                                       "id", id,
                                       "author", page->author,
                                       "name", name,
                                       "cuisine", cuisine,
                                       "category", category,
                                       "season", season,
                                       "prep-time", prep_time,
                                       "cook-time", cook_time,
                                       "serves", serves,
                                       "spiciness", spiciness,
                                       "description", description,
                                       "ingredients", ingredients,
                                       "instructions", instructions,
                                       "diets", diets,
                                       "images", images,
                                       NULL);
                ret = gr_recipe_store_add_recipe (store, recipe, &error);
                if (ret)
                        g_set_object (&page->recipe, recipe);
        }
        populate_cuisine_combo (page);
        if (ret) {
                gr_image_viewer_persist_changes (GR_IMAGE_VIEWER (page->images));

                return TRUE;
        }

        gr_image_viewer_revert_changes (GR_IMAGE_VIEWER (page->images));

        gtk_label_set_label (GTK_LABEL (page->error_label), error->message);
        gtk_revealer_set_reveal_child (GTK_REVEALER (page->error_revealer), TRUE);

        if (page->index_handler_id) {
                g_signal_handler_disconnect (page->recipe, page->index_handler_id);
                page->index_handler_id = 0;
        }

        return FALSE;
}

GrRecipe *
gr_edit_page_get_recipe (GrEditPage *page)
{
        return page->recipe;
}
