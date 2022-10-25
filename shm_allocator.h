#pragma once

#include <cstdint>

struct modeset_buf {
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t size;
	uint32_t handle;
	uint8_t *map_data;
	uint32_t fb;
};

struct shm_buf {
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t size;
	uint8_t *map_data;

    int fd;
};


bool shm_allocator_init(int fd);
void shm_allocator_destroy();

int shm_allocator_create_shm(struct shm_buf *buf);
void shm_allocator_destroy_shm(struct shm_buf *buf);

int shm_allocator_create_drm_fb(int fd, struct modeset_buf *buf);
void shm_allocator_destroy_drm_fb(int fd, struct modeset_buf *buf);