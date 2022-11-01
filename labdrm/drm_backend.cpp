#include "drm_backend.h"

#include <cassert>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h> // for open

namespace DrmLab
{

static const char default_seat[] = "seat0";

DrmBackend::DrmBackend()
    : m_SeatId(default_seat)
{}

DrmBackend::~DrmBackend()
{
    if (m_UdevContext != nullptr) {
        udev_unref(m_UdevContext);
    }
}

bool DrmBackend::Create()
{
    printf("Create()\n");

    // Init seat
    const char* s = getenv("XDG_SEAT");
    if (s != nullptr) {
        m_SeatId = s;
    }

    // Init udev context
    if (m_UdevContext != nullptr) {
        fprintf(stderr, "[!] DrmBackend already created.\n");
        return false;
    }
    m_UdevContext = udev_new();
    if (m_UdevContext == nullptr) {
        fprintf(stderr, "[!] failed to init udev context.\n");
        return false;
    }
    
    // Get primary GPU
    udev_device* primary_drm_device = FindPrimaryGPU();
    if (primary_drm_device == nullptr) {
        fprintf(stderr, "[!] failed to find primary GPU.\n");
        return false;
    }

    //...

    // Clean up
    udev_device_unref(primary_drm_device);

    return true;
}

struct udev_device* DrmBackend::FindPrimaryGPU()
{
    printf("FindPrimaryGPU()\n");

    // Get all GPU device by udev
    struct udev_enumerate* udev_enum = udev_enumerate_new(m_UdevContext);
    udev_enumerate_add_match_subsystem(udev_enum, "drm");
	udev_enumerate_add_match_sysname(udev_enum, "card[0-9]*");
    udev_enumerate_scan_devices(udev_enum);

    // Check all devices to find primary GPU device
    struct udev_device* primary_drm_device = nullptr;
    struct udev_list_entry* entry;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(udev_enum)) {
		bool is_boot_vga = false;

        /* Get udev device */
		const char* path = udev_list_entry_get_name(entry);
		struct udev_device* gpu_device = udev_device_new_from_syspath(m_UdevContext, path);
		if (gpu_device == nullptr)
			continue;
        const char* device_name = udev_device_get_devnode(gpu_device);
        printf("[*] Checking device: %s(%s)\n", device_name, path);

        /* Get device seat */
		const char* device_seat = udev_device_get_property_value(gpu_device, "ID_SEAT");
		if (device_seat == nullptr) {
            device_seat = default_seat;
        }
		if (device_seat != m_SeatId) {
            // device seat must be same as DRM backend seat id
			udev_device_unref(gpu_device);
            fprintf(stderr, "[!] Device(%s) is not at the same seat with DRM Backend: %s <-> %s\n", 
                device_name, device_seat, m_SeatId.c_str());
			continue;
		}

        /* Try to find a PCI device with the `boot_vga` sysfs attribute set to 1 
         * The GPU marked as `boot_vga` is a special case when it comes to doing PCI passthroughs, 
         * since the BIOS needs to use it in order to display things like boot messages or the 
         * BIOS configuration menu.*/
		struct udev_device* pci_device = udev_device_get_parent_with_subsystem_devtype(gpu_device,
								"pci", NULL);
		if (pci_device) {
			std::string boot_vga = udev_device_get_sysattr_value(pci_device, "boot_vga");
			if (!boot_vga.empty() && (boot_vga != "1")) {
				is_boot_vga = true;
            }
		}

		/* If we already have a modesetting-capable device, and this
		 * device isn't our boot-VGA device, we aren't going to use
		 * it. */
		if (!is_boot_vga && primary_drm_device != nullptr) {
            fprintf(stderr, "[!] Device(%s) will be dropped. Because we already got a primary device(%s)\n", 
                device_name, udev_device_get_devnode(primary_drm_device));
			udev_device_unref(gpu_device);
			continue;
		}

		/* Make sure this device is actually capable of modesetting;
		 * if this call succeeds, self->m_DrmDevice.{fd,filename} will be set,
		 * and any old values freed. */
		if (!IsKms(gpu_device)) {
            fprintf(stderr, "[!] Device(%s) is not supported KMS.\n", device_name);
			udev_device_unref(gpu_device);
			continue;
		}

		/* There can only be one boot_vga device, and we try to use it
		 * at all costs. */
		if (is_boot_vga) {
			if (primary_drm_device != nullptr) {
				udev_device_unref(primary_drm_device);
            }
            fprintf(stderr, "[!] Device(%s) will be dropped. Because we already got a primary device(%s)\n", 
                device_name, udev_device_get_devnode(primary_drm_device));
			primary_drm_device = gpu_device;
			break;
		}

		/* Per the (!is_boot_vga && primary_drm_device) test above, we only
		 * trump existing saved devices with boot-VGA devices, so if
		 * we end up here, this must be the first device we've seen. */
		assert(!primary_drm_device);
		primary_drm_device = gpu_device;
	}

	/* If we're returning a device to use, we must have an open FD for
	 * it. */
    assert(m_DrmDevice != nullptr);
	assert(!!primary_drm_device == (m_DrmDevice->fd >= 0));

	udev_enumerate_unref(udev_enum);
	return primary_drm_device;
}

bool DrmBackend::IsKms(struct udev_device* udev_device)
{
    printf("IsKms()\n");

    const char* device_name = udev_device_get_devnode(udev_device);
	const char* sysnum = udev_device_get_sysnum(udev_device);
	dev_t devnum = udev_device_get_devnum(udev_device);

    // Try open device
    int fd = ::open(device_name, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[!] Try to open deivce failed: %s\n", device_name);
		return false;
    }
    
    // Get DRM Resources of this device
    drmModeRes* drm_res = drmModeGetResources(fd);
    if (drm_res == nullptr) {
        fprintf(stderr, "[!] Try to get DRM resources failed: %s(%d)\n", device_name, fd);
        goto out_closefd;
    }

    // Check resources: drm crtc' & connector' & encoders' count
    if (drm_res->count_crtcs <= 0 || drm_res->count_connectors <= 0 || drm_res->count_encoders <= 0) {
        fprintf(stderr, "[!] one of Device(%s) DRM resources count is <=0. crtcs: %d, connectors: %d, encoders: %d\n",
            device_name, drm_res->count_crtcs, drm_res->count_connectors, drm_res->count_encoders);
        goto out_closeres;
    }

    // Check sysnum
    int sysnum_id;
    if (sysnum != nullptr) {
        sysnum_id = std::atoi(sysnum);
        if (sysnum_id < 0) {
            fprintf(stderr, "[!] Failed to get id from sysnum(%s). device name: %s\n", sysnum, device_name);
        }
    } else {
        fprintf(stderr, "[!] Failed to get sysnum of the device: %s\n", device_name);
    }

    // Clean up
    drmModeFreeResources(drm_res);

    // Set new drm device
    m_DrmDevice.reset(new DrmDevice());
    m_DrmDevice->fd = fd;
    m_DrmDevice->sysnum_id = sysnum_id;
    m_DrmDevice->devnode = device_name;
    m_DrmDevice->devnum = devnum;

    printf("[*] Current DRM device is support KMS. device name: %s, sysnum id: %d, count_crtcs: %d, "
        "count_connectors: %d, count_encoders: %d.\n", 
        device_name, sysnum_id, drm_res->count_crtcs, drm_res->count_connectors, drm_res->count_encoders);
    
    return true;

out_closeres:
    drmModeFreeResources(drm_res);
out_closefd:
    ::close(fd);
    return false;
}

} // namespace DrmLab
