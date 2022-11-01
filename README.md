# DRM Examples

## Get Started
```shell
meson setup build/ --prefix=out && cd build/
meson compile
meson install
# run examples in out dir
```

## Example list

- **gbm_atomic**: DRM atomic commit; gbm allocator creating DMABUF for FB
- **shm_atomic**: DRM atomic commit; shm allocator creating buffer and memcpy to Dumb buffer of FB 
