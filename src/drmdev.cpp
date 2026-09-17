#include "drmdev.h"
#include "io.h"

#include <filesystem>
#include <libdrm/drm_mode.h>
#include <vector>
#include <string>

#include <drm_mode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <time.h>

#include <libdrm/drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>

#ifdef DRM_DEBUG
#define LOG(fmt, args...) fprintf(stderr, "[drm] " fmt "\n", ##args)
#else
#define LOG(fmt, args...)
#endif

class DrmDevWatch : public IoPipe
{
public:
	DrmDevWatch(DrmDev &owner, int fd)
		: mOwner(owner)
	{
		setFd(fd);
	}

	~DrmDevWatch()
	{
		mOwner.mDrmWatch = nullptr;
	}

	void ready(bool isread) override
	{
		if (!isread)
			return;

		mOwner.handleDrmEvents();
	}

	// DRM events are dispatched through drmHandleEvent(), not read().
	void readyRead(s8 *buf, u32 len) override
	{
		(void)buf;
		(void)len;
	}
private:
	DrmDev &mOwner;
};

// Picks which encoder actually serves a connector - prefers whatever's
// already actively bound (conn->encoder_id) over any other candidate.
static drmModeEncoder *findEncoderForConnector(s32 fd, drmModeConnector *conn)
{
	if (conn->encoder_id) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
		if (enc) return enc;
	}

	for (int i = 0; i < conn->count_encoders; i++) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[i]);
		if (enc) return enc;
	}

	return nullptr;
}

// Picks which CRTC to use for a given encoder - prefers whatever it's
// already bound to over any other CRTC it could theoretically reach.
static bool findCrtcForEncoder(drmModeRes *res, drmModeEncoder *enc, u32 *crtcId, u32 *crtcIdx)
{
	if (enc->crtc_id) {
		*crtcId = enc->crtc_id;

		for (int i = 0; i < res->count_crtcs; i++) {
			if (res->crtcs[i] == enc->crtc_id) {
				*crtcIdx = i;
				return true;
			}
		}

		return false;
	}

	for (int i = 0; i < res->count_crtcs; i++) {
		if (enc->possible_crtcs & (1 << i)) {
			*crtcId = res->crtcs[i];
			*crtcIdx = i;
			return true;
		}
	}

	return false;
}

// Finds a connector by its type name (e.g. "HDMI-A-2", "eDP-1", "DisplayPort-1", etc.)
static bool findConnectorByName(s32 fd, drmModeRes *res, const char *name, drmModeConnector **outConn)
{
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn) continue;

		char connName[32];
		snprintf(connName, sizeof(connName), "%s-%d", drmModeGetConnectorTypeName(conn->connector_type), conn->connector_type_id);

		if (!strcmp(connName, name)) {
			*outConn = conn;
			return true;
		}

		drmModeFreeConnector(conn);
	}

	return false;
}

uint32_t GetPropertyId(s32 fd, uint32_t obj_id, uint32_t obj_type, const char* name)
{
    uint32_t prop_id = 0;

    drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (!props) return 0;

    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
        if (!prop) continue;

        if (strcmp(prop->name, name) == 0) {
            prop_id = prop->prop_id;
            drmModeFreeProperty(prop);
            break;
        }
        drmModeFreeProperty(prop);
    }

    drmModeFreeObjectProperties(props);
    return prop_id;
}

uint32_t GetPlaneType(s32 fd, uint32_t plane_id)
{
    drmModeObjectProperties *props =
        drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) {
        fprintf(stderr, "Failed to get properties for DRM plane %u!", plane_id);
        return uint32_t(-1);
    }

    uint32_t type_prop_id =
        GetPropertyId(fd, plane_id, DRM_MODE_OBJECT_PLANE, "type");

    for (uint32_t i = 0; i < props->count_props; i++) {
        if (props->props[i] == type_prop_id) {
            uint32_t type_val = (uint32_t)props->prop_values[i];
            drmModeFreeObjectProperties(props);
            return type_val;
        }
    }

    drmModeFreeObjectProperties(props);
    return uint32_t(-1);
}

uint32_t FindPlaneByType(s32 fd, u32 crtc_idx, uint32_t planeType)
{
	drmModePlaneRes *planes = drmModeGetPlaneResources(fd);
	if (!planes)
		return 0;

	uint32_t planeId = 0;

	for (u32 i = 0; i < planes->count_planes; ++i) {
		u32 id = planes->planes[i];

		if (GetPlaneType(fd, id) != planeType)
			continue;

		drmModePlane *plane = drmModeGetPlane(fd, id);
		if (!plane)
			continue;

		if (plane->possible_crtcs & (1U << crtc_idx)) {
			planeId = id;
			drmModeFreePlane(plane);
			break;
		}

		drmModeFreePlane(plane);
	}

	drmModeFreePlaneResources(planes);
	return planeId;
}

static bool isEmbeddedConnector(s32 connector_type)
{
	return connector_type == DRM_MODE_CONNECTOR_LVDS ||
		   connector_type == DRM_MODE_CONNECTOR_eDP  ||
		   connector_type == DRM_MODE_CONNECTOR_DSI;
}

static bool isConnectorUsable(drmModeConnectorPtr conn)
{
	// Only use connected connectors with at least one mode
	return (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0);
}

static bool findTheBestConnector(s32 fd, drmModeRes *res, drmModeConnector **outConn)
{
	drmModeConnector *bestConn = nullptr;

	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn) continue;

		if (!isConnectorUsable(conn)) {
			drmModeFreeConnector(conn);
			continue;
		}

		if(!bestConn) { // mark first as best
			bestConn = conn;
		} else {
			// New connector is external - pick it
			if(isEmbeddedConnector(bestConn->connector_type) && !isEmbeddedConnector(conn->connector_type)) {
				drmModeFreeConnector(bestConn);
				bestConn = conn;
			} else {
				drmModeFreeConnector(conn);
			}
		}
	}

	if (bestConn) {
		*outConn = bestConn;
		return true;
	}

	return false;
}

struct DrmCandidate {
	s32 fd = -1;
	drmModeRes *res = nullptr;
	drmModeConnector *conn = nullptr;
};

static void freeCandidate(DrmCandidate *c)
{
	if (c->conn) drmModeFreeConnector(c->conn);
	if (c->res) drmModeFreeResources(c->res);
	if (c->fd >= 0) {
		drmDropMaster(c->fd);
		close(c->fd);
	}
	*c = DrmCandidate();
}

// Tries one /dev/dri/cardN device and finds the best connector on it.
// If 'wanted' is provided, and isn't matched exactly the function fails.
static bool tryDevice(const char *path, const char *wanted, DrmCandidate *out)
{
	std::filesystem::path file(path);
	if (!std::filesystem::is_character_file(file)) return false;

	s32 fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		LOG("open %s: %s", path, strerror(errno));
		return false;
	}

	// Grabbing master forced DRM to reprobe the connectors and ensures that data is up-to-date.
	if (drmSetMaster(fd)) {
		LOG("drmSetMaster %s: %s (another process may hold master)", path, strerror(errno));
		close(fd);
		return false;
	}

	drmModeRes *res = drmModeGetResources(fd);
	if (!res) {
		LOG("drmModeGetResources %s: %s", path, strerror(errno));
		drmDropMaster(fd);
		close(fd);
		return false;
	}

	drmModeConnector *conn = nullptr;
	if (wanted) {
		if (!findConnectorByName(fd, res, wanted, &conn)) {
			LOG("%s: connector %s not found", path, wanted);
		} else if (!isConnectorUsable(conn)) {
			LOG("%s: connector %s is not usable", path, wanted);
			drmModeFreeConnector(conn);
			conn = nullptr;
		}
	} else if (!findTheBestConnector(fd, res, &conn)) {
		LOG("%s: no usable connector", path);
	}

	if (!conn) {
		drmModeFreeResources(res);
		drmDropMaster(fd);
		close(fd);
		return false;
	}

	out->fd = fd;
	out->res = res;
	out->conn = conn;
	return true;
}

std::vector<std::filesystem::path> listDrmDevices() {
	std::vector<std::filesystem::path> devices;
	std::filesystem::path dri_path("/dev/dri/");
	if (std::filesystem::exists(dri_path) && std::filesystem::is_directory(dri_path)) {
		for(auto entry : std::filesystem::directory_iterator(dri_path)) {
			if(!entry.is_character_file())
				continue;

			auto path = entry.path();
			if(path.filename().string().rfind("card") == 0) {
				devices.push_back(path);
			}
		}
	}
	return devices;
}

// Picks which device+connector pair to use. If the user requests DRM_DEVICE and/or
// DRM_CONNECTOR, that preference must be satisfied exactly. Otherwise the function
// will pick the best pair by enumerating over cards and conns (preferring external connectors).
static bool selectDrmDeviceAndConnector(DrmCandidate *chosen)
{
	char *devpath = getenv("DRM_DEVICE");
	char *wanted = getenv("DRM_CONNECTOR");
	if (wanted) LOG("DRM_CONNECTOR=%s", wanted);

	if (devpath) {
		LOG("DRM_DEVICE=%s", devpath);
		if (!tryDevice(devpath, wanted, chosen)) {
			LOG("%s: not usable", devpath);
			return false;
		}
		return true;
	}

	auto devices = listDrmDevices();
	if(devices.empty()) {
		LOG("No /dev/dri/card* devices found");
		return false;
	}

	bool found = false;
	for (auto device : devices) {
		DrmCandidate cand;
		if (!tryDevice(device.c_str(), wanted, &cand)) continue;

		if (wanted) {
			*chosen = cand;
			found = true;
			break;
		}

		if (!found) {
			*chosen = cand;
			found = true;
		} else if (isEmbeddedConnector(chosen->conn->connector_type) &&
			   !isEmbeddedConnector(cand.conn->connector_type)) {
			freeCandidate(chosen);
			*chosen = cand;
		} else {
			freeCandidate(&cand);
		}
	}

	if (!found) LOG("no usable DRI device/connector found");
	return found;
}

void DrmDev::cleanup()
{
	if (drm_saved_crtc) {
		// switchVc(false) may have already dropped master by the time we get here
		if (drmSetMaster(drm_fd)) {
			fprintf(stderr, "Could not reacquire master to restore CRTC %u: %s\n", drm_saved_crtc->crtc_id, strerror(errno));
		} else {
			int ret = drmModeSetCrtc(drm_fd, drm_saved_crtc->crtc_id, drm_saved_crtc->buffer_id,
				drm_saved_crtc->x, drm_saved_crtc->y, &drm_connector_id, 1, &drm_saved_crtc->mode);
			if(ret) {
				fprintf(stderr, "Could not restore saved CRTC %u: %s\n", drm_saved_crtc->crtc_id, strerror(errno));
			}
		}
		drmModeFreeCrtc(drm_saved_crtc);
		drm_saved_crtc = 0;
	}

	if (drm_fb_id) {
		drmModeRmFB(drm_fd, drm_fb_id);
		drm_fb_id = 0;
	}

	if (drm_handle) {
		struct drm_mode_destroy_dumb dreq;
		memset(&dreq, 0, sizeof(dreq));
		dreq.handle = drm_handle;
		ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		drm_handle = 0;
	}

	if (drm_fd >= 0) {
		drmDropMaster(drm_fd);
		close(drm_fd);
		drm_fd = -1;
	}

	if (drm_map && drm_map != MAP_FAILED) {
		munmap(drm_map, drm_size);
		drm_map = nullptr;
	}
}

bool DrmDev::restoreScanout()
{
	if (drm_fd < 0 || drm_crtc_id == 0 || drm_fb_id == 0)
		return false;

	if (drmModeSetCrtc(drm_fd, drm_crtc_id, drm_fb_id, 0, 0, &drm_connector_id, 1, &drm_mode) != 0) {
		perror("drmModeSetCrtc");
		return false;
	}

	return true;
}

bool DrmDev::setup() {
	LOG("probing DRM/KMS backend");

	DrmCandidate chosen;
	if (!selectDrmDeviceAndConnector(&chosen)) {
		return false;
	}

	drm_fd = chosen.fd;
	drmModeRes *res = chosen.res;
	drmModeConnector *conn = chosen.conn;

	// tryDevice() already grabbed master on this fd, don't need to do it again.
	// cleanup() will drop master on failure path
	LOG("using connector %s-%d", drmModeGetConnectorTypeName(conn->connector_type), conn->connector_type_id);

	drmModeEncoder *enc = findEncoderForConnector(drm_fd, conn);
	if (!enc || !findCrtcForEncoder(res, enc, &drm_crtc_id, &drm_crtc_index)) {
		LOG("no usable crtc for connector %u", conn->connector_id);
		if (enc) drmModeFreeEncoder(enc);
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		cleanup();
		return false;
	}
	drmModeFreeEncoder(enc);

	drm_connector_id = conn->connector_id;
	drm_mode = conn->modes[0];

	LOG("using connector %u, crtc %u, mode %s %ux%u@%uHz",
		conn->connector_id, drm_crtc_id, conn->modes[0].name,
		conn->modes[0].hdisplay, conn->modes[0].vdisplay, conn->modes[0].vrefresh);

	if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
		fprintf(stderr, "Failed to enable universal planes: %s\n", strerror(errno));
	}

	drm_primary_plane_id = FindPlaneByType(drm_fd, drm_crtc_index, DRM_PLANE_TYPE_PRIMARY);
	if (!drm_primary_plane_id) {
		fprintf(stderr, "Failed to find primary DRM plane\n");
		return false;
	}

	drm_cursor_plane_id = FindPlaneByType(drm_fd, drm_crtc_index, DRM_PLANE_TYPE_CURSOR);
	if (!drm_cursor_plane_id) {
		fprintf(stderr, "Failed to find cursor DRM plane\n");
		return false;
	}

	struct drm_mode_create_dumb creq = {0};
	creq.width = drm_mode.hdisplay;
	creq.height = drm_mode.vdisplay;
	creq.bpp = 32; // ARGB

	if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		LOG("create dumb buffer: %s", strerror(errno));
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		cleanup();
		return false;
	}
	drm_handle = creq.handle;

	if(drmModeAddFB(drm_fd, drm_mode.hdisplay, drm_mode.vdisplay, 24, 32, creq.pitch, creq.handle, &drm_fb_id)) {
		LOG("drmModeAddFB: %s", strerror(errno));
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		cleanup();
		return false;
	}
	LOG("created dumb buffer: handle=%u, pitch=%u, size=%llu", drm_handle, creq.pitch, (unsigned long long)creq.size);
	drm_pitch = creq.pitch;

	// MEMORY MAPPING
	// Mapping GPU-side
	struct drm_mode_map_dumb mreq = {0};
	mreq.handle = creq.handle;
	if(ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		LOG("map dumb buffer: %s", strerror(errno));
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		cleanup();
		return false;
	}

	// Mapping fbterm-side
	drm_size = creq.size;
	drm_map = (u8*)mmap(0, drm_size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mreq.offset);
	if (drm_map == MAP_FAILED) {
		drm_map = nullptr;
		LOG("mmap: %s", strerror(errno));
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		cleanup();
		return false;
	}
	LOG("mapped dumb buffer to %p", drm_map);

	drm_saved_crtc = drmModeGetCrtc(drm_fd, drm_crtc_id);

	int result = drmModeSetCrtc(drm_fd, drm_crtc_id, drm_fb_id, 0, 0, &drm_connector_id, 1, &drm_mode);

	if(result) {
		LOG("drmModeSetCrtc: %s", strerror(errno));
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		cleanup();
		return false;
	}

	LOG("set CRTC %u to use FB %u", drm_crtc_id, drm_fb_id);

	drmModeFreeConnector(conn);
	drmModeFreeResources(res);

	LOG("DRM/KMS backend initialized successfully");

	mScreenHeight = drm_mode.vdisplay;
	mScreenWidth = drm_mode.hdisplay;
	mWidth = drm_mode.hdisplay;
	mHeight = drm_mode.vdisplay;
	mBytesPerLine = drm_pitch;
	mVMemBase = (u8*)drm_map;

	return true;
}

DrmDev *DrmDev::initDrmDev()
{
	DrmDev* dev = new DrmDev();
	bool ok = dev->setup();

	if(!ok) {
		delete dev;
		dev = nullptr;
	}

	return dev;
}

DrmDev::DrmDev()
	: mPageFlipCompletedCallback(NULL),
	mPageFlipCompletedUserData(NULL)
{
	mBitsPerPixel = 32; // ARGB
	mOffsetLeft = 0;
	mOffsetTop = 0;
}

DrmDev::~DrmDev()
{
	LOG("shutting down DRM/KMS backend");
	delete mDrmWatch;
	cleanup();
}

const s8 *DrmDev::drvId()
{
	return (const s8 *)"drm";
}

void DrmDev::setupOffset()
{
}

void DrmDev::setupPalette(bool restore)
{
}

void DrmDev::switchVc(bool enter) {
	int status = 0;
	if(enter) {
		if( (status = drmSetMaster(drm_fd)) == 0) {
			drmModeSetCrtc(drm_fd, drm_crtc_id, drm_fb_id, 0, 0, &drm_connector_id, 1, &drm_mode);
		} else {
			fprintf(stderr, "Could not set DRM master, status: %d", status);
		}
	} else {
		if( (status = drmDropMaster(drm_fd)) == 0) {
		} else {
			fprintf(stderr, "Could not drop DRM master, status: %d", status);
		}
	}
}

void DrmDev::initDrmWatch()
{
	int watch_fd = dup(drm_fd);
	if (watch_fd == -1) {
		fprintf(stderr, "failed to duplicate DRM fd: %s", strerror(errno));
		return;
	}
	mDrmWatch = new DrmDevWatch(*this, watch_fd);
}

void DrmDev::present()
{
	if (drm_page_flip_pending) {
		return;
	}

	if (drmModePageFlip(drm_fd, drm_crtc_id, drm_fb_id, DRM_MODE_PAGE_FLIP_EVENT, this)) {
		if (errno != EBUSY) {
			LOG("drmModePageFlip: %s", strerror(errno));
		}
		return;
	}

	drm_page_flip_pending = true;
}

void DrmDev::pageFlipHandler(
        int fd,
        unsigned int sequence,
        unsigned int tv_sec,
        unsigned int tv_usec,
        void *user_data)
{
	auto *self = static_cast<DrmDev *>(user_data);
	self->drm_page_flip_pending = false;

	if (self->mPageFlipCompletedCallback != NULL)
		self->mPageFlipCompletedCallback(self->mPageFlipCompletedUserData);
}

void DrmDev::handleDrmEvents()
{
	drmEventContext context;
	memset(&context, 0, sizeof(context));

	context.version = DRM_EVENT_CONTEXT_VERSION;
	context.page_flip_handler = &DrmDev::pageFlipHandler;

	if (drmHandleEvent(drm_fd, &context) != 0)
		LOG("drmHandleEvent: %s", strerror(errno));
}

void DrmDev::setPageFlipCompletedCallback(
		PageFlipCompletedCallback callback,
		void *user_data)
{
	mPageFlipCompletedCallback = callback;
	mPageFlipCompletedUserData = user_data;
}

bool DrmDev::pageFlipPending() const
{
	return drm_page_flip_pending;
}

bool DrmDev::acquireLease(int &lease_fd)
{
	lease_fd = -1;

	if (drm_fd < 0 || drm_connector_id == 0 ||
			drm_crtc_id == 0 ||
			drm_primary_plane_id == 0 ||
			drm_cursor_plane_id == 0) {
		return false;
	}

	const u32 objects[] = {
		drm_primary_plane_id,
		drm_cursor_plane_id,
		drm_crtc_id,
		drm_connector_id,
	};

	u32 lessee_id = 0;

	int fd = drmModeCreateLease(
			drm_fd, objects, 4, O_CLOEXEC, &lessee_id);

	if (fd < 0) {
		fprintf(stderr,
				"drmModeCreateLease failed: %s\n",
				strerror(errno));
		return false;
	}


	int ret = drmSetClientCap(
			fd,
			DRM_CLIENT_CAP_UNIVERSAL_PLANES,
			1);
	if (ret != 0) {
		fprintf(stderr, "drmSetClientCap failed: %s\n",
				strerror(errno));
		close(fd);
		return false;
	}

	drm_lessee_id = lessee_id;
	drm_lease_active = true;

	lease_fd = fd;
	return true;
}

bool DrmDev::handleLeaseReleased()
{
	if (!drm_lease_active) {
		return false;
	}

	drmModeLesseeListPtr list = drmModeListLessees(drm_fd);

	if (!list)
		return false;

	bool lease_exists = false;

	for (uint32_t i = 0; i < list->count; ++i) {
		if (list->lessees[i] == drm_lessee_id) {
			lease_exists = true;
			break;
		}
	}

	drmFree(list);

	if (lease_exists)
		return false;

	LOG("DRM lease %u disappeared, restoring scanout", drm_lessee_id);

	if(!restoreScanout()) {
		LOG("Failed to restore scanout after DRM lease release");
		return false;
	}

	drm_lessee_id = 0;
	drm_lease_active = false;

	return true;

}
