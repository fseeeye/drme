#include "shm_allocator.h"

#include <cstdio>
#include <cerrno>

#include <ctime>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
// drm
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define RANDNAME_PATTERN "/wlroots-XXXXXX"

static void randname(char *buf) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A'+(r&15)+(r&16)*2;
		r >>= 5;
	}
}

static int excl_shm_open(char *name) {
	int retries = 100;
	do {
		randname(name + strlen(RANDNAME_PATTERN) - 6);

		--retries;
		// CLOEXEC is guaranteed to be set by shm_open
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);

	return -1;
}

int allocate_shm_file(size_t size) {
	char name[] = RANDNAME_PATTERN;
	int fd = excl_shm_open(name);
	if (fd < 0) {
		return -1;
	}
	shm_unlink(name);

	int ret;
	do {
		ret = ftruncate(fd, size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

bool shm_allocator_init(int fd)
{
    // do nothing
}

void shm_allocator_destroy()
{
    // do nothing
}

int shm_allocator_create_shm(struct shm_buf *buf)
{
    constexpr uint32_t bytes_per_pixel = 32 / 8;
    buf->stride = buf->width * bytes_per_pixel; // TODO: align?
    buf->size = buf->stride * buf->height;

    // create shm anonymous file
    buf->fd = allocate_shm_file(buf->size);
    if (buf->fd < 0)
    {
        fprintf(stderr, "[!] failed to create shm file for %d Bytes", buf->size);
        return -1;
    } else {
		printf("shm fd: %d\n", buf->fd);
	}
    
    // mmap
    void* data = mmap(nullptr, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, buf->fd, 0);
    if (data == MAP_FAILED)
    {
        fprintf(stderr, "[!] failed to mmap for fd: %d\n", buf->fd);
        close(buf->fd);
        return -1;
    }
    buf->map_data = static_cast<uint8_t*>(data);

	return 0;
}

void shm_allocator_destroy_shm(struct shm_buf *buf)
{
	/* unmap shm */
    munmap(buf->map_data, buf->size);

	/* close shm file fd*/
	::close(buf->fd);
}

int shm_allocator_create_drm_fb(int fd, struct modeset_buf *buf)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_destroy_dumb dreq;
	struct drm_mode_map_dumb mreq;
	int ret;
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};

	/* create dumb buffer */
	memset(&creq, 0, sizeof(creq));
	creq.width = buf->width;
	creq.height = buf->height;
	creq.bpp = 32;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		fprintf(stderr, "cannot create dumb buffer (%d): %m\n",
			errno);
		return -errno;
	}
	buf->stride = creq.pitch;
	buf->size = creq.size;
	buf->handle = creq.handle;

	/* create framebuffer object for the dumb-buffer */
	handles[0] = buf->handle;
	pitches[0] = buf->stride;
	ret = drmModeAddFB2(fd, buf->width, buf->height, DRM_FORMAT_XRGB8888,
			    handles, pitches, offsets, &buf->fb, 0);
	if (ret) {
		fprintf(stderr, "cannot create framebuffer (%d): %m\n",
			errno);
		ret = -errno;
		goto err_destroy;
	}

	/* prepare buffer for memory mapping */
	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = buf->handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		fprintf(stderr, "cannot map dumb buffer (%d): %m\n",
			errno);
		ret = -errno;
		goto err_fb;
	}

	/* perform actual memory mapping */
	buf->map_data = static_cast<uint8_t*>(mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		        fd, mreq.offset));
	if (buf->map_data == MAP_FAILED) {
		fprintf(stderr, "cannot mmap dumb buffer (%d): %m\n",
			errno);
		ret = -errno;
		goto err_fb;
	}

	/* clear the framebuffer to 0 */
	memset(buf->map_data, 0, buf->size);

	return 0;

err_fb:
	drmModeRmFB(fd, buf->fb);
err_destroy:
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = buf->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	return ret;
}

void shm_allocator_destroy_drm_fb(int fd, struct modeset_buf *buf)
{
    struct drm_mode_destroy_dumb dreq;

	/* delete framebuffer */
	if (drmModeRmFB(fd, buf->fb) != 0) {
		fprintf(stderr, "[!] failed to rm fb (%d): %m\n", errno);
	}
}
