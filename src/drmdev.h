#ifndef DRMDEV_H
#define DRMDEV_H

#include "screen.h"

#include <libdrm/drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>

class DrmDev : public Screen {
private:
	friend class Screen;
	static DrmDev *initDrmDev();

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

	s32 drm_fd = -1;
	u32 drm_crtc_id = 0;
	u32 drm_crtc_index = 0;
	u32 drm_connector_id = 0;
	u32 drm_encoder_id = 0;
	u32 drm_primary_plane_id = 0;
	u32 drm_cursor_plane_id = 0;
	u32 drm_fb_id = 0;
	u32 drm_handle = 0;
	u32 drm_pitch = 0;
	u64 drm_size = 0;
	drmModeModeInfo drm_mode = {};
	drmModeCrtc *drm_saved_crtc = nullptr;
	u8 *drm_map = nullptr;
};
#endif // DRMDEV_H
