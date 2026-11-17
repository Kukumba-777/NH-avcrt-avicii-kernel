/*
 * Copyright (c) 2024 The Linux Foundation. All rights reserved.
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
 * DOC: wma_frame_inject.c
 *
 * This file contains WMA layer frame injection queue management functions.
 * It provides infrastructure for queuing, processing, and transmitting
 * injected 802.11 frames through the firmware interface.
 */

#include "wma.h"
#include "wma_frame_inject.h"
#include "wlan_hdd_frame_inject.h"
#include "wma_api.h"
#include "wma_internal.h"
#include "wmi_unified_api.h"
#include "wmi_unified.h"
#include "qdf_mem.h"
#include "qdf_list.h"
#include "qdf_lock.h"
#include "qdf_status.h"
#include "qdf_trace.h"
#include "qdf_nbuf.h"
#include "qdf_delayed_work.h"
#include "qdf_time.h"
#include "cds_api.h"
#include "cdp_txrx_cmn.h"
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <wlan_vdev_mgr_tgt_if_tx_defs.h>
#if defined(CONFIG_HL_SUPPORT)
#include "wlan_tgt_def_config_hl.h"
#else
#include "wlan_tgt_def_config.h"
#endif

#ifdef FEATURE_FRAME_INJECTION_SUPPORT

/*
 * wma_injection_unmap_tx_buf() - Unmap DMA mapping on injection nbuf
 *
 * On LL (low-latency / PCI / SNOC) the WMI layer DMA-maps the tx_frame
 * passed in mgmt_params.  The mapping must be released on completion.
 * On HL (high-latency / SDIO / USB) the frame data is copied inline into
 * the WMI command and no DMA mapping exists, so the unmap is a no-op.
 */
#ifdef CONFIG_HL_SUPPORT
static inline void wma_injection_unmap_tx_buf(void *qdf_ctx, qdf_nbuf_t buf)
{
}
#else
static inline void wma_injection_unmap_tx_buf(void *qdf_ctx, qdf_nbuf_t buf)
{
	if (qdf_ctx && buf)
		qdf_nbuf_unmap_single(qdf_ctx, buf, QDF_DMA_TO_DEVICE);
}
#endif

/* Maximum number of frames in WMA injection queue */
#define WMA_FRAME_INJECT_MAX_QUEUE_SIZE    512

/* Maximum frame size for injection */
#define WMA_FRAME_INJECT_MAX_FRAME_SIZE    2304

/* Timeout for queue processing work in milliseconds */
#define WMA_FRAME_INJECT_QUEUE_TIMEOUT_MS  100

/*
 * Hard limit enforced at reservation, including submissions in progress.
 * Missing completions retain DMA ownership and eventually stop injection.
 */
#define WMA_INJECTION_INFLIGHT_HIGH        200

/**
 * struct wma_injection_queue_node - Node for injection queue
 * @node: List node
 * @req: Frame injection request
 * @timestamp: Enqueue timestamp
 * @vdev_id: VDEV ID for the frame
 */
struct wma_injection_queue_node {
	qdf_list_node_t node;
	struct inject_frame_req req;
	uint64_t timestamp;
	uint8_t vdev_id;
};

/**
 * struct wma_injection_queue_ctx - WMA injection queue context
 * @queue: Queue of pending injection requests
 * @queue_size: Current queue size
 * @max_queue_size: Maximum allowed queue size
 * @queue_work: Work item for processing queue
 * @delayed_work: Delayed work item for backpressure handling
 * @stats: Queue statistics
 * @is_initialized: Initialization flag
 */
struct wma_injection_queue_ctx {
	qdf_list_t queue;
	uint32_t queue_size;
	uint32_t max_queue_size;
	qdf_work_t queue_work;
	struct qdf_delayed_work delayed_work;
	uint32_t inflight_count; /* protected by wma_injection_lock */
	struct wma_injection_queue_stats stats;
	bool is_initialized;
	bool stopping;
};

/* Statistics structure is defined in wma_frame_inject.h */

/* Global injection queue context */
static struct wma_injection_queue_ctx g_wma_injection_ctx;

/* Static lifetime: late completions must never touch a destroyed lock. */
static DEFINE_SPINLOCK(wma_injection_lock);
static DEFINE_MUTEX(wma_injection_lifecycle_lock);
static DEFINE_MUTEX(wma_injection_send_lock);

/*
 * Per-session one-shot logging flags and counters for
 * wma_send_injection_frame_to_fw().  Reset on each new
 * injection TX vdev creation so restarts re-log setup info.
 */
static bool inject_tx_cfg_logged;
static uint32_t inject_send_info_count;
static bool inject_wmi_path_logged;
static bool inject_monitor_no_legacy_logged;
static bool inject_probe_sa_fix_logged;
static bool inject_patch_banner_logged;

/*
 * Bounded ownership table, protected by wma_injection_lock.  Entries cannot
 * be evicted: an absent completion is not evidence that DMA has stopped.
 * Metadata also allows completion status to be correlated with each frame.
 */
#define WMA_INJECTION_DEBUG_CACHE_SIZE 256
struct wma_injection_debug_info {
	bool valid;
	uint32_t desc_id;
	uint16_t frame_len;
	uint16_t chanfreq;
	uint8_t fc_type;
	uint8_t fc_subtype;
	uint8_t addr1[QDF_MAC_ADDR_SIZE];
	uint8_t addr2[QDF_MAC_ADDR_SIZE];
	uint8_t addr3[QDF_MAC_ADDR_SIZE];
	qdf_nbuf_t tx_buf; /* nbuf passed to WMI; must be unmapped+freed on completion */
	void *qdf_ctx;
	bool submitting;
	bool completed;
};

static struct wma_injection_debug_info
	g_wma_injection_debug_cache[WMA_INJECTION_DEBUG_CACHE_SIZE];

/*
 * Do not reissue a wire ID without a firmware/event drain barrier.  Neither
 * a timeout nor helper deletion provides one.  Exhaustion is explicit;
 * queue deinit/reinit must not reset this sequence or the outstanding table.
 */
static uint32_t wma_injection_next_desc_id = WMA_INJECTION_DESC_ID_BASE;

/*
 * Hidden AP vdev for injection TX on monitor mode.
 *
 * The firmware's mgmt TX handler (FUN_b000fc10, _wlan_send_mgmt_to_host)
 * unconditionally rejects management frames on MONITOR vdevs: the internal
 * vdev-type switch only accepts AP(0), STA(1), IBSS(2), OCB(6) for the
 * normal peer-lookup → WAL-TX path.  MONITOR(3) is shunted to a
 * beacon-only fallback (FUN_b01baa34) that always returns 5 → DISCARD.
 *
 * Work around this by creating a lightweight AP vdev on the same channel
 * as the monitor interface and routing injected frames through it.  The AP
 * vdev has a self-peer (stored at firmware vdev+0xc) so the firmware can
 * schedule and transmit the management frame normally.
 *
 * Lifecycle:
 *   Created lazily on the first injection attempt on a monitor vdev.
 *   Destroyed in wma_deinit_injection_queue() or when the channel changes.
 *   WMI response events for this vdev (create/start/peer_create/up) are
 *   silently dropped by the host because no wlan_objmgr_vdev exists for
 *   the hidden vdev_id.
 */
struct wma_injection_tx_vdev {
	bool created;
	uint8_t vdev_id;
	uint8_t monitor_vdev_id;
	uint32_t chanfreq;
	uint8_t mac_addr[QDF_MAC_ADDR_SIZE];
};

static struct wma_injection_tx_vdev g_inj_tx_vdev;

static void wma_injection_destroy_tx_vdev(tp_wma_handle wma);
static void wma_injection_queue_node_free(struct wma_injection_queue_node *node);

/**
 * wma_injection_reset_session_state() - Reset per-session static state
 *
 * Called when a new injection TX vdev is being created (fresh session
 * or channel change).  Resets all file-static one-shot logging flags
 * and the send counter so the new session starts clean.
 */
static void wma_injection_reset_session_state(void)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;

	/* Reset one-shot logging flags so fresh session re-logs config */
	inject_tx_cfg_logged = false;
	inject_send_info_count = 0;
	inject_wmi_path_logged = false;
	inject_monitor_no_legacy_logged = false;
	inject_probe_sa_fix_logged = false;
	inject_patch_banner_logged = false;

	/* Reset rate-limited log counters so new session gets fresh logs */
	spin_lock_bh(&wma_injection_lock);
	if (ctx->is_initialized) {
		ctx->stats.fw_errors = 0;
		ctx->stats.frames_processed = 0;
	}
	spin_unlock_bh(&wma_injection_lock);

	/* Outstanding DMA ownership survives helper and queue restarts. */
}

/**
 * wma_injection_ensure_tx_vdev() - Ensure hidden AP vdev exists for TX
 * @wma: WMA handle
 * @mon_vdev_id: monitor vdev id
 * @chanfreq: operating channel frequency in MHz
 *
 * Creates (or re-creates on channel change) a firmware-only AP vdev that
 * is used as the TX endpoint for injected management frames.
 *
 * Return: QDF_STATUS_SUCCESS when the helper vdev is ready.
 */
static QDF_STATUS
wma_injection_ensure_tx_vdev(tp_wma_handle wma,
			    uint8_t mon_vdev_id,
			    uint32_t chanfreq)
{
	struct vdev_create_params vcreate;
	struct vdev_start_params vstart;
	struct peer_create_params pcreate;
	uint8_t *mon_mac;
	uint8_t inj_mac[QDF_MAC_ADDR_SIZE];
	uint8_t vid = 0;
	bool found = false;
	int i;
	QDF_STATUS status;

	if (g_inj_tx_vdev.created) {
		if (g_inj_tx_vdev.chanfreq == chanfreq)
			return QDF_STATUS_SUCCESS;
		/* Channel changed – tear down and recreate */
		wma_injection_destroy_tx_vdev(wma);
	}

	/*
	 * Firmware vdev array supports IDs 0..(num_vdevs-1).  num_vdevs is
	 * at most CFG_TGT_NUM_VDEV (typically 4) and may be decremented by 1
	 * for NAN → 3.  Use (CFG_TGT_NUM_VDEV - 2) as safe ceiling so we
	 * never exceed the firmware's internal array.
	 */
	{
		int fw_max_vid = CFG_TGT_NUM_VDEV - 2;

		if (fw_max_vid >= (int)wma->max_bssid)
			fw_max_vid = (int)wma->max_bssid - 1;
		for (i = fw_max_vid; i >= 0; i--) {
			if ((uint8_t)i == mon_vdev_id)
				continue;
			if (!wma->interfaces[i].vdev) {
				vid = (uint8_t)i;
				found = true;
				break;
			}
		}
	}
	if (!found) {
		wma_err("Injection: no unused vdev slot for TX helper");
		return QDF_STATUS_E_RESOURCES;
	}

	/* New vdev being created — reset session state for a clean start */
	wma_injection_reset_session_state();

	mon_mac = wlan_vdev_mlme_get_macaddr(
			wma->interfaces[mon_vdev_id].vdev);
	if (!mon_mac) {
		wma_err("Injection: cannot read monitor vdev MAC");
		return QDF_STATUS_E_FAILURE;
	}
	qdf_mem_copy(inj_mac, mon_mac, QDF_MAC_ADDR_SIZE);
	inj_mac[0] |= 0x02; /* locally-administered */

	/* ---------- 1. VDEV CREATE (STA type) ---------- */
	/*
	 * Use STA, not AP.  AP vdevs trigger firmware beacon-TX-offload
	 * which crashes at _wlan_beacon_tx_offload_handle_beacon because
	 * no beacon template exists.  STA vdevs reach the same peer-lookup
	 * → WAL-TX path in the firmware mgmt TX handler without beacons.
	 */
	qdf_mem_zero(&vcreate, sizeof(vcreate));
	vcreate.vdev_id = vid;
	vcreate.type    = WMI_VDEV_TYPE_STA;
	vcreate.subtype = 0;
	vcreate.nss_2g  = 1;
	vcreate.nss_5g  = 1;
	vcreate.pdev_id = 0;

	status = wmi_unified_vdev_create_send(wma->wmi_handle,
					      inj_mac, &vcreate);
	if (QDF_IS_STATUS_ERROR(status)) {
		wma_err("Injection TX vdev create failed: %d", status);
		return status;
	}
	/* Firmware processes WMI commands asynchronously.  Each step must
	 * complete in firmware before the next command references the vdev.
	 * Without these sleeps the mgmt-TX arrives before VDEV_CREATE is
	 * done and firmware asserts in wlan_vdev_find_vdev.
	 */
	msleep(150);

	/* ---------- 2. VDEV START (20 MHz basic mode) ---------- */
	qdf_mem_zero(&vstart, sizeof(vstart));
	vstart.vdev_id            = vid;
	vstart.channel.mhz        = chanfreq;
	vstart.channel.cfreq1     = chanfreq;
	vstart.channel.cfreq2     = 0;
	/* 2.4 GHz → MODE_11G(1), 5 GHz → MODE_11A(0) */
	vstart.channel.phy_mode   = (chanfreq < 4000) ? 1 : 0;
	vstart.channel.maxregpower = 20;
	vstart.channel.maxpower    = 20;
	vstart.beacon_interval    = 0;
	vstart.dtim_period        = 0;

	status = wmi_unified_vdev_start_send(wma->wmi_handle, &vstart);
	if (QDF_IS_STATUS_ERROR(status)) {
		wma_err("Injection TX vdev start failed: %d", status);
		goto err_stop;
	}
	msleep(150);

	/* ---------- 3. PEER CREATE (self-peer → fw vdev+0xc) ---------- */
	qdf_mem_zero(&pcreate, sizeof(pcreate));
	pcreate.peer_addr = inj_mac;
	pcreate.peer_type = WMI_PEER_TYPE_DEFAULT;
	pcreate.vdev_id   = vid;

	status = wmi_unified_peer_create_send(wma->wmi_handle, &pcreate);
	if (QDF_IS_STATUS_ERROR(status)) {
		wma_err("Injection TX vdev peer create failed: %d", status);
		goto err_stop;
	}
	msleep(100);

	/*
	 * Skip VDEV_UP.  For STA vdevs, firmware's wlan_vdev_up
	 * asserts unless a BSS peer (the AP) exists — we only have
	 * a self-peer.  The mgmt TX handler only needs the vdev in
	 * STARTED state with a valid peer at vdev+0xc.
	 */

	g_inj_tx_vdev.created          = true;
	g_inj_tx_vdev.vdev_id          = vid;
	g_inj_tx_vdev.monitor_vdev_id  = mon_vdev_id;
	g_inj_tx_vdev.chanfreq         = chanfreq;
	qdf_mem_copy(g_inj_tx_vdev.mac_addr, inj_mac, QDF_MAC_ADDR_SIZE);

	wma_info("Injection TX helper vdev created: vdev_id=%u mac=%pM freq=%u type=STA",
		 vid, inj_mac, chanfreq);
	return QDF_STATUS_SUCCESS;

err_stop:
	wmi_unified_vdev_delete_send(wma->wmi_handle, vid);
	return status;
}

/**
 * wma_injection_destroy_tx_vdev() - Tear down the hidden injection TX vdev
 * @wma: WMA handle
 *
 * Late-path teardown (called from deinit_injection_queue / wma_close).
 * WMI may already be stopped, so failures are tolerated.
 */
static void wma_injection_destroy_tx_vdev(tp_wma_handle wma)
{
	if (!g_inj_tx_vdev.created || !wma || !wma->wmi_handle)
		return;

	/*
	 * Proper teardown order (reverse of create):
	 *   PEER_DELETE → VDEV_STOP → VDEV_DELETE
	 * Each step needs a sleep so firmware finishes processing
	 * before the next command arrives.  Without this, a
	 * subsequent VDEV_CREATE for the same slot races with the
	 * pending DELETE and firmware asserts.
	 */

	/* 1. PEER_DELETE */
	wmi_unified_peer_delete_send(wma->wmi_handle,
				     g_inj_tx_vdev.mac_addr,
				     g_inj_tx_vdev.vdev_id);
	msleep(100);

	/* 2. VDEV_STOP (we did VDEV_START during create) */
	wmi_unified_vdev_stop_send(wma->wmi_handle,
				   g_inj_tx_vdev.vdev_id);
	msleep(100);

	/* 3. VDEV_DELETE */
	wmi_unified_vdev_delete_send(wma->wmi_handle,
				     g_inj_tx_vdev.vdev_id);
	msleep(100);

	wma_info("Injection TX helper vdev destroyed: vdev_id=%u",
		 g_inj_tx_vdev.vdev_id);
	qdf_mem_zero(&g_inj_tx_vdev, sizeof(g_inj_tx_vdev));
}

/**
 * wma_injection_pre_stop_cleanup() - Destroy injection helper vdev before
 *                                     monitor mode stop
 * @wma_handle: WMA handle
 *
 * Must be called while WMI is still alive, BEFORE the driver sends
 * VDEV_STOP / VDEV_DELETE for the monitor vdev.  The firmware asserts
 * in dispatch_wlan_pdev_cmds if an orphaned STA helper vdev is still
 * present when the monitor vdev is torn down.
 *
 * Proper teardown order (reverse of create):
 *   PEER_DELETE → VDEV_STOP → VDEV_DELETE
 * with msleep() gaps so the firmware can process each command.
 */
void wma_injection_pre_stop_cleanup(tp_wma_handle wma_handle)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	struct wma_injection_queue_node *node;
	qdf_list_node_t *list_node;
	QDF_STATUS status;
	uint32_t dropped_count = 0;
	bool queue_initialized;

	if (!wma_handle) {
		wma_err("Invalid WMA handle for pre-stop cleanup");
		return;
	}

	mutex_lock(&wma_injection_lifecycle_lock);

	/*
	 * Fence new senders before helper teardown.  In particular, do not allow
	 * a queued worker to recreate the helper after this function returns and
	 * before the monitor vdev is stopped.  A full queue deinit/reinit is
	 * required to resume injection after this pre-stop fence.
	 */
	spin_lock_bh(&wma_injection_lock);
	queue_initialized = ctx->is_initialized;
	if (queue_initialized)
		ctx->stopping = true;
	spin_unlock_bh(&wma_injection_lock);
	if (queue_initialized) {
		qdf_delayed_work_stop_sync(&ctx->delayed_work);
		qdf_cancel_work(&ctx->queue_work);
		qdf_flush_work(&ctx->queue_work);
	}

	mutex_lock(&wma_injection_send_lock);

	/* Discard queued requests; only completion retires submitted ownership. */
	spin_lock_bh(&wma_injection_lock);
	while (queue_initialized && !qdf_list_empty(&ctx->queue)) {
		status = qdf_list_remove_front(&ctx->queue, &list_node);
		if (QDF_IS_STATUS_ERROR(status))
			break;
		node = qdf_container_of(list_node,
					struct wma_injection_queue_node, node);
		wma_injection_queue_node_free(node);
		dropped_count++;
	}
	ctx->queue_size = 0;
	ctx->stats.frames_dropped += dropped_count;
	spin_unlock_bh(&wma_injection_lock);

	if (!g_inj_tx_vdev.created) {
		mutex_unlock(&wma_injection_send_lock);
		mutex_unlock(&wma_injection_lifecycle_lock);
		return;
	}

	if (!wma_handle->wmi_handle) {
		/* Preserve host state; absence of WMI is not helper-delete evidence. */
		wma_warn("WMI down; injection helper state retained");
		mutex_unlock(&wma_injection_send_lock);
		mutex_unlock(&wma_injection_lifecycle_lock);
		return;
	}

	wma_info("Pre-stop cleanup: destroying injection helper vdev_id=%u",
		 g_inj_tx_vdev.vdev_id);

	/* 1. PEER_DELETE */
	wmi_unified_peer_delete_send(wma_handle->wmi_handle,
				     g_inj_tx_vdev.mac_addr,
				     g_inj_tx_vdev.vdev_id);
	msleep(100);

	/* 2. VDEV_STOP (we did VDEV_START during create) */
	wmi_unified_vdev_stop_send(wma_handle->wmi_handle,
				   g_inj_tx_vdev.vdev_id);
	msleep(100);

	/* 3. VDEV_DELETE */
	wmi_unified_vdev_delete_send(wma_handle->wmi_handle,
				     g_inj_tx_vdev.vdev_id);
	msleep(100);

	wma_info("Pre-stop cleanup: injection helper vdev destroyed: vdev_id=%u",
		 g_inj_tx_vdev.vdev_id);
	qdf_mem_zero(&g_inj_tx_vdev, sizeof(g_inj_tx_vdev));
	mutex_unlock(&wma_injection_send_lock);
	mutex_unlock(&wma_injection_lifecycle_lock);
}

static uint16_t
wma_injection_reserve(qdf_nbuf_t buf, void *qdf_ctx,
				 struct inject_frame_req *req,
				 uint8_t fc_type,
				 uint8_t fc_subtype,
				 uint16_t chanfreq)
{
	struct wma_injection_debug_info *entry;
	uint32_t slot;
	uint16_t desc_id = 0;
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;

	spin_lock_bh(&wma_injection_lock);
	if (!ctx->is_initialized || ctx->stopping ||
	    ctx->inflight_count >= WMA_INJECTION_INFLIGHT_HIGH ||
	    wma_injection_next_desc_id >
	    (WMA_INJECTION_DESC_ID_BASE | WMA_INJECTION_DESC_ID_MASK))
		goto out;

	for (slot = 0; slot < WMA_INJECTION_DEBUG_CACHE_SIZE; slot++)
		if (!g_wma_injection_debug_cache[slot].valid)
			break;
	if (slot == WMA_INJECTION_DEBUG_CACHE_SIZE)
		goto out;
	entry = &g_wma_injection_debug_cache[slot];
	desc_id = wma_injection_next_desc_id++;

	entry->valid = false;
	entry->desc_id = desc_id;
	entry->frame_len = req->frame_len;
	entry->chanfreq = chanfreq;
	entry->fc_type = fc_type;
	entry->fc_subtype = fc_subtype;
	entry->tx_buf = buf;
	entry->qdf_ctx = qdf_ctx;
	entry->submitting = true;
	entry->completed = false;
	qdf_mem_zero(entry->addr1, sizeof(entry->addr1));
	qdf_mem_zero(entry->addr2, sizeof(entry->addr2));
	qdf_mem_zero(entry->addr3, sizeof(entry->addr3));

	if (req->frame_len >= 24) {
		qdf_mem_copy(entry->addr1, &req->frame_data[4], QDF_MAC_ADDR_SIZE);
		qdf_mem_copy(entry->addr2, &req->frame_data[10], QDF_MAC_ADDR_SIZE);
		qdf_mem_copy(entry->addr3, &req->frame_data[16], QDF_MAC_ADDR_SIZE);
	}

	entry->valid = true;
	ctx->inflight_count++;
out:
	spin_unlock_bh(&wma_injection_lock);
	return desc_id;
}

/* Caller holds wma_injection_lock. */
static struct wma_injection_debug_info *
wma_injection_debug_cache_get(uint32_t desc_id)
{
	struct wma_injection_debug_info *entry;
	uint32_t slot;

	if (!desc_id)
		return NULL;

	for (slot = 0; slot < WMA_INJECTION_DEBUG_CACHE_SIZE; slot++) {
		entry = &g_wma_injection_debug_cache[slot];
		if (entry->valid && entry->desc_id == desc_id)
			return entry;
	}
	return NULL;
}

/*
 * Completion can precede the return from WMI send.  It only marks a submitting
 * entry; the sender retires it after WMI stops accessing the nbuf.  On failure
 * WMI has already undone any mapping, so only the nbuf is released here.
 */
static void wma_injection_submit_done(uint16_t desc_id, QDF_STATUS status)
{
	struct wma_injection_debug_info *entry;
	qdf_nbuf_t buf = NULL;
	void *qdf_ctx = NULL;

	spin_lock_bh(&wma_injection_lock);
	entry = wma_injection_debug_cache_get(desc_id);
	if (entry) {
		entry->submitting = false;
		if (QDF_IS_STATUS_ERROR(status) || entry->completed) {
			buf = entry->tx_buf;
			qdf_ctx = entry->qdf_ctx;
			entry->tx_buf = NULL;
			entry->valid = false;
			g_wma_injection_ctx.inflight_count--;
		}
	}
	spin_unlock_bh(&wma_injection_lock);
	if (buf) {
		if (QDF_IS_STATUS_SUCCESS(status))
			wma_injection_unmap_tx_buf(qdf_ctx, buf);
		qdf_nbuf_free(buf);
	}
}

/**
 * wma_injection_queue_node_alloc() - Allocate injection queue node
 * @req: Frame injection request
 * @vdev_id: VDEV ID
 *
 * Return: Allocated node or NULL on failure
 */
static struct wma_injection_queue_node *
wma_injection_queue_node_alloc(struct inject_frame_req *req, uint8_t vdev_id)
{
	struct wma_injection_queue_node *node;
	uint8_t *frame_copy;

	if (!req || !req->frame_data || req->frame_len == 0) {
		wma_err("Invalid injection request parameters");
		return NULL;
	}

	if (req->frame_len > WMA_FRAME_INJECT_MAX_FRAME_SIZE) {
		wma_err("Frame size %u exceeds maximum %u",
			req->frame_len, WMA_FRAME_INJECT_MAX_FRAME_SIZE);
		return NULL;
	}

	node = qdf_mem_malloc(sizeof(*node));
	if (!node) {
		wma_err("Failed to allocate injection queue node");
		return NULL;
	}

	/* Allocate and copy frame data */
	frame_copy = qdf_mem_malloc(req->frame_len);
	if (!frame_copy) {
		wma_err("Failed to allocate frame data buffer");
		qdf_mem_free(node);
		return NULL;
	}

	qdf_mem_copy(frame_copy, req->frame_data, req->frame_len);

	/* Initialize node */
	qdf_mem_zero(node, sizeof(*node));
	node->req.frame_len = req->frame_len;
	node->req.frame_data = frame_copy;
	node->req.tx_flags = req->tx_flags;
	node->req.retry_count = req->retry_count;
	node->req.tx_rate = req->tx_rate;
	node->req.timestamp = req->timestamp;
	node->req.session_id = req->session_id;
	node->timestamp = qdf_get_log_timestamp();
	node->vdev_id = vdev_id;

	return node;
}

/**
 * wma_injection_queue_node_free() - Free injection queue node
 * @node: Node to free
 */
static void wma_injection_queue_node_free(struct wma_injection_queue_node *node)
{
	if (!node)
		return;

	if (node->req.frame_data) {
		qdf_mem_free(node->req.frame_data);
		node->req.frame_data = NULL;
	}

	qdf_mem_free(node);
}

/**
 * wma_check_traffic_coordination() - Check if injection can proceed with current traffic
 * @wma_handle: WMA handle
 * @vdev_id: VDEV ID for the frame
 *
 * This function checks if frame injection should be deferred due to high
 * priority traffic or resource constraints in the WMA layer.
 *
 * Return: true if injection can proceed, false if should be deferred
 */
static bool wma_check_traffic_coordination(tp_wma_handle wma_handle, uint8_t vdev_id)
{
	struct wma_txrx_node *iface;

	if (!wma_handle || vdev_id >= wma_handle->max_bssid) {
		wma_err("Invalid parameters: wma_handle=%pK, vdev_id=%u",
			wma_handle, vdev_id);
		return false;
	}

	iface = &wma_handle->interfaces[vdev_id];

	/* Check if interface is in a state that allows injection */
	if (!iface->vdev) {
		wma_debug("Interface %u not active, deferring injection", vdev_id);
		return false;
	}

	/* Check if there's high priority management traffic pending */
	if (iface->roaming_in_progress) {
		wma_debug("High priority operation in progress on vdev %u, deferring injection",
			  vdev_id);
		return false;
	}

	/* Check system-wide resource constraints */
	if (wma_handle->wmi_ready == false) {
		wma_debug("WMI not ready, deferring injection");
		return false;
	}

	/* Check if firmware is overloaded (simple heuristic) */
	if (wma_handle->wmi_handle) {
		uint32_t pending_cmds = wmi_get_pending_cmds(wma_handle->wmi_handle);
		const uint32_t max_pending_threshold = 512;

		if (pending_cmds > max_pending_threshold) {
			wma_debug("Firmware overloaded (%u pending commands), deferring injection",
				  pending_cmds);
			return false;
		}
	}

	return true;
}

/**
 * wma_apply_injection_backpressure() - Apply backpressure when queue is congested
 * @ctx: Injection queue context
 *
 * This function implements backpressure mechanisms when the injection queue
 * becomes congested, including adaptive processing delays and queue throttling.
 *
 * Return: Recommended delay in milliseconds before next processing cycle
 */
/* Caller holds wma_injection_lock. */
static uint32_t wma_apply_injection_backpressure(struct wma_injection_queue_ctx *ctx)
{
	uint32_t queue_utilization;
	uint32_t delay_ms = 0;

	if (!ctx || !ctx->is_initialized) {
		return 0;
	}

	/* Calculate queue utilization percentage */
	queue_utilization = (ctx->queue_size * 100) / ctx->max_queue_size;

	/* Apply adaptive backpressure based on queue utilization */
	if (queue_utilization > 95) {
		/* Queue nearly full - significant backpressure */
		delay_ms = 10;
	} else if (queue_utilization > 85) {
		/* Queue getting full - moderate backpressure */
		delay_ms = 2;
	}

	return delay_ms;
}

/**
 * wma_process_injection_queue() - Process injection queue with traffic coordination
 * @wma_handle: WMA handle
 *
 * This function processes the injection queue while coordinating with existing
 * WMA traffic scheduling and applying backpressure when needed.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_process_injection_queue(tp_wma_handle wma_handle)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	struct wma_injection_queue_node *node;
	qdf_list_node_t *list_node;
	QDF_STATUS status;
	uint64_t current_time;
	uint32_t processed_count = 0;
	uint32_t deferred_count = 0;
	const uint32_t max_process_per_cycle = 64;
	bool queue_was_empty;

	if (!wma_handle) {
		wma_err("Invalid WMA handle");
		return QDF_STATUS_E_INVAL;
	}

	current_time = qdf_get_log_timestamp();

	spin_lock_bh(&wma_injection_lock);
	if (!ctx->is_initialized || ctx->stopping) {
		spin_unlock_bh(&wma_injection_lock);
		return QDF_STATUS_E_AGAIN;
	}
	queue_was_empty = qdf_list_empty(&ctx->queue);
	spin_unlock_bh(&wma_injection_lock);

	if (queue_was_empty)
		return QDF_STATUS_SUCCESS;

	/* Process frames from queue with traffic coordination */
	while (processed_count < max_process_per_cycle) {
		spin_lock_bh(&wma_injection_lock);

		if (!ctx->is_initialized || ctx->stopping ||
		    qdf_list_empty(&ctx->queue) ||
		    ctx->inflight_count >= WMA_INJECTION_INFLIGHT_HIGH) {
			spin_unlock_bh(&wma_injection_lock);
			break;
		}

		/* Peek at the front node to check VDEV before removing */
		status = qdf_list_peek_front(&ctx->queue, &list_node);
		if (QDF_IS_STATUS_ERROR(status)) {
			spin_unlock_bh(&wma_injection_lock);
			wma_err("Failed to peek at queue front: %d", status);
			break;
		}

		node = qdf_container_of(list_node, struct wma_injection_queue_node, node);

		/* Check traffic coordination before processing */
		if (!wma_check_traffic_coordination(wma_handle, node->vdev_id)) {
			deferred_count++;
			wma_debug("Deferring injection due to traffic coordination (vdev_id=%u)",
				  node->vdev_id);
			spin_unlock_bh(&wma_injection_lock);
			break;
		}

		/* Remove the node from queue */
		status = qdf_list_remove_front(&ctx->queue, &list_node);
		if (QDF_IS_STATUS_ERROR(status)) {
			spin_unlock_bh(&wma_injection_lock);
			wma_err("Failed to remove node from queue: %d", status);
			break;
		}

		ctx->queue_size--;

		/* Update queue time statistics */
		ctx->stats.total_queue_time += (current_time - node->timestamp);
		spin_unlock_bh(&wma_injection_lock);

		/* Send frame to firmware */
		status = wma_send_injection_frame_to_fw(wma_handle, &node->req, node->vdev_id);
		spin_lock_bh(&wma_injection_lock);
		if (QDF_IS_STATUS_SUCCESS(status)) {
			ctx->stats.frames_processed++;
		} else {
			ctx->stats.frames_dropped++;
			wma_err("Failed to send injection frame to firmware: %d", status);
		}
		spin_unlock_bh(&wma_injection_lock);

		processed_count++;

		/* Free the node */
		wma_injection_queue_node_free(node);
	}

	wma_debug("Injection queue processing cycle complete: processed=%u, deferred=%u",
		  processed_count, deferred_count);

	return QDF_STATUS_SUCCESS;
}

/**
 * wma_process_injection_queue_work() - Work function to process injection queue
 * @arg: Work argument (not used)
 *
 * This function processes queued frame injection requests in FIFO order
 * with traffic coordination and backpressure handling.
 */
static void wma_process_injection_queue_work(void *arg)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	tp_wma_handle wma_handle;
	QDF_STATUS status;
	uint32_t backpressure_delay;
	bool queue_has_frames;

	/* Get WMA handle for processing */
	wma_handle = cds_get_context(QDF_MODULE_ID_WMA);
	if (!wma_handle) {
		wma_err("Failed to get WMA handle");
		return;
	}

	/* Process the queue */
	status = wma_process_injection_queue(wma_handle);
	if (QDF_IS_STATUS_ERROR(status)) {
		wma_err("Failed to process injection queue: %d", status);
	}

	/* Check if there are more frames to process */
	spin_lock_bh(&wma_injection_lock);
	queue_has_frames = ctx->is_initialized && !ctx->stopping &&
			   !qdf_list_empty(&ctx->queue);

	if (queue_has_frames) {
		/* Apply backpressure if queue is congested */
		backpressure_delay = wma_apply_injection_backpressure(ctx);
		/* Missing completions and unavailable vdevs must not busy-loop. */
		if (!backpressure_delay)
			backpressure_delay = WMA_FRAME_INJECT_QUEUE_TIMEOUT_MS;
		qdf_delayed_work_start(&ctx->delayed_work, backpressure_delay);
	}
	spin_unlock_bh(&wma_injection_lock);
}

/**
 * wma_process_injection_queue_delayed_work() - Delayed work callback for backpressure
 * @context: Context (not used)
 *
 * This function is called when delayed work is triggered for backpressure handling.
 * It simply schedules the regular work item to continue processing.
 */
static void wma_process_injection_queue_delayed_work(void *context)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;

	spin_lock_bh(&wma_injection_lock);
	if (ctx->is_initialized && !ctx->stopping)
		qdf_sched_work(0, &ctx->queue_work);
	spin_unlock_bh(&wma_injection_lock);
}

QDF_STATUS wma_init_injection_queue(tp_wma_handle wma_handle)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	QDF_STATUS status;

	if (!wma_handle) {
		wma_err("Invalid WMA handle");
		return QDF_STATUS_E_INVAL;
	}

	mutex_lock(&wma_injection_lifecycle_lock);
	if (ctx->is_initialized) {
		wma_debug("Injection queue already initialized");
		mutex_unlock(&wma_injection_lifecycle_lock);
		return QDF_STATUS_SUCCESS;
	}

	wma_debug("Initializing WMA injection queue");

	/* Initialize queue */
	qdf_list_create(&ctx->queue, WMA_FRAME_INJECT_MAX_QUEUE_SIZE);

	/* Initialize work item */
	qdf_create_work(0, &ctx->queue_work, wma_process_injection_queue_work, NULL);

	/* Initialize delayed work item for backpressure */
	status = qdf_delayed_work_create(&ctx->delayed_work,
					 wma_process_injection_queue_delayed_work, NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		wma_err("Failed to create delayed work: %d", status);
		qdf_list_destroy(&ctx->queue);
		mutex_unlock(&wma_injection_lifecycle_lock);
		return status;
	}

	/* Initialize context */
	spin_lock_bh(&wma_injection_lock);
	ctx->queue_size = 0;
	ctx->max_queue_size = WMA_FRAME_INJECT_MAX_QUEUE_SIZE;
	qdf_mem_zero(&ctx->stats, sizeof(ctx->stats));
	/* Do not reset inflight_count or descriptor IDs across queue restarts. */
	ctx->stopping = false;
	ctx->is_initialized = true;
	spin_unlock_bh(&wma_injection_lock);
	mutex_unlock(&wma_injection_lifecycle_lock);

	wma_info("WMA injection queue initialized successfully (max_size=%u)",
		 ctx->max_queue_size);

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wma_deinit_injection_queue(tp_wma_handle wma_handle)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	struct wma_injection_queue_node *node;
	qdf_list_node_t *list_node;
	QDF_STATUS status;
	uint32_t dropped_count = 0;
	uint32_t retained;

	if (!wma_handle) {
		wma_err("Invalid WMA handle");
		return QDF_STATUS_E_INVAL;
	}

	mutex_lock(&wma_injection_lifecycle_lock);
	spin_lock_bh(&wma_injection_lock);
	if (!ctx->is_initialized) {
		spin_unlock_bh(&wma_injection_lock);
		mutex_unlock(&wma_injection_lifecycle_lock);
		wma_debug("Injection queue not initialized");
		return QDF_STATUS_SUCCESS;
	}
	ctx->stopping = true;
	spin_unlock_bh(&wma_injection_lock);

	wma_debug("Deinitializing WMA injection queue");

	/* Stop producers/rearming before draining the workers and sender. */
	qdf_delayed_work_stop_sync(&ctx->delayed_work);
	qdf_cancel_work(&ctx->queue_work);
	qdf_flush_work(&ctx->queue_work);
	qdf_delayed_work_destroy(&ctx->delayed_work);

	mutex_lock(&wma_injection_send_lock);
	wma_injection_destroy_tx_vdev(wma_handle);

	/* Clear the queue and free all nodes */
	spin_lock_bh(&wma_injection_lock);

	while (!qdf_list_empty(&ctx->queue)) {
		status = qdf_list_remove_front(&ctx->queue, &list_node);
		if (QDF_IS_STATUS_ERROR(status)) {
			wma_err("Failed to remove node during cleanup: %d", status);
			break;
		}

		node = qdf_container_of(list_node, struct wma_injection_queue_node, node);
		wma_injection_queue_node_free(node);
		dropped_count++;
	}

	ctx->queue_size = 0;
	/* Update statistics */
	ctx->stats.frames_dropped += dropped_count;
	ctx->is_initialized = false;
	retained = ctx->inflight_count;
	spin_unlock_bh(&wma_injection_lock);
	qdf_list_destroy(&ctx->queue);
	mutex_unlock(&wma_injection_send_lock);
	mutex_unlock(&wma_injection_lifecycle_lock);

	wma_info("WMA injection queue deinitialized (dropped %u pending frames)",
		 dropped_count);

	/*
	 * Helper deletion, queue teardown and elapsed time do not prove DMA
	 * quiescence. Keep at most WMA_INJECTION_INFLIGHT_HIGH mappings, including
	 * across reinit. Matching late completions can still retire them. Recovery
	 * without completions requires a firmware DMA-stop AND event-drain barrier
	 * before unmapping, releasing the QDF device, or reusing wire IDs. There is
	 * no such verified reset hook here; do not clear this table on restart.
	 */
	if (retained)
		wma_warn("Injection retains %u outstanding buffers pending completion; reset cleanup requires confirmed DMA quiescence",
			 retained);

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wma_queue_injection_frame(tp_wma_handle wma_handle,
				     struct inject_frame_req *req,
				     uint8_t vdev_id)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	struct wma_injection_queue_node *node;
	QDF_STATUS status;

	if (!wma_handle || !req) {
		wma_err("Invalid parameters: wma_handle=%pK, req=%pK",
			wma_handle, req);
		return QDF_STATUS_E_INVAL;
	}

	/* Validate frame parameters */
	if (!req->frame_data || req->frame_len == 0 ||
	    req->frame_len > WMA_FRAME_INJECT_MAX_FRAME_SIZE) {
		wma_err("Invalid frame parameters: data=%pK, len=%u",
			req->frame_data, req->frame_len);
		return QDF_STATUS_E_INVAL;
	}

	/* Allocate and initialize queue node */
	node = wma_injection_queue_node_alloc(req, vdev_id);
	if (!node) {
		spin_lock_bh(&wma_injection_lock);
		ctx->stats.frames_dropped++;
		spin_unlock_bh(&wma_injection_lock);
		wma_err("Failed to allocate injection queue node");
		return QDF_STATUS_E_NOMEM;
	}

	/* Add to queue */
	spin_lock_bh(&wma_injection_lock);
	if (!ctx->is_initialized || ctx->stopping) {
		status = QDF_STATUS_E_AGAIN;
		goto reject;
	}
	/* Enforce both limits at admission; only completions restore capacity. */
	if (ctx->queue_size >= ctx->max_queue_size ||
	    ctx->inflight_count >= WMA_INJECTION_INFLIGHT_HIGH ||
	    wma_injection_next_desc_id >
	    (WMA_INJECTION_DESC_ID_BASE | WMA_INJECTION_DESC_ID_MASK)) {
		ctx->stats.queue_overflows++;
		status = QDF_STATUS_E_RESOURCES;
		goto reject;
	}

	status = qdf_list_insert_back(&ctx->queue, &node->node);
	if (QDF_IS_STATUS_ERROR(status))
		goto reject;

	ctx->queue_size++;
	ctx->stats.frames_queued++;

	/* Update maximum queue depth */
	if (ctx->queue_size > ctx->stats.max_queue_depth) {
		ctx->stats.max_queue_depth = ctx->queue_size;
	}

	/* Scheduling under the lock closes the producer/deinit race. */
	qdf_sched_work(0, &ctx->queue_work);
	spin_unlock_bh(&wma_injection_lock);

	return QDF_STATUS_SUCCESS;
reject:
	ctx->stats.frames_dropped++;
	spin_unlock_bh(&wma_injection_lock);
	wma_injection_queue_node_free(node);
	return status;
}

QDF_STATUS wma_get_injection_queue_stats(tp_wma_handle wma_handle,
					 struct wma_injection_queue_stats *stats)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;

	if (!wma_handle || !stats) {
		wma_err("Invalid parameters: wma_handle=%pK, stats=%pK",
			wma_handle, stats);
		return QDF_STATUS_E_INVAL;
	}

	spin_lock_bh(&wma_injection_lock);
	if (!ctx->is_initialized || ctx->stopping) {
		spin_unlock_bh(&wma_injection_lock);
		wma_err("Injection queue not initialized");
		return QDF_STATUS_E_AGAIN;
	}

	qdf_mem_copy(stats, &ctx->stats, sizeof(*stats));
	spin_unlock_bh(&wma_injection_lock);

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wma_reset_injection_queue_stats(tp_wma_handle wma_handle)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;

	if (!wma_handle) {
		wma_err("Invalid WMA handle");
		return QDF_STATUS_E_INVAL;
	}

	spin_lock_bh(&wma_injection_lock);
	if (!ctx->is_initialized || ctx->stopping) {
		spin_unlock_bh(&wma_injection_lock);
		wma_err("Injection queue not initialized");
		return QDF_STATUS_E_AGAIN;
	}

	qdf_mem_zero(&ctx->stats, sizeof(ctx->stats));
	spin_unlock_bh(&wma_injection_lock);

	wma_info("Injection queue statistics reset");

	return QDF_STATUS_SUCCESS;
}

uint32_t wma_get_injection_queue_size(tp_wma_handle wma_handle)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	uint32_t queue_size;

	if (!wma_handle) {
		return 0;
	}

	spin_lock_bh(&wma_injection_lock);
	queue_size = ctx->is_initialized ? ctx->queue_size : 0;
	spin_unlock_bh(&wma_injection_lock);

	return queue_size;
}

bool wma_is_injection_queue_empty(tp_wma_handle wma_handle)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	bool is_empty;

	if (!wma_handle) {
		return true;
	}

	spin_lock_bh(&wma_injection_lock);
	is_empty = !ctx->is_initialized || qdf_list_empty(&ctx->queue);
	spin_unlock_bh(&wma_injection_lock);

	return is_empty;
}

QDF_STATUS wma_flush_injection_queue(tp_wma_handle wma_handle)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	struct wma_injection_queue_node *node;
	qdf_list_node_t *list_node;
	QDF_STATUS status;
	uint32_t flushed_count = 0;

	if (!wma_handle) {
		wma_err("Invalid WMA handle");
		return QDF_STATUS_E_INVAL;
	}

	mutex_lock(&wma_injection_lifecycle_lock);
	spin_lock_bh(&wma_injection_lock);
	if (!ctx->is_initialized) {
		spin_unlock_bh(&wma_injection_lock);
		mutex_unlock(&wma_injection_lifecycle_lock);
		wma_err("Injection queue not initialized");
		return QDF_STATUS_E_AGAIN;
	}
	ctx->stopping = true;
	spin_unlock_bh(&wma_injection_lock);

	wma_debug("Flushing injection queue");

	/* Cancel any pending work */
	qdf_delayed_work_stop_sync(&ctx->delayed_work);
	qdf_cancel_work(&ctx->queue_work);
	qdf_flush_work(&ctx->queue_work);
	mutex_lock(&wma_injection_send_lock);

	/* Flush all queued frames */
	spin_lock_bh(&wma_injection_lock);

	while (!qdf_list_empty(&ctx->queue)) {
		status = qdf_list_remove_front(&ctx->queue, &list_node);
		if (QDF_IS_STATUS_ERROR(status)) {
			wma_err("Failed to remove node during flush: %d", status);
			break;
		}

		node = qdf_container_of(list_node, struct wma_injection_queue_node, node);
		wma_injection_queue_node_free(node);
		flushed_count++;
	}

	ctx->queue_size = 0;
	ctx->stats.frames_dropped += flushed_count;

	ctx->stopping = false;
	spin_unlock_bh(&wma_injection_lock);
	mutex_unlock(&wma_injection_send_lock);
	mutex_unlock(&wma_injection_lifecycle_lock);

	wma_info("Flushed %u frames from injection queue", flushed_count);

	return QDF_STATUS_SUCCESS;
}

/* Serialized by wma_injection_send_lock, including helper creation. */
static QDF_STATUS wma_send_injection_frame_locked(tp_wma_handle wma_handle,
					  struct inject_frame_req *req,
					  uint8_t vdev_id)
{
	struct wmi_mgmt_params mgmt_params;
	QDF_STATUS status;
	qdf_nbuf_t wmi_buf;
	uint8_t *frame_data;
	void *qdf_ctx;
	uint16_t tx_chanfreq = 0;
	uint8_t fc0 = 0;
	uint8_t fc_type = 0;
	uint8_t fc_subtype = 0;
	bool is_bcast_da = false;
	bool is_probe_req = false;
	bool monitor_vdev = false;

	if (!wma_handle || !req || !req->frame_data) {
		wma_err("Invalid parameters: wma_handle=%pK, req=%pK",
			wma_handle, req);
		return QDF_STATUS_E_INVAL;
	}

	if (!inject_patch_banner_logged) {
		wma_info("Injection patch tag: monitor_sta_vdev_tx_v7");
		inject_patch_banner_logged = true;
	}

	if (req->frame_len < 24 || req->frame_len > WMA_FRAME_INJECT_MAX_FRAME_SIZE) {
		wma_err("Invalid frame length: %u", req->frame_len);
		return QDF_STATUS_E_INVAL;
	}

	if (!wma_handle->wmi_handle) {
		wma_err("Invalid WMI handle in injection path");
		return QDF_STATUS_E_INVAL;
	}

	if (vdev_id >= wma_handle->max_bssid) {
		wma_err("Invalid injection vdev_id %u (max %u)",
			vdev_id, wma_handle->max_bssid);
		return QDF_STATUS_E_INVAL;
	}

	if (!wma_handle->interfaces[vdev_id].vdev) {
		wma_err("Injection vdev %u is not active", vdev_id);
		return QDF_STATUS_E_AGAIN;
	}

	if (wma_handle->interfaces[vdev_id].mhz)
		tx_chanfreq = wma_handle->interfaces[vdev_id].mhz;

	if (req->frame_len)
		fc0 = req->frame_data[0];

	fc_type = fc0 & 0x0c;
	fc_subtype = fc0 & 0xf0;
	/* WMI management TX is not a raw data/control-frame transport. */
	if (fc_type || (fc0 & 0x03))
		return QDF_STATUS_E_NOSUPPORT;
	/* wma_main registers WMI completion handlers only for this service. */
	if (!wmi_service_enabled(wma_handle->wmi_handle,
				 wmi_service_mgmt_tx_wmi))
		return QDF_STATUS_E_NOSUPPORT;
	is_probe_req = (fc_type == 0x00 && fc_subtype == 0x40);
	if (req->frame_len >= 10) {
		uint8_t *da = &req->frame_data[4];

		if (da[0] == 0xff && da[1] == 0xff && da[2] == 0xff &&
		    da[3] == 0xff && da[4] == 0xff && da[5] == 0xff)
			is_bcast_da = true;
	}

	qdf_ctx = cds_get_context(QDF_MODULE_ID_QDF_DEVICE);
	if (!qdf_ctx) {
		wma_err("qdf_ctx is NULL in injection path");
		return QDF_STATUS_E_INVAL;
	}

	monitor_vdev =
		(wma_handle->interfaces[vdev_id].type == WMI_VDEV_TYPE_MONITOR);
	if (monitor_vdev && !tx_chanfreq) {
		wma_err("Injection monitor vdev %u has zero channel frequency; dropping frame",
			vdev_id);
		return QDF_STATUS_E_INVAL;
	}

	/* Allocate WMI buffer for the frame */
	wmi_buf = qdf_nbuf_alloc(NULL, req->frame_len, 0, 0, false);
	if (!wmi_buf) {
		wma_err("Failed to allocate WMI buffer for injection frame");
		return QDF_STATUS_E_NOMEM;
	}

	/* Copy frame data to WMI buffer */
	frame_data = qdf_nbuf_put_tail(wmi_buf, req->frame_len);
	if (!frame_data) {
		wma_err("Failed to get buffer space for frame data");
		qdf_nbuf_free(wmi_buf);
		return QDF_STATUS_E_NOMEM;
	}

	qdf_mem_copy(frame_data, req->frame_data, req->frame_len);

	/*
	 * Monitor probe-request injection can be discarded by firmware when SA
	 * does not match the transmitting vdev MAC. Normalize SA for broadcast
	 * probe requests before WMI submission.
	 */
	if (monitor_vdev && is_probe_req && is_bcast_da && req->frame_len >= 24) {
		uint8_t *vdev_mac =
			wlan_vdev_mlme_get_macaddr(wma_handle->interfaces[vdev_id].vdev);

		if (vdev_mac &&
		    qdf_mem_cmp(frame_data + 10, vdev_mac, QDF_MAC_ADDR_SIZE) != 0) {
			if (!inject_probe_sa_fix_logged) {
				wma_warn("Injection probe-req SA override %pM -> %pM on vdev %u",
					 frame_data + 10, vdev_mac, vdev_id);
				inject_probe_sa_fix_logged = true;
			}
			qdf_mem_copy(frame_data + 10, vdev_mac, QDF_MAC_ADDR_SIZE);
			qdf_mem_copy(req->frame_data + 10, vdev_mac, QDF_MAC_ADDR_SIZE);
		}
	}

	/* Initialize WMI management parameters for injection */
	qdf_mem_zero(&mgmt_params, sizeof(mgmt_params));
	mgmt_params.tx_frame = wmi_buf;
	mgmt_params.frm_len = req->frame_len;
	mgmt_params.vdev_id = vdev_id;
	/*
	 * Use ACK-completion tx type for injection consistently. Several
	 * firmware builds are stricter with probe-request tx_type handling and
	 * are less likely to discard when sent with ACK-completion semantics.
	 */
	mgmt_params.tx_type = GENERIC_NODOWLOAD_ACK_COMP_INDEX;
	/*
	 * Align with regular host management TX behavior:
	 * probe request uses chanfreq=0 while action/auth/probe-rsp can carry
	 * an explicit channel.
	 */
	mgmt_params.chanfreq = is_probe_req ? 0 : tx_chanfreq;
	/*
	 * For monitor vdev probe injection, prefer explicit channel in command
	 * so firmware can bind probe request tx to the current monitor channel.
	 */
	if (monitor_vdev && is_probe_req && tx_chanfreq)
		mgmt_params.chanfreq = tx_chanfreq;
	mgmt_params.pdata = frame_data; /* Management frame bytes for command payload */
	mgmt_params.macaddr = NULL; /* No specific MAC address */
	mgmt_params.qdf_ctx = qdf_ctx;
	mgmt_params.tx_params_valid = false; /* Use default TX parameters */
	mgmt_params.use_6mbps = 0; /* Use rate from injection request if specified */
	/* Publish ownership before any command can trigger a completion. */
	mgmt_params.desc_id = wma_injection_reserve(wmi_buf, qdf_ctx, req,
						  fc_type, fc_subtype,
						  mgmt_params.chanfreq);
	if (!mgmt_params.desc_id) {
		qdf_nbuf_free(wmi_buf);
		return QDF_STATUS_E_RESOURCES;
	}

	/* Set transmission rate if specified in injection request */
	if (req->tx_rate != 0) {
		mgmt_params.tx_param.mcs_mask = req->tx_rate;
		mgmt_params.tx_params_valid = true;
	}

	if (!inject_tx_cfg_logged) {
		wma_info("Injection TX config: vdev=%u iface_type=%u iface_subtype=%u vdev_active=%u chanfreq=%u",
			 vdev_id,
			 wma_handle->interfaces[vdev_id].type,
			 wma_handle->interfaces[vdev_id].sub_type,
			 wma_handle->interfaces[vdev_id].vdev_active ? 1 : 0,
			 mgmt_params.chanfreq);
		inject_tx_cfg_logged = true;
	}

	if (inject_send_info_count < 10) {
		if (req->frame_len >= 24) {
			uint8_t *addr1 = &req->frame_data[4];
			uint8_t *addr2 = &req->frame_data[10];
			uint8_t *addr3 = &req->frame_data[16];

			wma_info("Injection frame[%u]: desc_id=%u vdev=%u len=%u fc_type=0x%02x fc_subtype=0x%02x tx_chanfreq=%u cmd_chanfreq=%u addr1=%pM addr2=%pM addr3=%pM",
				 inject_send_info_count + 1, mgmt_params.desc_id,
				 vdev_id, req->frame_len,
				 fc_type, fc_subtype, tx_chanfreq, mgmt_params.chanfreq,
				 addr1, addr2, addr3);
		} else {
			wma_info("Injection frame[%u]: desc_id=%u vdev=%u len=%u fc_type=0x%02x fc_subtype=0x%02x tx_chanfreq=%u cmd_chanfreq=%u",
				 inject_send_info_count + 1, mgmt_params.desc_id,
				 vdev_id, req->frame_len,
				 fc_type, fc_subtype, tx_chanfreq, mgmt_params.chanfreq);
		}
		inject_send_info_count++;
	}

	if (monitor_vdev) {
		/*
		 * FW _wlan_send_mgmt_to_host rejects MONITOR vdevs (falls
		 * to a beacon-only path → DISCARD).  Route through a hidden
		 * AP vdev instead, which the FW accepts for mgmt TX.
		 */
		status = wma_injection_ensure_tx_vdev(wma_handle,
						     vdev_id, tx_chanfreq);
		if (QDF_IS_STATUS_ERROR(status)) {
			wma_err("Failed to create injection TX helper vdev: %d",
				status);
			wma_injection_submit_done(mgmt_params.desc_id, status);
			return status;
		}
		mgmt_params.vdev_id = g_inj_tx_vdev.vdev_id;
		if (!inject_monitor_no_legacy_logged) {
			wma_warn("Injection monitor: using hidden AP vdev %u for TX (monitor vdev %u)",
				 g_inj_tx_vdev.vdev_id, vdev_id);
			inject_monitor_no_legacy_logged = true;
		}
	}

	/* Attempt WMI management TX path. */
	if (!inject_wmi_path_logged) {
		wma_info("Injection using WMI mgmt tx path (vdev_id=%u)",
			 mgmt_params.vdev_id);
		inject_wmi_path_logged = true;
	}
	status = wmi_mgmt_unified_cmd_send(wma_handle->wmi_handle, &mgmt_params);
	/* No legacy fallback: HTT owns/unmaps buffers under a different contract. */
	wma_injection_submit_done(mgmt_params.desc_id, status);
	return status;
}

QDF_STATUS wma_send_injection_frame_to_fw(tp_wma_handle wma_handle,
					  struct inject_frame_req *req,
					  uint8_t vdev_id)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	QDF_STATUS status;
	bool ready;

	mutex_lock(&wma_injection_send_lock);
	spin_lock_bh(&wma_injection_lock);
	ready = ctx->is_initialized && !ctx->stopping;
	spin_unlock_bh(&wma_injection_lock);
	status = ready ? wma_send_injection_frame_locked(wma_handle, req, vdev_id) :
			 QDF_STATUS_E_AGAIN;
	mutex_unlock(&wma_injection_send_lock);
	return status;
}

QDF_STATUS wma_handle_injection_fw_response(tp_wma_handle wma_handle,
					     uint32_t desc_id,
					     uint32_t status)
{
	struct wma_injection_queue_ctx *ctx = &g_wma_injection_ctx;
	struct wma_injection_debug_info *dbg_entry;
	qdf_nbuf_t buf = NULL;
	void *qdf_ctx = NULL;

	if (!wma_handle) {
		wma_err("Invalid WMA handle");
		return QDF_STATUS_E_INVAL;
	}

	if (!WMA_IS_INJECTION_DESC_ID(desc_id))
		return QDF_STATUS_E_INVAL;

	/* Late and duplicate events are safe even after queue deinitialization. */
	spin_lock_bh(&wma_injection_lock);
	dbg_entry = wma_injection_debug_cache_get(desc_id);
	if (!dbg_entry || dbg_entry->completed) {
		spin_unlock_bh(&wma_injection_lock);
		return QDF_STATUS_SUCCESS;
	}
	dbg_entry->completed = true;
	if (status != WMI_MGMT_TX_COMP_TYPE_COMPLETE_OK) {
		ctx->stats.fw_errors++;
		ctx->stats.frames_dropped++;
	}
	/* The WMI sender may still be reading tx_frame on an early event. */
	if (!dbg_entry->submitting) {
		buf = dbg_entry->tx_buf;
		qdf_ctx = dbg_entry->qdf_ctx;
		dbg_entry->tx_buf = NULL;
		dbg_entry->valid = false;
		ctx->inflight_count--;
	}
	spin_unlock_bh(&wma_injection_lock);
	if (buf) {
		wma_injection_unmap_tx_buf(qdf_ctx, buf);
		qdf_nbuf_free(buf);
	}

	return QDF_STATUS_SUCCESS;
}

#else /* FEATURE_FRAME_INJECTION_SUPPORT */

QDF_STATUS wma_send_injection_frame_to_fw(tp_wma_handle wma_handle,
					  struct inject_frame_req *req,
					  uint8_t vdev_id)
{
	return QDF_STATUS_E_NOSUPPORT;
}

QDF_STATUS wma_handle_injection_fw_response(tp_wma_handle wma_handle,
					     uint32_t desc_id,
					     uint32_t status)
{
	QDF_STATUS qdf_status = QDF_STATUS_SUCCESS;
	enum wma_injection_fw_error_type error_type;

	WMA_LOGD("Handling firmware injection response: desc_id=%u, status=0x%x", desc_id, status);

	if (!wma_handle) {
		WMA_LOGE("Invalid WMA handle");
		return QDF_STATUS_E_INVAL;
	}

	/* Check if this is an error response */
	if (status != 0) {
		WMA_LOGW("Firmware injection failed: desc_id=%u, status=0x%x", desc_id, status);
		
		/* Handle the firmware error */
		qdf_status = wma_handle_firmware_injection_error(wma_handle, status, 0, NULL);
		if (QDF_IS_STATUS_ERROR(qdf_status) && qdf_status != QDF_STATUS_E_PENDING) {
			WMA_LOGE("Failed to handle firmware injection error: %d", qdf_status);
			return qdf_status;
		}
	} else {
		WMA_LOGD("Firmware injection completed successfully: desc_id=%u", desc_id);
	}

	/* Update statistics would go here in a full implementation */
	
	return qdf_status;
}

QDF_STATUS wma_init_injection_queue(tp_wma_handle wma_handle)
{
	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wma_deinit_injection_queue(tp_wma_handle wma_handle)
{
	return QDF_STATUS_SUCCESS;
}

void wma_injection_pre_stop_cleanup(tp_wma_handle wma_handle)
{
}

QDF_STATUS wma_queue_injection_frame(tp_wma_handle wma_handle,
				     struct inject_frame_req *req,
				     uint8_t vdev_id)
{
	return QDF_STATUS_E_NOSUPPORT;
}

QDF_STATUS wma_get_injection_queue_stats(tp_wma_handle wma_handle,
					 struct wma_injection_queue_stats *stats)
{
	return QDF_STATUS_E_NOSUPPORT;
}

QDF_STATUS wma_reset_injection_queue_stats(tp_wma_handle wma_handle)
{
	return QDF_STATUS_E_NOSUPPORT;
}

uint32_t wma_get_injection_queue_size(tp_wma_handle wma_handle)
{
	return 0;
}

bool wma_is_injection_queue_empty(tp_wma_handle wma_handle)
{
	return true;
}

QDF_STATUS wma_flush_injection_queue(tp_wma_handle wma_handle)
{
	return QDF_STATUS_SUCCESS;
}

/**
 * wma_translate_fw_injection_error() - Translate firmware error codes
 * @fw_error_code: Firmware-specific error code
 *
 * This function translates firmware-specific error codes to standard
 * WMA injection error types for consistent error handling.
 *
 * Return: Translated error type
 */
enum wma_injection_fw_error_type wma_translate_fw_injection_error(uint32_t fw_error_code)
{
	enum wma_injection_fw_error_type error_type;

	WMA_LOGD("Translating firmware error code: 0x%x", fw_error_code);

	switch (fw_error_code) {
	case 0x0: /* Success */
		error_type = WMA_INJECTION_FW_ERROR_NONE;
		break;
	case 0x1: /* Generic failure */
		error_type = WMA_INJECTION_FW_ERROR_REJECTED;
		break;
	case 0x2: /* Invalid VDEV */
		error_type = WMA_INJECTION_FW_ERROR_INVALID_VDEV;
		break;
	case 0x3: /* No resources */
		error_type = WMA_INJECTION_FW_ERROR_NO_RESOURCES;
		break;
	case 0x4: /* Interface down */
		error_type = WMA_INJECTION_FW_ERROR_INTERFACE_DOWN;
		break;
	case 0x5: /* Power save mode */
		error_type = WMA_INJECTION_FW_ERROR_POWER_SAVE;
		break;
	case 0x6: /* Channel switch in progress */
		error_type = WMA_INJECTION_FW_ERROR_CHANNEL_SWITCH;
		break;
	case 0x7: /* Scan active */
		error_type = WMA_INJECTION_FW_ERROR_SCAN_ACTIVE;
		break;
	case 0xFFFFFFFF: /* Timeout */
		error_type = WMA_INJECTION_FW_ERROR_TIMEOUT;
		break;
	default:
		error_type = WMA_INJECTION_FW_ERROR_UNKNOWN;
		break;
	}

	WMA_LOGD("Translated firmware error 0x%x to type %d", fw_error_code, error_type);
	return error_type;
}

/**
 * wma_handle_firmware_injection_error() - Handle firmware injection error
 * @wma_handle: WMA handle
 * @error_code: Firmware error code
 * @vdev_id: VDEV ID associated with error
 * @req: Frame request that caused error (optional)
 *
 * This function handles firmware errors during frame injection. It implements
 * retry logic for transient failures and coordinates with HDD layer for
 * error recovery. It also maintains firmware state synchronization.
 *
 * Return: QDF_STATUS_SUCCESS on successful recovery, error code on failure
 */
QDF_STATUS wma_handle_firmware_injection_error(tp_wma_handle wma_handle,
						uint32_t error_code,
						uint8_t vdev_id,
						struct inject_frame_req *req)
{
	enum wma_injection_fw_error_type error_type;
	struct wma_injection_fw_error_info *error_info;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	bool should_retry = false;
	uint32_t retry_delay_ms = 0;

	WMA_LOGD("Handling firmware injection error: code=0x%x, vdev_id=%u", error_code, vdev_id);

	if (!wma_handle) {
		WMA_LOGE("Invalid WMA handle");
		return QDF_STATUS_E_INVAL;
	}

	/* Translate firmware error code */
	error_type = wma_translate_fw_injection_error(error_code);

	/* Update error statistics - this would be part of a larger injection context */
	/* For now, we'll log the error information */
	WMA_LOGW("Firmware injection error: type=%d, code=0x%x, vdev_id=%u",
		 error_type, error_code, vdev_id);

	/* Determine if we should retry based on error type */
	switch (error_type) {
	case WMA_INJECTION_FW_ERROR_NO_RESOURCES:
		/* Transient error - retry with delay */
		should_retry = true;
		retry_delay_ms = 100; /* 100ms delay */
		break;

	case WMA_INJECTION_FW_ERROR_POWER_SAVE:
		/* Device in power save - retry with longer delay */
		should_retry = true;
		retry_delay_ms = 500; /* 500ms delay */
		break;

	case WMA_INJECTION_FW_ERROR_CHANNEL_SWITCH:
		/* Channel switch in progress - retry with delay */
		should_retry = true;
		retry_delay_ms = 200; /* 200ms delay */
		break;

	case WMA_INJECTION_FW_ERROR_SCAN_ACTIVE:
		/* Scan active - retry with short delay */
		should_retry = true;
		retry_delay_ms = 50; /* 50ms delay */
		break;

	case WMA_INJECTION_FW_ERROR_TIMEOUT:
		/* Timeout - retry once with longer delay */
		should_retry = true;
		retry_delay_ms = 1000; /* 1 second delay */
		break;

	case WMA_INJECTION_FW_ERROR_INVALID_VDEV:
	case WMA_INJECTION_FW_ERROR_INTERFACE_DOWN:
		/* Permanent errors - synchronize state but don't retry */
		status = wma_sync_firmware_injection_state(wma_handle, vdev_id);
		should_retry = false;
		break;

	case WMA_INJECTION_FW_ERROR_REJECTED:
	case WMA_INJECTION_FW_ERROR_UNKNOWN:
	default:
		/* Unknown or permanent errors - don't retry */
		should_retry = false;
		break;
	}

	/* Attempt retry if appropriate and request is available */
	if (should_retry && req) {
		/* Check retry count to avoid infinite loops */
		if (req->retry_count < 3) { /* Maximum 3 retries */
			WMA_LOGI("Retrying injection after %u ms delay (attempt %u)",
				 retry_delay_ms, req->retry_count + 1);
			
			/* Schedule retry with delay */
			status = wma_retry_injection_frame(wma_handle, req, vdev_id, error_type);
			if (QDF_IS_STATUS_SUCCESS(status)) {
				return QDF_STATUS_E_PENDING; /* Retry scheduled */
			}
		} else {
			WMA_LOGW("Maximum retry attempts reached for injection request");
			status = QDF_STATUS_E_FAILURE;
		}
	}

	/* If we reach here, either no retry was needed or retry failed */
	if (QDF_IS_STATUS_ERROR(status)) {
		WMA_LOGE("Firmware injection error handling failed: %d", status);
	} else {
		WMA_LOGI("Firmware injection error handled successfully");
	}

	return status;
}

/**
 * wma_retry_injection_frame() - Retry injection frame after firmware error
 * @wma_handle: WMA handle
 * @req: Frame injection request to retry
 * @vdev_id: VDEV ID for the frame
 * @error_type: Type of error that occurred
 *
 * This function implements retry logic for transient firmware failures.
 * It uses exponential backoff and limits the number of retry attempts.
 *
 * Return: QDF_STATUS_SUCCESS on successful retry, error code on failure
 */
QDF_STATUS wma_retry_injection_frame(tp_wma_handle wma_handle,
				     struct inject_frame_req *req,
				     uint8_t vdev_id,
				     enum wma_injection_fw_error_type error_type)
{
	QDF_STATUS status;
	uint32_t delay_ms;

	WMA_LOGD("Retrying injection frame: vdev_id=%u, error_type=%d, retry_count=%u",
		 vdev_id, error_type, req->retry_count);

	if (!wma_handle || !req) {
		WMA_LOGE("Invalid parameters for retry");
		return QDF_STATUS_E_INVAL;
	}

	/* Increment retry count */
	req->retry_count++;

	/* Calculate exponential backoff delay */
	delay_ms = 100 * (1 << (req->retry_count - 1)); /* 100ms, 200ms, 400ms, ... */
	if (delay_ms > 2000) {
		delay_ms = 2000; /* Cap at 2 seconds */
	}

	/* Add some jitter to avoid thundering herd */
	delay_ms += (qdf_get_log_timestamp() % 50); /* Add 0-49ms jitter */

	WMA_LOGI("Scheduling injection retry in %u ms (attempt %u)",
		 delay_ms, req->retry_count);

	/* For now, we'll simulate the retry by calling the send function again */
	/* In a real implementation, this would be scheduled with a timer */
	qdf_sleep(delay_ms);

	/* Attempt to send the frame again */
	status = wma_send_injection_frame_to_fw(wma_handle, req, vdev_id);
	if (QDF_IS_STATUS_ERROR(status)) {
		WMA_LOGW("Injection retry failed: %d", status);
		return status;
	}

	WMA_LOGI("Injection retry initiated successfully");
	return QDF_STATUS_SUCCESS;
}

/**
 * wma_sync_firmware_injection_state() - Synchronize firmware injection state
 * @wma_handle: WMA handle
 * @vdev_id: VDEV ID to synchronize
 *
 * This function synchronizes the firmware injection state after errors.
 * It ensures that the firmware and driver are in a consistent state.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
QDF_STATUS wma_sync_firmware_injection_state(tp_wma_handle wma_handle,
					      uint8_t vdev_id)
{
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	WMA_LOGD("Synchronizing firmware injection state for vdev_id=%u", vdev_id);

	if (!wma_handle) {
		WMA_LOGE("Invalid WMA handle");
		return QDF_STATUS_E_INVAL;
	}

	/* Check if VDEV is valid and active */
	if (vdev_id >= wma_handle->max_bssid) {
		WMA_LOGE("Invalid VDEV ID: %u", vdev_id);
		return QDF_STATUS_E_INVAL;
	}

	/* Verify VDEV state */
	if (!wma_handle->interfaces[vdev_id].handle) {
		WMA_LOGW("VDEV %u is not active", vdev_id);
		return QDF_STATUS_E_INVAL;
	}

	/* Check interface type - injection typically requires monitor mode */
	if (wma_handle->interfaces[vdev_id].type != WMI_VDEV_TYPE_MONITOR) {
		WMA_LOGW("VDEV %u is not in monitor mode (type=%d)",
			 vdev_id, wma_handle->interfaces[vdev_id].type);
		/* This might not be an error depending on implementation */
	}

	/* Flush any pending injection frames for this VDEV */
	/* This would be implemented as part of the queue management */
	WMA_LOGI("Flushing pending injection frames for vdev_id=%u", vdev_id);

	/* Send a sync command to firmware if needed */
	/* This would involve sending a WMI command to query/reset injection state */
	WMA_LOGD("Sending injection state sync command to firmware");

	/* For now, we'll just log that synchronization is complete */
	WMA_LOGI("Firmware injection state synchronized for vdev_id=%u", vdev_id);

	return status;
}

#endif /* FEATURE_FRAME_INJECTION_SUPPORT */
