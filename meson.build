project('tmpl_chs_patch', default_options : ['cpp_std=c++17'])
add_languages('c', 'cpp', required: true)

zlib = subproject('zlib', required: true)
libzip = subproject('libzip', required: true)
utils = subproject('utils', required: true)
detours = subproject('detours', required: true)

zlib_dep = zlib.get_variable('zlib_dep')
libzip_dep = libzip.get_variable('libzip_dep')
utils_dep = utils.get_variable('utils_dep')
detours_dep = detours.get_variable('detours_dep')

deps = [
    zlib_dep,
    libzip_dep,
    utils_dep,
    detours_dep,
]

cc = meson.get_compiler('c')

MSVC = cc.get_id() == 'msvc'
if MSVC
    add_project_arguments('/utf-8', language: 'c')
    add_project_arguments('/utf-8', language: 'cpp')
endif

shared_library('d3d9',
    files(['dllmain.cpp', 'proxy.h', 'proxy.cpp', 'vfs.hpp', 'vfs.cpp', 'm3t.h', 'm3t.cpp']),
    dependencies: deps,
    name_prefix: '',
    vs_module_defs: 'exports.def',
)
