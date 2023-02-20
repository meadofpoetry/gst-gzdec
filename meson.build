project('gst-gunzip', 'c',
  version : '0.1',
  license: 'LGPL')

cc = meson.get_compiler('c')

gst_version = meson.project_version()

api_version = '1.0'

zlib_dep = dependency('zlib', version : '>=1.2',
  required : true)

bzip_dep = dependency('bzip2', version : '>=1.0',
  required : true)

gst_dep = dependency('gstreamer-1.0', version : '>=1.19',
  required : true)

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

plugin_c_args = ['-DHAVE_CONFIG_H']
cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-gunzip')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer gunzip Plug-in')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://gstreamer.freedesktop.org')
configure_file(output : 'config.h', configuration : cdata)

gzdec_sources = [
  'src/gstgzdec.c',
  'src/gstgzdec.h'
]

gstudp = library('gstgzdec',
   gzdec_sources,
   c_args : plugin_c_args,
   dependencies : [zlib_dep, bzip_dep, gst_dep],
   install : true,
   install_dir : plugins_install_dir,
)