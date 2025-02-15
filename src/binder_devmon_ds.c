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

typedef struct binder_devmon_ds {
    BinderDevmon pub;
    int cell_info_interval_short_ms;
    int cell_info_interval_long_ms;
} DevMon;

typedef struct binder_devmon_ds_io {
    BinderDevmonIo pub;
    struct ofono_slot* slot;
    GObject *logind;
    GObject *nm;
    GObject *upower;
    RadioClient* client;
    RadioRequest* low_data_req;
    RadioRequest* charging_req;
    gboolean low_data;
    gboolean charging;
    gboolean display_on;
    gboolean access_point_enabled;
    gboolean wifi_connection_enabled;
    gboolean low_data_supported;
    gboolean charging_supported;
    int cell_info_interval_short_ms;
    int cell_info_interval_long_ms;
} DevMonIo;

#define DBG_(self,fmt,args...) \
    DBG("%s: " fmt, radio_client_slot((self)->client), ##args)

static inline DevMon* binder_devmon_ds_cast(BinderDevmon* pub)
    { return G_CAST(pub, DevMon, pub); }

static inline DevMonIo* binder_devmon_ds_io_cast(BinderDevmonIo* pub)
    { return G_CAST(pub, DevMonIo, pub); }

static
void
binder_devmon_ds_io_low_data_state_sent(
    RadioRequest* req,
    RADIO_TX_STATUS status,
    RADIO_RESP resp,
    RADIO_ERROR error,
    const GBinderReader* args,
    gpointer user_data)
{
    DevMonIo* self = user_data;

    GASSERT(self->low_data_req == req);
    radio_request_unref(self->low_data_req);
    self->low_data_req = NULL;

    if (status == RADIO_TX_STATUS_OK) {
        if (resp == RADIO_RESP_SEND_DEVICE_STATE) {
            if (error == RADIO_ERROR_REQUEST_NOT_SUPPORTED) {
                DBG_(self, "LOW_DATA_EXPECTED state is not supported");
                self->low_data_supported = FALSE;
            }
        } else {
            ofono_error("Unexpected sendDeviceState response %d", resp);
            self->low_data_supported = FALSE;
        }
    }
}

static
void
binder_devmon_ds_io_charging_state_sent(
    RadioRequest* req,
    RADIO_TX_STATUS status,
    RADIO_RESP resp,
    RADIO_ERROR error,
    const GBinderReader* args,
    gpointer user_data)
{
    DevMonIo* self = user_data;

    GASSERT(self->charging_req == req);
    radio_request_unref(self->charging_req);
    self->charging_req = NULL;

    if (status == RADIO_TX_STATUS_OK) {
        if (resp == RADIO_RESP_SEND_DEVICE_STATE) {
            if (error == RADIO_ERROR_REQUEST_NOT_SUPPORTED) {
                DBG_(self, "CHARGING state is not supported");
                self->charging_supported = FALSE;
            }
        } else {
            ofono_error("Unexpected sendDeviceState response %d", resp);
            self->charging_supported = FALSE;
        }
    }
}

static
RadioRequest*
binder_devmon_ds_io_send_device_state(
    DevMonIo* self,
    RADIO_DEVICE_STATE type,
    gboolean state,
    RadioRequestCompleteFunc callback)
{
    GBinderWriter writer;
    RadioRequest* req = radio_request_new(self->client,
        RADIO_REQ_SEND_DEVICE_STATE, &writer, callback, NULL, self);

    /* sendDeviceState(int32_t serial, DeviceStateType type, bool state); */
    gbinder_writer_append_int32(&writer, type);
    gbinder_writer_append_bool(&writer, state);
    if (radio_request_submit(req)) {
        return req;
    } else {
        radio_request_unref(req);
        return NULL;
    }
}

static
void
binder_devmon_ds_io_update_charging(
    DevMonIo* self)
{
    ofono_info("Charging: %b", self->charging);
    if (self->charging_supported) {
        radio_request_drop(self->charging_req);
        self->charging_req = binder_devmon_ds_io_send_device_state(self,
            RADIO_DEVICE_STATE_CHARGING_STATE, self->charging,
            binder_devmon_ds_io_charging_state_sent);
    }
}

static
void
binder_devmon_ds_io_update_low_data(
    DevMonIo* self)
{
    const gboolean low_data =
        (self->wifi_connection_enabled || !self->display_on) &&
        (!self->charging && !self->access_point_enabled);

    if (self->low_data != low_data) {
        self->low_data = low_data;
        ofono_info("Low data: %b", low_data);
        if (self->low_data_supported) {
            radio_request_drop(self->low_data_req);
            self->low_data_req = binder_devmon_ds_io_send_device_state(self,
                RADIO_DEVICE_STATE_LOW_DATA_EXPECTED, low_data,
                binder_devmon_ds_io_low_data_state_sent);
        }
    }
}

static
void
binder_devmon_ds_io_set_cell_info_update_interval(
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

    binder_devmon_ds_io_update_low_data(self);
    binder_devmon_ds_io_set_cell_info_update_interval(self);
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

    binder_devmon_ds_io_update_low_data(self);
    binder_devmon_ds_io_update_charging(self);
    binder_devmon_ds_io_set_cell_info_update_interval(self);
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

    binder_devmon_ds_io_update_low_data(self);
    binder_devmon_ds_io_set_cell_info_update_interval(self);
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

    binder_devmon_ds_io_update_low_data(self);
    binder_devmon_ds_io_set_cell_info_update_interval(self);
}

static
void
binder_devmon_ds_io_free(
    BinderDevmonIo* io)
{
    DevMonIo* self = binder_devmon_ds_io_cast(io);

    radio_request_drop(self->low_data_req);
    radio_request_drop(self->charging_req);
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
binder_devmon_ds_start_io(
    BinderDevmon* devmon,
    RadioClient* client,
    struct ofono_slot* slot)
{
    DevMon* ds = binder_devmon_ds_cast(devmon);
    DevMonIo* self = g_new0(DevMonIo, 1);

    self->pub.free = binder_devmon_ds_io_free;
    self->low_data_supported = TRUE;
    self->charging_supported = TRUE;
    self->low_data = FALSE;
    self->display_on = TRUE;
    self->charging = FALSE;
    self->access_point_enabled = FALSE;
    self->wifi_connection_enabled = FALSE;
    self->client = radio_client_ref(client);
    self->logind = binder_logind_new();
    self->nm = binder_nm_new();
    self->upower = binder_upower_new();
    self->slot = ofono_slot_ref(slot);

    self->cell_info_interval_short_ms = ds->cell_info_interval_short_ms;
    self->cell_info_interval_long_ms = ds->cell_info_interval_long_ms;

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

    binder_screen_state_changed_cb(self->logind, FALSE, self);
    binder_charging_state_changed_cb(self->upower, FALSE, self);
    binder_devmon_ds_io_set_cell_info_update_interval(self);

    return &self->pub;
}

static
void
binder_devmon_ds_free(
    BinderDevmon* devmon)
{
    DevMon* self = binder_devmon_ds_cast(devmon);

    g_free(self);
}

/*==========================================================================*
 * API
 *==========================================================================*/

BinderDevmon*
binder_devmon_ds_new(
    const BinderSlotConfig* config)
{
    DevMon* self = g_new0(DevMon, 1);

    self->pub.free = binder_devmon_ds_free;
    self->pub.start_io = binder_devmon_ds_start_io;
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
