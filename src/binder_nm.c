/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#include <stdio.h>
#include <stdarg.h>

#include <gio/gio.h>

#include "binder_dbus_property.h"
#include "binder_log.h"
#include "binder_nm.h"

#define BINDER_NM_DBUS_NAME        "org.freedesktop.NetworkManager"
#define BINDER_NM_DBUS_PATH        "/org/freedesktop/NetworkManager"
#define BINDER_NM_DBUS_INTERFACE   "org.freedesktop.NetworkManager"
#define BINDER_NM_DBUS_DEVICE      "org.freedesktop.NetworkManager.Device"
#define BINDER_NM_DBUS_WIRELESS    "org.freedesktop.NetworkManager.Device.Wireless"
#define BINDER_NM_DBUS_AP          "org.freedesktop.NetworkManager.AccessPoint"

#define GFOREACH(list, item) \
    for(GList *__glist = list; \
        __glist && (item = __glist->data, TRUE); \
        __glist = __glist->next)

/* signals */
enum
{
    ACCESS_POINT_ENABLED,
    WIFI_CONNECTION_ENABLED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];


struct _BinderNMPrivate {
    GDBusProxy *binder_nm_proxy;

    GList *devices;

    /* We do not check this on startup, assume it can't be on */
    gboolean access_point;

    GCancellable *cancellable;
};

G_DEFINE_TYPE_WITH_CODE (
    BinderNM,
    binder_nm,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE(BinderNM)
)

static void
binder_nm_proxy_properties_cb(
    GDBusProxy  *proxy,
    GVariant    *changed_properties,
    char       **invalidated_properties,
    gpointer     user_data);

static void
binder_nm_device_type_available_cb(
    BinderDBusProperty *binder_dbus_property,
    GVariant  *value,
    BinderNM *self);

static void
add_device(
    BinderNM   *self,
    const char *device_path)
{
    GObject *binder_dbus_property;

    binder_dbus_property = binder_dbus_property_new(
        BINDER_NM_DBUS_NAME,
        device_path,
        BINDER_NM_DBUS_DEVICE,
        "DeviceType");

    g_signal_connect(
        binder_dbus_property,
        "property-available",
        G_CALLBACK (binder_nm_device_type_available_cb),
        self);
}

static void
del_device (
    BinderNM *self,
    const char     *device_path)
{
    GDBusProxy *network_wireless_proxy;

    GFOREACH (self->priv->devices, network_wireless_proxy) {
        if (g_strcmp0 (g_dbus_proxy_get_object_path (network_wireless_proxy), device_path) == 0) {
            self->priv->devices = g_list_remove (
                self->priv->devices, network_wireless_proxy
            );
            g_clear_object (&network_wireless_proxy);
            break;
        }
    }
}

static void
binder_nm_proxy_signal_cb(
    GDBusProxy *proxy,
    const char *sender_name,
    const char *signal_name,
    GVariant   *parameters,
    gpointer    user_data)
{
    BinderNM *self = user_data;
    const char *object_path = NULL;

    if (g_strcmp0(signal_name, "DeviceAdded") == 0) {
        g_variant_get(parameters, "(&o)", &object_path);
        add_device(self, object_path);
    } else if (g_strcmp0(signal_name, "DeviceRemoved") == 0) {
        g_variant_get(parameters, "(&o)", &object_path);
        del_device(self, object_path);
    }
}

static void
new_device_bus_cb (
  GObject      *source,
  GAsyncResult *res,
  BinderNM     *self)
{
    GDBusProxy *binder_device_proxy = NULL;
    g_autoptr(GError) error = NULL;

    binder_device_proxy = g_dbus_proxy_new_for_bus_finish(
        res,
        &error);

    if (error != NULL) {
        ofono_warn("Failed to get network device: %s", error->message);
        return;
    }

    self->priv->devices = g_list_append (
        self->priv->devices, binder_device_proxy
    );

    g_signal_connect (
        binder_device_proxy,
        "g-properties-changed",
        G_CALLBACK (binder_nm_proxy_properties_cb),
        self
    );
}

static void
binder_nm_device_type_available_cb(
    BinderDBusProperty *binder_dbus_property,
    GVariant  *value,
    BinderNM *self)
{
    guint device_type;
    const char *device_path;

    g_variant_get (value, "u", &device_type);

    device_path = binder_dbus_property_get_path (binder_dbus_property);
    g_clear_object(&binder_dbus_property);

    if (device_type != 2) { /* NM_DEVICE_TYPE_WIFI */
        return;
    }

    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        BINDER_NM_DBUS_NAME,
        device_path,
        BINDER_NM_DBUS_WIRELESS,
        self->priv->cancellable,
        (GAsyncReadyCallback) new_device_bus_cb,
        self);
}

static void
binder_nm_primary_connection_available_cb(
    GObject *object,
    GVariant  *value,
    BinderNM *self)
{
    g_autofree char *connection_type = NULL;

    g_variant_get(value, "s", &connection_type);
    g_signal_emit(
        self,
        signals[WIFI_CONNECTION_ENABLED],
        0,
        g_strcmp0 (connection_type, "802-11-wireless") == 0);

    if (object != NULL)
        g_clear_object(&object);
}

static void
binder_nm_bandwidth_available_cb(
    BinderDBusProperty *binder_dbus_property,
    GVariant  *value,
    BinderNM *self)
{
    gboolean access_point = g_variant_get_uint32 (value) == 0;

    if (self->priv->access_point != access_point) {
        self->priv->access_point = access_point;
        g_signal_emit(
            self,
            signals[ACCESS_POINT_ENABLED],
            0,
            access_point);
    }

    g_clear_object (&binder_dbus_property);
}

static void
binder_nm_proxy_properties_cb(
    GDBusProxy  *proxy,
    GVariant    *changed_properties,
    char       **invalidated_properties,
    gpointer     user_data)
{
    BinderNM *self = user_data;
    GVariant *value;
    char *property;
    GVariantIter i;

    g_variant_iter_init (&i, changed_properties);
    while (g_variant_iter_next (&i, "{&sv}", &property, &value)) {
        if (g_strcmp0(property, "PrimaryConnectionType") == 0) {
            binder_nm_primary_connection_available_cb (
                NULL,
                value,
                self);
        } else if (g_strcmp0(property, "ActiveAccessPoint") == 0) {
            const char *object_path = NULL;

            g_variant_get(value, "&o", &object_path);

            if (g_strcmp0(object_path, "/") != 0) {
                GObject *binder_dbus_property;

                /* Really? I'm missing something on how to detect AP */
                binder_dbus_property = binder_dbus_property_new(
                    BINDER_NM_DBUS_NAME,
                    object_path,
                    BINDER_NM_DBUS_AP,
                    "Bandwidth");

                g_signal_connect(
                    binder_dbus_property,
                    "property-available",
                    G_CALLBACK (binder_nm_bandwidth_available_cb),
                    self);
            }
        }
        g_variant_unref (value);
    }
}

static void
get_devices_cb (
  GDBusProxy   *proxy,
  GAsyncResult *res,
  BinderNM     *self)
{
    g_autoptr (GVariantIter) iter = NULL;
    g_autoptr (GVariant) value = NULL;
    g_autoptr (GError) error = NULL;
    const char *device_path;

    value = g_dbus_proxy_call_finish (
        proxy,
        res,
        &error);

    if (error != NULL) {
        ofono_warn("Failed to get network devices: %s", error->message);
        return;
    }

    self->priv->devices = NULL;
    g_variant_get(value, "(ao)", &iter);
    while (g_variant_iter_loop (iter, "&o", &device_path, NULL)) {
        add_device(self, device_path);
    }
}

static void
new_proxy_bus_cb (
  GObject      *source,
  GAsyncResult *res,
  BinderNM     *self)
{
    GObject *binder_dbus_property;
    g_autoptr(GError) error = NULL;

    self->priv->binder_nm_proxy = g_dbus_proxy_new_for_bus_finish(
        res,
        &error);

    if (error != NULL) {
        ofono_warn("Failed to get network-manager bus: %s", error->message);
        return;
    }

    g_signal_connect(
        self->priv->binder_nm_proxy,
        "g-properties-changed",
        G_CALLBACK(binder_nm_proxy_properties_cb),
        self);

    g_signal_connect(
        self->priv->binder_nm_proxy,
        "g-signal",
        G_CALLBACK (binder_nm_proxy_signal_cb),
        self);

    g_dbus_proxy_call(
        self->priv->binder_nm_proxy,
        "GetDevices",
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        self->priv->cancellable,
        (GAsyncReadyCallback) get_devices_cb,
        self);

    binder_dbus_property = binder_dbus_property_new(
        BINDER_NM_DBUS_NAME,
        BINDER_NM_DBUS_PATH,
        BINDER_NM_DBUS_INTERFACE,
        "PrimaryConnectionType");

    g_signal_connect(
        binder_dbus_property,
        "property-available",
        G_CALLBACK (binder_nm_primary_connection_available_cb),
        self);
}

static void
binder_nm_dispose(GObject *binder_nm)
{
    BinderNM *self = BINDER_NM (binder_nm);

    g_cancellable_cancel(self->priv->cancellable);

    g_clear_object(&self->priv->binder_nm_proxy);
    g_clear_object(&self->priv->cancellable);

    G_OBJECT_CLASS(binder_nm_parent_class)->dispose (binder_nm);
}

static void
binder_nm_finalize(GObject *binder_nm)
{
    BinderNM *self = BINDER_NM(binder_nm);

    g_list_free(self->priv->devices);

    G_OBJECT_CLASS(binder_nm_parent_class)->finalize(binder_nm);
}

static void
binder_nm_class_init(BinderNMClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = binder_nm_dispose;
    object_class->finalize = binder_nm_finalize;

    signals[ACCESS_POINT_ENABLED] = g_signal_new (
        "access-point-enabled",
        G_OBJECT_CLASS_TYPE (object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL, NULL,
        G_TYPE_NONE,
        1,
        G_TYPE_BOOLEAN
    );

    signals[WIFI_CONNECTION_ENABLED] = g_signal_new (
        "wifi-connection-enabled",
        G_OBJECT_CLASS_TYPE (object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL, NULL,
        G_TYPE_NONE,
        1,
        G_TYPE_BOOLEAN
    );
}

static void
binder_nm_init(BinderNM *self)
{
    self->priv = binder_nm_get_instance_private (self);
    self->priv->cancellable = g_cancellable_new();

    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        BINDER_NM_DBUS_NAME,
        BINDER_NM_DBUS_PATH,
        BINDER_NM_DBUS_INTERFACE,
        self->priv->cancellable,
        (GAsyncReadyCallback) new_proxy_bus_cb,
        self);
}

GObject *
binder_nm_new (void)
{
    GObject *binder_nm;

    binder_nm = g_object_new(TYPE_BINDER_NM, NULL);

    return binder_nm;
}