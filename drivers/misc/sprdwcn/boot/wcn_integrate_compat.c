// SPDX-License-Identifier: GPL-2.0
/*
 * Compatibility shims for the R-11 sprdwl_ng external module.
 *
 * These two symbols live in platform/wcn_boot.c which is only built for
 * the (unused) marlin module flavour.  Under CONFIG_WCN_INTEG the same
 * roles are handled inside the integrate boot path, so provide thin
 * equivalents here so the wlan module can resolve them.
 */

#include <linux/export.h>
#include <misc/marlin_platform.h>
#include <misc/wcn_bus.h>

/*
 * sc2355/Marlin2 ships a single wifi config (wifi_2355b001_1ant.ini);
 * the AA variant file is the default the driver falls back to anyway.
 */
enum wcn_chip_id_type wcn_get_chip_type(void)
{
	return WCN_CHIP_ID_AA;
}
EXPORT_SYMBOL_GPL(wcn_get_chip_type);

/*
 * The integrate boot path reloads CP firmware (and its ini data) on every
 * power-on, so the wlan driver should always perform the ini download
 * after a fresh CP start.
 */
int cali_ini_need_download(enum wcn_sub_sys subsys)
{
	return 1;
}
EXPORT_SYMBOL_GPL(cali_ini_need_download);
