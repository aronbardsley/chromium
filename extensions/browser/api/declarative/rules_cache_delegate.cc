// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/declarative/rules_cache_delegate.h"

#include <utility>

#include "base/bind.h"
#include "base/task/post_task.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "extensions/browser/api/declarative/rules_registry.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/info_map.h"
#include "extensions/browser/state_store.h"
#include "extensions/common/permissions/permissions_data.h"

namespace {

// Returns the key to use for storing declarative rules in the state store.
std::string GetDeclarativeRuleStorageKey(const std::string& event_name,
                                         bool incognito) {
  if (incognito)
    return "declarative_rules.incognito." + event_name;
  else
    return "declarative_rules." + event_name;
}


}  // namespace

namespace extensions {

// RulesCacheDelegate

const char RulesCacheDelegate::kRulesStoredKey[] =
    "has_declarative_rules";

RulesCacheDelegate::RulesCacheDelegate(Type type, bool log_storage_init_delay)
    : type_(type),
      browser_context_(nullptr),
      log_storage_init_delay_(log_storage_init_delay),
      notified_registry_(false),
      weak_ptr_factory_(this) {}

RulesCacheDelegate::~RulesCacheDelegate() {}

// Returns the key to use for storing whether the rules have been stored.
// static
std::string RulesCacheDelegate::GetRulesStoredKey(const std::string& event_name,
                                                  bool incognito) {
  std::string result(kRulesStoredKey);
  result += incognito ? ".incognito." : ".";
  return result + event_name;
}

// This is called from the constructor of RulesRegistry, so it is
// important that it both
// 1. calls no (in particular virtual) methods of the rules registry, and
// 2. does not create scoped_refptr holding the registry. (A short-lived
// scoped_refptr might delete the rules registry before it is constructed.)
void RulesCacheDelegate::Init(RulesRegistry* registry) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // WARNING: The first use of |registry_| will bind it to the calling thread
  // so don't use this here.
  registry_ = registry->GetWeakPtr();
  rules_registry_thread_ = registry->owner_thread();
  browser_context_ = registry->browser_context();

  if (browser_context_->IsOffTheRecord())
    log_storage_init_delay_ = false;

  ExtensionSystem& system = *ExtensionSystem::Get(browser_context_);

  if (type_ == Type::kPersistent) {
    storage_key_ = GetDeclarativeRuleStorageKey(
        registry->event_name(), browser_context_->IsOffTheRecord());
    rules_stored_key_ = GetRulesStoredKey(registry->event_name(),
                                          browser_context_->IsOffTheRecord());

    StateStore* store = system.rules_store();
    if (store)
      store->RegisterKey(storage_key_);

    system.ready().Post(
        FROM_HERE, base::BindRepeating(
                       &RulesCacheDelegate::ReadRulesForInstalledExtensions,
                       weak_ptr_factory_.GetWeakPtr()));
  }

  system.ready().Post(FROM_HERE,
                      base::BindRepeating(&RulesCacheDelegate::CheckIfReady,
                                          weak_ptr_factory_.GetWeakPtr()));
}

void RulesCacheDelegate::UpdateRules(const std::string& extension_id,
                                     base::Value value) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser_context_)
    return;

  DCHECK(value.is_list());
  has_nonempty_ruleset_ = !value.GetList().empty();
  for (auto& observer : observers_)
    observer.OnUpdateRules();

  if (type_ == Type::kEphemeral)
    return;

  bool rules_stored_previously = GetDeclarativeRulesStored(extension_id);
  SetDeclarativeRulesStored(extension_id, has_nonempty_ruleset_);
  if (!rules_stored_previously && !has_nonempty_ruleset_)
    return;

  StateStore* store = ExtensionSystem::Get(browser_context_)->rules_store();
  if (store) {
    store->SetExtensionValue(extension_id, storage_key_,
                             std::make_unique<base::Value>(std::move(value)));
  }
}

bool RulesCacheDelegate::HasRules() const {
  return has_nonempty_ruleset_;
}

void RulesCacheDelegate::AddObserver(Observer* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void RulesCacheDelegate::RemoveObserver(Observer* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void RulesCacheDelegate::CheckIfReady() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (notified_registry_ || !waiting_for_extensions_.empty())
    return;

  base::PostTaskWithTraits(
      FROM_HERE, {rules_registry_thread_},
      base::Bind(&RulesRegistry::MarkReady, registry_, storage_init_time_));
  notified_registry_ = true;
}

void RulesCacheDelegate::ReadRulesForInstalledExtensions() {
  bool is_ready = ExtensionSystem::Get(browser_context_)->ready().is_signaled();
  // In an OTR context, we start on top of a normal context already, so the
  // extension service should be ready.
  DCHECK(!browser_context_->IsOffTheRecord() || is_ready);
  DCHECK_EQ(Type::kPersistent, type_);
  if (is_ready) {
    const ExtensionSet& extensions =
        ExtensionRegistry::Get(browser_context_)->enabled_extensions();
    const ExtensionPrefs* extension_prefs =
        ExtensionPrefs::Get(browser_context_);
    for (ExtensionSet::const_iterator i = extensions.begin();
         i != extensions.end();
         ++i) {
      bool needs_apis_storing_rules =
          (*i)->permissions_data()->HasAPIPermission(
              APIPermission::kDeclarativeContent) ||
          (*i)->permissions_data()->HasAPIPermission(
              APIPermission::kDeclarativeWebRequest);
      bool respects_off_the_record =
          !(browser_context_->IsOffTheRecord()) ||
          extension_prefs->IsIncognitoEnabled((*i)->id());
      if (needs_apis_storing_rules && respects_off_the_record)
        ReadFromStorage((*i)->id());
    }
  }
}

void RulesCacheDelegate::ReadFromStorage(const std::string& extension_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK_EQ(Type::kPersistent, type_);
  if (!browser_context_)
    return;

  if (log_storage_init_delay_ && storage_init_time_.is_null())
    storage_init_time_ = base::Time::Now();

  if (!GetDeclarativeRulesStored(extension_id)) {
    ExtensionSystem::Get(browser_context_)->ready().Post(
        FROM_HERE, base::Bind(&RulesCacheDelegate::CheckIfReady,
                              weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  StateStore* store = ExtensionSystem::Get(browser_context_)->rules_store();
  if (!store)
    return;
  waiting_for_extensions_.insert(extension_id);
  store->GetExtensionValue(
      extension_id,
      storage_key_,
      base::Bind(&RulesCacheDelegate::ReadFromStorageCallback,
                 weak_ptr_factory_.GetWeakPtr(),
                 extension_id));
}

void RulesCacheDelegate::ReadFromStorageCallback(
    const std::string& extension_id,
    std::unique_ptr<base::Value> value) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK_EQ(Type::kPersistent, type_);
  base::PostTaskWithTraits(
      FROM_HERE, {rules_registry_thread_},
      base::Bind(&RulesRegistry::DeserializeAndAddRules, registry_,
                 extension_id, base::Passed(&value)));

  waiting_for_extensions_.erase(extension_id);

  if (waiting_for_extensions_.empty())
    ExtensionSystem::Get(browser_context_)->ready().Post(
        FROM_HERE, base::Bind(&RulesCacheDelegate::CheckIfReady,
                              weak_ptr_factory_.GetWeakPtr()));
}

bool RulesCacheDelegate::GetDeclarativeRulesStored(
    const std::string& extension_id) const {
  CHECK(browser_context_);
  DCHECK_EQ(Type::kPersistent, type_);
  const ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(browser_context_);

  bool rules_stored = true;
  if (extension_prefs->ReadPrefAsBoolean(
          extension_id, rules_stored_key_, &rules_stored))
    return rules_stored;

  // Safe default -- if we don't know that the rules are not stored, we force
  // a read by returning true.
  return true;
}

void RulesCacheDelegate::SetDeclarativeRulesStored(
    const std::string& extension_id,
    bool rules_stored) {
  CHECK(browser_context_);
  DCHECK_EQ(Type::kPersistent, type_);
  DCHECK(ExtensionRegistry::Get(browser_context_)
             ->GetExtensionById(extension_id, ExtensionRegistry::EVERYTHING));

  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(browser_context_);
  extension_prefs->UpdateExtensionPref(
      extension_id, rules_stored_key_,
      std::make_unique<base::Value>(rules_stored));
}

}  // namespace extensions
