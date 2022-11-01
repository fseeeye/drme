labdrm = static_library('labdrm',
    'drm_backend.cpp',
    dependencies : [ dep_libdrm, dep_udev ],
    install: false
)
dep_labdrm = declare_dependency(link_with: labdrm)
