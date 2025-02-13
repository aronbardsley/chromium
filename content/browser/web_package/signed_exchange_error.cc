// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/web_package/signed_exchange_error.h"

namespace content {

// static
base::Optional<SignedExchangeError::Field>
SignedExchangeError::GetFieldFromSignatureVerifierResult(
    SignedExchangeSignatureVerifier::Result verify_result) {
  switch (verify_result) {
    case SignedExchangeSignatureVerifier::Result::kSuccess:
      return base::nullopt;
    case SignedExchangeSignatureVerifier::Result::kErrCertificateSHA256Mismatch:
      return Field::kSignatureCertSha256;
    case SignedExchangeSignatureVerifier::Result::
        kErrSignatureVerificationFailed:
      return Field::kSignatureSig;
    case SignedExchangeSignatureVerifier::Result::kErrInvalidSignatureIntegrity:
      return Field::kSignatureIintegrity;
    case SignedExchangeSignatureVerifier::Result::kErrUnsupportedCertType:
      return Field::kSignatureCertUrl;
    case SignedExchangeSignatureVerifier::Result::kErrValidityPeriodTooLong:
    case SignedExchangeSignatureVerifier::Result::kErrFutureDate:
    case SignedExchangeSignatureVerifier::Result::kErrExpired:
      return Field::kSignatureTimestamps;

    // Deprecated error results.
    case SignedExchangeSignatureVerifier::Result::kErrNoCertificate_deprecated:
    case SignedExchangeSignatureVerifier::Result::
        kErrNoCertificateSHA256_deprecated:
    case SignedExchangeSignatureVerifier::Result::
        kErrInvalidSignatureFormat_deprecated:
    case SignedExchangeSignatureVerifier::Result::
        kErrInvalidTimestamp_deprecated:
      NOTREACHED();
  }

  NOTREACHED();
  return base::nullopt;
}

SignedExchangeError::SignedExchangeError(const std::string& message,
                                         base::Optional<FieldIndexPair> field)
    : message(message), field(field) {}

SignedExchangeError::SignedExchangeError(const SignedExchangeError& other) =
    default;
SignedExchangeError::SignedExchangeError(SignedExchangeError&& other) = default;
SignedExchangeError::~SignedExchangeError() = default;

}  // namespace content
