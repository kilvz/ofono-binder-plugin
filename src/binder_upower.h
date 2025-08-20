/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#ifndef BINDER_UPOWER_H
#define BINDER_UPOWER_H

#include <glib.h>
#include <glib-object.h>

#define TYPE_BINDER_UPOWER \
    (binder_upower_get_type ())
#define BINDER_UPOWER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST \
    ((obj), TYPE_BINDER_UPOWER, BinderUPower))
#define BINDER_UPOWER_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_CAST \
    ((cls), TYPE_BINDER_UPOWER, BinderUPowerClass))
#define IS_BINDER_UPOWER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE \
    ((obj), TYPE_BINDER_UPOWER))
#define IS_BINDER_UPOWER_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_TYPE \
    ((cls), TYPE_BINDER_UPOWER))
#define BINDER_UPOWER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS \
    ((obj), TYPE_BINDER_UPOWER, BinderUPowerClass))

G_BEGIN_DECLS

typedef struct _BinderUPower BinderUPower;
typedef struct _BinderUPowerClass BinderUPowerClass;
typedef struct _BinderUPowerPrivate BinderUPowerPrivate;

struct _BinderUPower {
    GObject parent;
    BinderUPowerPrivate *priv;
};

struct _BinderUPowerClass {
    GObjectClass parent_class;
};

GType           binder_upower_get_type            (void) G_GNUC_CONST;

GObject*        binder_upower_new                 (void);

G_END_DECLS

#endif

