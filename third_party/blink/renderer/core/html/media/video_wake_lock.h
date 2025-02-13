// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_MEDIA_VIDEO_WAKE_LOCK_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_MEDIA_VIDEO_WAKE_LOCK_H_

#include "services/device/public/mojom/wake_lock.mojom-blink.h"
#include "third_party/blink/public/platform/modules/remoteplayback/web_remote_playback_state.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/events/native_event_listener.h"
#include "third_party/blink/renderer/core/html/media/remote_playback_observer.h"
#include "third_party/blink/renderer/core/page/page_visibility_observer.h"

namespace blink {

class HTMLVideoElement;

// This is implementing the wake lock logic related to a video element. It will
// take wake lock iif:
//  - the video is playing;
//  - the page is visible OR the video is in picture-in-picture;
//  - the video isn't being remoted.
// Each video element implements its own wake lock logic. The service will then
// merge all the requests and take the appropriate system wake lock.
// VideoWakeLock only uses "screen" related wake lock: it prevents the screen
// from locking on mobile or the lockscreen to show up on desktop.
class CORE_EXPORT VideoWakeLock final : public NativeEventListener,
                                        public PageVisibilityObserver,
                                        public RemotePlaybackObserver {
  USING_GARBAGE_COLLECTED_MIXIN(VideoWakeLock);

 public:
  explicit VideoWakeLock(HTMLVideoElement&);

  void Trace(Visitor*) final;

  // EventListener implementation.
  void Invoke(ExecutionContext*, Event*) final;

  // RemotePlaybackObserver implementation.
  void OnRemotePlaybackStateChanged(WebRemotePlaybackState) final;

  bool active_for_tests() const { return active_; }

 private:
  // PageVisibilityObserver implementation.
  void PageVisibilityChanged() final;

  // Called when any state is changed. Will update active state and notify the
  // service if needed.
  void Update();

  // Return whether the wake lock should be active given the current state.
  bool ShouldBeActive() const;

  // Create a mojo pointer to the wake lock service if possible.
  void EnsureWakeLockService();

  // Called when the service is no longer connected.
  void OnConnectionError();

  // Notify the wake lock service of the current wake lock state.
  void UpdateWakeLockService();

  HTMLVideoElement& VideoElement() { return *video_element_; }
  const HTMLVideoElement& VideoElement() const { return *video_element_; }

  // `video_element_` owns |this|.
  Member<HTMLVideoElement> video_element_;

  device::mojom::blink::WakeLockPtr wake_lock_service_;

  bool playing_ = false;
  bool active_ = false;
  WebRemotePlaybackState remote_playback_state_ =
      WebRemotePlaybackState::kDisconnected;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_MEDIA_VIDEO_WAKE_LOCK_H_
