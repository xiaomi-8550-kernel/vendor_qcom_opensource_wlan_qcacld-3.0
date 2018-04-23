/*
 * Copyright (c) 2012-2017 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 *  DOC: wlan_hdd_lpass.c
 *
 *  WLAN Host Device Driver LPASS feature implementation
 *
 */

/* Include Files */
#include "wlan_hdd_main.h"
#include "wlan_hdd_lpass.h"
#include "wlan_hdd_oemdata.h"
#include <cds_utils.h>
#include "qwlan_version.h"

/**
 * wlan_hdd_get_channel_info() - Get channel info
 * @hdd_ctx: HDD context
 * @chan_info: Pointer to the structure that stores channel info
 * @chan_id: Channel ID
 *
 * Fill in the channel info to chan_info structure.
 */
static void wlan_hdd_get_channel_info(struct hdd_context *hdd_ctx,
				      struct svc_channel_info *chan_info,
				      uint32_t chan_id)
{
	uint32_t reg_info_1;
	uint32_t reg_info_2;
	QDF_STATUS status = QDF_STATUS_E_FAILURE;

	status = sme_get_reg_info(hdd_ctx->hHal, chan_id,
				  &reg_info_1, &reg_info_2);
	if (status != QDF_STATUS_SUCCESS)
		return;

	chan_info->mhz = cds_chan_to_freq(chan_id);
	chan_info->band_center_freq1 = chan_info->mhz;
	chan_info->band_center_freq2 = 0;
	chan_info->info = 0;
	if (CHANNEL_STATE_DFS ==
	    wlan_reg_get_channel_state(hdd_ctx->hdd_pdev,
				       chan_id))
		WMI_SET_CHANNEL_FLAG(chan_info,
				     WMI_CHAN_FLAG_DFS);
	hdd_update_channel_bw_info(hdd_ctx, chan_id,
				   chan_info);
	chan_info->reg_info_1 = reg_info_1;
	chan_info->reg_info_2 = reg_info_2;
}

/**
 * wlan_hdd_gen_wlan_status_pack() - Create lpass adapter status package
 * @data: Status data record to be created
 * @adapter: Adapter whose status is to being packaged
 * @sta_ctx: Station-specific context of @adapter
 * @is_on: Is wlan driver loaded?
 * @is_connected: Is @adapater connected to an AP?
 *
 * Generate a wlan vdev status package. The status info includes wlan
 * on/off status, vdev ID, vdev mode, supported channels, etc.
 *
 * Return: 0 if package was created, otherwise a negative errno
 */
static int wlan_hdd_gen_wlan_status_pack(struct wlan_status_data *data,
					 struct hdd_adapter *adapter,
					 struct hdd_station_ctx *sta_ctx,
					 uint8_t is_on, uint8_t is_connected)
{
	struct hdd_context *hdd_ctx = NULL;
	uint8_t buflen = WLAN_SVC_COUNTRY_CODE_LEN;
	int i;
	uint32_t chan_id;
	struct svc_channel_info *chan_info;

	if (!data) {
		hdd_err("invalid data pointer");
		return -EINVAL;
	}
	if (!adapter) {
		if (is_on) {
			/* no active interface */
			data->lpss_support = 0;
			data->is_on = is_on;
			return 0;
		}
		hdd_err("invalid adapter pointer");
		return -EINVAL;
	}

	if (wlan_hdd_validate_session_id(adapter->session_id)) {
		hdd_err("invalid session id: %d", adapter->session_id);
		return -EINVAL;
	}

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	if (hdd_ctx->lpss_support && hdd_ctx->config->enable_lpass_support)
		data->lpss_support = 1;
	else
		data->lpss_support = 0;
	data->numChannels = WLAN_SVC_MAX_NUM_CHAN;
	sme_get_cfg_valid_channels(data->channel_list,
				   &data->numChannels);

	for (i = 0; i < data->numChannels; i++) {
		chan_info = &data->channel_info[i];
		chan_id = data->channel_list[i];
		chan_info->chan_id = chan_id;
		wlan_hdd_get_channel_info(hdd_ctx, chan_info, chan_id);
	}

	sme_get_country_code(hdd_ctx->hHal, data->country_code, &buflen);
	data->is_on = is_on;
	data->vdev_id = adapter->session_id;
	data->vdev_mode = adapter->device_mode;
	if (sta_ctx) {
		data->is_connected = is_connected;
		data->rssi = adapter->rssi;
		data->freq =
			cds_chan_to_freq(sta_ctx->conn_info.operationChannel);
		if (WLAN_SVC_MAX_SSID_LEN >=
		    sta_ctx->conn_info.SSID.SSID.length) {
			data->ssid_len = sta_ctx->conn_info.SSID.SSID.length;
			memcpy(data->ssid,
			       sta_ctx->conn_info.SSID.SSID.ssId,
			       sta_ctx->conn_info.SSID.SSID.length);
		}
		if (QDF_MAC_ADDR_SIZE >= sizeof(sta_ctx->conn_info.bssId))
			memcpy(data->bssid, sta_ctx->conn_info.bssId.bytes,
			       QDF_MAC_ADDR_SIZE);
	}
	return 0;
}

/**
 * wlan_hdd_gen_wlan_version_pack() - Create lpass version package
 * @data: Version data record to be created
 * @fw_version: Version code from firmware
 * @chip_id: WLAN chip ID
 * @chip_name: WLAN chip name
 *
 * Generate a wlan software/hw version info package. The version info
 * includes wlan host driver version, wlan fw driver version, wlan hw
 * chip id & wlan hw chip name.
 *
 * Return: 0 if package was created, otherwise a negative errno
 */
static int wlan_hdd_gen_wlan_version_pack(struct wlan_version_data *data,
					  uint32_t fw_version,
					  uint32_t chip_id,
					  const char *chip_name)
{
	if (!data) {
		hdd_err("invalid data pointer");
		return -EINVAL;
	}

	data->chip_id = chip_id;
	strlcpy(data->chip_name, chip_name, WLAN_SVC_MAX_STR_LEN);
	if (strncmp(chip_name, "Unknown", 7))
		strlcpy(data->chip_from, "Qualcomm", WLAN_SVC_MAX_STR_LEN);
	else
		strlcpy(data->chip_from, "Unknown", WLAN_SVC_MAX_STR_LEN);
	strlcpy(data->host_version, QWLAN_VERSIONSTR, WLAN_SVC_MAX_STR_LEN);
	scnprintf(data->fw_version, WLAN_SVC_MAX_STR_LEN, "%d.%d.%d.%d",
		  (fw_version & 0xf0000000) >> 28,
		  (fw_version & 0xf000000) >> 24,
		  (fw_version & 0xf00000) >> 20, (fw_version & 0x7fff));
	return 0;
}

/**
 * wlan_hdd_send_status_pkg() - Send adapter status to lpass
 * @adapter: Adapter whose status is to be sent to lpass
 * @sta_ctx: Station-specific context of @adapter
 * @is_on: Is @adapter enabled
 * @is_connected: Is @adapater connected
 *
 * Generate wlan vdev status pacakge and send it to a user space
 * daemon through netlink.
 *
 * Return: none
 */
static void wlan_hdd_send_status_pkg(struct hdd_adapter *adapter,
				     struct hdd_station_ctx *sta_ctx,
				     uint8_t is_on, uint8_t is_connected)
{
	int ret = 0;
	struct wlan_status_data *data = NULL;
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);

	if (!hdd_ctx)
		return;

	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam())
		return;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	if (is_on)
		ret = wlan_hdd_gen_wlan_status_pack(data, adapter, sta_ctx,
						    is_on, is_connected);

	if (!ret)
		wlan_hdd_send_svc_nlink_msg(hdd_ctx->radio_index,
					    WLAN_SVC_WLAN_STATUS_IND,
					    data, sizeof(*data));
	kfree(data);
}

/**
 * wlan_hdd_send_version_pkg() - report version information to lpass
 * @fw_version: Version code from firmware
 * @chip_id: WLAN chip ID
 * @chip_name: WLAN chip name
 *
 * Generate a wlan sw/hw version info package and send it to a user
 * space daemon through netlink.
 *
 * Return: none
 */
static void wlan_hdd_send_version_pkg(uint32_t fw_version,
				      uint32_t chip_id,
				      const char *chip_name)
{
	int ret = 0;
	struct wlan_version_data data;
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);

	if (!hdd_ctx)
		return;

	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam())
		return;

	memset(&data, 0, sizeof(struct wlan_version_data));
	ret = wlan_hdd_gen_wlan_version_pack(&data, fw_version, chip_id,
					     chip_name);
	if (!ret)
		wlan_hdd_send_svc_nlink_msg(hdd_ctx->radio_index,
					WLAN_SVC_WLAN_VERSION_IND,
					    &data, sizeof(data));
}

/**
 * wlan_hdd_send_all_scan_intf_info() - report scan interfaces to lpass
 * @hdd_ctx: The global HDD context
 *
 * This function iterates through all of the interfaces registered
 * with HDD and indicates to lpass all that support scanning.
 * If no interfaces support scanning then that fact is also indicated.
 *
 * Return: none
 */
static void wlan_hdd_send_all_scan_intf_info(struct hdd_context *hdd_ctx)
{
	struct hdd_adapter *adapter = NULL;
	bool scan_intf_found = false;

	if (!hdd_ctx) {
		hdd_err("NULL pointer for hdd_ctx");
		return;
	}

	hdd_for_each_adapter(hdd_ctx, adapter) {
		if (adapter->device_mode == QDF_STA_MODE ||
		    adapter->device_mode == QDF_P2P_CLIENT_MODE ||
		    adapter->device_mode == QDF_P2P_DEVICE_MODE) {
			scan_intf_found = true;
			wlan_hdd_send_status_pkg(adapter, NULL, 1, 0);
		}
	}

	if (!scan_intf_found)
		wlan_hdd_send_status_pkg(adapter, NULL, 1, 0);
}

/*
 * hdd_lpass_target_config() - Handle LPASS target configuration
 * (public function documented in wlan_hdd_lpass.h)
 */
void hdd_lpass_target_config(struct hdd_context *hdd_ctx,
			     struct wma_tgt_cfg *target_config)
{
	hdd_ctx->lpss_support = target_config->lpss_support;
}

/*
 * hdd_lpass_populate_cds_config() - Populate LPASS configuration
 * (public function documented in wlan_hdd_lpass.h)
 */
void hdd_lpass_populate_cds_config(struct cds_config_info *cds_config,
				   struct hdd_context *hdd_ctx)
{
	cds_config->is_lpass_enabled = hdd_ctx->config->enable_lpass_support;
}

/*
 * hdd_lpass_populate_pmo_config() - Populate LPASS configuration
 * (public function documented in wlan_hdd_lpass.h)
 */
void hdd_lpass_populate_pmo_config(struct pmo_psoc_cfg *pmo_config,
				   struct hdd_context *hdd_ctx)
{
	pmo_config->lpass_enable = hdd_ctx->config->enable_lpass_support;
}

/*
 * hdd_lpass_notify_connect() - Notify LPASS of interface connect
 * (public function documented in wlan_hdd_lpass.h)
 */
void hdd_lpass_notify_connect(struct hdd_adapter *adapter)
{
	struct hdd_station_ctx *sta_ctx;

	/* only send once per connection */
	if (adapter->rssi_send)
		return;

	/* don't send if driver is unloading */
	if (cds_is_driver_unloading())
		return;

	adapter->rssi_send = true;
	sta_ctx = WLAN_HDD_GET_STATION_CTX_PTR(adapter);
	wlan_hdd_send_status_pkg(adapter, sta_ctx, 1, 1);
}

/*
 * hdd_lpass_notify_disconnect() - Notify LPASS of interface disconnect
 * (public function documented in wlan_hdd_lpass.h)
 */
void hdd_lpass_notify_disconnect(struct hdd_adapter *adapter)
{
	struct hdd_station_ctx *sta_ctx;

	adapter->rssi_send = false;
	sta_ctx = WLAN_HDD_GET_STATION_CTX_PTR(adapter);
	wlan_hdd_send_status_pkg(adapter, sta_ctx, 1, 0);
}

/*
 * hdd_lpass_notify_mode_change() - Notify LPASS of interface mode change
 * (public function documented in wlan_hdd_lpass.h)
 *
 * implementation note: when one interfaces changes we notify the
 * state of all of the interfaces.
 */
void hdd_lpass_notify_mode_change(struct hdd_adapter *adapter)
{
	struct hdd_context *hdd_ctx;

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	wlan_hdd_send_all_scan_intf_info(hdd_ctx);
}

/*
 * hdd_lpass_notify_start() - Notify LPASS of driver start
 * (public function documented in wlan_hdd_lpass.h)
 */
void hdd_lpass_notify_start(struct hdd_context *hdd_ctx)
{
	wlan_hdd_send_all_scan_intf_info(hdd_ctx);
	wlan_hdd_send_version_pkg(hdd_ctx->target_fw_version,
				  hdd_ctx->target_hw_version,
				  hdd_ctx->target_hw_name);
}

/*
 * hdd_lpass_notify_stop() - Notify LPASS of driver stop
 * (public function documented in wlan_hdd_lpass.h)
 */
void hdd_lpass_notify_stop(struct hdd_context *hdd_ctx)
{
	wlan_hdd_send_status_pkg(NULL, NULL, 0, 0);
}

/*
 * hdd_lpass_is_supported() - Is lpass feature supported?
 * (public function documented in wlan_hdd_lpass.h)
 */
bool hdd_lpass_is_supported(struct hdd_context *hdd_ctx)
{
	return hdd_ctx->config->enable_lpass_support;
}
