#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint flutter_pty.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'flutter_pty'
  s.version          = '0.0.1'
  s.summary          = 'A new Flutter FFI plugin project.'
  s.description      = <<-DESC
A new Flutter FFI plugin project.
                       DESC
  s.homepage         = 'http://example.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }

  # This will ensure the source files in Classes/ are included in the native
  # builds of apps using this FFI plugin. Podspec does not support relative
  # paths, so Classes contains a forwarder C file that relatively imports
  # `../src/*` so that the C sources can be shared among all target platforms.
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.dependency 'FlutterMacOS'

  s.platform = :osx, '10.11'
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES' }
  # CocoaPods links the resulting static archive into the host binary via
  # `-framework flutter_pty`, which lets the linker dead-strip every
  # symbol nothing references at link time. Dart only touches the FFI
  # entry points at runtime, so without these `-u` flags the binary
  # ships with no pty symbols and DynamicLibrary.process() can't find
  # them. Keep this list in sync with src/flutter_pty.h. Dart_InitializeApiDL
  # is the Dart Native API init function from src/include/dart_api_dl.c —
  # without it the DL function pointers stay null and the read loop
  # crashes on first Dart_PostCObject_DL.
  s.user_target_xcconfig = {
    'OTHER_LDFLAGS' => '-Wl,-u,_pty_create -Wl,-u,_pty_write -Wl,-u,_pty_ack_read -Wl,-u,_pty_resize -Wl,-u,_pty_getpid -Wl,-u,_pty_error -Wl,-u,_pty_get_buffer_status -Wl,-u,_pty_write_nonblocking -Wl,-u,_pty_can_write -Wl,-u,_pty_set_discard_notification_port -Wl,-u,_pty_get_discarded_bytes -Wl,-u,_pty_clear_discarded_bytes -Wl,-u,_pty_write_buffered -Wl,-u,_Dart_InitializeApiDL',
  }
  s.swift_version = '5.0'
end
