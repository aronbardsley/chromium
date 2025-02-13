// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/ukm_page_load_metrics_observer.h"

#include <cmath>
#include <memory>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/engagement/site_engagement_service.h"
#include "chrome/browser/page_load_metrics/page_load_metrics_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/metrics/net/network_metrics_provider.h"
#include "content/public/browser/web_contents.h"
#include "net/base/load_timing_info.h"
#include "net/http/http_response_headers.h"
#include "services/metrics/public/cpp/metrics_utils.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "services/network/public/cpp/network_quality_tracker.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/metrics_proto/system_profile.pb.h"

// static
std::unique_ptr<page_load_metrics::PageLoadMetricsObserver>
UkmPageLoadMetricsObserver::CreateIfNeeded() {
  if (!ukm::UkmRecorder::Get()) {
    return nullptr;
  }
  return std::make_unique<UkmPageLoadMetricsObserver>(
      g_browser_process->network_quality_tracker());
}

UkmPageLoadMetricsObserver::UkmPageLoadMetricsObserver(
    network::NetworkQualityTracker* network_quality_tracker)
    : network_quality_tracker_(network_quality_tracker) {
  DCHECK(network_quality_tracker_);
}

UkmPageLoadMetricsObserver::~UkmPageLoadMetricsObserver() = default;

UkmPageLoadMetricsObserver::ObservePolicy UkmPageLoadMetricsObserver::OnStart(
    content::NavigationHandle* navigation_handle,
    const GURL& currently_committed_url,
    bool started_in_foreground) {
  if (!started_in_foreground) {
    was_hidden_ = true;
    return CONTINUE_OBSERVING;
  }

  browser_context_ = navigation_handle->GetWebContents()->GetBrowserContext();

  // When OnStart is invoked, we don't yet know whether we're observing a web
  // page load, vs another kind of load (e.g. a download or a PDF). Thus,
  // metrics and source information should not be recorded here. Instead, we
  // store data we might want to persist in member variables below, and later
  // record UKM metrics for that data once we've confirmed that we're observing
  // a web page load.

  effective_connection_type_ =
      network_quality_tracker_->GetEffectiveConnectionType();
  http_rtt_estimate_ = network_quality_tracker_->GetHttpRTT();
  transport_rtt_estimate_ = network_quality_tracker_->GetTransportRTT();
  downstream_kbps_estimate_ =
      network_quality_tracker_->GetDownstreamThroughputKbps();
  page_transition_ = navigation_handle->GetPageTransition();
  return CONTINUE_OBSERVING;
}

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
UkmPageLoadMetricsObserver::OnRedirect(
    content::NavigationHandle* navigation_handle) {
  main_frame_request_redirect_count_++;
  return CONTINUE_OBSERVING;
}

UkmPageLoadMetricsObserver::ObservePolicy UkmPageLoadMetricsObserver::OnCommit(
    content::NavigationHandle* navigation_handle,
    ukm::SourceId source_id) {
  const net::HttpResponseHeaders* response_headers =
      navigation_handle->GetResponseHeaders();
  if (response_headers)
    http_response_code_ = response_headers->response_code();
  // The PageTransition for the navigation may be updated on commit.
  page_transition_ = navigation_handle->GetPageTransition();
  was_cached_ = navigation_handle->WasResponseCached();
  is_signed_exchange_inner_response_ =
      navigation_handle->IsSignedExchangeInnerResponse();
  navigation_start_ = navigation_handle->NavigationStart();
  return CONTINUE_OBSERVING;
}

UkmPageLoadMetricsObserver::ObservePolicy
UkmPageLoadMetricsObserver::FlushMetricsOnAppEnterBackground(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  if (!was_hidden_) {
    RecordPageLoadExtraInfoMetrics(info, base::TimeTicks::Now());
    RecordTimingMetrics(timing, info);
  }
  ReportLayoutStability(info);
  return STOP_OBSERVING;
}

UkmPageLoadMetricsObserver::ObservePolicy UkmPageLoadMetricsObserver::OnHidden(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  if (!was_hidden_) {
    RecordPageLoadExtraInfoMetrics(
        info, base::TimeTicks() /* no app_background_time */);
    RecordTimingMetrics(timing, info);
    was_hidden_ = true;
  }
  return CONTINUE_OBSERVING;
}

void UkmPageLoadMetricsObserver::OnFailedProvisionalLoad(
    const page_load_metrics::FailedProvisionalLoadInfo& failed_load_info,
    const page_load_metrics::PageLoadExtraInfo& extra_info) {
  if (was_hidden_)
    return;
  RecordPageLoadExtraInfoMetrics(
      extra_info, base::TimeTicks() /* no app_background_time */);

  // Error codes have negative values, however we log net error code enum values
  // for UMA histograms using the equivalent positive value. For consistency in
  // UKM, we convert to a positive value here.
  int64_t net_error_code = static_cast<int64_t>(failed_load_info.error) * -1;
  DCHECK_GE(net_error_code, 0);
  ukm::builders::PageLoad(extra_info.source_id)
      .SetNet_ErrorCode_OnFailedProvisionalLoad(net_error_code)
      .SetPageTiming_NavigationToFailedProvisionalLoad(
          failed_load_info.time_to_failed_provisional_load.InMilliseconds())
      .Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::OnComplete(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  if (!was_hidden_) {
    RecordPageLoadExtraInfoMetrics(
        info, base::TimeTicks() /* no app_background_time */);
    RecordTimingMetrics(timing, info);
  }
  ReportLayoutStability(info);
}

void UkmPageLoadMetricsObserver::OnLoadedResource(
    const page_load_metrics::ExtraRequestCompleteInfo&
        extra_request_complete_info) {
  if (was_hidden_)
    return;
  if (extra_request_complete_info.was_cached) {
    cache_bytes_ += extra_request_complete_info.raw_body_bytes;
  } else {
    network_bytes_ += extra_request_complete_info.raw_body_bytes;
  }

  if (extra_request_complete_info.resource_type ==
      content::RESOURCE_TYPE_MAIN_FRAME) {
    DCHECK(!main_frame_timing_.has_value());
    main_frame_timing_ = *extra_request_complete_info.load_timing_info;
  }
}

void UkmPageLoadMetricsObserver::RecordTimingMetrics(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  ukm::builders::PageLoad builder(info.source_id);

  base::Optional<int64_t> rounded_site_engagement_score =
      GetRoundedSiteEngagementScore(info);
  if (rounded_site_engagement_score) {
    builder.SetSiteEngagementScore(rounded_site_engagement_score.value());
  }

  if (timing.input_to_navigation_start) {
    builder.SetExperimental_InputToNavigationStart(
        timing.input_to_navigation_start.value().InMilliseconds());
  }
  if (timing.parse_timing->parse_start) {
    builder.SetParseTiming_NavigationToParseStart(
        timing.parse_timing->parse_start.value().InMilliseconds());
  }
  if (timing.document_timing->dom_content_loaded_event_start) {
    builder.SetDocumentTiming_NavigationToDOMContentLoadedEventFired(
        timing.document_timing->dom_content_loaded_event_start.value()
            .InMilliseconds());
  }
  if (timing.document_timing->load_event_start) {
    builder.SetDocumentTiming_NavigationToLoadEventFired(
        timing.document_timing->load_event_start.value().InMilliseconds());
  }
  if (timing.paint_timing->first_paint) {
    builder.SetPaintTiming_NavigationToFirstPaint(
        timing.paint_timing->first_paint.value().InMilliseconds());
  }
  if (timing.paint_timing->first_contentful_paint) {
    builder.SetPaintTiming_NavigationToFirstContentfulPaint(
        timing.paint_timing->first_contentful_paint.value().InMilliseconds());
  }
  if (timing.paint_timing->first_meaningful_paint) {
    builder.SetExperimental_PaintTiming_NavigationToFirstMeaningfulPaint(
        timing.paint_timing->first_meaningful_paint.value().InMilliseconds());
  }
  if (timing.paint_timing->largest_image_paint.has_value() &&
      WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->largest_image_paint, info)) {
    builder.SetExperimental_PaintTiming_NavigationToLargestImagePaint(
        timing.paint_timing->largest_image_paint.value().InMilliseconds());
  }
  if (timing.paint_timing->last_image_paint.has_value() &&
      WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->last_image_paint, info)) {
    builder.SetExperimental_PaintTiming_NavigationToLastImagePaint(
        timing.paint_timing->last_image_paint.value().InMilliseconds());
  }
  if (timing.paint_timing->largest_text_paint.has_value() &&
      WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->largest_text_paint, info)) {
    builder.SetExperimental_PaintTiming_NavigationToLargestTextPaint(
        timing.paint_timing->largest_text_paint.value().InMilliseconds());
  }
  if (timing.paint_timing->last_text_paint.has_value() &&
      WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->last_text_paint, info)) {
    builder.SetExperimental_PaintTiming_NavigationToLastTextPaint(
        timing.paint_timing->last_text_paint.value().InMilliseconds());
  }
  base::Optional<base::TimeDelta> largest_content_paint_time;
  uint64_t largest_content_paint_size;
  AssignTimeAndSizeForLargestContentfulPaint(largest_content_paint_time,
                                             largest_content_paint_size,
                                             timing.paint_timing);
  if (largest_content_paint_size > 0 &&
      WasStartedInForegroundOptionalEventInForeground(
          largest_content_paint_time, info)) {
    builder.SetExperimental_PaintTiming_NavigationToLargestContentPaint(
        largest_content_paint_time.value().InMilliseconds());
  }
  if (timing.interactive_timing->interactive) {
    base::TimeDelta time_to_interactive =
        timing.interactive_timing->interactive.value();
    if (!timing.interactive_timing->first_invalidating_input ||
        timing.interactive_timing->first_invalidating_input.value() >
            time_to_interactive) {
      builder.SetExperimental_NavigationToInteractive(
          time_to_interactive.InMilliseconds());
    }
  }
  if (timing.interactive_timing->first_input_delay) {
    base::TimeDelta first_input_delay =
        timing.interactive_timing->first_input_delay.value();
    builder.SetInteractiveTiming_FirstInputDelay2(
        first_input_delay.InMilliseconds());
  }
  if (timing.interactive_timing->first_input_timestamp) {
    base::TimeDelta first_input_timestamp =
        timing.interactive_timing->first_input_timestamp.value();
    builder.SetInteractiveTiming_FirstInputTimestamp2(
        first_input_timestamp.InMilliseconds());
  }

  if (timing.interactive_timing->longest_input_delay) {
    base::TimeDelta longest_input_delay =
        timing.interactive_timing->longest_input_delay.value();
    builder.SetInteractiveTiming_LongestInputDelay2(
        longest_input_delay.InMilliseconds());
  }
  if (timing.interactive_timing->longest_input_timestamp) {
    base::TimeDelta longest_input_timestamp =
        timing.interactive_timing->longest_input_timestamp.value();
    builder.SetInteractiveTiming_LongestInputTimestamp2(
        longest_input_timestamp.InMilliseconds());
  }

  // Use a bucket spacing factor of 1.3 for bytes.
  builder.SetNet_CacheBytes(ukm::GetExponentialBucketMin(cache_bytes_, 1.3));
  builder.SetNet_NetworkBytes(
      ukm::GetExponentialBucketMin(network_bytes_, 1.3));

  if (main_frame_timing_)
    ReportMainResourceTimingMetrics(timing, &builder);

  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::RecordPageLoadExtraInfoMetrics(
    const page_load_metrics::PageLoadExtraInfo& info,
    base::TimeTicks app_background_time) {
  ukm::builders::PageLoad builder(info.source_id);
  base::Optional<base::TimeDelta> foreground_duration =
      page_load_metrics::GetInitialForegroundDuration(info,
                                                      app_background_time);
  if (foreground_duration) {
    builder.SetPageTiming_ForegroundDuration(
        foreground_duration.value().InMilliseconds());
  }

  bool is_user_initiated_navigation =
      // All browser initiated page loads are user-initiated.
      info.user_initiated_info.browser_initiated ||

      // Renderer-initiated navigations are user-initiated if there is an
      // associated input event.
      info.user_initiated_info.user_input_event;

  builder.SetExperimental_Navigation_UserInitiated(
      is_user_initiated_navigation);

  // Convert to the EffectiveConnectionType as used in SystemProfileProto
  // before persisting the metric.
  metrics::SystemProfileProto::Network::EffectiveConnectionType
      proto_effective_connection_type =
          metrics::ConvertEffectiveConnectionType(effective_connection_type_);
  if (proto_effective_connection_type !=
      metrics::SystemProfileProto::Network::EFFECTIVE_CONNECTION_TYPE_UNKNOWN) {
    builder.SetNet_EffectiveConnectionType2_OnNavigationStart(
        static_cast<int64_t>(proto_effective_connection_type));
  }

  if (http_response_code_) {
    builder.SetNet_HttpResponseCode(
        static_cast<int64_t>(http_response_code_.value()));
  }
  if (http_rtt_estimate_) {
    builder.SetNet_HttpRttEstimate_OnNavigationStart(
        static_cast<int64_t>(http_rtt_estimate_.value().InMilliseconds()));
  }
  if (transport_rtt_estimate_) {
    builder.SetNet_TransportRttEstimate_OnNavigationStart(
        static_cast<int64_t>(transport_rtt_estimate_.value().InMilliseconds()));
  }
  if (downstream_kbps_estimate_) {
    builder.SetNet_DownstreamKbpsEstimate_OnNavigationStart(
        static_cast<int64_t>(downstream_kbps_estimate_.value()));
  }
  // page_transition_ fits in a uint32_t, so we can safely cast to int64_t.
  builder.SetNavigation_PageTransition(static_cast<int64_t>(page_transition_));
  // info.page_end_reason fits in a uint32_t, so we can safely cast to int64_t.
  builder.SetNavigation_PageEndReason(
      static_cast<int64_t>(info.page_end_reason));
  if (info.did_commit && was_cached_) {
    builder.SetWasCached(1);
  }
  if (info.did_commit && is_signed_exchange_inner_response_) {
    builder.SetIsSignedExchangeInnerResponse(1);
  }
  builder.Record(ukm::UkmRecorder::Get());
}

void UkmPageLoadMetricsObserver::ReportMainResourceTimingMetrics(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    ukm::builders::PageLoad* builder) {
  DCHECK(main_frame_timing_.has_value());

  builder->SetMainFrameResource_SocketReused(main_frame_timing_->socket_reused);

  int64_t dns_start_ms =
      main_frame_timing_->connect_timing.dns_start.since_origin()
          .InMilliseconds();
  int64_t dns_end_ms = main_frame_timing_->connect_timing.dns_end.since_origin()
                           .InMilliseconds();
  int64_t connect_start_ms =
      main_frame_timing_->connect_timing.connect_start.since_origin()
          .InMilliseconds();
  int64_t connect_end_ms =
      main_frame_timing_->connect_timing.connect_end.since_origin()
          .InMilliseconds();
  int64_t request_start_ms =
      main_frame_timing_->request_start.since_origin().InMilliseconds();
  int64_t send_start_ms =
      main_frame_timing_->send_start.since_origin().InMilliseconds();
  int64_t receive_headers_end_ms =
      main_frame_timing_->receive_headers_end.since_origin().InMilliseconds();

  DCHECK_LE(dns_start_ms, dns_end_ms);
  DCHECK_LE(dns_end_ms, connect_start_ms);
  DCHECK_LE(dns_start_ms, connect_start_ms);
  DCHECK_LE(connect_start_ms, connect_end_ms);

  int64_t dns_duration_ms = dns_end_ms - dns_start_ms;
  int64_t connect_duration_ms = connect_end_ms - connect_start_ms;
  int64_t request_start_to_send_start_ms = send_start_ms - request_start_ms;
  int64_t send_start_to_receive_headers_end_ms =
      receive_headers_end_ms - send_start_ms;
  int64_t request_start_to_receive_headers_end_ms =
      receive_headers_end_ms - request_start_ms;

  builder->SetMainFrameResource_DNSDelay(dns_duration_ms);
  builder->SetMainFrameResource_ConnectDelay(connect_duration_ms);
  if (request_start_to_send_start_ms >= 0) {
    builder->SetMainFrameResource_RequestStartToSendStart(
        request_start_to_send_start_ms);
  }
  if (send_start_to_receive_headers_end_ms >= 0) {
    builder->SetMainFrameResource_SendStartToReceiveHeadersEnd(
        send_start_to_receive_headers_end_ms);
  }
  builder->SetMainFrameResource_RequestStartToReceiveHeadersEnd(
      request_start_to_receive_headers_end_ms);

  if (!main_frame_timing_->request_start.is_null() &&
      !navigation_start_.is_null()) {
    base::TimeDelta navigation_start_to_request_start =
        main_frame_timing_->request_start - navigation_start_;

    builder->SetMainFrameResource_NavigationStartToRequestStart(
        navigation_start_to_request_start.InMilliseconds());
  }

  if (main_frame_request_redirect_count_ > 0) {
    builder->SetMainFrameResource_RedirectCount(
        main_frame_request_redirect_count_);
  }
}

void UkmPageLoadMetricsObserver::ReportLayoutStability(
    const page_load_metrics::PageLoadExtraInfo& info) {
  // Avoid reporting when the feature is disabled. This ensures that a report
  // with score == 0 only occurs for a page that was actually jank-free.
  if (!base::FeatureList::IsEnabled(blink::features::kJankTracking))
    return;

  // Report (jank_score * 100) as an int in the range [0, 1000].
  float jank_score = info.main_frame_render_data.layout_jank_score;
  int64_t ukm_value =
      static_cast<int>(roundf(std::min(jank_score, 10.0f) * 100.0f));

  ukm::builders::PageLoad builder(info.source_id);
  builder.SetLayoutStability_JankScore(ukm_value);
  builder.Record(ukm::UkmRecorder::Get());

  int32_t uma_value =
      static_cast<int>(roundf(std::min(jank_score, 10.0f) * 10.0f));
  UMA_HISTOGRAM_COUNTS_100("PageLoad.Experimental.LayoutStability.JankScore",
                           uma_value);
}

base::Optional<int64_t>
UkmPageLoadMetricsObserver::GetRoundedSiteEngagementScore(
    const page_load_metrics::PageLoadExtraInfo& info) const {
  if (!browser_context_)
    return base::nullopt;

  Profile* profile = Profile::FromBrowserContext(browser_context_);
  SiteEngagementService* engagement_service =
      SiteEngagementService::Get(profile);

  // UKM privacy requires the engagement score be rounded to nearest
  // value of 10.
  int64_t rounded_document_engagement_score =
      static_cast<int>(
          std::roundf(engagement_service->GetScore(info.url) / 10.0)) *
      10;

  DCHECK(rounded_document_engagement_score >= 0 &&
         rounded_document_engagement_score <=
             engagement_service->GetMaxPoints());

  return rounded_document_engagement_score;
}
