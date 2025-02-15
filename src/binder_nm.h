/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#ifndef BINDER_NM_H
#define BINDER_NM_H

#include <glib.h>
#include <glib-object.h>

#define TYPE_BINDER_NM \
    (binder_nm_get_type ())
#define BINDER_NM(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST \
    ((obj), TYPE_BINDER_NM, BinderNM))
#define BINDER_NM_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_CAST \
    ((cls), TYPE_BINDER_NM, BinderNMClass))
#define IS_BINDER_NM(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE \
    ((obj), TYPE_BINDER_NM))
#define IS_BINDER_NM_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_TYPE \
    ((cls), TYPE_BINDER_NM))
#define BINDER_NM_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS \
    ((obj), TYPE_BINDER_NM, BinderNMClass))

G_BEGIN_DECLS

typedef struct _BinderNM BinderNM;
typedef struct _BinderNMClass BinderNMClass;
typedef struct _BinderNMPrivate BinderNMPrivate;

struct _BinderNM {
    GObject parent;
    BinderNMPrivate *priv;
};

struct _BinderNMClass {
    GObjectClass parent_class;
};

GType           binder_nm_get_type      (void) G_GNUC_CONST;

GObject*        binder_nm_new           (void);
G_END_DECLS

#endif


