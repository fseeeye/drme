project('drme', 'cpp',
    version : '0.1',
    default_options : ['warning_level=3', 'cpp_std=c++20']
)

dep_libdrm = dependency('libdrm', version : '>=2.4.113')
dep_udev = dependency('libudev', version: '>= 249')
dep_gbm = dependency('gbm', version : '>=22.2.1')

inc_labdrm = include_directories('labdrm')

subdir('labdrm')
subdir('examples')
subdir('src')
