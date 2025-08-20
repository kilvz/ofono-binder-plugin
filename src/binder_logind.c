/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#include <stdio.h>
#include <stdarg.h>

#include <gio/gio.h>

#include "binder_dbus_property.h"
#include "binder_log.h"
#include "binder_logind.h"

#define BINDER_LOGIND_DBUS_NAME       "org.freedesktop.login1"
#define BINDER_LOGIND_DBUS_PATH       "/org/freedesktop/login1/seat/seat0"
#define BINDER_LOGIND_DBUS_INTERFACE  "org.freedesktop.login1.Seat"

/* signals */
enum
{
    SCREEN_STATE_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

struct _BinderLogindPrivate {
    GDBusProxy *binder_logind_proxy;

    GCancellable *cancellable;
};

G_DEFINE_TYPE_WITH_CODE (
    BinderLogind,
    binder_logind,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE (BinderLogind)
)

static void
binder_logind_idle_hint_available_cb(
    GObject      *object,
    GVariant     *value,
    BinderLogind *self)
{
    gboolean idle_hint = g_variant_get_boolean(value);

    g_signal_emit(
        self,
        signals[SCREEN_STATE_CHANGED],
        0,
        !idle_hint);

    if (object != NULL)
        g_clear_object(&object);
}

static void
binder_logind_proxy_properties_cb (
    GDBusProxy    *proxy,
    GVariant      *changed_properties,
    char         **invalidated_properties,
    BinderLogind  *self)
{
    GVariant *value;
    char *property;
    GVariantIter i;

    g_variant_iter_init(&i, changed_properties);
    while (g_variant_iter_next(&i, "{&sv}", &property, &value)) {
        if (g_strcmp0(property, "IdleHint") == 0) {
            binder_logind_idle_hint_available_cb(NULL, value, self);
        }
        g_variant_unref(value);
    }
}

static void
new_proxy_bus_cb (
  GObject      *source,
  GAsyncResult *res,
  BinderLogind *self)
{
    GObject *binder_dbus_property;
    g_autoptr(GError) error = NULL;

    self->priv->binder_logind_proxy = g_dbus_proxy_new_for_bus_finish(
        res,
        &error);

    if (error != NULL) {
        ofono_warn("Failed to get logind bus: %s", error->message);
        return;
    }

    g_signal_connect(
        self->priv->binder_logind_proxy,
        "g-properties-changed",
        G_CALLBACK(binder_logind_proxy_properties_cb),
        self);

    binder_dbus_property = binder_dbus_property_new(
        BINDER_LOGIND_DBUS_NAME,
        BINDER_LOGIND_DBUS_PATH,
        BINDER_LOGIND_DBUS_INTERFACE,
        "IdleHint");

    g_signal_connect(
        binder_dbus_property,
        "property-available",
        G_CALLBACK (binder_logind_idle_hint_available_cb),
        self);
}

static void
binder_logind_dispose (
    GObject *binder_logind)
{
    BinderLogind *self = BINDER_LOGIND (binder_logind);

    g_cancellable_cancel(self->priv->cancellable);

    g_clear_object(&self->priv->binder_logind_proxy);
    g_clear_object(&self->priv->cancellable);

    G_OBJECT_CLASS(binder_logind_parent_class)->dispose(binder_logind);
}

static void
binder_logind_finalize(
    GObject *binder_logind)
{
    G_OBJECT_CLASS(binder_logind_parent_class)->finalize(binder_logind);
}

static void
binder_logind_class_init(
    BinderLogindClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = binder_logind_dispose;
    object_class->finalize = binder_logind_finalize;

    signals[SCREEN_STATE_CHANGED] = g_signal_new(
        "screen-state-changed",
        G_OBJECT_CLASS_TYPE(object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL, NULL,
        G_TYPE_NONE,
        1,
        G_TYPE_BOOLEAN
    );
}

static void
binder_logind_init(
    BinderLogind *self)
{
    self->priv = binder_logind_get_instance_private (self);
    self->priv->cancellable = g_cancellable_new();

    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        BINDER_LOGIND_DBUS_NAME,
        BINDER_LOGIND_DBUS_PATH,
        BINDER_LOGIND_DBUS_INTERFACE,
        self->priv->cancellable,
        (GAsyncReadyCallback) new_proxy_bus_cb,
        self);
}

GObject *
binder_logind_new(
    void)
{
    GObject *binder_logind;

    binder_logind = g_object_new(TYPE_BINDER_LOGIND, NULL);

    return binder_logind;
}