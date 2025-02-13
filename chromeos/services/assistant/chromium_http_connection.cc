// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The file comes from Google Home(cast) implementation.

#include "chromeos/services/assistant/chromium_http_connection.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/task/post_task.h"
#include "net/base/load_flags.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

using assistant_client::HttpConnection;
using network::SharedURLLoaderFactory;
using network::SharedURLLoaderFactoryInfo;

namespace chromeos {
namespace assistant {

namespace {

// Invalid/Unknown HTTP response code.
constexpr int kResponseCodeInvalid = -1;

}  // namespace

ChromiumHttpConnectionFactory::ChromiumHttpConnectionFactory(
    std::unique_ptr<SharedURLLoaderFactoryInfo> url_loader_factory_info)
    : url_loader_factory_(
          SharedURLLoaderFactory::Create(std::move(url_loader_factory_info))) {}

ChromiumHttpConnectionFactory::~ChromiumHttpConnectionFactory() = default;

HttpConnection* ChromiumHttpConnectionFactory::Create(
    HttpConnection::Delegate* delegate) {
  return new ChromiumHttpConnection(url_loader_factory_->Clone(), delegate);
}

ChromiumHttpConnection::ChromiumHttpConnection(
    std::unique_ptr<SharedURLLoaderFactoryInfo> url_loader_factory_info,
    Delegate* delegate)
    : delegate_(delegate),
      task_runner_(base::CreateSequencedTaskRunnerWithTraits({})),
      url_loader_factory_info_(std::move(url_loader_factory_info)) {
  DCHECK(delegate_);
  DCHECK(url_loader_factory_info_);

  // Add a reference, so |this| cannot go away until Close() is called.
  AddRef();
}

ChromiumHttpConnection::~ChromiumHttpConnection() {
  // The destructor may be called on another sequence when the connection
  // is cancelled early, for example due to a reconfigure event.
  DCHECK_EQ(state_, State::DESTROYED);
}

void ChromiumHttpConnection::SetRequest(const std::string& url, Method method) {
  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ChromiumHttpConnection::SetRequestOnTaskRunner,
                                this, url, method));
}

void ChromiumHttpConnection::SetRequestOnTaskRunner(const std::string& url,
                                                    Method method) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(state_, State::NEW);
  url_ = GURL(url);
  method_ = method;
}

void ChromiumHttpConnection::AddHeader(const std::string& name,
                                       const std::string& value) {
  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ChromiumHttpConnection::AddHeaderOnTaskRunner,
                                this, name, value));
}

void ChromiumHttpConnection::AddHeaderOnTaskRunner(const std::string& name,
                                                   const std::string& value) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(state_, State::NEW);
  // From https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2:
  // "Multiple message-header fields with the same field-name MAY be present in
  // a message if and only if the entire field-value for that header field is
  // defined as a comma-separated list [i.e., #(values)]. It MUST be possible to
  // combine the multiple header fields into one "field-name: field-value" pair,
  // without changing the semantics of the message, by appending each subsequent
  // field-value to the first, each separated by a comma."
  std::string existing_value;
  if (headers_.GetHeader(name, &existing_value)) {
    headers_.SetHeader(name, existing_value + ',' + value);
  } else {
    headers_.SetHeader(name, value);
  }
}

void ChromiumHttpConnection::SetUploadContent(const std::string& content,
                                              const std::string& content_type) {
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ChromiumHttpConnection::SetUploadContentOnTaskRunner,
                     this, content, content_type));
}

void ChromiumHttpConnection::SetUploadContentOnTaskRunner(
    const std::string& content,
    const std::string& content_type) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(state_, State::NEW);
  upload_content_ = content;
  upload_content_type_ = content_type;
  chunked_upload_content_type_ = "";
}

void ChromiumHttpConnection::SetChunkedUploadContentType(
    const std::string& content_type) {
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &ChromiumHttpConnection::SetChunkedUploadContentTypeOnTaskRunner,
          this, content_type));
}

void ChromiumHttpConnection::SetChunkedUploadContentTypeOnTaskRunner(
    const std::string& content_type) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(state_, State::NEW);
  upload_content_ = "";
  upload_content_type_ = "";
  chunked_upload_content_type_ = content_type;
}

void ChromiumHttpConnection::EnableHeaderResponse() {
  NOTIMPLEMENTED();
}

void ChromiumHttpConnection::EnablePartialResults() {
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ChromiumHttpConnection::EnablePartialResultsOnTaskRunner,
                     this));
}

void ChromiumHttpConnection::EnablePartialResultsOnTaskRunner() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(state_, State::NEW);
  handle_partial_response_ = true;
}

void ChromiumHttpConnection::Start() {
  VLOG(2) << "Requested to start connection";
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ChromiumHttpConnection::StartOnTaskRunner, this));
}

void ChromiumHttpConnection::StartOnTaskRunner() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(state_, State::NEW);
  state_ = State::STARTED;

  if (!url_.is_valid()) {
    state_ = State::COMPLETED;
    VLOG(2) << "Completing connection with invalid URL";
    delegate_->OnNetworkError(kResponseCodeInvalid, "Invalid GURL");
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url_;
  resource_request->headers = headers_;
  switch (method_) {
    case Method::GET:
      resource_request->method = "GET";
      break;
    case Method::POST:
      resource_request->method = "POST";
      break;
    case Method::HEAD:
      resource_request->method = "HEAD";
      break;
  }
  resource_request->load_flags = net::LOAD_DO_NOT_SEND_AUTH_DATA |
                                 net::LOAD_DO_NOT_SAVE_COOKIES |
                                 net::LOAD_DO_NOT_SEND_COOKIES;

  const bool chunked_upload =
      !chunked_upload_content_type_.empty() && method_ == Method::POST;
  if (chunked_upload) {
    // Attach a chunked upload body.
    network::mojom::ChunkedDataPipeGetterPtr data_pipe;
    binding_set_.AddBinding(this, mojo::MakeRequest(&data_pipe));
    resource_request->request_body = new network::ResourceRequestBody();
    resource_request->request_body->SetToChunkedDataPipe(std::move(data_pipe));
  }

  url_loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 NO_TRAFFIC_ANNOTATION_YET);
  url_loader_->SetRetryOptions(
      /*max_retries=*/0, network::SimpleURLLoader::RETRY_NEVER);
  if (!upload_content_type_.empty())
    url_loader_->AttachStringForUpload(upload_content_, upload_content_type_);

  auto factory =
      SharedURLLoaderFactory::Create(std::move(url_loader_factory_info_));
  if (handle_partial_response_) {
    url_loader_->DownloadAsStream(factory.get(), this);
  } else {
    url_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
        factory.get(),
        base::BindOnce(&ChromiumHttpConnection::OnURLLoadComplete, this));
  }
}

void ChromiumHttpConnection::Pause() {
  NOTIMPLEMENTED();
}

void ChromiumHttpConnection::Resume() {
  NOTIMPLEMENTED();
}

void ChromiumHttpConnection::Close() {
  VLOG(2) << "Requesting to close connection object";
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&ChromiumHttpConnection::CloseOnTaskRunner, this));
}

void ChromiumHttpConnection::CloseOnTaskRunner() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (state_ == State::DESTROYED)
    return;

  VLOG(2) << "Closing connection object";
  state_ = State::DESTROYED;
  url_loader_.reset();

  delegate_->OnConnectionDestroyed();

  Release();
}

void ChromiumHttpConnection::GetSize(GetSizeCallback get_size_callback) {
  if (has_last_chunk_)
    std::move(get_size_callback).Run(net::OK, upload_body_size_);
  else
    get_size_callback_ = std::move(get_size_callback);
}

void ChromiumHttpConnection::StartReading(
    mojo::ScopedDataPipeProducerHandle pipe) {
  // Delete any existing pipe, if any.
  upload_pipe_watcher_.reset();
  upload_pipe_ = std::move(pipe);
  upload_pipe_watcher_ = std::make_unique<mojo::SimpleWatcher>(
      FROM_HERE, mojo::SimpleWatcher::ArmingPolicy::MANUAL);
  upload_pipe_watcher_->Watch(
      upload_pipe_.get(), MOJO_HANDLE_SIGNAL_WRITABLE,
      base::BindRepeating(&ChromiumHttpConnection::OnUploadPipeWriteable,
                          base::Unretained(this)));

  // Will attempt to start sending the request body, if any data is available.
  SendData();
}

void ChromiumHttpConnection::OnDataReceived(base::StringPiece string_piece,
                                            base::OnceClosure resume) {
  DCHECK(handle_partial_response_);
  delegate_->OnPartialResponse(string_piece.as_string());
  std::move(resume).Run();
}

void ChromiumHttpConnection::OnComplete(bool success) {
  DCHECK(handle_partial_response_);

  if (state_ != State::STARTED)
    return;

  state_ = State::COMPLETED;

  int response_code = kResponseCodeInvalid;
  std::string raw_headers;
  if (url_loader_->ResponseInfo() && url_loader_->ResponseInfo()->headers) {
    raw_headers = url_loader_->ResponseInfo()->headers->raw_headers();
    response_code = url_loader_->ResponseInfo()->headers->response_code();
  }

  if (success) {
    DCHECK_NE(response_code, kResponseCodeInvalid);
    delegate_->OnCompleteResponse(response_code, raw_headers, "");
    return;
  }

  if (url_loader_->NetError() != net::OK) {
    delegate_->OnNetworkError(kResponseCodeInvalid,
                              net::ErrorToString(url_loader_->NetError()));
    return;
  }

  const std::string message = net::ErrorToString(url_loader_->NetError());
  VLOG(2) << "ChromiumHttpConnection completed with network error="
          << response_code << ": " << message;
  delegate_->OnNetworkError(response_code, message);
}

void ChromiumHttpConnection::OnRetry(base::OnceClosure start_retry) {
  DCHECK(handle_partial_response_);
  // Retries are not enabled for these requests.
  NOTREACHED();
}

// Attempts to send more of the upload body, if more data is available, and
// |upload_pipe_| is valid.
void ChromiumHttpConnection::SendData() {
  if (!upload_pipe_.is_valid() || upload_body_.empty())
    return;

  uint32_t write_bytes = upload_body_.size();
  MojoResult result = upload_pipe_->WriteData(upload_body_.data(), &write_bytes,
                                              MOJO_WRITE_DATA_FLAG_NONE);

  if (result == MOJO_RESULT_SHOULD_WAIT) {
    // Wait for the pipe to have more capacity available.
    upload_pipe_watcher_->ArmOrNotify();
    return;
  }

  // Depend on |url_loader_| to notice the other pipes being closed on error.
  if (result != MOJO_RESULT_OK)
    return;

  upload_body_.erase(0, write_bytes);

  // If more data is available, arm the watcher again. Don't write again in a
  // loop, even if WriteData would allow it, to avoid blocking the current
  // thread.
  if (!upload_body_.empty())
    upload_pipe_watcher_->ArmOrNotify();
}

void ChromiumHttpConnection::OnUploadPipeWriteable(MojoResult unused) {
  SendData();
}

void ChromiumHttpConnection::UploadData(const std::string& data,
                                        bool is_last_chunk) {
  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&ChromiumHttpConnection::UploadDataOnTaskRunner,
                                this, data, is_last_chunk));
}

void ChromiumHttpConnection::UploadDataOnTaskRunner(const std::string& data,
                                                    bool is_last_chunk) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  if (state_ != State::STARTED)
    return;

  upload_body_ += data;

  upload_body_size_ += data.size();
  if (is_last_chunk) {
    // Send size before the rest of the body. While it doesn't matter much, if
    // the other side receives the size before the last chunk, which Mojo does
    // not guarantee, some protocols can merge the data and the last chunk
    // itself into a single frame.
    has_last_chunk_ = is_last_chunk;
    if (get_size_callback_)
      std::move(get_size_callback_).Run(net::OK, upload_body_size_);
  }

  SendData();
}

void ChromiumHttpConnection::OnURLLoadComplete(
    std::unique_ptr<std::string> response_body) {
  DCHECK(!handle_partial_response_);

  if (state_ != State::STARTED)
    return;

  state_ = State::COMPLETED;

  if (url_loader_->NetError() != net::OK) {
    delegate_->OnNetworkError(kResponseCodeInvalid,
                              net::ErrorToString(url_loader_->NetError()));
    return;
  }

  int response_code = kResponseCodeInvalid;
  std::string raw_headers;
  if (url_loader_->ResponseInfo() && url_loader_->ResponseInfo()->headers) {
    raw_headers = url_loader_->ResponseInfo()->headers->raw_headers();
    response_code = url_loader_->ResponseInfo()->headers->response_code();
  }

  if (response_code == kResponseCodeInvalid) {
    std::string message = net::ErrorToString(url_loader_->NetError());

    VLOG(2) << "ChromiumHttpConnection completed with network error="
            << response_code << ": " << message;
    delegate_->OnNetworkError(response_code, message);
    return;
  }

  VLOG(2) << "ChromiumHttpConnection completed with response_code="
          << response_code;

  delegate_->OnCompleteResponse(response_code, raw_headers, *response_body);
}

}  // namespace assistant
}  // namespace chromeos
