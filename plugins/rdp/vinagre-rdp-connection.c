/*
 * vinagre-rdp-connection.c
 * Child class of abstract VinagreConnection, specific to RDP protocol
 * This file is part of vinagre
 *
 * Copyright (C) 2010 - Jonh Wendell <wendell@bani.com.br>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib/gi18n.h>
#include <vinagre/vinagre-cache-prefs.h>
#include "vinagre-rdp-connection.h"

#include "vinagre-vala.h"

struct _VinagreRdpConnectionPrivate
{
  gboolean scaling;
};

enum
{
  PROP_0,
  PROP_SCALING,
};

#define VINAGRE_RDP_CONNECTION_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), VINAGRE_TYPE_RDP_CONNECTION, VinagreRdpConnectionPrivate))
G_DEFINE_TYPE (VinagreRdpConnection, vinagre_rdp_connection, VINAGRE_TYPE_CONNECTION);

static void
vinagre_rdp_connection_init (VinagreRdpConnection *conn)
{
  conn->priv = G_TYPE_INSTANCE_GET_PRIVATE (conn, VINAGRE_TYPE_RDP_CONNECTION, VinagreRdpConnectionPrivate);
}

static void
vinagre_rdp_connection_constructed (GObject *object)
{
  vinagre_connection_set_protocol (VINAGRE_CONNECTION (object), "rdp");
}

static void
vinagre_rdp_connection_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  VinagreRdpConnection *conn;

  g_return_if_fail (VINAGRE_IS_RDP_CONNECTION (object));

  conn = VINAGRE_RDP_CONNECTION (object);

  switch (prop_id)
    {
      case PROP_SCALING:
        vinagre_rdp_connection_set_scaling (conn, g_value_get_boolean (value));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
vinagre_rdp_connection_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  VinagreRdpConnection *conn;

  g_return_if_fail (VINAGRE_IS_RDP_CONNECTION (object));

  conn = VINAGRE_RDP_CONNECTION (object);

  switch (prop_id)
    {
      case PROP_SCALING:
        g_value_set_boolean (value, conn->priv->scaling);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
rdp_fill_writer (VinagreConnection *conn, xmlTextWriter *writer)
{
  VinagreRdpConnection *rdp_conn = VINAGRE_RDP_CONNECTION (conn);
  VINAGRE_CONNECTION_CLASS (vinagre_rdp_connection_parent_class)->impl_fill_writer (conn, writer);

  xmlTextWriterWriteFormatElement (writer, BAD_CAST "scaling", "%d", rdp_conn->priv->scaling);
}

static void
rdp_parse_item (VinagreConnection *conn, xmlNode *root)
{
  xmlNode *curr;
  xmlChar *s_value;
  VinagreRdpConnection *rdp_conn = VINAGRE_RDP_CONNECTION (conn);

  VINAGRE_CONNECTION_CLASS (vinagre_rdp_connection_parent_class)->impl_parse_item (conn, root);

  for (curr = root->children; curr; curr = curr->next)
    {
      s_value = xmlNodeGetContent (curr);

      if (!xmlStrcmp(curr->name, BAD_CAST "scaling"))
        {
          vinagre_rdp_connection_set_scaling (rdp_conn, vinagre_utils_parse_boolean ((const gchar *) s_value));
        }

      xmlFree (s_value);
    }
}

static void
rdp_parse_options_widget (VinagreConnection *conn, GtkWidget *widget)
{
  const gchar *text;
  GtkWidget   *u_entry, *d_entry, *spin_button, *scaling_button;
  gboolean     scaling;
  guint        width, height;

  d_entry = g_object_get_data (G_OBJECT (widget), "domain_entry");
  if (!d_entry)
    {
      g_warning ("Wrong widget passed to rdp_parse_options_widget()");
      return;
    }

  text = gtk_entry_get_text (GTK_ENTRY (d_entry));
  vinagre_cache_prefs_set_string  ("rdp-connection", "domain", text);

  g_object_set (conn,
		"domain", text != NULL && *text != '\0' ? text : NULL,
		NULL);


  u_entry = g_object_get_data (G_OBJECT (widget), "username_entry");
  if (!u_entry)
    {
      g_warning ("Wrong widget passed to rdp_parse_options_widget()");
      return;
    }

  vinagre_cache_prefs_set_string  ("rdp-connection", "username", gtk_entry_get_text (GTK_ENTRY (u_entry)));

  g_object_set (conn,
		"username", gtk_entry_get_text (GTK_ENTRY (u_entry)),
		NULL);


  spin_button = g_object_get_data (G_OBJECT (widget), "width_spin_button");
  if (!spin_button)
    {
      g_warning ("Wrong widget passed to rdp_parse_options_widget()");
      return;
    }

  width = (guint) gtk_spin_button_get_value (GTK_SPIN_BUTTON (spin_button));

  vinagre_cache_prefs_set_integer  ("rdp-connection", "width", width);

  vinagre_connection_set_width (conn, width);


  spin_button = g_object_get_data (G_OBJECT (widget), "height_spin_button");
  if (!spin_button)
    {
      g_warning ("Wrong widget passed to rdp_parse_options_widget()");
      return;
    }

  height = (guint) gtk_spin_button_get_value (GTK_SPIN_BUTTON (spin_button));

  vinagre_cache_prefs_set_integer  ("rdp-connection", "height", height);

  vinagre_connection_set_height (conn, height);


  scaling_button = g_object_get_data (G_OBJECT (widget), "scaling");
  if (!scaling_button)
    {
      g_warning ("Wrong widget passed to rdp_parse_options_widget()");
      return;
    }

  scaling = (gboolean) gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scaling_button));

  vinagre_cache_prefs_set_boolean ("rdp-connection", "scaling", scaling);

  g_object_set (conn,
                "scaling", scaling,
                NULL);
}

static void
vinagre_rdp_connection_class_init (VinagreRdpConnectionClass *klass)
{
  GObjectClass* object_class = G_OBJECT_CLASS (klass);
  VinagreConnectionClass* parent_class = VINAGRE_CONNECTION_CLASS (klass);

  g_type_class_add_private (klass, sizeof (VinagreRdpConnectionPrivate));

  object_class->set_property = vinagre_rdp_connection_set_property;
  object_class->get_property = vinagre_rdp_connection_get_property;
  object_class->constructed  = vinagre_rdp_connection_constructed;

  parent_class->impl_fill_writer = rdp_fill_writer;
  parent_class->impl_parse_item  = rdp_parse_item;
  parent_class->impl_parse_options_widget = rdp_parse_options_widget;

  g_object_class_install_property (object_class,
                                   PROP_SCALING,
                                   g_param_spec_boolean ("scaling",
                                                         "Use scaling",
                                                         "Whether to use scaling on this connection",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT |
                                                         G_PARAM_STATIC_STRINGS));

}

VinagreConnection *
vinagre_rdp_connection_new (void)
{
  return VINAGRE_CONNECTION (g_object_new (VINAGRE_TYPE_RDP_CONNECTION, NULL));
}

void
vinagre_rdp_connection_set_scaling (VinagreRdpConnection *conn,
                                    gboolean              scaling)
{
  g_return_if_fail (VINAGRE_IS_RDP_CONNECTION (conn));

  conn->priv->scaling = scaling;
}

gboolean
vinagre_rdp_connection_get_scaling (VinagreRdpConnection *conn)
{
  g_return_val_if_fail (VINAGRE_IS_RDP_CONNECTION (conn), FALSE);

  return conn->priv->scaling;
}


/* vim: set ts=8: */
