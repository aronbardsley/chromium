// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_LOGIN_LOGIN_HANDLER_H_
#define CHROME_BROWSER_UI_LOGIN_LOGIN_HANDLER_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/synchronization/lock.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/login_delegate.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/resource_request_info.h"
#include "content/public/browser/web_contents_observer.h"

class GURL;
class LoginInterstitialDelegate;

namespace content {
class WebContents;
}  // namespace content

namespace net {
class AuthChallengeInfo;
}  // namespace net

// This is the base implementation for the OS-specific classes that prompt for
// authentication information.
class LoginHandler : public content::NotificationObserver,
                     public content::WebContentsObserver {
 public:
  // The purpose of this struct is to enforce that BuildViewImpl receives either
  // both the login model and the observed form, or none. That is a bit spoiled
  // by the fact that the model is a pointer to LoginModel, as opposed to a
  // reference. Having it as a reference would go against the style guide, which
  // forbids non-const references in arguments, presumably also inside passed
  // structs, because the guide's rationale still applies. Therefore at least
  // the constructor DCHECKs that |login_model| is not null.
  struct LoginModelData {
    LoginModelData(password_manager::LoginModel* login_model,
                   const autofill::PasswordForm& observed_form);

    password_manager::LoginModel* const model;
    const autofill::PasswordForm& form;
  };

  LoginHandler(net::AuthChallengeInfo* auth_info,
               content::WebContents* web_contents,
               LoginAuthRequiredCallback auth_required_callback);
  ~LoginHandler() override;

  // Builds the platform specific LoginHandler. Used from within
  // CreateLoginPrompt() which creates tasks. The resulting handler calls
  // auth_required_callback when credentials are available. If destroyed before
  // them, the login request is aborted and the callback will not be called. The
  // callback must remain valid until one of those two events occurs.
  static std::unique_ptr<LoginHandler> Create(
      net::AuthChallengeInfo* auth_info,
      content::WebContents* web_contents,
      LoginAuthRequiredCallback auth_required_callback);

  void Start(const content::GlobalRequestID& request_id,
             bool is_main_frame,
             const GURL& request_url,
             scoped_refptr<net::HttpResponseHeaders> response_headers);

  // Resend the request with authentication credentials.
  // This function can be called from either thread.
  void SetAuth(const base::string16& username, const base::string16& password);

  // Display the error page without asking for credentials again.
  // This function can be called from either thread.
  void CancelAuth();

  // Implements the content::NotificationObserver interface.
  // Listens for AUTH_SUPPLIED and AUTH_CANCELLED notifications from other
  // LoginHandlers so that this LoginHandler has the chance to dismiss itself
  // if it was waiting for the same authentication.
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // Who/where/what asked for the authentication.
  const net::AuthChallengeInfo* auth_info() const { return auth_info_.get(); }

 protected:
  // Implement this to initialize the underlying platform specific view. If
  // |login_model_data| is not null, the contained LoginModel and PasswordForm
  // should be used to register the view with the password manager.
  virtual void BuildViewImpl(const base::string16& authority,
                             const base::string16& explanation,
                             LoginModelData* login_model_data) = 0;

  // Closes the native dialog.
  virtual void CloseDialog() = 0;

 private:
  FRIEND_TEST_ALL_PREFIXES(LoginHandlerTest, DialogStringsAndRealm);

  // Notify observers that authentication is needed.
  void NotifyAuthNeeded();

  // Notify observers that authentication is supplied.
  void NotifyAuthSupplied(const base::string16& username,
                          const base::string16& password);

  // Notify observers that authentication is cancelled.
  void NotifyAuthCancelled();

  // Returns the PasswordManager for the web contents that needs login.
  password_manager::PasswordManager* GetPasswordManagerForLogin();

  // Returns whether authentication had been handled (SetAuth or CancelAuth).
  bool WasAuthHandled() const;

  // Closes the view_contents from the UI loop.
  void CloseContents();

  // Get the signon_realm under which this auth info should be stored.
  //
  // The format of the signon_realm for proxy auth is:
  //     proxy-host:proxy-port/auth-realm
  // The format of the signon_realm for server auth is:
  //     url-scheme://url-host[:url-port]/auth-realm
  //
  // Be careful when changing this function, since you could make existing
  // saved logins un-retrievable.
  static std::string GetSignonRealm(const GURL& url,
                                    const net::AuthChallengeInfo& auth_info);

  // Helper to create a PasswordForm for PasswordManager to start looking for
  // saved credentials.
  static autofill::PasswordForm MakeInputForPasswordManager(
      const GURL& url,
      const net::AuthChallengeInfo& auth_info);

  static void GetDialogStrings(const GURL& request_url,
                               const net::AuthChallengeInfo& auth_info,
                               base::string16* authority,
                               base::string16* explanation);

  // Continuation from |Start| after any potential interception from the
  // extensions WebRequest API. If |should_cancel| is |true| the request is
  // cancelled. Otherwise |credentials| are used if supplied. Finally if the
  // request is NOT cancelled AND |credentials| is empty, then we'll actually
  // show a login prompt.
  void MaybeSetUpLoginPrompt(
      const GURL& request_url,
      bool is_main_frame,
      const base::Optional<net::AuthCredentials>& credentials,
      bool should_cancel);

  void ShowLoginPrompt(const GURL& request_url);

  void BuildViewAndNotify(const base::string16& authority,
                          const base::string16& explanation,
                          LoginModelData* login_model_data);

  // Who/where/what asked for the authentication.
  scoped_refptr<net::AuthChallengeInfo> auth_info_;

  // The PasswordForm sent to the PasswordManager. This is so we can refer to it
  // when later notifying the password manager if the credentials were accepted
  // or rejected.  This should only be accessed on the UI loop.
  autofill::PasswordForm password_form_;

  // Observes other login handlers so this login handler can respond.
  content::NotificationRegistrar registrar_;

  LoginAuthRequiredCallback auth_required_callback_;

  base::WeakPtr<LoginInterstitialDelegate> interstitial_delegate_;

  // True if the extensions logic has run and the prompt logic has started.
  bool prompt_started_;
  base::WeakPtrFactory<LoginHandler> weak_factory_;
};

// Details to provide the content::NotificationObserver.  Used by the automation
// proxy for testing.
class LoginNotificationDetails {
 public:
  explicit LoginNotificationDetails(LoginHandler* handler)
      : handler_(handler) {}
  LoginHandler* handler() const { return handler_; }

 private:
  LoginNotificationDetails() {}

  LoginHandler* handler_;  // Where to send the response.

  DISALLOW_COPY_AND_ASSIGN(LoginNotificationDetails);
};

// Details to provide the NotificationObserver.  Used by the automation proxy
// for testing and by other LoginHandlers to dismiss themselves when an
// identical auth is supplied.
class AuthSuppliedLoginNotificationDetails : public LoginNotificationDetails {
 public:
  AuthSuppliedLoginNotificationDetails(LoginHandler* handler,
                                       const base::string16& username,
                                       const base::string16& password)
      : LoginNotificationDetails(handler),
        username_(username),
        password_(password) {}
  const base::string16& username() const { return username_; }
  const base::string16& password() const { return password_; }

 private:
  // The username that was used for the authentication.
  const base::string16 username_;

  // The password that was used for the authentication.
  const base::string16 password_;

  DISALLOW_COPY_AND_ASSIGN(AuthSuppliedLoginNotificationDetails);
};

// Prompts the user for their username and password.  This is designed to
// be called on the background (I/O) thread, in response to
// net::URLRequest::Delegate::OnAuthRequired.  The prompt will be created
// on the main UI thread via a call to UI loop's InvokeLater, and will send the
// credentials back to the net::URLRequest on the calling thread.
// A LoginDelegate object (which lives on the calling thread) is returned,
// which can be used to set or cancel authentication programmatically.  The
// caller must invoke OnRequestCancelled() on this LoginDelegate before
// destroying the net::URLRequest.
scoped_refptr<content::LoginDelegate> CreateLoginPrompt(
    net::AuthChallengeInfo* auth_info,
    content::ResourceRequestInfo::WebContentsGetter web_contents_getter,
    const content::GlobalRequestID& request_id,
    bool is_main_frame,
    const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    LoginAuthRequiredCallback auth_required_callback);

#endif  // CHROME_BROWSER_UI_LOGIN_LOGIN_HANDLER_H_
