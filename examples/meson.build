executable('shm_atomic',
           'shm_atomic.cpp',
           'shm_allocator.cpp',
           dependencies : dep_libdrm,
           install : true)

executable('gbm_atomic',
           'gbm_atomic.cpp',
           'gbm_allocator.cpp',
           dependencies : [ dep_libdrm, dep_gbm],
           install : true)