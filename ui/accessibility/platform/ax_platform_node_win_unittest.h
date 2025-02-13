// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_NODE_WIN_UNITTEST_H_
#define UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_NODE_WIN_UNITTEST_H_

#include "ui/accessibility/platform/ax_platform_node_unittest.h"

struct IAccessible;
struct IAccessible2;
struct IAccessible2_2;
struct IAccessibleTableCell;
struct IRawElementProviderFragment;
struct IRawElementProviderFragmentRoot;
struct IRawElementProviderSimple;
struct IUnknown;

namespace base {
namespace win {
class ScopedVariant;
}
}  // namespace base

namespace ui {

class AXFragmentRootWin;

class AXPlatformNodeWinTest : public ui::AXPlatformNodeTest {
 public:
  AXPlatformNodeWinTest();
  ~AXPlatformNodeWinTest() override;

  void SetUp() override;

  void TearDown() override;

 protected:
  template <typename T>
  Microsoft::WRL::ComPtr<T> QueryInterfaceFromNode(AXNode* node);
  Microsoft::WRL::ComPtr<IRawElementProviderSimple>
  GetRootIRawElementProviderSimple();
  Microsoft::WRL::ComPtr<IRawElementProviderFragment>
  GetRootIRawElementProviderFragment();
  Microsoft::WRL::ComPtr<IAccessible> IAccessibleFromNode(AXNode* node);
  Microsoft::WRL::ComPtr<IAccessible> GetRootIAccessible();
  Microsoft::WRL::ComPtr<IAccessible2> ToIAccessible2(
      Microsoft::WRL::ComPtr<IUnknown> unknown);
  Microsoft::WRL::ComPtr<IAccessible2> ToIAccessible2(
      Microsoft::WRL::ComPtr<IAccessible> accessible);
  Microsoft::WRL::ComPtr<IAccessible2_2> ToIAccessible2_2(
      Microsoft::WRL::ComPtr<IAccessible> accessible);
  void CheckVariantHasName(base::win::ScopedVariant& variant,
                           const wchar_t* expected_name);
  void CheckIUnknownHasName(Microsoft::WRL::ComPtr<IUnknown> unknown,
                            const wchar_t* expected_name);
  Microsoft::WRL::ComPtr<IAccessibleTableCell> GetCellInTable();

  void InitFragmentRoot();
  Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> GetFragmentRoot();

  void InitListBox(bool option_1_is_selected,
                   bool option_2_is_selected,
                   bool option_3_is_selected,
                   ax::mojom::State additional_state);

  std::unique_ptr<AXFragmentRootWin> ax_fragment_root_;
};

}  // namespace ui

#endif  // UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_NODE_WIN_UNITTEST_H_
