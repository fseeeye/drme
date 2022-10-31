#pragma once

#include <cstdint>

struct modeset_buf {
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	// uint32_t size; // don't need anymore
	uint32_t handle;
	uint8_t *map_data;
	uint32_t fb;
	
	struct gbm_bo* gbm_bo; // gbm_bo
};

bool gbm_allocator_init(int fd);
void gbm_allocator_destroy();

int gbm_allocator_create_drm_fb(int fd, struct modeset_buf *buf);
void gbm_allocator_destroy_drm_fb(int fd, struct modeset_buf *buf);
