/*
 * vinagre-mdns.c
 * This file is part of vinagre
 *
 * Copyright (C) Jonh Wendell 2008 <wendell@bani.com.br>
 * 
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <avahi-gobject/ga-service-browser.h>
#include <avahi-gobject/ga-service-resolver.h>
#include <avahi-common/malloc.h>
#include <glib/gi18n.h>

#include "vinagre-mdns.h"
#include "vinagre-connection.h"
#include "vinagre-bookmarks-entry.h"
#include "vinagre-plugins-engine.h"
#include "vinagre-protocol.h"

typedef struct
{
  GaServiceBrowser *browser;
  VinagreProtocol  *protocol;
} BrowserEntry;

struct _VinagreMdnsPrivate
{
  GSList           *entries;
  GaClient         *client;
  GHashTable       *browsers;
};

enum
{
  MDNS_CHANGED,
  LAST_SIGNAL
};

G_DEFINE_TYPE (VinagreMdns, vinagre_mdns, G_TYPE_OBJECT);

static VinagreMdns *mdns_singleton = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

static void
mdns_resolver_found (GaServiceResolver *resolver,
                     AvahiIfIndex         iface,
                     GaProtocol           proto,
                     gchar               *name,
                     gchar               *type,
                     gchar               *domain,
                     gchar               *host_name,
                     AvahiAddress        *address,
                     gint                 port,
                     AvahiStringList     *txt,
                     GaLookupResultFlags flags,
                     VinagreMdns         *mdns)
{
  VinagreConnection     *conn;
  VinagreBookmarksEntry *entry;
  BrowserEntry          *b_entry;
  char                  a[AVAHI_ADDRESS_STR_MAX], *u = NULL;
  GSList *l;

  b_entry = g_hash_table_lookup (mdns->priv->browsers, type);
  if (!b_entry)
    {
      g_warning ("Service name not found in mDNS resolver hash table. This probably is a bug somewhere.");
      return;
    }

  for (l = mdns->priv->entries; l; l = l->next)
    {
      VinagreBookmarksEntry *entry = VINAGRE_BOOKMARKS_ENTRY (l->data);
      if (strcmp (vinagre_connection_get_name (vinagre_bookmarks_entry_get_conn (entry)), name) == 0)
	{
	  goto out;
	}
    }

  for (; txt; txt = txt->next)
    {
      char *key, *value;

      if (avahi_string_list_get_pair (txt, &key, &value, NULL) < 0)
	break;

      if (strcmp(key, "u") == 0)
	u = g_strdup (value);

      avahi_free (key);
      avahi_free (value);
    }

  avahi_address_snprint (a, sizeof(a), address);
  conn = vinagre_protocol_new_connection (b_entry->protocol);
  g_object_set (conn,
                "name", name,
                "port", port,
                "host", a,
                "username", u,
                NULL);
  entry = vinagre_bookmarks_entry_new_conn (conn);
  g_object_unref (conn);

  mdns->priv->entries = g_slist_insert_sorted (mdns->priv->entries,
					       entry,
					       (GCompareFunc)vinagre_bookmarks_entry_compare);

  g_signal_emit (mdns, signals[MDNS_CHANGED], 0);

out:
  g_object_unref (resolver);
  g_free (u);
}

static void
mdns_resolver_failure (GaServiceResolver *resolver,
                       GError            *error,
                       VinagreMdns       *mdns)
{
  g_warning ("%s", error->message);
  g_object_unref (resolver);
}

static void
mdns_browser_new_cb (GaServiceBrowser   *browser,
                     AvahiIfIndex        iface,
                     GaProtocol          proto,
                     gchar              *name,
                     gchar              *type,
                     gchar              *domain,
                     GaLookupResultFlags flags,
                     VinagreMdns        *mdns)
{
  GaServiceResolver *resolver;
  GError *error = NULL;

  resolver = ga_service_resolver_new (iface,
                                      proto,
                                      name,
                                      type,
                                      domain,
                                      GA_PROTOCOL_UNSPEC,
                                      GA_LOOKUP_NO_FLAGS);

  g_signal_connect (resolver,
                    "found",
                    G_CALLBACK (mdns_resolver_found),
                    mdns);
  g_signal_connect (resolver,
                    "failure",
                    G_CALLBACK (mdns_resolver_failure),
                    mdns);

  if (!ga_service_resolver_attach (resolver,
                                   mdns->priv->client,
                                   &error))
    {
      g_warning (_("Failed to resolve avahi hostname: %s\n"), error->message);
      g_error_free (error);
    }
}

static void
mdns_browser_del_cb (GaServiceBrowser   *browser,
                     AvahiIfIndex        iface,
                     GaProtocol          proto,
                     gchar              *name,
                     gchar              *type,
                     gchar              *domain,
                     GaLookupResultFlags flags,
                     VinagreMdns        *mdns)
{
  GSList *l;

  for (l = mdns->priv->entries; l; l = l->next)
    {
      VinagreBookmarksEntry *entry = VINAGRE_BOOKMARKS_ENTRY (l->data);
      if (strcmp (vinagre_connection_get_name (vinagre_bookmarks_entry_get_conn (entry)), name) == 0)
	{
	  mdns->priv->entries = g_slist_remove (mdns->priv->entries, entry);
	  g_object_unref (entry);
	  g_signal_emit (mdns, signals[MDNS_CHANGED], 0);
	  return;
	}
    }
}

static void
destroy_browser_entry (BrowserEntry *entry)
{
  g_object_unref (entry->browser);
  g_object_unref (entry->protocol);
  g_slice_free (BrowserEntry, entry);
}

static void
vinagre_mdns_add_service (VinagreMdns     *mdns,
			  VinagreProtocol *protocol)
{
  GaServiceBrowser *browser;
  GError           *error = NULL;
  const gchar      *service;
  BrowserEntry     *entry;

  service = vinagre_protocol_get_mdns_service (protocol);
  if (!service)
    return;

  entry = g_hash_table_lookup (mdns->priv->browsers, service);
  if (entry)
    {
      g_warning (_("The service %s was already registered by another plugin."),
		 service);
      return;
    }

  browser = ga_service_browser_new ((gchar *)service);
  if (!browser)
    {
      g_warning (_("Failed to add mDNS browser for service %s."), service);
      return;
    }
  g_signal_connect (browser,
                    "new-service",
                    G_CALLBACK (mdns_browser_new_cb),
                    mdns);
  g_signal_connect (browser,
                    "removed-service",
                    G_CALLBACK (mdns_browser_del_cb),
                    mdns);

  if (!ga_service_browser_attach (browser,
                                  mdns->priv->client,
                                  &error))
    {
        /* Translators: "Browse for hosts" means the ability to find/locate some remote hosts [with the VNC service enabled] in the local network */
        g_warning (_("Failed to browse for hosts: %s\n"), error->message);
        g_error_free (error);
        return;
    }

  entry = g_slice_new (BrowserEntry);
  entry->browser = g_object_ref (browser);
  entry->protocol = g_object_ref (protocol);
  g_hash_table_insert (mdns->priv->browsers, (gpointer)service, entry);
}

static void
vinagre_mdns_remove_entries_by_protocol (VinagreMdns *mdns, const gchar *protocol)
{
  GSList *l, *next;

  for (l = mdns->priv->entries; l; l = next)
    {
      VinagreBookmarksEntry *entry = VINAGRE_BOOKMARKS_ENTRY (l->data);
      next = l->next;
      if (strcmp (vinagre_connection_get_protocol (vinagre_bookmarks_entry_get_conn (entry)), protocol) == 0)
	{
	  mdns->priv->entries = g_slist_remove (mdns->priv->entries, entry);
	  g_object_unref (entry);
	}
    }
}

static void
protocol_added_cb (VinagrePluginsEngine *engine,
		   VinagreProtocol      *protocol,
		   VinagreMdns          *mdns)
{
  vinagre_mdns_add_service (mdns, protocol);
}

static void
protocol_removed_cb (VinagrePluginsEngine *engine,
		     VinagreProtocol      *protocol,
		     VinagreMdns          *mdns)
{
  const gchar *service;

  service = vinagre_protocol_get_mdns_service (protocol);
  if (!service)
    return;

  vinagre_mdns_remove_entries_by_protocol (mdns,
					   vinagre_protocol_get_protocol (protocol));
  g_hash_table_remove (mdns->priv->browsers, (gconstpointer)service);
}

static void
vinagre_mdns_init (VinagreMdns *mdns)
{
  GError *error = NULL;
  VinagrePluginsEngine *engine;
  VinagreProtocol *protocol;
  GHashTable *protocols;
  GHashTableIter iter;

  mdns->priv = G_TYPE_INSTANCE_GET_PRIVATE (mdns, VINAGRE_TYPE_MDNS, VinagreMdnsPrivate);

  mdns->priv->entries = NULL;
  mdns->priv->browsers = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)destroy_browser_entry);
  mdns->priv->client = ga_client_new (GA_CLIENT_FLAG_NO_FLAGS);

  if (!ga_client_start (mdns->priv->client, &error))
    {
        g_warning (_("Failed to initialize mDNS browser: %s\n"), error->message);
        g_error_free (error);
        g_object_unref (mdns->priv->client);
        mdns->priv->client = NULL;
        return;
    }

  engine = vinagre_plugins_engine_get_default ();
  protocols = vinagre_plugins_engine_get_plugins_by_protocol (engine);
  g_hash_table_iter_init (&iter, protocols);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&protocol))
    {
      vinagre_mdns_add_service (mdns, protocol);
    }

  g_signal_connect (engine,
		    "protocol-added",
		    G_CALLBACK (protocol_added_cb),
		    mdns);
  g_signal_connect (engine,
		    "protocol-removed",
		    G_CALLBACK (protocol_removed_cb),
		    mdns);
}

static void
vinagre_mdns_dispose (GObject *object)
{
  VinagreMdns *mdns = VINAGRE_MDNS (object);

  if (mdns->priv->browsers)
    {
      g_hash_table_unref (mdns->priv->browsers);
      mdns->priv->browsers = NULL;
    }

  if (mdns->priv->client)
    {
      g_object_unref (mdns->priv->client);
      mdns->priv->client = NULL;
    }

  if (mdns->priv->entries)
    {
      g_slist_free_full (mdns->priv->entries, g_object_unref);
      mdns->priv->entries = NULL;
    }

  G_OBJECT_CLASS (vinagre_mdns_parent_class)->dispose (object);
}

static void
vinagre_mdns_class_init (VinagreMdnsClass *klass)
{
  GObjectClass* object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (VinagreMdnsPrivate));

  object_class->dispose = vinagre_mdns_dispose;

  signals[MDNS_CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (VinagreMdnsClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

}

VinagreMdns *
vinagre_mdns_get_default (void)
{
  if (G_UNLIKELY (!mdns_singleton))
    mdns_singleton = VINAGRE_MDNS (g_object_new (VINAGRE_TYPE_MDNS,
                                                 NULL));
  return mdns_singleton;
}

GSList *
vinagre_mdns_get_all (VinagreMdns *mdns)
{
  g_return_val_if_fail (VINAGRE_IS_MDNS (mdns), NULL);

  return mdns->priv->entries;
}

/* vim: set ts=8: */
