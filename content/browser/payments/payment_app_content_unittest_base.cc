// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/payments/payment_app_content_unittest_base.h"

#include <stdint.h>

#include <set>
#include <utility>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "content/browser/service_worker/embedded_worker_test_helper.h"
#include "content/browser/service_worker/fake_embedded_worker_instance_client.h"
#include "content/browser/service_worker/fake_service_worker.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "mojo/public/cpp/bindings/associated_interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "third_party/blink/public/common/service_worker/service_worker_status_code.h"
#include "third_party/blink/public/mojom/service_worker/service_worker.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_registration.mojom.h"

namespace content {

namespace {

void RegisterServiceWorkerCallback(bool* called,
                                   int64_t* out_registration_id,
                                   blink::ServiceWorkerStatusCode status,
                                   const std::string& status_message,
                                   int64_t registration_id) {
  EXPECT_EQ(blink::ServiceWorkerStatusCode::kOk, status)
      << blink::ServiceWorkerStatusToString(status);
  *called = true;
  *out_registration_id = registration_id;
}

void UnregisterServiceWorkerCallback(bool* called,
                                     blink::ServiceWorkerStatusCode status) {
  EXPECT_EQ(blink::ServiceWorkerStatusCode::kOk, status)
      << blink::ServiceWorkerStatusToString(status);
  *called = true;
}

void StopWorkerCallback(bool* called) {
  *called = true;
}

}  // namespace

class PaymentAppContentUnitTestBase::PaymentAppForWorkerTestHelper
    : public EmbeddedWorkerTestHelper {
 public:
  PaymentAppForWorkerTestHelper()
      : EmbeddedWorkerTestHelper(base::FilePath()),
        last_sw_registration_id_(
            blink::mojom::kInvalidServiceWorkerRegistrationId) {}
  ~PaymentAppForWorkerTestHelper() override {}

  class EmbeddedWorkerInstanceClient : public FakeEmbeddedWorkerInstanceClient {
   public:
    explicit EmbeddedWorkerInstanceClient(
        PaymentAppForWorkerTestHelper* worker_helper)
        : FakeEmbeddedWorkerInstanceClient(worker_helper),
          worker_helper_(worker_helper) {}
    ~EmbeddedWorkerInstanceClient() override = default;

    void StartWorker(
        blink::mojom::EmbeddedWorkerStartParamsPtr params) override {
      ServiceWorkerVersion* version = worker_helper_->context()->GetLiveVersion(
          params->service_worker_version_id);
      worker_helper_->last_sw_registration_id_ = version->registration_id();
      worker_helper_->last_sw_scope_ = version->scope();

      FakeEmbeddedWorkerInstanceClient::StartWorker(std::move(params));
    }

   private:
    PaymentAppForWorkerTestHelper* const worker_helper_;

    DISALLOW_COPY_AND_ASSIGN(EmbeddedWorkerInstanceClient);
  };

  class ServiceWorker : public FakeServiceWorker {
   public:
    explicit ServiceWorker(PaymentAppForWorkerTestHelper* worker_helper)
        : FakeServiceWorker(worker_helper), worker_helper_(worker_helper) {}
    ~ServiceWorker() override = default;

    void DispatchPaymentRequestEvent(
        payments::mojom::PaymentRequestEventDataPtr event_data,
        payments::mojom::PaymentHandlerResponseCallbackPtr response_callback,
        DispatchPaymentRequestEventCallback callback) override {
      if (!worker_helper_)
        return;
      if (worker_helper_->respond_payment_request_immediately_) {
        FakeServiceWorker::DispatchPaymentRequestEvent(
            std::move(event_data), std::move(response_callback),
            std::move(callback));
      } else {
        worker_helper_->pending_response_callback_ =
            std::move(response_callback);
        std::move(callback).Run(
            blink::mojom::ServiceWorkerEventStatus::COMPLETED);
      }
    }

   private:
    PaymentAppForWorkerTestHelper* const worker_helper_;

    DISALLOW_COPY_AND_ASSIGN(ServiceWorker);
  };

  std::unique_ptr<FakeEmbeddedWorkerInstanceClient> CreateInstanceClient()
      override {
    return std::make_unique<EmbeddedWorkerInstanceClient>(this);
  }

  std::unique_ptr<FakeServiceWorker> CreateServiceWorker() override {
    return std::make_unique<ServiceWorker>(this);
  }

  int64_t last_sw_registration_id_;
  GURL last_sw_scope_;

  // Variables to delay payment request response.
  bool respond_payment_request_immediately_ = true;
  payments::mojom::PaymentHandlerResponseCallbackPtr pending_response_callback_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PaymentAppForWorkerTestHelper);
};

PaymentAppContentUnitTestBase::PaymentAppContentUnitTestBase()
    : thread_bundle_(
          new TestBrowserThreadBundle(TestBrowserThreadBundle::IO_MAINLOOP)),
      worker_helper_(new PaymentAppForWorkerTestHelper()) {
  worker_helper_->context_wrapper()->set_storage_partition(storage_partition());
  storage_partition()->service_worker_context_->Shutdown();
  base::RunLoop().RunUntilIdle();

  storage_partition()->service_worker_context_ =
      worker_helper_->context_wrapper();
  payment_app_context()->Init(worker_helper_->context_wrapper());
  base::RunLoop().RunUntilIdle();
}

PaymentAppContentUnitTestBase::~PaymentAppContentUnitTestBase() {}

BrowserContext* PaymentAppContentUnitTestBase::browser_context() {
  DCHECK(worker_helper_);
  return worker_helper_->browser_context();
}

PaymentManager* PaymentAppContentUnitTestBase::CreatePaymentManager(
    const GURL& scope_url,
    const GURL& sw_script_url) {
  // Register service worker for payment manager.
  bool called = false;
  int64_t registration_id;
  blink::mojom::ServiceWorkerRegistrationOptions registration_opt;
  registration_opt.scope = scope_url;
  worker_helper_->context()->RegisterServiceWorker(
      sw_script_url, registration_opt,
      base::BindOnce(&RegisterServiceWorkerCallback, &called,
                     &registration_id));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(called);

  // Ensure the worker used for installation has stopped.
  called = false;
  ServiceWorkerRegistration* registration =
      worker_helper_->context()->GetLiveRegistration(registration_id);
  EXPECT_TRUE(registration);
  EXPECT_TRUE(registration->active_version());
  EXPECT_FALSE(registration->waiting_version());
  EXPECT_FALSE(registration->installing_version());
  registration->active_version()->StopWorker(
      base::BindOnce(&StopWorkerCallback, &called));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(called);

  // This function should eventually return created payment manager
  // but there is no way to get last created payment manager from
  // payment_app_context()->payment_managers_ because its type is std::map
  // and can not ensure its order. So, just make a set of existing payment app
  // managers before creating a new manager and then check what is a new thing.
  std::set<PaymentManager*> existing_managers;
  for (const auto& existing_manager :
       payment_app_context()->payment_managers_) {
    existing_managers.insert(existing_manager.first);
  }

  // Create a new payment manager.
  payments::mojom::PaymentManagerPtr manager;
  mojo::InterfaceRequest<payments::mojom::PaymentManager> request =
      mojo::MakeRequest(&manager);
  payment_managers_.push_back(std::move(manager));
  payment_app_context()->CreatePaymentManager(std::move(request));
  base::RunLoop().RunUntilIdle();

  // Find a last registered payment manager.
  for (const auto& candidate_manager :
       payment_app_context()->payment_managers_) {
    if (!base::ContainsKey(existing_managers, candidate_manager.first)) {
      candidate_manager.first->Init(sw_script_url, scope_url.spec());
      base::RunLoop().RunUntilIdle();
      return candidate_manager.first;
    }
  }

  NOTREACHED();
  return nullptr;
}

void PaymentAppContentUnitTestBase::UnregisterServiceWorker(
    const GURL& scope_url) {
  // Unregister service worker.
  bool called = false;
  worker_helper_->context()->UnregisterServiceWorker(
      scope_url, base::BindOnce(&UnregisterServiceWorkerCallback, &called));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(called);
}

void PaymentAppContentUnitTestBase::SetNoPaymentRequestResponseImmediately() {
  worker_helper_->respond_payment_request_immediately_ = false;
}

void PaymentAppContentUnitTestBase::RespondPendingPaymentRequest() {
  std::move(worker_helper_->pending_response_callback_)
      ->OnResponseForPaymentRequest(
          payments::mojom::PaymentHandlerResponse::New());
}

int64_t PaymentAppContentUnitTestBase::last_sw_registration_id() const {
  return worker_helper_->last_sw_registration_id_;
}

const GURL& PaymentAppContentUnitTestBase::last_sw_scope_url() const {
  return worker_helper_->last_sw_scope_;
}

StoragePartitionImpl* PaymentAppContentUnitTestBase::storage_partition() {
  return static_cast<StoragePartitionImpl*>(
      BrowserContext::GetDefaultStoragePartition(browser_context()));
}

PaymentAppContextImpl* PaymentAppContentUnitTestBase::payment_app_context() {
  return storage_partition()->GetPaymentAppContext();
}

}  // namespace content
