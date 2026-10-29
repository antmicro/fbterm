#ifndef DRMDEV_H
#define DRMDEV_H

#include "screen.h"

#include <libdrm/drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>

#include <memory>

class DrmDevWatch;

class DrmDev : public Screen {
public:
	typedef void (*PageFlipCompletedCallback)(void *user_data);

	bool acquireLease(int &lease_fd);
	bool releaseLease(int &lease_fd);
	bool handleLeaseReleased();
	bool restoreScanout();
	void handleDrmEvents();

	void setPageFlipCompletedCallback(
			PageFlipCompletedCallback callback,
			void *user_data);

	DrmDev *getDrmDev() override { return this; }

	bool pageFlipPending() const;
	void suspendRendering();
	void resumeRendering();
private:
	friend class DrmDevWatch;
	friend class Screen;
	static DrmDev *initDrmDev();
	void initDrmWatch();

	DrmDev();
	~DrmDev();

	virtual void switchVc(bool enter);
	virtual void setupOffset();
	virtual void setupPalette(bool restore);
	virtual const s8 *drvId();
	virtual void present();

	bool setup(void);
	void cleanup(void);

	bool findPrimaryPlane();
	bool findCursorPlane();

	static void pageFlipHandler(
		int fd,
		unsigned int sequence,
		unsigned int tv_sec,
		unsigned int tv_usec,
		void *user_data);

	DrmDevWatch* mDrmWatch = nullptr;

	PageFlipCompletedCallback mPageFlipCompletedCallback;
	void *mPageFlipCompletedUserData;

	s32 drm_fd = -1;
	u32 drm_crtc_id = 0;
	u32 drm_crtc_index = 0;
	u32 drm_connector_id = 0;
	u32 drm_encoder_id = 0;
	u32 drm_primary_plane_id = 0;
	u32 drm_cursor_plane_id = 0;

	u8 *back_buffer = nullptr;

	// Used for tracking, which buffer is currently displayed.
	u32 front_buffer_idx = 0;
	u32 flip_buffer_idx = 1;

	// 2 for each of buffers (flip buffer and front buffer).
	u32 drm_fb_ids[2] = {0, 0};
	u32 drm_handles[2] = {0, 0};
	u32 drm_pitches[2] = {0, 0};
	u64 drm_sizes[2] = {0, 0};
	u8 *drm_maps[2] = {nullptr, nullptr};

	drmModeModeInfo drm_mode = {};
	drmModeCrtc *drm_saved_crtc = nullptr;
	uint32_t drm_lessee_id = 0;
	bool drm_lease_active = false;
	bool drm_rendering_enabled = true;

	bool drm_page_flip_pending = false;
	bool pending_present = true;
};
#endif // DRMDEV_H
