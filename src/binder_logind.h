/*
 * Copyright Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 */

#ifndef BINDER_LOGIND_H
#define BINDER_LOGIND_H

#include <glib.h>
#include <glib-object.h>

#define TYPE_BINDER_LOGIND \
    (binder_logind_get_type ())
#define BINDER_LOGIND(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST \
    ((obj), TYPE_BINDER_LOGIND, BinderLogind))
#define BINDER_LOGIND_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_CAST \
    ((cls), TYPE_BINDER_LOGIND, BinderLogindClass))
#define IS_BINDER_LOGIND(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE \
    ((obj), TYPE_BINDER_LOGIND))
#define IS_BINDER_LOGIND_CLASS(cls) \
    (G_TYPE_CHECK_CLASS_TYPE \
    ((cls), TYPE_BINDER_LOGIND))
#define BINDER_LOGIND_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS \
    ((obj), TYPE_BINDER_LOGIND, BinderLogindClass))

G_BEGIN_DECLS

typedef struct _BinderLogind BinderLogind;
typedef struct _BinderLogindClass BinderLogindClass;
typedef struct _BinderLogindPrivate BinderLogindPrivate;

struct _BinderLogind {
    GObject parent;
    BinderLogindPrivate *priv;
};

struct _BinderLogindClass {
    GObjectClass parent_class;
};

GType           binder_logind_get_type            (void) G_GNUC_CONST;

GObject*        binder_logind_new                 (void);

G_END_DECLS

#endif

