/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#include <stdio.h>
#include <stdarg.h>

#include <gio/gio.h>

#include "binder_dbus_property.h"
#include "binder_log.h"

#define DBUS_PROPERTIES_INTERFACE        "org.freedesktop.DBus.Properties"

/* signals */
enum
{
    PROPERTY_AVAILABLE,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

struct _BinderDBusPropertyPrivate {
    GDBusProxy *proxy;

    const char *name;
    char *path;
    const char *interface;
    const char *property;

    GCancellable *cancellable;
};

G_DEFINE_TYPE_WITH_CODE (
    BinderDBusProperty,
    binder_dbus_property,
    G_TYPE_OBJECT,
    G_ADD_PRIVATE (BinderDBusProperty)
)

static void
get_property_cb (
  GDBusProxy   *proxy,
  GAsyncResult *res,
  BinderDBusProperty *self)
{
    g_autoptr (GVariant) value = NULL;
    g_autoptr (GVariant) inner_value = NULL;
    g_autoptr (GError) error = NULL;

    value = g_dbus_proxy_call_finish (
        proxy,
        res,
        &error);

    if (error != NULL) {
        ofono_warn("Failed to read property value: %s", error->message);
        return;
    }

    g_variant_get (value, "(v)", &inner_value);

    g_signal_emit(
        self,
        signals[PROPERTY_AVAILABLE],
        0,
        inner_value);
}

static void
new_proxy_bus_cb (
  GObject      *source,
  GAsyncResult *res,
  BinderDBusProperty *self)
{
    g_autoptr(GError) error = NULL;

    self->priv->proxy = g_dbus_proxy_new_for_bus_finish(
        res,
        &error);

    if (error != NULL) {
        ofono_warn("Failed to read property: %s", error->message);
        return;
    }

    g_dbus_proxy_call (
        self->priv->proxy,
        "Get",
        g_variant_new ("(ss)",
                       self->priv->interface,
                       self->priv->property
        ),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        self->priv->cancellable,
        (GAsyncReadyCallback) get_property_cb,
        self);
}

static void
binder_dbus_property_dispose (
    GObject *binder_dbus_property)
{
    BinderDBusProperty *self = BINDER_DBUS_PROPERTY (binder_dbus_property);

    g_cancellable_cancel(self->priv->cancellable);

    g_clear_object(&self->priv->proxy);
    g_clear_object(&self->priv->cancellable);

    G_OBJECT_CLASS(binder_dbus_property_parent_class)->dispose(binder_dbus_property);
}

static void
binder_dbus_property_finalize(
    GObject *binder_dbus_property)
{
    BinderDBusProperty *self = BINDER_DBUS_PROPERTY (binder_dbus_property);

    if (self->priv->path != NULL)
        g_free(self->priv->path);

    G_OBJECT_CLASS(binder_dbus_property_parent_class)->finalize(binder_dbus_property);
}

static void
binder_dbus_property_class_init(
    BinderDBusPropertyClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = binder_dbus_property_dispose;
    object_class->finalize = binder_dbus_property_finalize;

    signals[PROPERTY_AVAILABLE] = g_signal_new(
        "property-available",
        G_OBJECT_CLASS_TYPE(object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL, NULL,
        G_TYPE_NONE,
        1,
        G_TYPE_VARIANT
    );
}

static void
binder_dbus_property_init(
    BinderDBusProperty *self)
{
    self->priv = binder_dbus_property_get_instance_private (self);
    self->priv->cancellable = g_cancellable_new();
}

static void
binder_dbus_set(
    BinderDBusProperty *self,
    const char *name,
    const char *path,
    const char *interface,
    const char *property)
{
    self->priv->name = name;
    self->priv->path = g_strdup(path);
    self->priv->interface = interface;
    self->priv->property = property;

    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        self->priv->name,
        self->priv->path,
        DBUS_PROPERTIES_INTERFACE,
        self->priv->cancellable,
        (GAsyncReadyCallback) new_proxy_bus_cb,
        self);
}

GObject *
binder_dbus_property_new(
    const char *name,
    const char *path,
    const char *interface,
    const char *property)
{
    GObject *self;

    self = g_object_new(TYPE_BINDER_DBUS_PROPERTY, NULL);
    binder_dbus_set(
        BINDER_DBUS_PROPERTY(self),
        name,
        path,
        interface,
        property);

    return self;
}

const char*
binder_dbus_property_get_path(
    BinderDBusProperty *self)
{
    return self->priv->path;
}