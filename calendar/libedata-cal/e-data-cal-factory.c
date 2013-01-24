/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar factory
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 *
 * Authors:
 *   Federico Mena-Quintero <federico@ximian.com>
 *   JP Rosevear <jpr@ximian.com>
 *   Ross Burton <ross@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <config.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>

/* Private D-Bus classes. */
#include <e-dbus-calendar-factory.h>

#include "e-cal-backend.h"
#include "e-cal-backend-factory.h"
#include "e-data-cal.h"
#include "e-data-cal-factory.h"

#include <libical/ical.h>

#define d(x)

#define E_DATA_CAL_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_CAL_FACTORY, EDataCalFactoryPrivate))

struct _EDataCalFactoryPrivate {
	ESourceRegistry *registry;
	EDBusCalendarFactory *dbus_factory;

	GMutex calendars_lock;
	/* A hash of object paths for calendar URIs to EDataCals */
	GHashTable *calendars;

	GMutex connections_lock;
	/* This is a hash of client addresses to GList* of EDataCals */
	GHashTable *connections;
};

enum {
	PROP_0,
	PROP_REGISTRY
};

/* Forward Declarations */
static void	e_data_cal_factory_initable_init
						(GInitableIface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EDataCalFactory,
	e_data_cal_factory,
	E_TYPE_DATA_FACTORY,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_cal_factory_initable_init))

static EBackend *
data_cal_factory_ref_backend (EDataFactory *factory,
                              ESource *source,
                              const gchar *extension_name,
                              const gchar *type_string,
                              GError **error)
{
	EBackend *backend;
	ESourceBackend *extension;
	gchar *backend_name;
	gchar *hash_key;

	extension = e_source_get_extension (source, extension_name);
	backend_name = e_source_backend_dup_backend_name (extension);

	if (backend_name == NULL || *backend_name == '\0') {
		g_set_error (
			error, E_DATA_CAL_ERROR, NoSuchCal,
			_("No backend name in source '%s'"),
			e_source_get_display_name (source));
		g_free (backend_name);
		return NULL;
	}

	hash_key = g_strdup_printf ("%s:%s", backend_name, type_string);
	backend = e_data_factory_ref_backend (factory, hash_key, source);
	g_free (hash_key);

	if (backend == NULL)
		g_set_error (
			error, E_DATA_CAL_ERROR, NoSuchCal,
			_("Invalid backend name '%s' in source '%s'"),
			backend_name, e_source_get_display_name (source));

	g_free (backend_name);

	return backend;
}

static gchar *
construct_cal_factory_path (void)
{
	static volatile gint counter = 1;

	g_atomic_int_inc (&counter);

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/Calendar/%d/%u",
		getpid (), counter);
}

static gboolean
remove_dead_calendar_cb (gpointer path,
                         gpointer calendar,
                         gpointer dead_calendar)
{
	return calendar == dead_calendar;
}

static void
calendar_freed_cb (EDataCalFactory *factory,
                   GObject *dead)
{
	EDataCalFactoryPrivate *priv = factory->priv;
	GHashTableIter iter;
	gpointer hkey, hvalue;

	d (g_debug ("in factory %p (%p) is dead", factory, dead));

	g_mutex_lock (&priv->calendars_lock);
	g_mutex_lock (&priv->connections_lock);

	g_hash_table_foreach_remove (
		priv->calendars, remove_dead_calendar_cb, dead);

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GList *calendars = hvalue;

		if (g_list_find (calendars, dead)) {
			calendars = g_list_remove (calendars, dead);
			if (calendars != NULL)
				g_hash_table_insert (
					priv->connections,
					g_strdup (hkey), calendars);
			else
				g_hash_table_remove (priv->connections, hkey);

			break;
		}
	}

	g_mutex_unlock (&priv->connections_lock);
	g_mutex_unlock (&priv->calendars_lock);

	e_dbus_server_release (E_DBUS_SERVER (factory));
}

static gchar *
data_cal_factory_open (EDataCalFactory *factory,
                       GDBusConnection *connection,
                       const gchar *sender,
                       const gchar *uid,
                       const gchar *extension_name,
                       const gchar *type_string,
                       GError **error)
{
	EDataCal *calendar;
	EBackend *backend;
	ESourceRegistry *registry;
	ESource *source;
	gchar *object_path;
	GList *list;

	if (uid == NULL || *uid == '\0') {
		g_set_error (
			error, E_DATA_CAL_ERROR, NoSuchCal,
			_("Missing source UID"));
		return NULL;
	}

	registry = e_data_cal_factory_get_registry (factory);
	source = e_source_registry_ref_source (registry, uid);

	if (source == NULL) {
		g_set_error (
			error, E_DATA_CAL_ERROR, NoSuchCal,
			_("No such source for UID '%s'"), uid);
		return NULL;
	}

	backend = data_cal_factory_ref_backend (
		E_DATA_FACTORY (factory), source,
		extension_name, type_string, error);

	g_object_unref (source);

	if (backend == NULL)
		return NULL;

	e_dbus_server_hold (E_DBUS_SERVER (factory));

	object_path = construct_cal_factory_path ();

	calendar = e_data_cal_new (
		E_CAL_BACKEND (backend),
		connection, object_path, error);

	if (calendar != NULL) {
		g_mutex_lock (&factory->priv->calendars_lock);
		g_hash_table_insert (
			factory->priv->calendars,
			g_strdup (object_path), calendar);
		g_mutex_unlock (&factory->priv->calendars_lock);

		e_cal_backend_add_client (E_CAL_BACKEND (backend), calendar);

		g_object_weak_ref (
			G_OBJECT (calendar), (GWeakNotify)
			calendar_freed_cb, factory);

		/* Update the hash of open connections. */
		g_mutex_lock (&factory->priv->connections_lock);
		list = g_hash_table_lookup (
			factory->priv->connections, sender);
		list = g_list_prepend (list, calendar);
		g_hash_table_insert (
			factory->priv->connections,
			g_strdup (sender), list);
		g_mutex_unlock (&factory->priv->connections_lock);

	} else {
		g_free (object_path);
		object_path = NULL;
	}

	g_object_unref (backend);

	return object_path;
}

static gboolean
data_cal_factory_handle_open_calendar_cb (EDBusCalendarFactory *interface,
                                          GDBusMethodInvocation *invocation,
                                          const gchar *uid,
                                          EDataCalFactory *factory)
{
	GDBusConnection *connection;
	const gchar *sender;
	gchar *object_path;
	GError *error = NULL;

	connection = g_dbus_method_invocation_get_connection (invocation);
	sender = g_dbus_method_invocation_get_sender (invocation);

	object_path = data_cal_factory_open (
		factory, connection, sender, uid,
		E_SOURCE_EXTENSION_CALENDAR, "VEVENT", &error);

	if (object_path != NULL) {
		e_dbus_calendar_factory_complete_open_calendar (
			interface, invocation, object_path);
		g_free (object_path);
	} else {
		g_return_val_if_fail (error != NULL, FALSE);
		g_dbus_method_invocation_take_error (invocation, error);
	}

	return TRUE;
}

static gboolean
data_cal_factory_handle_open_task_list_cb (EDBusCalendarFactory *interface,
                                           GDBusMethodInvocation *invocation,
                                           const gchar *uid,
                                           EDataCalFactory *factory)
{
	GDBusConnection *connection;
	const gchar *sender;
	gchar *object_path;
	GError *error = NULL;

	connection = g_dbus_method_invocation_get_connection (invocation);
	sender = g_dbus_method_invocation_get_sender (invocation);

	object_path = data_cal_factory_open (
		factory, connection, sender, uid,
		E_SOURCE_EXTENSION_TASK_LIST, "VTODO", &error);

	if (object_path != NULL) {
		e_dbus_calendar_factory_complete_open_task_list (
			interface, invocation, object_path);
		g_free (object_path);
	} else {
		g_return_val_if_fail (error != NULL, FALSE);
		g_dbus_method_invocation_take_error (invocation, error);
	}

	return TRUE;
}

static gboolean
data_cal_factory_handle_open_memo_list_cb (EDBusCalendarFactory *interface,
                                           GDBusMethodInvocation *invocation,
                                           const gchar *uid,
                                           EDataCalFactory *factory)
{
	GDBusConnection *connection;
	const gchar *sender;
	gchar *object_path;
	GError *error = NULL;

	connection = g_dbus_method_invocation_get_connection (invocation);
	sender = g_dbus_method_invocation_get_sender (invocation);

	object_path = data_cal_factory_open (
		factory, connection, sender, uid,
		E_SOURCE_EXTENSION_MEMO_LIST, "VJOURNAL", &error);

	if (object_path != NULL) {
		e_dbus_calendar_factory_complete_open_memo_list (
			interface, invocation, object_path);
		g_free (object_path);
	} else {
		g_return_val_if_fail (error != NULL, FALSE);
		g_dbus_method_invocation_take_error (invocation, error);
	}

	return TRUE;
}

static void
remove_data_cal_cb (EDataCal *data_cal)
{
	ECalBackend *backend;

	g_return_if_fail (data_cal != NULL);

	backend = e_data_cal_get_backend (data_cal);
	e_cal_backend_remove_client (backend, data_cal);

	g_object_unref (data_cal);
}

static void
data_cal_factory_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_REGISTRY:
			g_value_set_object (
				value,
				e_data_cal_factory_get_registry (
				E_DATA_CAL_FACTORY (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_cal_factory_dispose (GObject *object)
{
	EDataCalFactoryPrivate *priv;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (object);

	if (priv->registry != NULL) {
		g_object_unref (priv->registry);
		priv->registry = NULL;
	}

	if (priv->dbus_factory != NULL) {
		g_object_unref (priv->dbus_factory);
		priv->dbus_factory = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_cal_factory_parent_class)->dispose (object);
}

static void
data_cal_factory_finalize (GObject *object)
{
	EDataCalFactoryPrivate *priv;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (object);

	g_hash_table_destroy (priv->calendars);
	g_hash_table_destroy (priv->connections);

	g_mutex_clear (&priv->calendars_lock);
	g_mutex_clear (&priv->connections_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_cal_factory_parent_class)->finalize (object);
}

static void
data_cal_factory_bus_acquired (EDBusServer *server,
                               GDBusConnection *connection)
{
	EDataCalFactoryPrivate *priv;
	GError *error = NULL;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (server);

	g_dbus_interface_skeleton_export (
		G_DBUS_INTERFACE_SKELETON (priv->dbus_factory),
		connection,
		"/org/gnome/evolution/dataserver/CalendarFactory",
		&error);

	if (error != NULL) {
		g_error (
			"Failed to export CalendarFactory interface: %s",
			error->message);
		g_assert_not_reached ();
	}

	/* Chain up to parent's bus_acquired() method. */
	E_DBUS_SERVER_CLASS (e_data_cal_factory_parent_class)->
		bus_acquired (server, connection);
}

static void
data_cal_factory_bus_name_lost (EDBusServer *server,
                                GDBusConnection *connection)
{
	EDataCalFactoryPrivate *priv;
	GList *list = NULL;
	gchar *key;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (server);

	g_mutex_lock (&priv->connections_lock);

	while (g_hash_table_lookup_extended (
		priv->connections,
		CALENDAR_DBUS_SERVICE_NAME,
		(gpointer) &key, (gpointer) &list)) {
		GList *copy;

		/* this should trigger the calendar's weak ref notify
		 * function, which will remove it from the list before
		 * it's freed, and will remove the connection from
		 * priv->connections once they're all gone */
		copy = g_list_copy (list);
		g_list_foreach (copy, (GFunc) remove_data_cal_cb, NULL);
		g_list_free (copy);
	}

	g_mutex_unlock (&priv->connections_lock);

	/* Chain up to parent's bus_name_lost() method. */
	E_DBUS_SERVER_CLASS (e_data_cal_factory_parent_class)->
		bus_name_lost (server, connection);
}

static void
data_cal_factory_quit_server (EDBusServer *server,
                              EDBusServerExitCode exit_code)
{
	/* This factory does not support reloading, so stop the signal
	 * emission and return without chaining up to prevent quitting. */
	if (exit_code == E_DBUS_SERVER_EXIT_RELOAD) {
		g_signal_stop_emission_by_name (server, "quit-server");
		return;
	}

	/* Chain up to parent's quit_server() method. */
	E_DBUS_SERVER_CLASS (e_data_cal_factory_parent_class)->
		quit_server (server, exit_code);
}

static gboolean
data_cal_factory_initable_init (GInitable *initable,
                                GCancellable *cancellable,
                                GError **error)
{
	EDataCalFactoryPrivate *priv;

	priv = E_DATA_CAL_FACTORY_GET_PRIVATE (initable);

	priv->registry = e_source_registry_new_sync (cancellable, error);

	return (priv->registry != NULL);
}

static void
e_data_cal_factory_class_init (EDataCalFactoryClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;
	EDataFactoryClass *data_factory_class;
	const gchar *modules_directory = BACKENDDIR;
	const gchar *modules_directory_env;

	modules_directory_env = g_getenv (EDS_CALENDAR_MODULES);
	if (modules_directory_env &&
	    g_file_test (modules_directory_env, G_FILE_TEST_IS_DIR))
		modules_directory = g_strdup (modules_directory_env);

	g_type_class_add_private (class, sizeof (EDataCalFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = data_cal_factory_get_property;
	object_class->dispose = data_cal_factory_dispose;
	object_class->finalize = data_cal_factory_finalize;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_name = CALENDAR_DBUS_SERVICE_NAME;
	dbus_server_class->module_directory = modules_directory;
	dbus_server_class->bus_acquired = data_cal_factory_bus_acquired;
	dbus_server_class->bus_name_lost = data_cal_factory_bus_name_lost;
	dbus_server_class->quit_server = data_cal_factory_quit_server;

	data_factory_class = E_DATA_FACTORY_CLASS (class);
	data_factory_class->backend_factory_type = E_TYPE_CAL_BACKEND_FACTORY;

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Data source registry",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_data_cal_factory_initable_init (GInitableIface *interface)
{
	interface->init = data_cal_factory_initable_init;
}

static void
e_data_cal_factory_init (EDataCalFactory *factory)
{
	factory->priv = E_DATA_CAL_FACTORY_GET_PRIVATE (factory);

	factory->priv->dbus_factory =
		e_dbus_calendar_factory_skeleton_new ();

	g_signal_connect (
		factory->priv->dbus_factory, "handle-open-calendar",
		G_CALLBACK (data_cal_factory_handle_open_calendar_cb),
		factory);

	g_signal_connect (
		factory->priv->dbus_factory, "handle-open-task-list",
		G_CALLBACK (data_cal_factory_handle_open_task_list_cb),
		factory);

	g_signal_connect (
		factory->priv->dbus_factory, "handle-open-memo-list",
		G_CALLBACK (data_cal_factory_handle_open_memo_list_cb),
		factory);

	g_mutex_init (&factory->priv->calendars_lock);
	factory->priv->calendars = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	g_mutex_init (&factory->priv->connections_lock);
	factory->priv->connections = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);
}

EDBusServer *
e_data_cal_factory_new (GCancellable *cancellable,
                        GError **error)
{
	icalarray *builtin_timezones;
	gint ii;

#ifdef HAVE_ICAL_UNKNOWN_TOKEN_HANDLING
	ical_set_unknown_token_handling_setting (ICAL_DISCARD_TOKEN);
#endif

	/* XXX Pre-load all built-in timezones in libical.
	 *
	 *     Built-in time zones in libical 0.43 are loaded on demand,
	 *     but not in a thread-safe manner, resulting in a race when
	 *     multiple threads call icaltimezone_load_builtin_timezone()
	 *     on the same time zone.  Until built-in time zone loading
	 *     in libical is made thread-safe, work around the issue by
	 *     loading all built-in time zones now, so libical's internal
	 *     time zone array will be fully populated before any threads
	 *     are spawned.
	 */
	builtin_timezones = icaltimezone_get_builtin_timezones ();
	for (ii = 0; ii < builtin_timezones->num_elements; ii++) {
		icaltimezone *zone;

		zone = icalarray_element_at (builtin_timezones, ii);

		/* We don't care about the component right now,
		 * we just need some function that will trigger
		 * icaltimezone_load_builtin_timezone(). */
		icaltimezone_get_component (zone);
	}

	return g_initable_new (
		E_TYPE_DATA_CAL_FACTORY,
		cancellable, error, NULL);
}

/**
 * e_data_cal_factory_get_registry:
 * @factory: an #EDataCalFactory
 *
 * Returns the #ESourceRegistry owned by @factory.
 *
 * Returns: the #ESourceRegistry
 *
 * Since: 3.6
 **/
ESourceRegistry *
e_data_cal_factory_get_registry (EDataCalFactory *factory)
{
	g_return_val_if_fail (E_IS_DATA_CAL_FACTORY (factory), NULL);

	return factory->priv->registry;
}
