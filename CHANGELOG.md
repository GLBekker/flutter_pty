## 0.5.0-echorb.1 (Echorb Fork)

**Buffer Management Enhancements:**
* Add `pty_get_buffer_status()` - Monitor PTY buffer fill level
* Add `pty_write_nonblocking()` - Non-blocking write with result codes
* Add `pty_can_write()` - Quick buffer availability check
* Add `writeAsync()` - Dart async API with automatic backpressure handling
* Add `PtyBufferStatus` class for buffer information
* Cross-platform: Unix (forkpty) + Windows (ConPTY)
* Inspired by tmux's libevent bufferevent approach
* Eliminates manual chunking requirements

**Backward Compatibility:**
* All existing APIs unchanged
* Existing `write()` method still works

Fork maintained by: Echorb Team
Original repo: https://github.com/TerminalStudio/flutter_pty

## 0.4.2
* Fix Linux compile error, thanks [@mengyanshou].

## 0.4.1
* Fix compile warning, thanks [@mengyanshou].

## 0.4.0
* Update to Dart3

## 0.3.1
* Update deps

## 0.3.0

* Fixes ignored working directory parameter for Unix [#3], thanks [@devmil].
* Support setting Windows environmental variable and working directory.

## 0.2.0

* Add optional read acknowledge [#2], thanks [@devmil].

## 0.1.1

* Update README

## 0.1.0

* Windows support.
* Support getting exit code

## 0.0.7

* Work on Linux #1
* Work on Android

## 0.0.6

* Flutter >=2.12.0

## 0.0.5

* Fix README syntax

## 0.0.4

* Support resizing of the pty

## 0.0.3

* Support passing env vars
## 0.0.2

* Support passing arguments
## 0.0.1

* Initial release

[#2]: https://github.com/TerminalStudio/flutter_pty/pull/2
[#3]: https://github.com/TerminalStudio/flutter_pty/pull/3

[@devmil]: https://github.com/devmil
[@mengyanshou]: https://github.com/mengyanshou