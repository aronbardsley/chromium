// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.feed;

import android.support.annotation.NonNull;

import com.google.android.libraries.feed.api.requestmanager.RequestManager;
import com.google.android.libraries.feed.host.scheduler.SchedulerApi;
import com.google.search.now.wire.feed.FeedQueryProto.FeedQuery.RequestReason;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.components.feed.NativeRequestBehavior;

/**
 * Provides access to native implementations of SchedulerApi.
 */
@JNINamespace("feed")
public class FeedSchedulerBridge implements FeedScheduler {
    private long mNativeBridge;
    private RequestManager mRequestManager;

    /**
     * Creates a FeedSchedulerBridge for accessing native scheduling logic.
     *
     * @param profile Profile of the user we are rendering the Feed for.
     */
    public FeedSchedulerBridge(Profile profile) {
        mNativeBridge = nativeInit(profile);
    }

    @Override
    public void destroy() {
        assert mNativeBridge != 0;
        nativeDestroy(mNativeBridge);
        mNativeBridge = 0;
    }

    /**
     * Sets our copies for various interfaces provided by the Feed library. Should be done as early
     * as possible, as the scheduler will be unable to trigger refreshes until after it has the
     * mechanisms to correctly do so. When this is called, it is assumed that the given
     * RequestManager is initialized and can be used immediately.
     *
     * @param requestManager The interface that allows us make refresh requests.
     */
    public void initializeFeedDependencies(@NonNull RequestManager requestManager) {
        assert mRequestManager == null;
        mRequestManager = requestManager;
    }

    @Override
    public int shouldSessionRequestData(SessionManagerState sessionManagerState) {
        if (mNativeBridge == 0) return SchedulerApi.RequestBehavior.UNKNOWN;

        @NativeRequestBehavior
        int nativeBehavior = nativeShouldSessionRequestData(mNativeBridge,
                sessionManagerState.hasContent, sessionManagerState.contentCreationDateTimeMs,
                sessionManagerState.hasOutstandingRequest);
        // If this breaks, it is because SchedulerApi.RequestBehavior and the NativeRequestBehavior
        // defined in feed_scheduler_host.h have diverged. If this happens during a feed DEPS roll,
        // it likely means that the native side needs to be updated. Note that this will not catch
        // new values and should handle value changes. Only removals/renames will cause compile
        // failures.
        switch (nativeBehavior) {
            case NativeRequestBehavior.REQUEST_WITH_WAIT:
                return SchedulerApi.RequestBehavior.REQUEST_WITH_WAIT;
            case NativeRequestBehavior.REQUEST_WITH_CONTENT:
                return SchedulerApi.RequestBehavior.REQUEST_WITH_CONTENT;
            case NativeRequestBehavior.REQUEST_WITH_TIMEOUT:
                return SchedulerApi.RequestBehavior.REQUEST_WITH_TIMEOUT;
            case NativeRequestBehavior.NO_REQUEST_WITH_WAIT:
                return SchedulerApi.RequestBehavior.NO_REQUEST_WITH_WAIT;
            case NativeRequestBehavior.NO_REQUEST_WITH_CONTENT:
                return SchedulerApi.RequestBehavior.NO_REQUEST_WITH_CONTENT;
            case NativeRequestBehavior.NO_REQUEST_WITH_TIMEOUT:
                return SchedulerApi.RequestBehavior.NO_REQUEST_WITH_TIMEOUT;
        }

        return SchedulerApi.RequestBehavior.UNKNOWN;
    }

    @Override
    public void onReceiveNewContent(long contentCreationDateTimeMs) {
        if (mNativeBridge != 0) {
            nativeOnReceiveNewContent(mNativeBridge, contentCreationDateTimeMs);
        }
    }

    @Override
    public void onRequestError(int networkResponseCode) {
        if (mNativeBridge != 0) {
            nativeOnRequestError(mNativeBridge, networkResponseCode);
        }
    }

    @Override
    public void onForegrounded() {
        assert mNativeBridge != 0;
        nativeOnForegrounded(mNativeBridge);
    }

    @Override
    public void onFixedTimer(Runnable onCompletion) {
        assert mNativeBridge != 0;
        nativeOnFixedTimer(mNativeBridge, onCompletion);
    }

    @Override
    public void onSuggestionConsumed() {
        assert mNativeBridge != 0;
        nativeOnSuggestionConsumed(mNativeBridge);
    }

    @Override
    public boolean onArticlesCleared(boolean suppressRefreshes) {
        assert mNativeBridge != 0;
        return nativeOnArticlesCleared(mNativeBridge, suppressRefreshes);
    }

    @CalledByNative
    private boolean triggerRefresh() {
        if (mRequestManager != null) {
            mRequestManager.triggerRefresh(RequestReason.SCHEDULED_REFRESH);
            return true;
        }
        return false;
    }

    @CalledByNative
    private void scheduleWakeUp(long thresholdMs) {
        FeedRefreshTask.scheduleWakeUp(thresholdMs);
    }

    @CalledByNative
    private void cancelWakeUp() {
        FeedRefreshTask.cancelWakeUp();
    }

    private native long nativeInit(Profile profile);
    private native void nativeDestroy(long nativeFeedSchedulerBridge);
    private native int nativeShouldSessionRequestData(long nativeFeedSchedulerBridge,
            boolean hasContent, long contentCreationDateTimeMs, boolean hasOutstandingRequest);
    private native void nativeOnReceiveNewContent(
            long nativeFeedSchedulerBridge, long contentCreationDateTimeMs);
    private native void nativeOnRequestError(
            long nativeFeedSchedulerBridge, int networkResponseCode);
    private native void nativeOnForegrounded(long nativeFeedSchedulerBridge);
    private native void nativeOnFixedTimer(long nativeFeedSchedulerBridge, Runnable onCompletion);
    private native void nativeOnSuggestionConsumed(long nativeFeedSchedulerBridge);
    private native boolean nativeOnArticlesCleared(
            long nativeFeedSchedulerBridge, boolean suppressRefreshes);
}
