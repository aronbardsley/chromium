// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/frame_host/ipc_utils.h"

#include <utility>

#include "content/browser/bad_message.h"
#include "content/browser/blob_storage/chrome_blob_storage_context.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/common/frame.mojom.h"
#include "content/common/frame_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/url_constants.h"
#include "mojo/public/cpp/system/message_pipe.h"

namespace content {

namespace {

bool VerifyBlobToken(
    int process_id,
    mojo::MessagePipeHandle received_token,
    const GURL& received_url,
    blink::mojom::BlobURLTokenPtrInfo* out_blob_url_token_info) {
  DCHECK_NE(ChildProcessHost::kInvalidUniqueID, process_id);
  DCHECK(out_blob_url_token_info);

  mojo::ScopedMessagePipeHandle blob_url_token_handle(
      std::move(received_token));
  blink::mojom::BlobURLTokenPtrInfo blob_url_token_info(
      std::move(blob_url_token_handle), blink::mojom::BlobURLToken::Version_);
  if (blob_url_token_info) {
    if (!received_url.SchemeIsBlob()) {
      bad_message::ReceivedBadMessage(
          process_id, bad_message::BLOB_URL_TOKEN_FOR_NON_BLOB_URL);
      return false;
    }
  }

  *out_blob_url_token_info = std::move(blob_url_token_info);
  return true;
}

bool VerifyInitiatorOrigin(int process_id,
                           const url::Origin& initiator_origin) {
  // TODO(lukasza, nasko): Verify precursor origin via CanAccessDataForOrigin.
  if (initiator_origin.opaque())
    return true;

  auto* policy = ChildProcessSecurityPolicyImpl::GetInstance();
  if (!policy->CanAccessDataForOrigin(process_id, initiator_origin)) {
    bad_message::ReceivedBadMessage(process_id,
                                    bad_message::INVALID_INITIATOR_ORIGIN);
    return false;
  }

  return true;
}

}  // namespace

bool VerifyDownloadUrlParams(
    int process_id,
    const FrameHostMsg_DownloadUrl_Params& params,
    blink::mojom::BlobURLTokenPtrInfo* out_blob_url_token_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_NE(ChildProcessHost::kInvalidUniqueID, process_id);
  DCHECK(out_blob_url_token_info);

  // Verify |params.blob_url_token| and populate |out_blob_url_token_info|.
  if (!VerifyBlobToken(process_id, params.blob_url_token, params.url,
                       out_blob_url_token_info)) {
    return false;
  }

  // Verify |params.initiator_origin|.
  if (!VerifyInitiatorOrigin(process_id, params.initiator_origin))
    return false;

  // Verification succeeded.
  return true;
}

bool VerifyOpenURLParams(SiteInstance* site_instance,
                         const FrameHostMsg_OpenURL_Params& params,
                         GURL* out_validated_url,
                         scoped_refptr<network::SharedURLLoaderFactory>*
                             out_blob_url_loader_factory) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(site_instance);
  DCHECK(out_validated_url);
  DCHECK(out_blob_url_loader_factory);
  RenderProcessHost* process = site_instance->GetProcess();
  int process_id = process->GetID();

  // Verify |params.url| and populate |out_validated_url|.
  *out_validated_url = params.url;
  process->FilterURL(false, out_validated_url);

  // Verify |params.blob_url_token| and populate |out_blob_url_loader_factory|.
  blink::mojom::BlobURLTokenPtrInfo blob_url_token_info;
  if (!VerifyBlobToken(process_id, params.blob_url_token, params.url,
                       &blob_url_token_info)) {
    return false;
  }
  if (blob_url_token_info) {
    blink::mojom::BlobURLTokenPtr blob_url_token(
        std::move(blob_url_token_info));
    *out_blob_url_loader_factory =
        ChromeBlobStorageContext::URLLoaderFactoryForToken(
            process->GetBrowserContext(), std::move(blob_url_token));
  }

  // Verify |params.resource_request_body|.
  auto* policy = ChildProcessSecurityPolicyImpl::GetInstance();
  if (!policy->CanReadRequestBody(site_instance,
                                  params.resource_request_body)) {
    bad_message::ReceivedBadMessage(process,
                                    bad_message::ILLEGAL_UPLOAD_PARAMS);
    return false;
  }

  // Verify |params.initiator_origin|.
  if (!VerifyInitiatorOrigin(process_id, params.initiator_origin))
    return false;

  // Verification succeeded.
  return true;
}

bool VerifyBeginNavigationCommonParams(SiteInstance* site_instance,
                                       CommonNavigationParams* common_params) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(site_instance);
  DCHECK(common_params);
  RenderProcessHost* process = site_instance->GetProcess();
  int process_id = process->GetID();

  // Verify (and possibly rewrite) |url|.
  process->FilterURL(false, &common_params->url);
  if (common_params->url.SchemeIs(kChromeErrorScheme)) {
    mojo::ReportBadMessage("Renderer cannot request error page URLs directly");
    return false;
  }

  // Verify |resource_request_body|.
  auto* policy = ChildProcessSecurityPolicyImpl::GetInstance();
  if (!policy->CanReadRequestBody(site_instance, common_params->post_data)) {
    bad_message::ReceivedBadMessage(process,
                                    bad_message::ILLEGAL_UPLOAD_PARAMS);
    return false;
  }

  // Verify |initiator_origin|.
  if (common_params->initiator_origin.has_value() &&
      !VerifyInitiatorOrigin(process_id,
                             common_params->initiator_origin.value())) {
    return false;
  }

  // Verify |base_url_for_data_url|.
  if (!common_params->base_url_for_data_url.is_empty()) {
    // Kills the process. http://crbug.com/726142
    bad_message::ReceivedBadMessage(
        process, bad_message::RFH_BASE_URL_FOR_DATA_URL_SPECIFIED);
    return false;
  }

  // Verification succeeded.
  return true;
}

}  // namespace content
