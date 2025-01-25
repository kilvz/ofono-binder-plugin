/*
 *  oFono - Open Source Telephony - binder based adaptation
 *
 *  Copyright (C) 2021-2022 Jolla Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include "binder_devmon.h"
#include "binder_log.h"
#include "binder_logind.h"
#include "binder_nm.h"
#include "binder_upower.h"

#include <ofono/log.h>

#include <radio_client.h>
#include <radio_request.h>

#include <gbinder_writer.h>

#include <gutil_macros.h>

typedef struct binder_devmon_if {
    BinderDevmon pub;
    int cell_info_interval_short_ms;
    int cell_info_interval_long_ms;
} DevMon;

typedef struct binder_devmon_if_io {
    BinderDevmonIo pub;
    struct ofono_slot* slot;
    GObject *logind;
    GObject *nm;
    GObject *upower;
    RadioClient* client;
    RadioRequest* req;
    gint32 indication_filter;
    gboolean display_on;
    gboolean charging;
    gboolean access_point_enabled;
    gboolean wifi_connection_enabled;
    gboolean ind_filter_supported;
    int cell_info_interval_short_ms;
    int cell_info_interval_long_ms;
} DevMonIo;

#define DBG_(self,fmt,args...) \
    DBG("%s: " fmt, radio_client_slot((self)->client), ##args)

inline static DevMon* binder_devmon_if_cast(BinderDevmon* pub)
    { return G_CAST(pub, DevMon, pub); }

inline static DevMonIo* binder_devmon_if_io_cast(BinderDevmonIo* pub)
    { return G_CAST(pub, DevMonIo, pub); }

static
void
binder_devmon_if_io_indication_filter_sent(
    RadioRequest* req,
    RADIO_TX_STATUS status,
    RADIO_RESP resp,
    RADIO_ERROR error,
    const GBinderReader* args,
    gpointer user_data)
{
    DevMonIo* self = user_data;

    GASSERT(self->req == req);
    radio_request_unref(self->req);
    self->req = NULL;

    if (status == RADIO_TX_STATUS_OK) {
        if (resp == RADIO_RESP_SET_INDICATION_FILTER) {
            if (error == RADIO_ERROR_REQUEST_NOT_SUPPORTED) {
                /* This is a permanent failure */
                DBG_(self, "Indication response filter is not supported");
                self->ind_filter_supported = FALSE;
            }
        } else {
            ofono_error("Unexpected setIndicationFilter response %d", resp);
        }
    }
}

static
void
binder_devmon_if_io_set_indication_filter(
    DevMonIo* self)
{
    if (self->ind_filter_supported) {
        GBinderWriter args;
        RADIO_REQ code;
        gint32 value;
        gboolean powersave =
            (self->wifi_connection_enabled || !self->display_on) &&
            (!self->charging && !self->access_point_enabled);

        /*
         * Both requests take the same args:
         *
         * setIndicationFilter(serial, bitfield<IndicationFilter>)
         * setIndicationFilter_1_2(serial, bitfield<IndicationFilter>)
         *
         * and both produce IRadioResponse.setIndicationFilterResponse()
         *
         * However setIndicationFilter_1_2 comments says "If unset, defaults
         * to @1.2::IndicationFilter:ALL" and it's unclear what "unset" means
         * wrt a bitmask. How is "unset" different from NONE which is zero.
         * To be on the safe side, let's always set the most innocently
         * looking bit which I think is DATA_CALL_DORMANCY.
         */
        if (radio_client_interface(self->client) < RADIO_INTERFACE_1_2) {
            code = RADIO_REQ_SET_INDICATION_FILTER;
            value = powersave ? RADIO_IND_FILTER_DATA_CALL_DORMANCY :
                RADIO_IND_FILTER_ALL;
        } else if (radio_client_interface(self->client) < RADIO_INTERFACE_1_5) {
            code = RADIO_REQ_SET_INDICATION_FILTER_1_2;
            value = powersave ? RADIO_IND_FILTER_DATA_CALL_DORMANCY :
                RADIO_IND_FILTER_ALL_1_2;
        } else {
            code = RADIO_REQ_SET_INDICATION_FILTER_1_5;
            value = powersave ? RADIO_IND_FILTER_DATA_CALL_DORMANCY :
                RADIO_IND_FILTER_ALL_1_5;
        }

        if (self->indication_filter == value)
            return;

        self->indication_filter = value;
        radio_request_drop(self->req);
        self->req = radio_request_new(self->client, code, &args,
            binder_devmon_if_io_indication_filter_sent, NULL, self);
        gbinder_writer_append_int32(&args, value);
        ofono_info("Indication filter: 0x%02x", value);
        radio_request_submit(self->req);
    }
}

static
void
binder_devmon_if_io_set_cell_info_update_interval(
    DevMonIo* self)
{
    gboolean powersave =
        (self->wifi_connection_enabled || !self->display_on) &&
        (!self->charging && !self->access_point_enabled);

    ofono_slot_set_cell_info_update_interval(self->slot, self,
            powersave ?
                self->cell_info_interval_long_ms :
                self->cell_info_interval_short_ms);
}

static void
binder_screen_state_changed_cb (
    GObject  *logind,
    gboolean  display_on,
    DevMonIo *self)
{
    if (self->display_on == display_on)
        return;

    self->display_on = display_on;

    binder_devmon_if_io_set_indication_filter(self);
    binder_devmon_if_io_set_cell_info_update_interval(self);
}

static void
binder_charging_state_changed_cb (
    GObject  *upower,
    gboolean  charging,
    DevMonIo *self)
{
    if (self->charging == charging)
        return;

    self->charging = charging;

    binder_devmon_if_io_set_indication_filter(self);
    binder_devmon_if_io_set_cell_info_update_interval(self);
}

static void
binder_access_point_enabled_cb (
    GObject  *upower,
    gboolean  enabled,
    DevMonIo *self)
{
    if (self->access_point_enabled == enabled)
        return;

    self->access_point_enabled = enabled;

    binder_devmon_if_io_set_indication_filter(self);
    binder_devmon_if_io_set_cell_info_update_interval(self);
}

static void
binder_wifi_connection_enabled_cb (
    GObject  *upower,
    gboolean  enabled,
    DevMonIo *self)
{
    if (self->wifi_connection_enabled == enabled)
        return;

    self->wifi_connection_enabled = enabled;

    binder_devmon_if_io_set_indication_filter(self);
    binder_devmon_if_io_set_cell_info_update_interval(self);
}

static
void
binder_devmon_if_io_free(
    BinderDevmonIo* io)
{
    DevMonIo* self = binder_devmon_if_io_cast(io);

    radio_request_drop(self->req);
    radio_client_unref(self->client);

    g_clear_object (&self->logind);
    g_clear_object (&self->nm);
    g_clear_object (&self->upower);

    ofono_slot_drop_cell_info_requests(self->slot, self);
    ofono_slot_unref(self->slot);
    g_free(self);
}

static
BinderDevmonIo*
binder_devmon_if_start_io(
    BinderDevmon* devmon,
    RadioClient* client,
    struct ofono_slot* slot)
{
    DevMon* impl = binder_devmon_if_cast(devmon);
    DevMonIo* self = g_new0(DevMonIo, 1);

    self->pub.free = binder_devmon_if_io_free;
    self->ind_filter_supported = TRUE;
    self->display_on = TRUE;
    self->charging = FALSE;
    self->access_point_enabled = FALSE;
    self->wifi_connection_enabled = FALSE;
    self->client = radio_client_ref(client);
    self->slot = ofono_slot_ref(slot);
    self->logind = binder_logind_new();
    self->nm = binder_nm_new();
    self->upower = binder_upower_new();

    self->cell_info_interval_short_ms = impl->cell_info_interval_short_ms;
    self->cell_info_interval_long_ms = impl->cell_info_interval_long_ms;

    g_signal_connect (
        self->logind,
        "screen-state-changed",
        G_CALLBACK (binder_screen_state_changed_cb),
        self);

    g_signal_connect (
        self->upower,
        "charging-state-changed",
        G_CALLBACK (binder_charging_state_changed_cb),
        self);

    g_signal_connect (
        self->nm,
        "access-point-enabled",
        G_CALLBACK (binder_access_point_enabled_cb),
        self);

    g_signal_connect (
        self->nm,
        "wifi-connection-enabled",
        G_CALLBACK (binder_wifi_connection_enabled_cb),
        self);

    return &self->pub;
}

static
void
binder_devmon_if_free(
    BinderDevmon* devmon)
{
    DevMon* self = binder_devmon_if_cast(devmon);

    g_free(self);
}

/*==========================================================================*
 * API
 *==========================================================================*/

BinderDevmon*
binder_devmon_if_new(
    const BinderSlotConfig* config)
{
    DevMon* self = g_new0(DevMon, 1);

    self->pub.free = binder_devmon_if_free;
    self->pub.start_io = binder_devmon_if_start_io;
    self->cell_info_interval_short_ms = config->cell_info_interval_short_ms;
    self->cell_info_interval_long_ms = config->cell_info_interval_long_ms;
    return &self->pub;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
