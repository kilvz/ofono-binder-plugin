/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#include <stdio.h>
#include <stdarg.h>

#include <gio/gio.h>

#include "binder_dbus_property.h"
#include "binder_log.h"
#include "binder_upower.h"

#define BINDER_UPOWER_DBUS_NAME       "org.freedesktop.UPower"
#define BINDER_UPOWER_DBUS_PATH       "/org/freedesktop/UPower/devices/DisplayDevice"
#define BINDER_UPOWER_DBUS_INTERFACE  "org.freedesktop.UPower.Device"

/* signals */
enum
{
    CHARGING_STATE_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

struct _BinderUPowerPrivate {
    GDBusProxy *binder_upower_proxy;

    GCancellable *cancellable;
};

G_DEFINE_TYPE_WITH_CODE(
    BinderUPower,
    binder_upower,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE (BinderUPower)
)

static void
binder_upower_state_available_cb(
    GObject      *object,
    GVariant     *value,
    BinderUPower *self)
{
    guint state = g_variant_get_uint32(value);

    g_signal_emit(
        self,
        signals[CHARGING_STATE_CHANGED],
        0,
        state != 2);

    if (object != NULL)
        g_clear_object(&object);
}

static void
binder_upower_proxy_properties_cb(
    GDBusProxy   *proxy,
    GVariant     *changed_properties,
    char        **invalidated_properties,
    BinderUPower *self)
{
    GVariant *value;
    char *property;
    GVariantIter i;

    g_variant_iter_init(&i, changed_properties);
    while (g_variant_iter_next(&i, "{&sv}", &property, &value)) {
        if (g_strcmp0(property, "State") == 0) {
            binder_upower_state_available_cb(NULL, value, self);
        }
        g_variant_unref(value);
    }
}

static void
new_proxy_bus_cb(
  GObject      *source,
  GAsyncResult *res,
  BinderUPower *self)
{
    GObject *binder_dbus_property;
    g_autoptr(GError) error = NULL;

    self->priv->binder_upower_proxy = g_dbus_proxy_new_for_bus_finish(
        res,
        &error);

    if (error != NULL) {
        ofono_warn("Failed to get UPower bus: %s", error->message);
        return;
    }

    g_signal_connect(
        self->priv->binder_upower_proxy,
        "g-properties-changed",
        G_CALLBACK (binder_upower_proxy_properties_cb),
        self);

    binder_dbus_property = binder_dbus_property_new(
        BINDER_UPOWER_DBUS_NAME,
        BINDER_UPOWER_DBUS_PATH,
        BINDER_UPOWER_DBUS_INTERFACE,
        "State");

    g_signal_connect(
        binder_dbus_property,
        "property-available",
        G_CALLBACK (binder_upower_state_available_cb),
        self);
}

static void
binder_upower_dispose(
    GObject *binder_upower)
{
    BinderUPower *self = BINDER_UPOWER (binder_upower);

    g_cancellable_cancel(self->priv->cancellable);

    g_clear_object(&self->priv->binder_upower_proxy);
    g_clear_object(&self->priv->cancellable);

    G_OBJECT_CLASS(binder_upower_parent_class)->dispose (binder_upower);
}

static void
binder_upower_finalize(
    GObject *binder_upower)
{
    G_OBJECT_CLASS(binder_upower_parent_class)->finalize (binder_upower);
}

static void
binder_upower_class_init(
    BinderUPowerClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = binder_upower_dispose;
    object_class->finalize = binder_upower_finalize;

    signals[CHARGING_STATE_CHANGED] = g_signal_new(
        "charging-state-changed",
        G_OBJECT_CLASS_TYPE (object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL, NULL,
        G_TYPE_NONE,
        1,
        G_TYPE_BOOLEAN);
}

static void
binder_upower_init(
    BinderUPower *self)
{
    self->priv = binder_upower_get_instance_private (self);

    self->priv->cancellable = g_cancellable_new();

    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        BINDER_UPOWER_DBUS_NAME,
        BINDER_UPOWER_DBUS_PATH,
        BINDER_UPOWER_DBUS_INTERFACE,
        self->priv->cancellable,
        (GAsyncReadyCallback) new_proxy_bus_cb,
        self);
}

GObject *
binder_upower_new(
    void)
{
    GObject *binder_upower;

    binder_upower = g_object_new(TYPE_BINDER_UPOWER, NULL);

    return binder_upower;
}