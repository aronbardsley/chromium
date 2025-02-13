// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UPDATER_WIN_NET_NETWORK_H_
#define CHROME_UPDATER_WIN_NET_NETWORK_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "chrome/updater/win/net/scoped_hinternet.h"
#include "components/update_client/network.h"

namespace updater {

class NetworkFetcherWinHTTPFactory
    : public update_client::NetworkFetcherFactory {
 public:
  NetworkFetcherWinHTTPFactory();

  std::unique_ptr<update_client::NetworkFetcher> Create() const override;

 protected:
  ~NetworkFetcherWinHTTPFactory() override;

 private:
  ScopedHInternet session_handle_;

  DISALLOW_COPY_AND_ASSIGN(NetworkFetcherWinHTTPFactory);
};

}  // namespace updater

#endif  // CHROME_UPDATER_WIN_NET_NETWORK_H_
