#include <iostream>
#include <memory>

#include "drm_backend.h"

int main()
{
    std::unique_ptr<DrmLab::DrmBackend> drm_backend = std::make_unique<DrmLab::DrmBackend>();
    if (drm_backend->Create()) {
        fprintf(stderr, "[!] DRM Backend init failed!\n");
        return -1;
    }
    printf("[*] DRM Backend init done.\n");
    
    return 0;
}