// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/in_product_help/reopen_tab_in_product_help_trigger.h"

#include <utility>

#include "base/metrics/field_trial_params.h"
#include "base/optional.h"
#include "base/strings/string_number_conversions.h"
#include "components/feature_engagement/public/event_constants.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/feature_engagement/public/tracker.h"

namespace {

// These parameters start with "x_" to indicate that the IPH backend should
// ignore these.
const char kTabMinimumActiveDurationParamName[] =
    "x_tab_minimum_active_duration";
const char kNewTabOpenedTimeoutParamName[] = "x_new_tab_opened_timeout";

// Default timeouts, if a field trial isn't present (only used for interactive
// testing).
const base::TimeDelta kDefaultTabMinimumActiveDuration =
    base::TimeDelta::FromSeconds(10);
const base::TimeDelta kDefaultNewTabOpenedTimeout =
    base::TimeDelta::FromSeconds(10);

base::Optional<base::TimeDelta> GetTimeoutFromFieldTrialParam(
    const std::string& name) {
  std::string str = base::GetFieldTrialParamValueByFeature(
      feature_engagement::kIPHReopenTabFeature, name);
  int timeout_seconds = 0;
  if (str.size() > 0) {
    base::StringToInt(str, &timeout_seconds);
    DCHECK_GT(timeout_seconds, 0);
    return base::TimeDelta::FromSeconds(timeout_seconds);
  }

  return base::Optional<base::TimeDelta>();
}

}  // namespace

ReopenTabInProductHelpTrigger::ReopenTabInProductHelpTrigger(
    feature_engagement::Tracker* tracker,
    const base::TickClock* clock)
    : tracker_(tracker),
      clock_(clock),
      tab_minimum_active_duration_(
          GetTimeoutFromFieldTrialParam(kTabMinimumActiveDurationParamName)
              .value_or(kDefaultTabMinimumActiveDuration)),
      new_tab_opened_timeout_(
          GetTimeoutFromFieldTrialParam(kNewTabOpenedTimeoutParamName)
              .value_or(kDefaultNewTabOpenedTimeout)),
      trigger_state_(NO_ACTIONS_SEEN) {
  DCHECK(tracker);
  DCHECK(clock);
}

ReopenTabInProductHelpTrigger::~ReopenTabInProductHelpTrigger() = default;

void ReopenTabInProductHelpTrigger::SetShowHelpCallback(
    ShowHelpCallback callback) {
  DCHECK(callback);
  cb_ = std::move(callback);
}

void ReopenTabInProductHelpTrigger::ActiveTabClosed(
    base::TimeDelta active_duration) {
  // Reset all flags at this point. We should only trigger IPH if the events
  // happen in the prescribed order.
  ResetTriggerState();

  DCHECK(active_duration >= base::TimeDelta());
  // We only go to the next state if the closing tab was active for long enough.
  if (active_duration >= tab_minimum_active_duration_) {
    trigger_state_ = ACTIVE_TAB_CLOSED;
    time_of_last_step_ = clock_->NowTicks();
  }
}

void ReopenTabInProductHelpTrigger::NewTabOpened() {
  const base::TimeDelta elapsed_time = clock_->NowTicks() - time_of_last_step_;

  if (trigger_state_ == ACTIVE_TAB_CLOSED &&
      elapsed_time < new_tab_opened_timeout_) {
    tracker_->NotifyEvent(feature_engagement::events::kReopenTabConditionsMet);
  } else {
    ResetTriggerState();
  }

  // Make sure ShouldTriggerHelpUI is always called when a new tab is
  // opened. This makes testing the UI easier when the feature engagement demo
  // mode is enabled (in which the first call always returns true). Making the
  // call not conditional on our triggering conditions means the UI can be
  // triggered more easily and consistently. In production, this will always
  // return false anyway since we didn't call NotifyEvent().
  if (tracker_->ShouldTriggerHelpUI(feature_engagement::kIPHReopenTabFeature)) {
    DCHECK(cb_);
    cb_.Run();
  }
}

void ReopenTabInProductHelpTrigger::HelpDismissed() {
  tracker_->Dismissed(feature_engagement::kIPHReopenTabFeature);
  ResetTriggerState();
}

// static
std::map<std::string, std::string>
ReopenTabInProductHelpTrigger::GetFieldTrialParamsForTest(
    int tab_minimum_active_duration_seconds,
    int new_tab_opened_timeout_seconds) {
  std::map<std::string, std::string> params;
  params[kTabMinimumActiveDurationParamName] =
      base::NumberToString(tab_minimum_active_duration_seconds);
  params[kNewTabOpenedTimeoutParamName] =
      base::NumberToString(new_tab_opened_timeout_seconds);
  return params;
}

void ReopenTabInProductHelpTrigger::ResetTriggerState() {
  time_of_last_step_ = base::TimeTicks();
  trigger_state_ = NO_ACTIONS_SEEN;
}
