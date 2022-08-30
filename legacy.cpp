#include "drm.h"

#include <iostream>
#include <memory>
#include <vector>

// #include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstring> // memset
#include <fcntl.h> // open
#include <sys/mman.h> // mmap
#include <unistd.h> // close
#include <xf86drm.h>
#include <xf86drmMode.h>
// #include <stdbool.h>
// #include <time.h>

struct drme_dumb_buffer;
static std::shared_ptr<drme_dumb_buffer> alloc_buffer(int drm_fd, uint32_t width, uint32_t height);
static uint32_t add_fb(struct drme_conn_info* conn_info, std::shared_ptr<drme_dumb_buffer> buffer);

struct drme_dumb_buffer {
	uint32_t handle; // a DRM handle to the buffer object that we can draw into
	uint32_t stride;
	uint32_t width, height;

    uint32_t size; // size of the memory mapped buffer
	uint8_t *map = nullptr; // pointer to the memory mapped buffer

    uint32_t format;
};

struct drme_conn_info {
    drme_conn_info() {}

    int drm_fd;

	uint32_t buf_width, buf_height; // TODO: remove
    std::shared_ptr<drme_dumb_buffer> buf;

	drmModeModeInfo mode; // the display mode that we want to use
	uint32_t fb_handle; // framebuffer handle with our buffer object as scanout buffer
	uint32_t connector_id; // the connector ID that we want to use with this buffer
	uint32_t crtc_id; // the crtc ID that we want to use with this connector
	drmModeCrtcPtr saved_crtc = nullptr; // the configuration of the crtc before we changed it. We use it so we can restore the same mode when we exit.
};

static std::vector<drme_conn_info*> conn_info_list = {};

static int drme_device_setup(const char* card_node)
{
    // open drm device
    int drm_fd = open(card_node, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        fprintf(stderr, "[!] Failed to open DRM render node '%s': %m\n", card_node);
        close(drm_fd);
        return -1;
    }

    // check DUMB BUF Capability
    uint64_t has_dumb;
    if (drmGetCap(drm_fd, DRM_CAP_DUMB_BUFFER, &has_dumb) != 0 ||
        has_dumb == false) {
        fprintf(stderr, "[!] DRM device '%s' does not support DUMB_BUFFER capability.\n",
			card_node);
		close(drm_fd);
		return -1;
    }

    return drm_fd;
}

static bool drme_scan_connectors(int drm_fd)
{
    // get all of the resources
    drmModeResPtr drm_res = drmModeGetResources(drm_fd);
    if (drm_res == nullptr) {
        fprintf(stderr, "[!] Failed to get DRM resources : (%d) %m\n", errno);
        drmModeFreeResources(drm_res);
        return false;
    }

    // traverse all connectors
    for (int i = 0; i < drm_res->count_connectors; ++i) {
        auto conn_info = new drme_conn_info();
        conn_info->drm_fd = drm_fd;

        // TODO: checking existing output

        /* Step1: get conn */
        drmModeConnectorPtr drm_conn =
            drmModeGetConnector(drm_fd, drm_res->connectors[i]);
        if (drm_conn == nullptr) {
            fprintf(stderr, "[!] Failed to get DRM connector[%u]:%u : (%d) %m\n",
                    i, drm_res->connectors[i], errno);
            drmModeFreeConnector(drm_conn);
            continue;
        }
        conn_info->connector_id = drm_conn->connector_id;

        /* Step2: check if a display device is connected */
        if (drm_conn->connection != DRM_MODE_CONNECTED) {
            drmModeFreeConnector(drm_conn);
            break;
        }

        /* Step3: get encoder+crtc */
        // check active encoder+crtc on current connection first
        drmModeEncoderPtr current_encoder =
            drmModeGetEncoder(drm_fd, drm_conn->encoder_id); // might be nullptr

        if (current_encoder != nullptr && current_encoder->crtc_id != 0) {
            // check if this crtc has benn used
            bool been_used_crtc = false;
            for (const auto& ci : conn_info_list ) {
                if (ci->crtc_id == current_encoder->crtc_id) {
                    been_used_crtc = true;
                    break;
                }
            }
            if (been_used_crtc == false) {
                // choose active encoder+crtc binding to this connector.
                conn_info->crtc_id = current_encoder->crtc_id;
            }
        }
        if (conn_info->crtc_id == 0) {
            fprintf(stderr, "[!] Failed to get suitable crtc : (%d) %m\n", errno);
            drmModeFreeEncoder(current_encoder);
            drmModeFreeConnector(drm_conn);
            continue;
        }
        // TODO: find suitable encoder+crtc if current active encoder+crtc is unavailable.

        /* Step4: set modeinfo to drme_conn_info, choosing suitable resolution and refresh-rate */
        if (drm_conn->modes != nullptr) {
            conn_info->mode = drm_conn->modes[0]; // copy first mode
            conn_info->buf_width = drm_conn->modes[0].hdisplay;
            conn_info->buf_height = drm_conn->modes[0].vdisplay;
        } else {
            // TODO
        }
        printf("[*] mode for connector %u is %ux%u\n", drm_conn->connector_id, 
            conn_info->buf_width, conn_info->buf_height);

        /* Step5: create framebuffer */
        // create dumb buffer
        // TODO: move to allocator
        auto buffer = alloc_buffer(drm_fd, conn_info->buf_width, conn_info->buf_height);
        if (buffer == nullptr) {
            drmModeFreeEncoder(current_encoder);
            drmModeFreeConnector(drm_conn);
            continue;
        }
        // create framebuffer object for the dumb-buffer
        // TODO: sperate to independent opt
        auto fb_handle = add_fb(conn_info, buffer);
        if (fb_handle == 0) {
            drmModeFreeEncoder(current_encoder);
            drmModeFreeConnector(drm_conn);
            continue;
        }
        conn_info->fb_handle = fb_handle;

        /* Step6: cleanup */
        drmModeFreeEncoder(current_encoder);
        drmModeFreeConnector(drm_conn);
        // store conn_info into single-list
        conn_info_list.emplace_back(conn_info);
    }

    // clean up resources
    drmModeFreeResources(drm_res);
    return true;
}

static std::shared_ptr<drme_dumb_buffer> alloc_buffer(int drm_fd, uint32_t width, uint32_t height)
{
    std::shared_ptr<drme_dumb_buffer> buffer = std::make_shared<drme_dumb_buffer>();

    // Step1: create dumb buffer
    struct drm_mode_create_dumb create = {0};
    create.width = width;
    create.height = height;
    create.bpp = 32; // bits per pixel, TODO: set by swapchain
    //create.flags = xxx 
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        fprintf(stderr, "Failed to create DRM dumb buffer : (%d) %m\n",
			errno);
		return nullptr;
    }

    buffer->width  = create.width;
    buffer->height = create.height;

    buffer->stride = create.pitch;
    buffer->handle = create.handle;
    buffer->size   = create.size;

    // buffer->drm_fd = drm_fd

    // Step2: map the dumb buffer for userspace drawing
    // prepare buffer for mmap
	struct drm_mode_map_dumb map = {0};
	map.handle = buffer->handle;

    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map)) {
        fprintf(stderr, "Failed to map DRM dumb buffer : (%d) %m\n",
			errno);
		return nullptr;
    }
    // perform actual memory mapping
    buffer->map = static_cast<uint8_t*>(mmap(0, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED,
        drm_fd, map.offset));

    // clear buffer to 0
    memset(buffer->map, 0, buffer->size);

    // TODO: Step3: convert to DMA buffer fd

    return buffer;
}

/**
 * @brief 
 * 
 * @param conn_info 
 * @param buffer buffer handle
 * @return uint32_t 0 means fail, other means new fb handle
 */
static uint32_t add_fb(struct drme_conn_info* conn_info, std::shared_ptr<drme_dumb_buffer> buffer)
{
    uint32_t id = 0;

    uint8_t depth = 32;
    uint8_t bpp = 32;
    // TODO: use drmModeAddFB2 & drmMOdeAddFB2WithModifiers
    if (drmModeAddFB(conn_info->drm_fd, buffer->width, buffer->height, depth, 
        bpp, buffer->stride, buffer->handle, &id)) {
        fprintf(stderr, "drmModeAddFB failed : (%d) %m\n",
			errno);
    }

    return id;
}

int main()
{
    /* open the DRM device */
    int drm_fd = drme_device_setup("/dev/dri/card0");
    if (drm_fd == -1) {
        std::exit(1);
    }

    /* prepare all connectors and CRTCs */
    drme_scan_connectors(drm_fd);

    /* perform actual modesetting on each found connector+CRTC */
    // WORKINGON

    return 0;
}
