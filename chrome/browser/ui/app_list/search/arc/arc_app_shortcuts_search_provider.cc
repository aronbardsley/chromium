// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/arc/arc_app_shortcuts_search_provider.h"

#include <memory>
#include <utility>

#include "ash/public/cpp/app_list/app_list_features.h"
#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/app_list/search/arc/arc_app_shortcut_search_result.h"
#include "chrome/browser/ui/app_list/search/search_result_ranker/app_search_result_ranker.h"
#include "components/arc/arc_bridge_service.h"
#include "components/arc/arc_service_manager.h"

namespace app_list {

ArcAppShortcutsSearchProvider::ArcAppShortcutsSearchProvider(
    int max_results,
    Profile* profile,
    AppListControllerDelegate* list_controller,
    AppSearchResultRanker* ranker)
    : max_results_(max_results),
      profile_(profile),
      list_controller_(list_controller),
      ranker_(ranker),
      weak_ptr_factory_(this) {}

ArcAppShortcutsSearchProvider::~ArcAppShortcutsSearchProvider() = default;

void ArcAppShortcutsSearchProvider::Start(const base::string16& query) {
  arc::mojom::AppInstance* app_instance =
      arc::ArcServiceManager::Get()
          ? ARC_GET_INSTANCE_FOR_METHOD(
                arc::ArcServiceManager::Get()->arc_bridge_service()->app(),
                GetAppShortcutGlobalQueryItems)
          : nullptr;

  if (!app_instance || query.empty()) {
    ClearResults();
    return;
  }

  // Invalidate the weak ptr to prevent previous callback run.
  weak_ptr_factory_.InvalidateWeakPtrs();
  app_instance->GetAppShortcutGlobalQueryItems(
      base::UTF16ToUTF8(query), max_results_,
      base::BindOnce(
          &ArcAppShortcutsSearchProvider::OnGetAppShortcutGlobalQueryItems,
          weak_ptr_factory_.GetWeakPtr()));
}

void ArcAppShortcutsSearchProvider::OnGetAppShortcutGlobalQueryItems(
    std::vector<arc::mojom::AppShortcutItemPtr> shortcut_items) {
  const ArcAppListPrefs* arc_prefs = ArcAppListPrefs::Get(profile_);
  DCHECK(arc_prefs);

  SearchProvider::Results search_results;
  for (auto& item : shortcut_items) {
    const std::string app_id =
        arc_prefs->GetAppIdByPackageName(item->package_name.value());
    std::unique_ptr<ArcAppListPrefs::AppInfo> app_info =
        arc_prefs->GetApp(app_id);
    // Ignore shortcuts for apps that are not present in the launcher.
    if (!app_info || !app_info->show_in_launcher)
      continue;
    search_results.emplace_back(std::make_unique<ArcAppShortcutSearchResult>(
        std::move(item), profile_, list_controller_));

    if (app_list_features::IsAppSearchResultRankerEnabled() &&
        ranker_ != nullptr) {
      // TODO(crbug.com/931149): tweak the scores of each search result item
      // using the ranker.
    }
  }

  SwapResults(&search_results);
}

}  // namespace app_list
