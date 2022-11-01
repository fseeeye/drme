#include "gbm_allocator.h"

#include <gbm.h>
#include <EGL/egl.h>
#include <string>
// drm
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

static struct gbm_device* gbm = nullptr;

bool gbm_allocator_init(int fd)
{
    gbm = gbm_create_device(fd);
	if (gbm == nullptr) {
		fprintf(stderr, "[!] failed to create gbm device.\n");
		return false;
	}

    return true;
}

void gbm_allocator_destroy()
{
    gbm_device_destroy(gbm);
}

int gbm_allocator_create_drm_fb(int fd, struct modeset_buf *buf)
{
	uint32_t dmabuf_handles[4] = {0}, dmabuf_pitches[4] = {0}, dmabuf_offsets[4] = {0};
	int dmabuf_fds[4] = {0};

	/* check drm caps */
	uint64_t cap;
	if (drmGetCap(fd, DRM_CAP_PRIME, &cap) != 0 ||
			!(cap & DRM_PRIME_CAP_IMPORT)) {
		fprintf(stderr, "[!] PrimeFdToHandle not supported!\n");
		// return -1;
	}
	if (!(cap & DRM_PRIME_CAP_EXPORT)) {
		fprintf(stderr, "[!] PrimeHandleToFd not supported!\n");
		// return -1;
	}

	printf("Created GBM device with backend: %s\n", gbm_device_get_backend_name(gbm));
	char *device_name = drmGetDeviceNameFromFd2(fd);
	printf("Using DRM node: %s\n", device_name);
	free(device_name);
	
	/* CORE: gbm bo */
	// TODO: create with modifiers
	struct gbm_bo* gbm_bo = gbm_bo_create(
		gbm, 
		buf->width, buf->height, 
		GBM_FORMAT_XRGB8888, 
		GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_WRITE // create dumb buffer
	);
	if (gbm_bo == nullptr)
	{
		fprintf(stderr, "[!] failed to create gbm device.\n");
		gbm_device_destroy(gbm);
		return -1;
	}

	int n_planes = gbm_bo_get_plane_count(gbm_bo);
	if (n_planes > 4)
	{
		fprintf(stderr, "[!] GBM BO contains too many planes.\n");
		gbm_bo_destroy(gbm_bo);
		gbm_device_destroy(gbm);
		return -1;
	}
	if (n_planes <= 0)
	{
		fprintf(stderr, "[!] GBM BO contains no planes.\n");
		gbm_bo_destroy(gbm_bo);
		gbm_device_destroy(gbm);
		return -1;
	}
	
	for (size_t i = 0; i < n_planes; i++)
	{
		dmabuf_fds[i] = gbm_bo_get_fd_for_plane(gbm_bo, i);
		if (dmabuf_fds[i] < 0) {
			fprintf(stderr, "[!] gbm bo plane fd < 0.\n");
			// // TODO: close fds
			// gbm_bo_destroy(gbm_bo);
			// gbm_device_destroy(gbm);
			// return -1;
		} else {
			printf("[*] gbm bo plane fd: %d\n", dmabuf_fds[i]);
		}

		// GBM is lacking a function to get a FD for a given plane. Instead,
		// check all planes have the same handle. We can't use
		// drmPrimeHandleToFD because that messes up handle ref'counting in
		// the user-space driver.

		// // get handle
		// union gbm_bo_handle plane_handle = gbm_bo_get_handle_for_plane(gbm_bo, i);
		// if (plane_handle.s32 < 0) {
		// 	fprintf(stderr, "gbm_bo_get_handle_for_plane failed.\n");
		// 	gbm_bo_destroy(gbm_bo);
		// 	gbm_device_destroy(gbm);
		// 	return -1;
		// }
		// if (i == 0) {
		// 	handle = plane_handle.u32;
		// } else if (plane_handle.u32 != handle) {
		// 	fprintf(stderr, "Failed to export GBM BO: "
		// 		"all planes don't have the same GEM handle\n");
		// 	gbm_bo_destroy(gbm_bo);
		// 	gbm_device_destroy(gbm);
		// 	return -1;
		// }

		// // get fd
		// int plane_fd = gbm_bo_get_fd(gbm_bo);
		// if (plane_fd < 0) {
		// 	fprintf(stderr, "gbm bo fd < 0.\n");
		//  // close fds
		// 	gbm_bo_destroy(gbm_bo);
		// 	gbm_device_destroy(gbm);
		// 	return -1;
		// } else {
		// 	dmabuf_fds[i] = static_cast<uint32_t>(plane_fd);
		// }

		// // get offset & stride
		// dmabuf_offsets[i] = gbm_bo_get_offset(gbm_bo, i);
		// dmabuf_pitches[i] = gbm_bo_get_stride_for_plane(gbm_bo, i);
		// dmabuf_handles[i] = gbm_bo_get_handle_for_plane(gbm_bo, i).u32;
	}
	dmabuf_handles[0] = gbm_bo_get_handle(gbm_bo).u32;
	dmabuf_pitches[0] = gbm_bo_get_stride(gbm_bo);
	dmabuf_offsets[0] = gbm_bo_get_offset(gbm_bo, 0);

	int dma_fd = gbm_bo_get_fd(gbm_bo);
	if (dma_fd < 0) {
		// FIXME: use GBM_BO_USE_WRITE, gbm will create dumb buffer.
		//        so we can't get fd from it on some devices?
		fprintf(stderr, "[!] gbm bo fd < 0.\n");
	}  else {
		printf("[*] gbm bo fd: %d\n", dma_fd);
	}
	
	// // get handles from fd
	// for (size_t i = 0; i < n_planes; i++)
	// {
	// 	if (drmPrimeFDToHandle(fd, dmabuf_fds[i], &dmabuf_handles[i]) != 0) {
	// 		fprintf(stderr, "[!] failed to drmPrimeFDToHandle\n");
	// 		gbm_bo_destroy(gbm_bo);
	// 		gbm_device_destroy(gbm);
	// 		return -1;
	// 	}
	// }

	// mmap
	uint32_t dst_stride = 0;
	void* gbo_mapping = nullptr;
	char* map = static_cast<char*>(gbm_bo_map(gbm_bo, 
				0, 0,
				buf->width, buf->height,
				GBM_BO_TRANSFER_READ_WRITE,
				&dst_stride,
				&gbo_mapping));
	if (map == nullptr)
	{
		fprintf(stderr, "[!] failed to map gbm bo!\n");
		gbm_bo_destroy(gbm_bo);
		gbm_device_destroy(gbm);
		return -1;
	}
	
	// create fb
	uint32_t fb_id = 0;
	int ret = drmModeAddFB2(fd, buf->width, buf->height, DRM_FORMAT_XRGB8888,
			    dmabuf_handles, dmabuf_pitches, dmabuf_offsets, &fb_id, 0);
	if (ret) {
		fprintf(stderr, "[!] cannot create framebuffer new (%d): %m\n",
			errno);
		ret = -errno;
		gbm_bo_destroy(gbm_bo);
		gbm_device_destroy(gbm);
		return ret;
	}

	// close tmp handles
	// ERROR
	if(drmCloseBufferHandle(fd, dmabuf_handles[0]) != 0) {
		fprintf(stderr, "[!] failed to drmCloseBufferHandle\n");
	}

	buf->handle = gbm_bo_get_handle(gbm_bo).u32;
	buf->stride = gbm_bo_get_stride(gbm_bo);
	buf->map_data = static_cast<uint8_t*>(gbo_mapping);
	// buf->size = buf->stride * buf->height;
	buf->gbm_bo = gbm_bo;
	buf->fb = fb_id;

	printf("bo_get_stride: %u\n", buf->stride);
	printf("dst_stride: %u\n", dst_stride);

	return 0;
}

void gbm_allocator_destroy_drm_fb(int fd, struct modeset_buf *buf)
{
	struct drm_mode_destroy_dumb dreq;

	if (buf->map_data != nullptr && buf->gbm_bo != nullptr) {
		gbm_bo_unmap(buf->gbm_bo, buf->map_data);
	}

	/* delete framebuffer */
	if (drmModeRmFB(fd, buf->fb) != 0) {
		fprintf(stderr, "[!] failed to rm fb (%d): %m\n", errno);
	}

	// TODO: close all dmabuf fd created by gbm

	/* close gbm bo*/
	gbm_bo_destroy(buf->gbm_bo);
}
