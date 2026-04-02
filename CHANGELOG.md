# Changelog

## 2.1.0

Date: 24 February 2026  
Tag: 2.1.0

### Changes

* Coverity for westeros-gl-drm
* Add version logging.

### Dependencies

* wayland >= 1.6.0
* libxkbcommon >= 0.8.3
* xkeyboard-config >= 2.18
* gstreamer >= 1.10.4
* EGL >= 1.4
* GLES >= 2.0

## 2.0.0

Date: 10 January 2026  
Tag: 2.0.0

### Changes

From this release onwards, Westeros is separated into different repos.

The changes:

* Westeros-sink reporting pre-seek position after seek, causing playback position jumps on video-only playback
* fix westeros-sink timeCodeFound function
* westeros-soc-brcm: Ignore playback rate 0.25-2.0 when audio is passthrough

### Dependencies

* wayland >= 1.6.0
* libxkbcommon >= 0.8.3
* xkeyboard-config >= 2.18
* gstreamer >= 1.10.4
* EGL >= 1.4
* GLES >= 2.0

## 1.01.62

Date: Oct 28, 2025  
Tag: Westeros-1.01.62

### Changes

* v4l2: Fix frame dropping boundary condition for seek accuracy
* v4l2: Fix compile error on platforms without V4L2_PIX_FMT_AV1 defined
* v4l2: Fix thread race condition causing video decode crashes
* brcm: Increase EOS "safety net" timeout.  Currently too short for I-frame only streams (like REW)
* v4l2: update video decode error

### Dependencies

* wayland >= 1.6.0
* libxkbcommon >= 0.8.3
* xkeyboard-config >= 2.18
* gstreamer >= 1.10.4
* EGL >= 1.4
* GLES >= 2.0

## 1.01.61

Date: Sep 30, 2025  
Tag: Westeros-1.01.61

### Changes

* essos: Blacklist status, fix revoke defect

### Dependencies

* wayland >= 1.6.0
* libxkbcommon >= 0.8.3
* xkeyboard-config >= 2.18
* gstreamer >= 1.10.4
* EGL >= 1.4
* GLES >= 2.0

## 1.01.60

Date: Sep 10, 2025  
Tag: Westeros-1.01.60

### Changes

* v4l2: Add low-latency-mode for Netflix DPI 7.0 support
* brcm: Fix "NXCLIENT_BAD_SEQUENCE_NUMBER" error when leaving Netflix DolbyVision
* brcm: Add check for stc_channel==0 to reduce error logging during gaming/low latency

### Dependencies

* wayland >= 1.6.0
* libxkbcommon >= 0.8.3
* xkeyboard-config >= 2.18
* gstreamer >= 1.10.4
* EGL >= 1.4
* GLES >= 2.0
