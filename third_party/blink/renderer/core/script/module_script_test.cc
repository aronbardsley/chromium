// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/script/module_script.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/bindings/core/v8/script_controller.h"
#include "third_party/blink/renderer/bindings/core/v8/script_source_code.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_testing.h"
#include "third_party/blink/renderer/core/testing/dummy_modulator.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/platform/loader/fetch/cached_metadata.h"
#include "third_party/blink/renderer/platform/loader/fetch/cached_metadata_handler.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource.h"
#include "third_party/blink/renderer/platform/loader/fetch/script_cached_metadata_handler.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

using ::testing::_;

namespace blink {

namespace {

class ModuleScriptTestModulator final : public DummyModulator {
 public:
  ModuleScriptTestModulator(ScriptState* script_state)
      : script_state_(script_state) {}
  ~ModuleScriptTestModulator() override = default;

  Vector<ModuleRequest> ModuleRequestsFromScriptModule(ScriptModule) override {
    return Vector<ModuleRequest>();
  }

  void Trace(blink::Visitor* visitor) override {
    visitor->Trace(script_state_);
    DummyModulator::Trace(visitor);
  }

 private:
  ScriptState* GetScriptState() override { return script_state_; }

  Member<ScriptState> script_state_;
};

class MockCachedMetadataSender : public CachedMetadataSender {
 public:
  static std::unique_ptr<MockCachedMetadataSender> Create() {
    return base::WrapUnique(
        new ::testing::StrictMock<MockCachedMetadataSender>);
  }

  MockCachedMetadataSender() = default;

  MOCK_METHOD2(Send, void(const uint8_t*, size_t));
  bool IsServedFromCacheStorage() override { return false; }
};

static const int kScriptRepeatLength = 500;

}  // namespace

class ModuleScriptTest : public ::testing::Test {
 protected:
  static String LargeSourceText() {
    StringBuilder builder;
    // Returns a sufficiently long script that is eligible for V8 code cache.
    builder.Append(String("window.foo = "));
    for (int i = 0; i < kScriptRepeatLength; ++i) {
      builder.Append(String("1 + "));
    }
    builder.Append(String("0;"));
    return builder.ToString();
  }

  static ModuleScript* CreateModuleScript(
      Modulator* modulator,
      const String& source_text,
      SingleCachedMetadataHandler* cache_handler) {
    return ModuleScript::Create(
        ParkableString(source_text.IsolatedCopy().ReleaseImpl()), cache_handler,
        ScriptSourceLocationType::kExternalFile, modulator,
        KURL("https://fox.url/script.js"), KURL("https://fox.url/"),
        ScriptFetchOptions());
  }

  // Tests |window.foo| is set correctly, and reset |window.foo| for the next
  // test.
  static void TestFoo(V8TestingScope& scope) {
    v8::Local<v8::Value> value = scope.GetFrame()
                                     .GetScriptController()
                                     .ExecuteScriptInMainWorldAndReturnValue(
                                         ScriptSourceCode("window.foo"), KURL(),
                                         SanitizeScriptErrors::kSanitize);
    EXPECT_TRUE(value->IsNumber());
    EXPECT_EQ(kScriptRepeatLength,
              value->NumberValue(scope.GetContext()).ToChecked());

    scope.GetFrame()
        .GetScriptController()
        .ExecuteScriptInMainWorldAndReturnValue(
            ScriptSourceCode("window.foo = undefined;"), KURL(),
            SanitizeScriptErrors::kSanitize);
  }

  // Accessors for ModuleScript private members.
  static v8::ScriptCompiler::CompileOptions GetCompileOptions(
      const ModuleScript* module_script) {
    return module_script->produce_cache_data_->GetCompileOptions();
  }
  static V8CodeCache::ProduceCacheOptions GetProduceCacheOptions(
      const ModuleScript* module_script) {
    return module_script->produce_cache_data_->GetProduceCacheOptions();
  }
};

// Test expectations depends on heuristics in V8CodeCache and therefore these
// tests should be updated if necessary when V8CodeCache is modified.
TEST_F(ModuleScriptTest, V8CodeCache) {
  using Checkpoint = testing::StrictMock<testing::MockFunction<void(int)>>;

  V8TestingScope scope;
  Modulator* modulator =
      MakeGarbageCollected<ModuleScriptTestModulator>(scope.GetScriptState());
  Modulator::SetModulator(scope.GetScriptState(), modulator);

  std::unique_ptr<MockCachedMetadataSender> sender =
      MockCachedMetadataSender::Create();
  MockCachedMetadataSender* sender_ptr = sender.get();
  SingleCachedMetadataHandler* cache_handler =
      MakeGarbageCollected<ScriptCachedMetadataHandler>(UTF8Encoding(),
                                                        std::move(sender));

  // Tests the main code path: simply produce and consume code cache.
  for (int nth_load = 0; nth_load < 3; ++nth_load) {
    // Compile a module script.
    ModuleScript* module_script =
        CreateModuleScript(modulator, LargeSourceText(), cache_handler);
    ASSERT_TRUE(module_script);

    // Check that the module script is instantiated/evaluated correctly.
    ASSERT_TRUE(
        module_script->Record().Instantiate(scope.GetScriptState()).IsEmpty());
    ASSERT_TRUE(
        module_script->Record().Evaluate(scope.GetScriptState()).IsEmpty());
    TestFoo(scope);

    Checkpoint checkpoint;
    ::testing::InSequence s;

    switch (nth_load) {
      case 0:
        // For the first time, the cache handler doesn't contain any data, and
        // we'll set timestamp in ProduceCache() below.
        EXPECT_FALSE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForTimeStamp(cache_handler)));
        EXPECT_FALSE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForCodeCache(cache_handler)));
        EXPECT_EQ(V8CodeCache::ProduceCacheOptions::kSetTimeStamp,
                  GetProduceCacheOptions(module_script));
        EXPECT_EQ(v8::ScriptCompiler::kNoCompileOptions,
                  GetCompileOptions(module_script));
        EXPECT_CALL(*sender_ptr, Send(_, _));
        break;

      case 1:
        // For the second time, as timestamp is already set, we'll produce code
        // cache in ProduceCache() below.
        EXPECT_TRUE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForTimeStamp(cache_handler)));
        EXPECT_FALSE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForCodeCache(cache_handler)));
        EXPECT_EQ(V8CodeCache::ProduceCacheOptions::kProduceCodeCache,
                  GetProduceCacheOptions(module_script));
        EXPECT_EQ(v8::ScriptCompiler::kNoCompileOptions,
                  GetCompileOptions(module_script));
        EXPECT_CALL(*sender_ptr, Send(_, _));
        break;

      case 2:
        // For the third time, the code cache is already there and we've
        // consumed the code cache and won't do anything in ProduceCache().
        EXPECT_FALSE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForTimeStamp(cache_handler)));
        EXPECT_TRUE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForCodeCache(cache_handler)));
        EXPECT_EQ(v8::ScriptCompiler::kConsumeCodeCache,
                  GetCompileOptions(module_script));
        EXPECT_EQ(V8CodeCache::ProduceCacheOptions::kNoProduceCache,
                  GetProduceCacheOptions(module_script));
        break;
    }

    EXPECT_CALL(checkpoint, Call(3));

    module_script->ProduceCache();

    checkpoint.Call(3);

    switch (nth_load) {
      case 0:
        EXPECT_TRUE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForTimeStamp(cache_handler)));
        EXPECT_FALSE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForCodeCache(cache_handler)));
        break;

      case 1:
        EXPECT_FALSE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForTimeStamp(cache_handler)));
        EXPECT_TRUE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForCodeCache(cache_handler)));
        break;

      case 2:
        EXPECT_FALSE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForTimeStamp(cache_handler)));
        EXPECT_TRUE(cache_handler->GetCachedMetadata(
            V8CodeCache::TagForCodeCache(cache_handler)));
        break;
    }
  }

  // Tests anything wrong doesn't occur when module script code cache is
  // consumed by a classic script.

  Checkpoint checkpoint;
  ::testing::InSequence s;

  // As code cache is mismatched and rejected by V8, the CachedMetadata are
  // cleared and notified to Platform.
  EXPECT_CALL(*sender_ptr, Send(_, _));
  EXPECT_CALL(checkpoint, Call(4));

  // In actual cases CachedMetadataHandler and its code cache data are passed
  // via ScriptSourceCode+ScriptResource, but here they are passed via
  // ScriptSourceCode constructor for inline scripts. So far, this is sufficient
  // for unit testing.
  scope.GetFrame().GetScriptController().ExecuteScriptInMainWorldAndReturnValue(
      ScriptSourceCode(LargeSourceText(), ScriptSourceLocationType::kInternal,
                       cache_handler),
      KURL(), SanitizeScriptErrors::kSanitize);

  checkpoint.Call(4);

  TestFoo(scope);

  // The CachedMetadata are cleared.
  EXPECT_FALSE(cache_handler->GetCachedMetadata(
      V8CodeCache::TagForTimeStamp(cache_handler)));
  EXPECT_FALSE(cache_handler->GetCachedMetadata(
      V8CodeCache::TagForCodeCache(cache_handler)));
}

}  // namespace blink
