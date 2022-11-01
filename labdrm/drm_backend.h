#pragma once

#include <memory>
#include <string>

#include <libudev.h>
#include <unistd.h> // for close

namespace DrmLab
{

struct DrmDevice
{
    int fd = -1;
    int sysnum_id = -1;
    std::string devnode;
    dev_t devnum;

    ~DrmDevice()
    {
        ::close(fd);
        printf("[-] DRM device deconstructed.\n");
    }
};

class DrmBackend
{
public:
    DrmBackend();
    ~DrmBackend() noexcept;

    bool Create();

private:
    /**
     * @brief Find primary GPU
     * Some systems may have multiple DRM devices attached to a single seat. This
     * function loops over all devices and tries to find a PCI device with the
     * boot_vga sysfs attribute set to 1.
     * If no such device is found, the first DRM device reported by udev is used.
     * Devices are also vetted to make sure they are are capable of modesetting,
     * rather than pure render nodes (GPU with no display), or pure
     * memory-allocation devices (VGEM).
     * @return struct udev_device* the primary GPU or nullptr
     */
    struct udev_device* FindPrimaryGPU();
    
    /**
     * @brief Check whether the udev device is supported KMS or not.
     * 
     * Determines whether or not a device is capable of modesetting. 
     * If successful, we will sets m_DrmDevice.fd and m_DrmDevice.filename 
     * to the opened device.
     * @param udev_device 
     * @return true 
     * @return false 
     */
    bool IsKms(struct udev_device* udev_device); 

private:
    std::unique_ptr<DrmDevice> m_DrmDevice;
    struct udev* m_UdevContext;
    std::string m_SeatId;
};

} // namespace DrmLab
