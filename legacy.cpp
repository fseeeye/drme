#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cerrno>

#include <fcntl.h> // open
// #include <stdbool.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/mman.h>
// #include <time.h>
#include <unistd.h> // close
#include <xf86drm.h>
#include <xf86drmMode.h>

struct drme_conn_info {
	struct drme_conn_info *next = nullptr; // points to the next device in the single-linked list

    // buffer
	uint32_t buf_width;
	uint32_t buf_height;
	uint32_t buf_stride;
	uint32_t buf_handle; // a DRM handle to the buffer object that we can draw into
	uint32_t mmap_size; // size of the memory mapped buffer
	uint8_t *mmap_buffer = nullptr; // pointer to the memory mapped buffer

	drmModeModeInfo mode; // the display mode that we want to use
	uint32_t fb_handle; // framebuffer handle with our buffer object as scanout buffer
	uint32_t conn_id; // the connector ID that we want to use with this buffer
	uint32_t crtc_id; // the crtc ID that we want to use with this connector
	drmModeCrtcPtr saved_crtc = nullptr; // the configuration of the crtc before we changed it. We use it so we can restore the same mode when we exit.
};

static struct drme_conn_info* conn_info_list = nullptr;

static int device_setup(const char* card_node)
{
    // open drm device
    int drm_fd = open(card_node, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        fprintf(stderr, "[!] Failed to open DRM render node '%s': %m\n", card_node);
        std::exit(1);
    }

    // check DUMB BUF Capability
    uint64_t has_dumb;
    if (drmGetCap(1, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 ||
        has_dumb == false) {
        fprintf(stderr, "[!] DRM device '%s' does not support DUMB_BUFFER capability.\n",
			card_node);
		close(drm_fd);
        
		std::exit(1);
    }

    return drm_fd;
}

static bool scan_connectors(int drm_fd)
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
        conn_info->conn_id = drm_conn->connector_id;

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
            bool has_used = false;
            // check if this crtc has benn used
            struct drme_conn_info *tmp = conn_info;
            while (tmp != nullptr) {
                if (tmp->crtc_id == current_encoder->crtc_id) {
                    has_used = true;
                    break;
                }

                if (has_used == false) {
                    conn_info->crtc_id = current_encoder->crtc_id;
                }

                tmp = tmp->next;
            }
        }
        if (conn_info->crtc_id == 0) {
            fprintf(stderr, "[!] Failed to get suitable crtc : (%d) %m\n", errno);
            drmModeFreeEncoder(current_encoder);
            drmModeFreeConnector(drm_conn);
            continue;
        }
        // TODO: find suitable encoder+crtc if current active encoder+crtc is unavailable.

        /* Step4: set modeinfo to drme_conn_info, choose suitable resolution and refresh-rate */
        if (drm_conn->modes != nullptr) {
            conn_info->mode = drm_conn->modes[0]; // copy first mode
            conn_info->buf_width = drm_conn->modes[0].hdisplay;
            conn_info->buf_height = drm_conn->modes[0].vdisplay;
        } else {
            // TODO
        }
        printf("mode for connector %u is %ux%u\n", drm_conn->connector_id, 
            conn_info->buf_width, conn_info->buf_height);

        /* Step5: create framebuffer */
        // TODO

        /* Step6: cleanup */
        drmModeFreeEncoder(current_encoder);
        drmModeFreeConnector(drm_conn);
        // store conn_info into single-list
        conn_info->next = conn_info_list;
        conn_info_list = conn_info;
    }

    // clean up resources
    drmModeFreeResources(drm_res);
}

int main()
{
    int drm_fd = device_setup("/dev/dri/card0");

    scan_connectors(drm_fd);

    return 0;
}