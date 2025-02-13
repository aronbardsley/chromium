// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_list_ui;

import android.graphics.Bitmap;
import android.graphics.drawable.Drawable;
import android.support.annotation.Nullable;
import android.support.v4.content.res.ResourcesCompat;
import android.support.v7.widget.RecyclerView.ViewHolder;
import android.widget.FrameLayout;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.util.ViewUtils;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * {@link org.chromium.ui.modelutil.SimpleRecyclerViewMcp.ViewBinder} for tab grid.
 * This class supports both full and partial updates to the {@link TabStripViewHolder}.
 */
class TabStripViewBinder {
    /**
     * Partially or fully update the given ViewHolder based on the given model over propertyKey.
     * @param holder The {@link ViewHolder} to use.
     * @param item The model to use.
     * @param propertyKey If present, to be used as the key to partially update. If null, a full
     *                    bind is done.
     */
    public static void onBindViewHolder(
            TabStripViewHolder holder, PropertyModel item, @Nullable PropertyKey propertyKey) {
        if (propertyKey == null) {
            onBindViewHolder(holder, item);
            return;
        }
        if (TabProperties.IS_SELECTED == propertyKey) {
            ((FrameLayout) holder.itemView)
                    .setForeground(item.get(TabProperties.IS_SELECTED)
                                    ? ResourcesCompat.getDrawable(holder.itemView.getResources(),
                                            R.drawable.selected_tab_background,
                                            holder.itemView.getContext().getTheme())
                                    : null);
            if (item.get(TabProperties.IS_SELECTED)) {
                holder.button.setOnClickListener(view -> {
                    item.get(TabProperties.TAB_CLOSED_LISTENER).run(holder.getTabId());
                });
            } else {
                holder.button.setOnClickListener(view -> {
                    item.get(TabProperties.TAB_SELECTED_LISTENER).run(holder.getTabId());
                });
            }
        } else if (TabProperties.FAVICON == propertyKey) {
            Bitmap favicon = item.get(TabProperties.FAVICON);
            if (favicon == null) return;
            int faviconSize = holder.itemView.getContext().getResources().getDimensionPixelSize(
                    R.dimen.default_favicon_size);
            Drawable drawable = ViewUtils.createRoundedBitmapDrawable(
                    Bitmap.createScaledBitmap(favicon, faviconSize, faviconSize, true),
                    ViewUtils.DEFAULT_FAVICON_CORNER_RADIUS);
            holder.button.setImageDrawable(drawable);
        } else if (TabProperties.TAB_ID == propertyKey) {
            holder.setTabId(item.get(TabProperties.TAB_ID));
        }
    }

    private static void onBindViewHolder(TabStripViewHolder holder, PropertyModel item) {
        for (PropertyKey propertyKey : TabProperties.ALL_KEYS_TAB_STRIP) {
            onBindViewHolder(holder, item, propertyKey);
        }
    }
}
