project('amber-player', 'cpp',
  version: '2.0.0',
  meson_version: '>= 0.62',
  default_options: ['cpp_std=c++17', 'warning_level=2', 'buildtype=debugoptimized'],
)

app_id = 'io.github.knap43.Amber'

glib_dep = dependency('glib-2.0', version: '>= 2.74')
sigc_dep = dependency('sigc++-3.0')
pixbuf_dep = dependency('gdk-pixbuf-2.0')
taglib_dep = dependency('taglib', version: '>= 1.12')
threads_dep = dependency('threads')

gtk_dep = dependency('gtk4', version: '>= 4.12')
gtkmm_dep = dependency('gtkmm-4.0', version: '>= 4.10')
adw_dep = dependency('libadwaita-1', version: '>= 1.5')
gst_dep = dependency('gstreamer-1.0', version: '>= 1.20')
gst_tag_dep = dependency('gstreamer-tag-1.0')

conf = configuration_data()
conf.set_quoted('AMBER_VERSION', meson.project_version())
conf.set_quoted('AMBER_APP_ID', app_id)
configure_file(output: 'config.h', configuration: conf)
config_inc = include_directories('.')

core_sources = files(
  'src/ImageUtils.cpp',
  'src/Library.cpp',
  'src/Lyrics.cpp',
  'src/MetadataReader.cpp',
  'src/NaturalSort.cpp',
  'src/PlayQueue.cpp',
  'src/Track.cpp',
)

core_deps = [glib_dep, sigc_dep, pixbuf_dep, taglib_dep, threads_dep]
core_lib = static_library('ambercore', core_sources, dependencies: core_deps)
core_dep = declare_dependency(link_with: core_lib, dependencies: core_deps,
                              include_directories: include_directories('src'))

app_sources = files(
  'src/Application.cpp',
  'src/CoverLyricsView.cpp',
  'src/DragData.cpp',
  'src/LibraryPanel.cpp',
  'src/MainWindow.cpp',
  'src/Mpris.cpp',
  'src/NowPlayingPanel.cpp',
  'src/Player.cpp',
  'src/QueuePanel.cpp',
  'src/Session.cpp',
  'src/Theme.cpp',
  'src/main.cpp',
)

executable('amber-player', app_sources,
  dependencies: [core_dep, gtk_dep, gtkmm_dep, adw_dep, gst_dep, gst_tag_dep],
  include_directories: config_inc,
  install: true,
)

install_data('data/' + app_id + '.desktop', install_dir: get_option('datadir') / 'applications')
install_data('data/' + app_id + '.metainfo.xml', install_dir: get_option('datadir') / 'metainfo')
install_data('data/' + app_id + '.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps')
install_data('data/' + app_id + '-symbolic.svg',
             install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'symbolic' / 'apps')

foreach t : ['lyrics', 'playqueue', 'library']
  test(t, executable('test_' + t, 'tests/test_' + t + '.cpp', dependencies: core_dep))
endforeach
