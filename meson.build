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
    add_project_arguments('/utf-8', '-D_UNICODE', '-DUNICODE', language: 'c')
    add_project_arguments('/utf-8', '-D_UNICODE', '-DUNICODE', language: 'cpp')
endif

if get_option('zstd')
    zstd = subproject('zstd', required: true)
    zstd_dep = zstd.get_variable('libzstd_dep')
    deps += zstd_dep
    add_project_arguments('-DWITH_ZSTD=1', language: 'c')
    add_project_arguments('-DWITH_ZSTD=1', language: 'cpp')
endif

if get_option('mpv')
    mpv = declare_dependency(
        link_args: ['-L' + meson.current_source_dir() + '/lib', '-lmpv'],
        include_directories: include_directories('include')
    )
    deps += mpv
    add_project_arguments('-DMPV=1', language: 'c')
    add_project_arguments('-DMPV=1', language: 'cpp')
endif

sources = files(['dllmain.cpp', 'proxy.h', 'proxy.cpp', 'vfs.hpp', 'vfs.cpp', 'm3t.h', 'm3t.cpp', 'strings_dat.h', 'strings_dat.cpp'])

if get_option('ass')
    harfbuzz = subproject('harfbuzz', required: true)
    fribidi = subproject('fribidi', required: true)
    freetype2 = subproject('freetype2', default_options: ['harfbuzz=enabled'], required: true)
    libass = subproject('libass', default_options: ['directwrite=enabled'], required: true)
    libass_dep = libass.get_variable('libass_dep')
    deps += libass_dep
    directx_sdk = get_option('directx_sdk')
    directx= ''
    if directx_sdk != ''
        add_project_arguments('-I' + directx_sdk + '/Include', language: 'c')
        add_project_arguments('-I' + directx_sdk + '/Include', language: 'cpp')
        directx_lib = directx_sdk + '/Lib/x86'
        deps += cc.find_library('d3dx9', dirs: directx_lib)
    else
        deps += cc.find_library('d3dx9')
    endif
    sources += files(['ass_render.h', 'ass_render.cpp'])
    add_project_arguments('-DASS=1', language: 'c')
    add_project_arguments('-DASS=1', language: 'cpp')
endif

shared_library('d3d9',
    sources,
    dependencies: deps,
    name_prefix: '',
    vs_module_defs: 'exports.def',
    link_args: ['/NODEFAULTLIB:MSVCRT.lib']
)
