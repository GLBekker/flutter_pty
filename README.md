# flutter_pty

[![ci](https://github.com/TerminalStudio/flutter_pty/actions/workflows/ci.yml/badge.svg)](https://github.com/TerminalStudio/flutter_pty/actions/workflows/ci.yml)
[![pub points](https://badges.bar/flutter_pty/pub%20points)](https://pub.dev/packages/flutter_pty)


This is an experimental package to explore the possibilities of using native
code to implement PTY instead of pure FFI and blocking isolates. It's expected to be
more stable than the current implementation ([pty](https://pub.dev/packages/pty)).

## Platform


| Linux | macOS | Windows | Android |
| :---: | :---: | :-----: | :-----: |
|   ✔️   |   ✔️   |    ✔️    |    ✔️    |

## Quick start

```dart
import 'package:flutter_pty/flutter_pty.dart';

final pty = Pty.start('bash');

pty.output.listen((data) => ...);

pty.write(Utf8Encoder().convert('ls -al\n'));

pty.resize(30, 80);

pty.kill();
```

---

## Project stucture

This template uses the following structure:

* `src`: Contains the native source code, and a CmakeFile.txt file for building
  that source code into a dynamic library.

* `lib`: Contains the Dart code that defines the API of the plugin, and which
  calls into the native code using `dart:ffi`.

* platform folders (`android`, `ios`, `windows`, etc.): Contains the build files
  for building and bundling the native code library with the platform application.

## Buidling and bundling native code

The `pubspec.yaml` specifies FFI plugins as follows:

```yaml
  plugin:
    platforms:
      some_platform:
        ffiPlugin: true
```

This configuration invokes the native build for the various target platforms
and bundles the binaries in Flutter applications using these FFI plugins.

This can be combined with dartPluginClass, such as when FFI is used for the
implementation of one platform in a federated plugin:

```yaml
  plugin:
    implements: some_other_plugin
    platforms:
      some_platform:
        dartPluginClass: SomeClass
        ffiPlugin: true
```

A plugin can have both FFI and method channels:

```yaml
  plugin:
    platforms:
      some_platform:
        pluginClass: SomeName
        ffiPlugin: true
```

The native build systems that are invoked by FFI (and method channel) plugins are:

* For Android: Gradle, which invokes the Android NDK for native builds.
  * See the documentation in android/build.gradle.
* For iOS and MacOS: Xcode, via CocoaPods.
  * See the documentation in ios/flutter_pty.podspec.
  * See the documentation in macos/flutter_pty.podspec.
* For Linux and Windows: CMake.
  * See the documentation in linux/CMakeLists.txt.
  * See the documentation in windows/CMakeLists.txt.

## Binding to native code

To use the native code, bindings in Dart are needed.
To avoid writing these by hand, they are generated from the header file
(`src/flutter_pty.h`) by `package:ffigen`.
Regenerate the bindings by running `flutter pub run ffigen --config ffigen.yaml`.

## Invoking native code

Very short-running native functions can be directly invoked from any isolate.
For example, see `sum` in `lib/flutter_pty.dart`.

Longer-running functions should be invoked on a helper isolate to avoid
dropping frames in Flutter applications.
For example, see `sumAsync` in `lib/flutter_pty.dart`.

## Flutter help

For help getting started with Flutter, view our
[online documentation](https://flutter.dev/docs), which offers tutorials,
samples, guidance on mobile development, and a full API reference.


## Echorb Fork Enhancements

This fork adds buffer management and backpressure support inspired by tmux's bufferevent approach.

### New Features

#### Buffer Status API
Check PTY buffer fill level and availability:
```dart
final status = pty.getBufferStatus();
print('Buffer: ${status.currentSize}/${status.capacity}');
print('Can write: ${status.canWrite}');
```

#### Async Write with Backpressure
Write large commands without manual chunking:
```dart
// Old way (manual chunking):
const chunkSize = 50;
for (int i = 0; i < command.length; i += chunkSize) {
  final chunk = command.substring(i, min(i + chunkSize, command.length));
  pty.write(Utf8Encoder().convert(chunk));
  await Future.delayed(const Duration(milliseconds: 10));
}

// New way (automatic backpressure):
await pty.writeAsync(Utf8Encoder().convert(command));
```

The `writeAsync()` method automatically handles buffer full conditions by waiting for the buffer to drain, eliminating the need for manual chunking and arbitrary delays.

### Installation

Add to your `pubspec.yaml`:

```yaml
dependencies:
  flutter_pty:
    git:
      url: https://github.com/YOUR_USERNAME/flutter_pty.git
      ref: feature/buffer-backpressure-support
```

### Why This Fork?

We needed reliable PTY buffer management for [Echorb Desktop](https://echorb.com), a multi-instance Claude Code orchestrator. The original package required manual chunking with arbitrary delays, which was unreliable for large commands.

Our enhancements:
- **Buffer monitoring** - Know when PTY is ready for more data
- **Automatic backpressure** - No more manual chunking needed
- **Cross-platform** - Works on Windows (ConPTY), Linux, and macOS (forkpty)
- **Backward compatible** - All existing APIs work unchanged

### Credits

Original repository: https://github.com/TerminalStudio/flutter_pty  
Fork maintained by: Echorb Team  
Inspired by: tmux source code analysis
