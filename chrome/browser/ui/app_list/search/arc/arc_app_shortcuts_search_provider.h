// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_SEARCH_ARC_ARC_APP_SHORTCUTS_SEARCH_PROVIDER_H_
#define CHROME_BROWSER_UI_APP_LIST_SEARCH_ARC_ARC_APP_SHORTCUTS_SEARCH_PROVIDER_H_

#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "chrome/browser/ui/app_list/search/search_provider.h"
#include "components/arc/common/app.mojom.h"

class AppListControllerDelegate;
class Profile;

namespace app_list {

class AppSearchResultRanker;

class ArcAppShortcutsSearchProvider : public SearchProvider {
 public:
  ArcAppShortcutsSearchProvider(int max_results,
                                Profile* profile,
                                AppListControllerDelegate* list_controller,
                                AppSearchResultRanker* ranker);
  ~ArcAppShortcutsSearchProvider() override;

  // SearchProvider:
  void Start(const base::string16& query) override;

 private:
  void OnGetAppShortcutGlobalQueryItems(
      std::vector<arc::mojom::AppShortcutItemPtr> shortcut_items);

  const int max_results_;
  Profile* const profile_;                            // Owned by ProfileInfo.
  AppListControllerDelegate* const list_controller_;  // Owned by AppListClient.
  // TODO(crbug.com/931149): train this ranker on app shortcut clicks, and use
  // it to tweak their relevance scores.
  AppSearchResultRanker* ranker_;

  base::WeakPtrFactory<ArcAppShortcutsSearchProvider> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ArcAppShortcutsSearchProvider);
};

}  // namespace app_list

#endif  // CHROME_BROWSER_UI_APP_LIST_SEARCH_ARC_ARC_APP_SHORTCUTS_SEARCH_PROVIDER_H_
