/* pager object */

/*
 * Copyright (C) 2001 Havoc Pennington
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "pager.h"
#include "workspace.h"
#include "window.h"
#include "window-action-menu.h"

#define N_SCREEN_CONNECTIONS 9

struct _WnckPagerPrivate
{
  WnckScreen *screen;
  int n_rows; /* really columns for vertical orientation */
  GtkOrientation orientation;
  int workspace_size;
  guint screen_connections[N_SCREEN_CONNECTIONS];
  int drag_start_x;
  int drag_start_y;
  int drag_start_x_workspace_relative;
  int drag_start_y_workspace_relative;
  WnckWindow *drag_window;
  int drag_window_x;
  int drag_window_y;
  GtkWidget *action_menu;
  WnckWindow *action_window;
  int action_click_x;
  int action_click_y;
  guint dragging : 1;
};

enum
{
  LAST_SIGNAL
};


#define POINT_IN_RECT(xcoord, ycoord, rect) \
 ((xcoord) >= (rect).x &&                   \
  (xcoord) <  ((rect).x + (rect).width) &&  \
  (ycoord) >= (rect).y &&                   \
  (ycoord) <  ((rect).y + (rect).height))

static void wnck_pager_init        (WnckPager      *pager);
static void wnck_pager_class_init  (WnckPagerClass *klass);
static void wnck_pager_finalize    (GObject        *object);

static void     wnck_pager_realize       (GtkWidget        *widget);
static void     wnck_pager_unrealize     (GtkWidget        *widget);
static void     wnck_pager_size_request  (GtkWidget        *widget,
                                          GtkRequisition   *requisition);
static void     wnck_pager_size_allocate (GtkWidget        *widget,
                                          GtkAllocation    *allocation);
static gboolean wnck_pager_expose_event  (GtkWidget        *widget,
                                          GdkEventExpose   *event);
static gboolean wnck_pager_button_press  (GtkWidget        *widget,
                                          GdkEventButton   *event);
static gboolean wnck_pager_motion        (GtkWidget        *widget,
                                          GdkEventMotion   *event);
static gboolean wnck_pager_button_release (GtkWidget        *widget,
                                           GdkEventButton   *event);
static gboolean wnck_pager_focus         (GtkWidget        *widget,
                                          GtkDirectionType  direction);

static void wnck_pager_connect_screen    (WnckPager  *pager,
                                          WnckScreen *screen);
static void wnck_pager_connect_window    (WnckPager  *pager,
                                          WnckWindow *window);
static void wnck_pager_disconnect_screen (WnckPager  *pager);

static void wnck_pager_popup_action_menu (WnckPager  *pager,
                                          WnckWindow *window,
                                          int         button,
                                          int         x,
                                          int         y,
                                          guint32     timestamp);
static void wnck_pager_clear_action_menu (WnckPager  *pager);

static void wnck_pager_clear_drag (WnckPager *pager);

static gpointer parent_class;
static guint signals[LAST_SIGNAL] = { 0 };

GType
wnck_pager_get_type (void)
{
  static GType object_type = 0;

  g_type_init ();
  
  if (!object_type)
    {
      static const GTypeInfo object_info =
      {
        sizeof (WnckPagerClass),
        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc) wnck_pager_class_init,
        NULL,           /* class_finalize */
        NULL,           /* class_data */
        sizeof (WnckPager),
        0,              /* n_preallocs */
        (GInstanceInitFunc) wnck_pager_init,
      };
      
      object_type = g_type_register_static (GTK_TYPE_WIDGET,
                                            "WnckPager",
                                            &object_info, 0);
    }
  
  return object_type;
}

static void
wnck_pager_init (WnckPager *pager)
{  
  pager->priv = g_new0 (WnckPagerPrivate, 1);

  pager->priv->n_rows = 1;
  pager->priv->orientation = GTK_ORIENTATION_HORIZONTAL;
  pager->priv->workspace_size = 48;
}

static void
wnck_pager_class_init (WnckPagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  
  parent_class = g_type_class_peek_parent (klass);
  
  object_class->finalize = wnck_pager_finalize;

  widget_class->realize = wnck_pager_realize;
  widget_class->unrealize = wnck_pager_unrealize;
  widget_class->size_request = wnck_pager_size_request;
  widget_class->size_allocate = wnck_pager_size_allocate;
  widget_class->expose_event = wnck_pager_expose_event;
  widget_class->button_press_event = wnck_pager_button_press;
  widget_class->button_release_event = wnck_pager_button_release;
  widget_class->motion_notify_event = wnck_pager_motion;
  widget_class->focus = wnck_pager_focus;
}

static void
wnck_pager_finalize (GObject *object)
{
  WnckPager *pager;

  pager = WNCK_PAGER (object);

  wnck_pager_clear_action_menu (pager);
  
  wnck_pager_disconnect_screen (pager);
  
  g_free (pager->priv);
  
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
wnck_pager_realize (GtkWidget *widget)
{

  GdkWindowAttr attributes;
  gint attributes_mask;
  WnckPager *pager;

  pager = WNCK_PAGER (widget);

  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);
  attributes.event_mask = gtk_widget_get_events (widget) | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK |
    GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
  gdk_window_set_user_data (widget->window, widget);

  widget->style = gtk_style_attach (widget->style, widget->window);
  gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);
}

static void
wnck_pager_unrealize (GtkWidget *widget)
{
  WnckPager *pager;
  
  pager = WNCK_PAGER (widget);

  wnck_pager_clear_drag (pager);
  
  GTK_WIDGET_CLASS (parent_class)->unrealize (widget);
}

static void
wnck_pager_size_request  (GtkWidget      *widget,
                          GtkRequisition *requisition)
{
  WnckPager *pager;
  int n_spaces;
  int spaces_per_row;
  double screen_aspect;
  int other_dimension_size;
  
  pager = WNCK_PAGER (widget);
  
  n_spaces = wnck_screen_get_workspace_count (pager->priv->screen);

  g_assert (pager->priv->n_rows > 0);
  spaces_per_row = n_spaces /  pager->priv->n_rows;
  
  if (pager->priv->orientation == GTK_ORIENTATION_VERTICAL)
    {
      screen_aspect = (double) gdk_screen_height () / (double) gdk_screen_width ();
      other_dimension_size = screen_aspect * pager->priv->workspace_size;
      
      requisition->width = pager->priv->workspace_size * pager->priv->n_rows;
      requisition->height = other_dimension_size * spaces_per_row;
    }
  else
    {
      screen_aspect = (double) gdk_screen_width () / (double) gdk_screen_height ();
      other_dimension_size = screen_aspect * pager->priv->workspace_size;

      requisition->width = other_dimension_size * spaces_per_row;
      requisition->height = pager->priv->workspace_size * pager->priv->n_rows;
    }
}

static void
wnck_pager_size_allocate (GtkWidget      *widget,
                          GtkAllocation  *allocation)
{
  WnckPager *pager;

  pager = WNCK_PAGER (widget);

  GTK_WIDGET_CLASS (parent_class)->size_allocate (widget, allocation);
}

static void
get_workspace_rect (WnckPager    *pager,
                    int           space,
                    GdkRectangle *rect)
{
  int n_spaces;
  int spaces_per_row;
  GtkWidget *widget;
  
  widget = GTK_WIDGET (pager);
  
  n_spaces = wnck_screen_get_workspace_count (pager->priv->screen);

  g_assert (pager->priv->n_rows > 0);
  spaces_per_row = n_spaces /  pager->priv->n_rows;
  
  if (pager->priv->orientation == GTK_ORIENTATION_VERTICAL)
    {      
      rect->width = widget->allocation.width / pager->priv->n_rows;
      rect->height = widget->allocation.height / spaces_per_row;
      rect->x = rect->width * (space % pager->priv->n_rows); 
      rect->y = rect->height * (space % spaces_per_row);
    }
  else
    {
      rect->width = widget->allocation.width / spaces_per_row;
      rect->height = widget->allocation.height / pager->priv->n_rows;
      rect->x = rect->width * (space % spaces_per_row);
      rect->y = rect->height * (space % pager->priv->n_rows);
    }
}
                    
static GList*
get_windows_for_workspace_in_bottom_to_top (WnckScreen    *screen,
                                            WnckWorkspace *workspace)
{
  GList *result;
  GList *windows;
  GList *tmp;
  
  result = NULL;

  windows = wnck_screen_get_windows_stacked (screen);
  tmp = windows;
  while (tmp != NULL)
    {
      WnckWindow *win = WNCK_WINDOW (tmp->data);

      if (wnck_window_is_pinned (win) ||
          wnck_window_get_workspace (win) == workspace)
        result = g_list_prepend (result, win);
      
      tmp = tmp->next;
    }

  result = g_list_reverse (result);

  return result;
}

static void
get_window_rect (WnckWindow         *window,
                 const GdkRectangle *workspace_rect,
                 GdkRectangle       *rect)
{
  double width_ratio, height_ratio;
  int x, y, width, height;
  
  width_ratio = (double) workspace_rect->width / (double) gdk_screen_width ();
  height_ratio = (double) workspace_rect->height / (double) gdk_screen_height ();
  
  wnck_window_get_geometry (window, &x, &y, &width, &height);
  
  x *= width_ratio;
  y *= height_ratio;
  width *= width_ratio;
  height *= height_ratio;
  
  x += workspace_rect->x;
  y += workspace_rect->y;
  
  if (width < 3)
    width = 3;
  if (height < 3)
    height = 3;

  rect->x = x;
  rect->y = y;
  rect->width = width;
  rect->height = height;
}

static void
draw_window (GdkDrawable        *drawable,
             GtkWidget          *widget,
             WnckWindow         *win,
             const GdkRectangle *winrect)
{
  GdkPixbuf *icon;
  int icon_x, icon_y, icon_w, icon_h;
  gboolean is_active;

  is_active = wnck_window_is_active (win);
          
  gdk_draw_rectangle (drawable,
                      is_active ?
                      widget->style->bg_gc[GTK_STATE_SELECTED] :
                      widget->style->bg_gc[GTK_STATE_NORMAL],
                      TRUE,
                      winrect->x + 1, winrect->y + 1,
                      winrect->width - 2, winrect->height - 2);

  icon = wnck_window_get_icon (win);

  icon_w = icon_h = 0;
          
  if (icon)
    {              
      icon_w = gdk_pixbuf_get_width (icon);
      icon_h = gdk_pixbuf_get_height (icon);

      /* If the icon is too big, fall back to mini icon.
       * We don't arbitrarily scale the icon, because it's
       * just too slow on my Athlon 850.
       */
      if (icon_w > (winrect->width - 2) ||
          icon_h > (winrect->height - 2))
        {
          icon = wnck_window_get_mini_icon (win);
          if (icon)
            {
              icon_w = gdk_pixbuf_get_width (icon);
              icon_h = gdk_pixbuf_get_height (icon);

              /* Give up. */
              if (icon_w > (winrect->width - 2) ||
                  icon_h > (winrect->height - 2))
                icon = NULL;
            }
        }
    }

  if (icon)
    {
      icon_x = winrect->x + (winrect->width - icon_w) / 2;
      icon_y = winrect->y + (winrect->height - icon_h) / 2;
                
      {
        /* render_to_drawable should take a clip rect to save
         * us this mess...
         */
        GdkRectangle pixbuf_rect;
        GdkRectangle draw_rect;
                
        pixbuf_rect.x = icon_x;
        pixbuf_rect.y = icon_y;
        pixbuf_rect.width = icon_w;
        pixbuf_rect.height = icon_h;
                
        if (gdk_rectangle_intersect (winrect, &pixbuf_rect,
                                     &draw_rect))
          {
            gdk_pixbuf_render_to_drawable_alpha (icon,
                                                 drawable,
                                                 draw_rect.x - pixbuf_rect.x,
                                                 draw_rect.y - pixbuf_rect.y,
                                                 draw_rect.x, draw_rect.y,
                                                 draw_rect.width,
                                                 draw_rect.height,
                                                 GDK_PIXBUF_ALPHA_FULL,
                                                 128,
                                                 GDK_RGB_DITHER_NORMAL,
                                                 0, 0);
          }
      }
    }
          
  gdk_draw_rectangle (drawable,
                      is_active ?
                      widget->style->fg_gc[GTK_STATE_SELECTED] :
                      widget->style->fg_gc[GTK_STATE_NORMAL],
                      FALSE,
                      winrect->x, winrect->y,
                      winrect->width - 1, winrect->height - 1);
}            

#define REALLY_SMALL -100000000
#define REALLY_BIG    100000000

static int
workspace_at_point (WnckPager *pager,
                    int        x,
                    int        y)
{
  int i;
  int n_spaces;

  n_spaces = wnck_screen_get_workspace_count (pager->priv->screen);
  
  i = 0;
  while (i < n_spaces)
    {
      GdkRectangle rect;
      
      get_workspace_rect (pager, i, &rect);

      /* Extend rect outside the widget itself */
      if (pager->priv->orientation == GTK_ORIENTATION_VERTICAL)
        {
          if (i == 0)
            rect.y = REALLY_SMALL;

          if (i == (n_spaces - 1))
            rect.height = REALLY_BIG;
          
          rect.x = REALLY_SMALL;
          rect.width = REALLY_BIG * 3;
        }
      else
        {
          if (i == 0)
            rect.x = REALLY_SMALL;

          if (i == (n_spaces - 1))
            rect.width = REALLY_BIG;
          
          rect.y = REALLY_SMALL;
          rect.height = REALLY_BIG * 3;
        }
      
      if (POINT_IN_RECT (x, y, rect))
        return i;

      ++i;
    }
  
  return 0;
}

static gboolean
wnck_pager_expose_event  (GtkWidget      *widget,
                          GdkEventExpose *event)
{
  WnckPager *pager;
  int i;
  int n_spaces;
  WnckWorkspace *active_space;
  
  pager = WNCK_PAGER (widget);

  n_spaces = wnck_screen_get_workspace_count (pager->priv->screen);
  active_space = wnck_screen_get_active_workspace (pager->priv->screen);
  
  i = 0;
  while (i < n_spaces)
    {
      GdkRectangle rect;
      GList *windows;
      GList *tmp;
      gboolean is_current;
      
      get_workspace_rect (pager, i, &rect);
      
      is_current = active_space &&
        i == wnck_workspace_get_number (active_space);

      if (is_current)
        gdk_draw_rectangle (widget->window,                            
                            widget->style->dark_gc[GTK_STATE_NORMAL],
                            TRUE,
                            rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2);
      
      windows = get_windows_for_workspace_in_bottom_to_top (pager->priv->screen,
                                                            wnck_workspace_get (i));
      
      tmp = windows;
      while (tmp != NULL)
        {
          WnckWindow *win = tmp->data;
          GdkRectangle winrect;

          if (pager->priv->dragging &&
              win == pager->priv->drag_window)
            {
              tmp = tmp->next;
              continue;
            }
          
          get_window_rect (win, &rect, &winrect);

          draw_window (widget->window,
                       widget,
                       win,
                       &winrect);
          
          tmp = tmp->next;
        }

      g_list_free (windows);
      
      gdk_draw_rectangle (widget->window,
                          widget->style->fg_gc[GTK_STATE_NORMAL],
                          FALSE,
                          rect.x, rect.y, rect.width - 1, rect.height - 1);
      
      ++i;
    }

  /* Draw the drag window */
  if (pager->priv->dragging)
    {
      GdkRectangle rect;
      GdkRectangle winrect;
      int dx, dy;
      
      i = workspace_at_point (pager,
                              pager->priv->drag_window_x,
                              pager->priv->drag_window_y);
      
      get_workspace_rect (pager, i, &rect);          
      get_window_rect (pager->priv->drag_window, &rect, &winrect);

      dx = (pager->priv->drag_window_x - rect.x) -
        pager->priv->drag_start_x_workspace_relative;
      dy = (pager->priv->drag_window_y - rect.y) -
        pager->priv->drag_start_y_workspace_relative;
      
      winrect.x += dx;
      winrect.y += dy;
      
      draw_window (widget->window,
                   widget,
                   pager->priv->drag_window,
                   &winrect);
    }
  
  return FALSE;
}

static gboolean
wnck_pager_button_press  (GtkWidget      *widget,
                          GdkEventButton *event)
{
  WnckPager *pager;
  int i;
  int n_spaces;
  
  pager = WNCK_PAGER (widget);

  n_spaces = wnck_screen_get_workspace_count (pager->priv->screen);
  
  i = 0;
  while (i < n_spaces)
    {
      GdkRectangle rect;

      get_workspace_rect (pager, i, &rect);

      if (POINT_IN_RECT (event->x, event->y, rect))
        {
          WnckWorkspace *space = wnck_workspace_get (i);

          if (space &&
              space != wnck_screen_get_active_workspace (pager->priv->screen))
            {
              if (event->button == 1)
                wnck_workspace_activate (space);
              goto workspace_search_out;
            }
          else if (space)
            {
              GList *windows;
              GList *tmp;
              
              windows =
                get_windows_for_workspace_in_bottom_to_top (pager->priv->screen,
                                                            space);

              /* clicks on top windows first */
              windows = g_list_reverse (windows);

              tmp = windows;
              while (tmp != NULL)
                {
                  WnckWindow *win = WNCK_WINDOW (tmp->data);
                  GdkRectangle winrect;

                  get_window_rect (win, &rect, &winrect);

                  if (POINT_IN_RECT (event->x, event->y, winrect))
                    {
                      if (event->button == 1)
                        {
                          // wnck_window_activate (win);
                          pager->priv->drag_window = win;
                          pager->priv->drag_start_x = event->x;
                          pager->priv->drag_start_y = event->y;
                          pager->priv->drag_start_x_workspace_relative =
                            event->x - rect.x;
                          pager->priv->drag_start_y_workspace_relative =
                            event->y - rect.y;
                        }
                      else if (event->button == 3)
                        {
                          wnck_pager_popup_action_menu (pager,
                                                        win,
                                                        event->button,
                                                        event->x,
                                                        event->y,
                                                        event->time);
                        }

                      goto window_search_out;
                    }
                  
                  tmp = tmp->next;
                }

            window_search_out:
              
              g_list_free (windows);
              goto workspace_search_out;
            }
        }
      
      ++i;
    }

 workspace_search_out:

  return TRUE;
}

static gboolean
wnck_pager_motion (GtkWidget        *widget,
                   GdkEventMotion   *event)
{
  WnckPager *pager;
  int x, y;

  pager = WNCK_PAGER (widget);

  gdk_window_get_pointer (widget->window, &x, &y, NULL);

  if (!pager->priv->dragging &&
      pager->priv->drag_window != NULL &&
      gtk_drag_check_threshold (widget,
                                pager->priv->drag_start_x,
                                pager->priv->drag_start_y,
                                x, y))
    pager->priv->dragging = TRUE;

  if (pager->priv->dragging)
    {
      gtk_widget_queue_draw (widget);
      pager->priv->drag_window_x = event->x;
      pager->priv->drag_window_y = event->y;
    }
}

static gboolean
wnck_pager_button_release (GtkWidget        *widget,
                           GdkEventButton   *event)
{
  WnckPager *pager;

  pager = WNCK_PAGER (widget);

  if (event->button == 1 && pager->priv->dragging)
    {
      int i;
      WnckWorkspace *space;
      
      i = workspace_at_point (pager,
                              event->x,
                              event->y);

      space = wnck_workspace_get (i);

      if (space)
        wnck_window_move_to_workspace (pager->priv->drag_window,
                                       space);
      
      wnck_pager_clear_drag (pager);
    }
  else if (event->button == 1 && pager->priv->drag_window)
    {
      wnck_window_activate (pager->priv->drag_window);
      wnck_pager_clear_drag (pager);
    }

  return FALSE;
}

static gboolean
wnck_pager_focus (GtkWidget        *widget,
                  GtkDirectionType  direction)
{
  WnckPager *pager;

  pager = WNCK_PAGER (widget);
  
  return GTK_WIDGET_CLASS (parent_class)->focus (widget, direction);
}

GtkWidget*
wnck_pager_new (WnckScreen *screen)
{
  WnckPager *pager;
  
  pager = g_object_new (WNCK_TYPE_PAGER, NULL);

  wnck_pager_connect_screen (pager, screen);

  return GTK_WIDGET (pager);
}

void
wnck_pager_set_orientation (WnckPager     *pager,
                            GtkOrientation orientation)
{
  g_return_if_fail (WNCK_IS_PAGER (pager));

  if (pager->priv->orientation == orientation)
    return;

  pager->priv->orientation = orientation;
  gtk_widget_queue_resize (GTK_WIDGET (pager));
}

static void
active_window_changed_callback    (WnckScreen      *screen,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
active_workspace_changed_callback (WnckScreen      *screen,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
window_stacking_changed_callback  (WnckScreen      *screen,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
window_opened_callback            (WnckScreen      *screen,
                                   WnckWindow      *window,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);

  wnck_pager_connect_window (pager, window);
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
window_closed_callback            (WnckScreen      *screen,
                                   WnckWindow      *window,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);

  if (pager->priv->action_window == window)
    wnck_pager_clear_action_menu (pager);

  if (pager->priv->drag_window == window)
    wnck_pager_clear_drag (pager);
  
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
workspace_created_callback        (WnckScreen      *screen,
                                   WnckWorkspace   *space,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  gtk_widget_queue_resize (GTK_WIDGET (pager));
}

static void
workspace_destroyed_callback      (WnckScreen      *screen,
                                   WnckWorkspace   *space,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  gtk_widget_queue_resize (GTK_WIDGET (pager));
}

static void
application_opened_callback       (WnckScreen      *screen,
                                   WnckApplication *app,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
}

static void
application_closed_callback       (WnckScreen      *screen,
                                   WnckApplication *app,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
}

static void
window_name_changed_callback      (WnckWindow      *window,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
window_state_changed_callback     (WnckWindow      *window,
                                   WnckWindowState  changed,
                                   WnckWindowState  new,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
window_workspace_changed_callback (WnckWindow      *window,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
window_icon_changed_callback      (WnckWindow      *window,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
window_geometry_changed_callback  (WnckWindow      *window,
                                   gpointer         data)
{
  WnckPager *pager = WNCK_PAGER (data);
  
  gtk_widget_queue_draw (GTK_WIDGET (pager));
}

static void
wnck_pager_connect_screen (WnckPager  *pager,
                           WnckScreen *screen)
{
  int i;
  guint *c;
  GList *tmp;
  
  g_return_if_fail (pager->priv->screen == NULL);
  
  pager->priv->screen = screen;

  tmp = wnck_screen_get_windows (screen);
  while (tmp != NULL)
    {
      wnck_pager_connect_window (pager, WNCK_WINDOW (tmp->data));
      tmp = tmp->next;
    }
  
  i = 0;
  c = pager->priv->screen_connections;
  
  c[i] = g_signal_connect (G_OBJECT (screen), "active_window_changed",
                           G_CALLBACK (active_window_changed_callback),
                           pager);
  ++i;
  
  c[i] = g_signal_connect (G_OBJECT (screen), "active_workspace_changed",
                           G_CALLBACK (active_workspace_changed_callback),
                           pager);
  ++i;  

  c[i] = g_signal_connect (G_OBJECT (screen), "window_stacking_changed",
                           G_CALLBACK (window_stacking_changed_callback),
                           pager);
  ++i;

  c[i] = g_signal_connect (G_OBJECT (screen), "window_opened",
                           G_CALLBACK (window_opened_callback),
                           pager);
  ++i;

  c[i] = g_signal_connect (G_OBJECT (screen), "window_closed",
                           G_CALLBACK (window_closed_callback),
                           pager);
  ++i;

  c[i] = g_signal_connect (G_OBJECT (screen), "workspace_created",
                           G_CALLBACK (workspace_created_callback),
                           pager);
  ++i;

  c[i] = g_signal_connect (G_OBJECT (screen), "workspace_destroyed",
                           G_CALLBACK (workspace_destroyed_callback),
                           pager);
  ++i;

  c[i] = g_signal_connect (G_OBJECT (screen), "application_opened",
                           G_CALLBACK (application_opened_callback),
                           pager);
  ++i;  

  c[i] = g_signal_connect (G_OBJECT (screen), "application_closed",
                           G_CALLBACK (application_closed_callback),
                           pager);
  ++i;

  g_assert (i == N_SCREEN_CONNECTIONS);
}

static void
wnck_pager_connect_window (WnckPager  *pager,
                           WnckWindow *window)
{
  g_signal_connect_object (G_OBJECT (window), "name_changed",
                           G_CALLBACK (window_name_changed_callback),
                           pager, 0);
  g_signal_connect_object (G_OBJECT (window), "state_changed",
                           G_CALLBACK (window_state_changed_callback),
                           pager, 0);
  g_signal_connect_object (G_OBJECT (window), "workspace_changed",
                           G_CALLBACK (window_workspace_changed_callback),
                           pager, 0);
  g_signal_connect_object (G_OBJECT (window), "icon_changed",
                           G_CALLBACK (window_icon_changed_callback),
                           pager, 0);
  g_signal_connect_object (G_OBJECT (window), "geometry_changed",
                           G_CALLBACK (window_geometry_changed_callback),
                           pager, 0);
}

static void
wnck_pager_disconnect_screen (WnckPager  *pager)
{
  int i;

  if (pager->priv->screen == NULL)
    return;
  
  i = 0;
  while (i < N_SCREEN_CONNECTIONS)
    {
      if (pager->priv->screen_connections[i] != 0)
        g_signal_handler_disconnect (G_OBJECT (pager->priv->screen),
                                     pager->priv->screen_connections[i]);

      pager->priv->screen_connections[i] = 0;
      
      ++i;
    }

  pager->priv->screen = NULL;
}

static void
popup_position_func (GtkMenu   *menu,
                     gint      *x,
                     gint      *y,
                     gboolean  *push_in,
                     gpointer	user_data)
{
  WnckPager *pager;
  GtkWidget *widget;
  gint root_x, root_y;
  GtkRequisition req;
  
  pager = WNCK_PAGER (user_data);
  widget = GTK_WIDGET (pager);
  
  gtk_widget_size_request (pager->priv->action_menu, &req);

  gdk_window_get_origin (widget->window, &root_x, &root_y);
  root_x += pager->priv->action_click_x;
  root_y += pager->priv->action_click_y;
  
  /* Ensure sanity */
  *x = CLAMP (*x, root_x, (root_x + widget->allocation.width));
  *y = CLAMP (*y, root_y, (root_y + widget->allocation.height));

  *x = CLAMP (*x, 0, MAX (0, gdk_screen_width () - req.width));
  *y = CLAMP (*y, 0, MAX (0, gdk_screen_height () - req.height));
}

static void
wnck_pager_popup_action_menu (WnckPager  *pager,
                              WnckWindow *window,
                              int         button,
                              int         x,
                              int         y,
                              guint32     timestamp)
{
  wnck_pager_clear_action_menu (pager);

  pager->priv->action_window = window;
  pager->priv->action_menu = wnck_create_window_action_menu (window);
  pager->priv->action_click_x = x;
  pager->priv->action_click_y = y;
  
  gtk_menu_popup (GTK_MENU (pager->priv->action_menu),
                  NULL, NULL,
                  popup_position_func, pager,
                  button, timestamp);
}

static void
wnck_pager_clear_action_menu (WnckPager  *pager)
{
  if (pager->priv->action_menu)
    {
      gtk_widget_destroy (pager->priv->action_menu);
      pager->priv->action_menu = NULL;
      pager->priv->action_window = NULL;
    }
}

static void
wnck_pager_clear_drag (WnckPager *pager)
{
  if (pager->priv->dragging)
    gtk_widget_queue_draw (GTK_WIDGET (pager));

  pager->priv->dragging = FALSE;
  pager->priv->drag_window = NULL;
  pager->priv->drag_start_x = -1;
  pager->priv->drag_start_y = -1;
  pager->priv->drag_window_x = -1;
  pager->priv->drag_window_y = -1;
}