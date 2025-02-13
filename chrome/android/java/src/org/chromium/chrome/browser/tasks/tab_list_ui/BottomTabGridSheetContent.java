// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_list_ui;

import android.view.View;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.widget.bottomsheet.BottomSheet.BottomSheetContent;
import org.chromium.chrome.browser.widget.bottomsheet.BottomSheet.ContentPriority;

import javax.inject.Inject;

/** A {@link BottomSheetContent} that displays tab grid. **/
public class BottomTabGridSheetContent implements BottomSheetContent {
    private final TabListRecyclerView mRecyclerView;

    /**
     * Construct a new {@link ContextualSuggestionsBottomSheetContent}.
     * @param recyclerView The {@link TabListRecyclerView} holding the tab grid.
     */
    @Inject
    BottomTabGridSheetContent(TabListRecyclerView recyclerView) {
        mRecyclerView = recyclerView;
    }

    @Override
    public View getContentView() {
        return mRecyclerView;
    }

    @Override
    public View getToolbarView() {
        return null;
    }

    @Override
    public int getVerticalScrollOffset() {
        return mRecyclerView.computeVerticalScrollOffset();
    }

    @Override
    public void destroy() {}

    @Override
    public @ContentPriority int getPriority() {
        return ContentPriority.HIGH;
    }

    @Override
    public boolean swipeToDismissEnabled() {
        return true;
    }

    @Override
    public boolean isPeekStateEnabled() {
        return false;
    }

    @Override
    public int getSheetContentDescriptionStringId() {
        return R.string.bottom_tab_grid_description;
    }

    @Override
    public int getSheetHalfHeightAccessibilityStringId() {
        return R.string.bottom_tab_grid_opened_half;
    }

    @Override
    public int getSheetFullHeightAccessibilityStringId() {
        return R.string.bottom_tab_grid_opened_full;
    }

    @Override
    public int getSheetClosedAccessibilityStringId() {
        return R.string.bottom_tab_grid_closed;
    }
}
