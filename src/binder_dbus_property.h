/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#ifndef BINDER_DBUS_PROPERTY_H
#define BINDER_DBUS_PROPERTY_H

#include <glib.h>
#include <glib-object.h>

#define TYPE_BINDER_DBUS_PROPERTY \
    (binder_dbus_property_get_type ())
#define BINDER_DBUS_PROPERTY(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST \
    ((obj), TYPE_BINDER_DBUS_PROPERTY, BinderDBusProperty))
#define BINDER_DBUS_PROPERTY_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_CAST \
    ((cls), TYPE_BINDER_DBUS_PROPERTY, BinderDBusPropertyClass))
#define IS_BINDER_DBUS_PROPERTY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE \
    ((obj), TYPE_BINDER_DBUS_PROPERTY))
#define IS_BINDER_DBUS_PROPERTY_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_TYPE \
    ((cls), TYPE_BINDER_DBUS_PROPERTY))
#define BINDER_DBUS_PROPERTY_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS \
    ((obj), TYPE_BINDER_DBUS_PROPERTY, BinderDBusPropertyClass))

G_BEGIN_DECLS

typedef struct _BinderDBusProperty BinderDBusProperty;
typedef struct _BinderDBusPropertyClass BinderDBusPropertyClass;
typedef struct _BinderDBusPropertyPrivate BinderDBusPropertyPrivate;

struct _BinderDBusProperty {
    GObject parent;
    BinderDBusPropertyPrivate *priv;
};

struct _BinderDBusPropertyClass {
    GObjectClass parent_class;
};

GType           binder_dbus_property_get_type            (void) G_GNUC_CONST;

GObject*        binder_dbus_property_new                 (const char *name,
                                                          const char *path,
                                                          const char *interface,
                                                          const char *property);
const char*     binder_dbus_property_get_path            (BinderDBusProperty *self);
G_END_DECLS

#endif

