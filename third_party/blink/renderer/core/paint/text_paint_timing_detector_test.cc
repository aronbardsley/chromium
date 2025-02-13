// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/text_paint_timing_detector.h"
#include "third_party/blink/renderer/core/paint/paint_timing_detector.h"
#include "third_party/blink/renderer/core/svg/svg_text_content_element.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"

namespace blink {

class TextPaintTimingDetectorTest
    : public RenderingTest,
      private ScopedFirstContentfulPaintPlusPlusForTest {
 public:
  TextPaintTimingDetectorTest()
      : ScopedFirstContentfulPaintPlusPlusForTest(true) {}
  void SetUp() override {
    RenderingTest::SetUp();
    RenderingTest::EnableCompositing();
  }

 protected:
  LocalFrameView& GetFrameView() { return *GetFrame().View(); }
  PaintTimingDetector& GetPaintTimingDetector() {
    return GetFrameView().GetPaintTimingDetector();
  }

  unsigned CountVisibleTexts() {
    return GetPaintTimingDetector()
               .GetTextPaintTimingDetector()
               .id_record_map_.size() -
           GetPaintTimingDetector()
               .GetTextPaintTimingDetector()
               .detached_ids_.size();
    ;
  }

  unsigned CountDetachedTexts() {
    return GetPaintTimingDetector()
        .GetTextPaintTimingDetector()
        .detached_ids_.size();
  }

  void InvokeCallback() {
    TextPaintTimingDetector& detector =
        GetPaintTimingDetector().GetTextPaintTimingDetector();
    detector.ReportSwapTime(WebLayerTreeView::SwapResult::kDidSwap,
                            CurrentTimeTicks());
  }

  TimeTicks LargestPaintStoredResult() {
    return GetPaintTimingDetector()
        .GetTextPaintTimingDetector()
        .largest_text_paint_;
  }

  TimeTicks LastPaintStoredResult() {
    return GetPaintTimingDetector()
        .GetTextPaintTimingDetector()
        .last_text_paint_;
  }

  void UpdateAllLifecyclePhasesAndSimulateSwapTime() {
    GetFrameView().UpdateAllLifecyclePhases(
        DocumentLifecycle::LifecycleUpdateReason::kTest);
    TextPaintTimingDetector& detector =
        GetPaintTimingDetector().GetTextPaintTimingDetector();
    if (detector.texts_to_record_swap_time_.size() > 0) {
      detector.ReportSwapTime(WebLayerTreeView::SwapResult::kDidSwap,
                              CurrentTimeTicks());
    }
  }

  void SimulateAnalyze() {
    GetPaintTimingDetector().GetTextPaintTimingDetector().Analyze();
  }

  Element* AppendFontElementToBody(String content) {
    Element* element = GetDocument().CreateRawElement(html_names::kFontTag);
    element->setAttribute(html_names::kSizeAttr, AtomicString("5"));
    Text* text = GetDocument().createTextNode(content);
    element->AppendChild(text);
    GetDocument().body()->AppendChild(element);
    return element;
  }

  Element* AppendDivElementToBody(String content, String style = "") {
    Element* div = GetDocument().CreateRawElement(html_names::kDivTag);
    div->setAttribute(html_names::kStyleAttr, AtomicString(style));
    Text* text = GetDocument().createTextNode(content);
    div->AppendChild(text);
    GetDocument().body()->AppendChild(div);
    return div;
  }

  DOMNodeId NodeIdOfText(Element* element) {
    DCHECK_EQ(element->CountChildren(), 1u);
    DCHECK(element->firstChild()->IsTextNode());
    DCHECK(!element->firstChild()->hasChildren());
    return DOMNodeIds::IdForNode(element->firstChild());
  }

  TextRecord* TextRecordOfLargestTextPaint() {
    return GetPaintTimingDetector()
        .GetTextPaintTimingDetector()
        .FindLargestPaintCandidate();
  }

  TextRecord* TextRecordOfLastTextPaint() {
    return GetPaintTimingDetector()
        .GetTextPaintTimingDetector()
        .FindLastPaintCandidate();
  }

  void SetFontSize(Element* font_element, uint8_t font_size) {
    DCHECK_EQ(font_element->nodeName(), "FONT");
    font_element->setAttribute(html_names::kSizeAttr,
                               AtomicString(WTF::String::Number(font_size)));
  }

  void SetElementStyle(Element* element, String style) {
    element->setAttribute(html_names::kStyleAttr, AtomicString(style));
  }

  void RemoveElement(Element* element) {
    element->GetLayoutObject()->Parent()->GetNode()->removeChild(element);
  }
};

TEST_F(TextPaintTimingDetectorTest, LargestTextPaint_NoText) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_FALSE(TextRecordOfLargestTextPaint());
}

TEST_F(TextPaintTimingDetectorTest, LargestTextPaint_OneText) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  Element* only_text = AppendDivElementToBody("The only text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(only_text));
}

TEST_F(TextPaintTimingDetectorTest, NodeRemovedBeforeAssigningSwapTime) {
  SetBodyInnerHTML(R"HTML(
    <div id="parent">
      <div id="remove">The only text</div>
    </div>
  )HTML");
  UpdateAllLifecyclePhasesForTest();
  GetDocument().getElementById("parent")->RemoveChild(
      GetDocument().getElementById("remove"));
  InvokeCallback();
  EXPECT_EQ(CountVisibleTexts(), 0u);
  EXPECT_EQ(CountDetachedTexts(), 1u);
}

TEST_F(TextPaintTimingDetectorTest, LargestTextPaint_LargestText) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  AppendDivElementToBody("medium text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  Element* large_text = AppendDivElementToBody("a long-long-long text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  AppendDivElementToBody("small");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(large_text));
}

TEST_F(TextPaintTimingDetectorTest, UpdateResultWhenCandidateChanged) {
  TimeTicks time1 = CurrentTimeTicks();
  SetBodyInnerHTML(R"HTML(
    <div>small text</div>
  )HTML");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  SimulateAnalyze();
  TimeTicks time2 = CurrentTimeTicks();
  TimeTicks first_largest = LargestPaintStoredResult();
  TimeTicks first_last = LastPaintStoredResult();
  EXPECT_GE(first_largest, time1);
  EXPECT_GE(time2, first_largest);
  EXPECT_GE(first_last, time1);
  EXPECT_GE(time2, first_last);

  Text* larger_text = GetDocument().createTextNode("a long-long-long text");
  GetDocument().body()->AppendChild(larger_text);
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  SimulateAnalyze();
  TimeTicks time3 = CurrentTimeTicks();
  TimeTicks second_largest = LargestPaintStoredResult();
  TimeTicks second_last = LastPaintStoredResult();
  EXPECT_GE(second_largest, time2);
  EXPECT_GE(time3, second_largest);
  EXPECT_GE(second_last, time2);
  EXPECT_GE(time3, second_last);
}

// There is a risk that a text that is just recorded is selected to be the
// metric candidate. The algorithm should skip the text record if its paint time
// hasn't been recorded yet.
TEST_F(TextPaintTimingDetectorTest, PendingTextIsLargest) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  AppendDivElementToBody("text");
  GetFrameView().UpdateAllLifecyclePhases(
      DocumentLifecycle::LifecycleUpdateReason::kTest);
  // We do not call swap-time callback here in order to not set the paint time.
  EXPECT_FALSE(TextRecordOfLargestTextPaint());
}

// The same node may be visited by recordText for twice before the paint time
// is set. In some previous design, this caused the node to be recorded twice.
TEST_F(TextPaintTimingDetectorTest, VisitSameNodeTwiceBeforePaintTimeIsSet) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  Element* text = AppendDivElementToBody("text");
  GetFrameView().UpdateAllLifecyclePhases(
      DocumentLifecycle::LifecycleUpdateReason::kTest);
  // Change a property of the text to trigger repaint.
  text->setAttribute(html_names::kStyleAttr, AtomicString("color:red;"));
  GetFrameView().UpdateAllLifecyclePhases(
      DocumentLifecycle::LifecycleUpdateReason::kTest);
  InvokeCallback();
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(text));
}

TEST_F(TextPaintTimingDetectorTest, LargestTextPaint_ReportFirstPaintTime) {
  TimeTicks time1 = CurrentTimeTicks();
  SetBodyInnerHTML(R"HTML(
  )HTML");
  Element* size_changing_text = AppendFontElementToBody("size-changing text");
  Element* long_text =
      AppendFontElementToBody("a long-long-long-long moving text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  TimeTicks time2 = CurrentTimeTicks();
  SetFontSize(size_changing_text, 50);
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  SetFontSize(long_text, 100);
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  TextRecord* record = GetPaintTimingDetector()
                           .GetTextPaintTimingDetector()
                           .FindLargestPaintCandidate();
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(long_text));
  TimeTicks firing_time = record->first_paint_time;
  EXPECT_GE(firing_time, time1);
  EXPECT_GE(time2, firing_time);
}

TEST_F(TextPaintTimingDetectorTest,
       LargestTextPaint_IgnoreTextOutsideViewport) {
  SetBodyInnerHTML(R"HTML(
    <style>
      div.out {
        position: fixed;
        top: -100px;
      }
    </style>
    <div class='out'>text outside of viewport</div>
  )HTML");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_FALSE(GetPaintTimingDetector()
                   .GetTextPaintTimingDetector()
                   .FindLargestPaintCandidate());
  EXPECT_FALSE(GetPaintTimingDetector()
                   .GetTextPaintTimingDetector()
                   .FindLastPaintCandidate());
}

TEST_F(TextPaintTimingDetectorTest, LargestTextPaint_IgnoreRemovedText) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  Element* large_text = AppendDivElementToBody(
      "(large text)(large text)(large text)(large text)(large text)(large "
      "text)");
  Element* small_text = AppendDivElementToBody("small text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(large_text));

  RemoveElement(large_text);
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(small_text));
}

TEST_F(TextPaintTimingDetectorTest, LargestTextPaint_ReportLastNullCandidate) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  Element* text = AppendDivElementToBody("text to remove");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  SimulateAnalyze();
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(text));
  EXPECT_NE(LargestPaintStoredResult(), base::TimeTicks());

  RemoveElement(text);
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  SimulateAnalyze();
  EXPECT_FALSE(TextRecordOfLargestTextPaint());
  EXPECT_EQ(LargestPaintStoredResult(), base::TimeTicks());
}

TEST_F(TextPaintTimingDetectorTest,
       LargestTextPaint_CompareVisualSizeNotActualSize) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  AppendDivElementToBody("a long text", "position:fixed;left:-10px");
  Element* short_text = AppendDivElementToBody("short");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(short_text));
}

// Depite that the l
TEST_F(TextPaintTimingDetectorTest, LargestTextPaint_CompareSizesAtFirstPaint) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  Element* shortening_long_text = AppendDivElementToBody("123456789");
  AppendDivElementToBody("12345678");  // 1 letter shorter than the above.
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  // The visual size becomes smaller when less portion intersecting with
  // viewport.
  SetElementStyle(shortening_long_text, "position:fixed;left:-10px");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id,
            NodeIdOfText(shortening_long_text));
}

TEST_F(TextPaintTimingDetectorTest, LastTextPaint_NoText) {
  SetBodyInnerHTML(R"HTML(
    <div></div>
  )HTML");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  TextRecord* record = GetPaintTimingDetector()
                           .GetTextPaintTimingDetector()
                           .FindLastPaintCandidate();
  EXPECT_FALSE(record);
}

TEST_F(TextPaintTimingDetectorTest, LastTextPaint_OneText) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  Element* text = AppendDivElementToBody("The only text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(text));
}

TEST_F(TextPaintTimingDetectorTest, LastTextPaint_LastText) {
  SetBodyInnerHTML(R"HTML(
    <div>1st text</div>
  )HTML");
  AppendDivElementToBody("s");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  AppendDivElementToBody("loooooooong");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  Element* third_text = AppendDivElementToBody("medium");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  EXPECT_EQ(TextRecordOfLastTextPaint()->node_id, NodeIdOfText(third_text));
}

TEST_F(TextPaintTimingDetectorTest, LastTextPaint_ReportFirstPaintTime) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  AppendDivElementToBody("a loooooooooooooooooooong text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  TimeTicks time1 = CurrentTimeTicks();
  Element* latest_text = AppendFontElementToBody("latest text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  TimeTicks time2 = CurrentTimeTicks();
  SetFontSize(latest_text, 50);
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  SetFontSize(latest_text, 100);
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  TextRecord* record = TextRecordOfLastTextPaint();
  EXPECT_EQ(record->node_id, NodeIdOfText(latest_text));
  TimeTicks firing_time = record->first_paint_time;
  EXPECT_GE(firing_time, time1);
  EXPECT_GE(time2, firing_time);
}

TEST_F(TextPaintTimingDetectorTest, TreatEllipsisAsText) {
  LoadAhem();
  SetBodyInnerHTML(R"HTML(
    <div style="font:10px Ahem;white-space:nowrap;width:50px;overflow:hidden;text-overflow:ellipsis;">
    00000000000000000000000000000000000000000000000000000000000000000000000000
    00000000000000000000000000000000000000000000000000000000000000000000000000
    </div>
  )HTML");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  // The FCP++ hook in ellipsis box painter is using the line layout item as the
  // tracking node while layout ng is using the layout text as the tracking
  // node.
  if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
    EXPECT_EQ(CountVisibleTexts(), 1u);
  } else {
    // The text and the elllipsis are recorded.
    EXPECT_EQ(CountVisibleTexts(), 2u);
  }
}

TEST_F(TextPaintTimingDetectorTest, CaptureFileUploadController) {
  SetBodyInnerHTML("<input type='file'>");
  Element* element = GetDocument().QuerySelector("input");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  EXPECT_EQ(CountVisibleTexts(), 1u);
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id,
            DOMNodeIds::IdForNode(element));
}

TEST_F(TextPaintTimingDetectorTest, NotCapturingListMarkers) {
  SetBodyInnerHTML(R"HTML(
    <ul>
      <li></li>
    </ul>
    <ol>
      <li></li>
    </ol>
  )HTML");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  EXPECT_EQ(CountVisibleTexts(), 0u);
}

TEST_F(TextPaintTimingDetectorTest, CaptureSVGText) {
  SetBodyInnerHTML(R"HTML(
    <svg height="40" width="300">
      <text x="0" y="15">A SVG text.</text>
    </svg>
  )HTML");

  SVGTextContentElement* elem =
      ToSVGTextContentElement(GetDocument().QuerySelector("text"));
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  EXPECT_EQ(CountVisibleTexts(), 1u);
  EXPECT_EQ(TextRecordOfLargestTextPaint()->node_id, NodeIdOfText(elem));
}

TEST_F(TextPaintTimingDetectorTest, LastTextPaint_IgnoreRemovedText) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  Element* first_text = AppendDivElementToBody("1st text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  Element* second_text = AppendDivElementToBody("2nd text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  RemoveElement(second_text);
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_EQ(TextRecordOfLastTextPaint()->node_id, NodeIdOfText(first_text));
}

TEST_F(TextPaintTimingDetectorTest, LastTextPaint_StopRecordingOverNodeLimit) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  for (int i = 1; i <= 4999; i++)
    AppendDivElementToBody(WTF::String::Number(i), "position:fixed;left:0px");

  UpdateAllLifecyclePhasesAndSimulateSwapTime();

  Element* text_5000 = AppendDivElementToBody(WTF::String::Number(5000));
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_EQ(TextRecordOfLastTextPaint()->node_id, NodeIdOfText(text_5000));

  AppendDivElementToBody(WTF::String::Number(5001));
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  EXPECT_EQ(TextRecordOfLastTextPaint()->node_id, NodeIdOfText(text_5000));
}

TEST_F(TextPaintTimingDetectorTest, LastTextPaint_ReportLastNullCandidate) {
  SetBodyInnerHTML(R"HTML(
  )HTML");
  Element* text = AppendDivElementToBody("text");
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  SimulateAnalyze();
  EXPECT_EQ(TextRecordOfLastTextPaint()->node_id, NodeIdOfText(text));
  EXPECT_NE(LastPaintStoredResult(), base::TimeTicks());

  RemoveElement(text);
  UpdateAllLifecyclePhasesAndSimulateSwapTime();
  SimulateAnalyze();
  EXPECT_FALSE(TextRecordOfLastTextPaint());
  EXPECT_EQ(LastPaintStoredResult(), base::TimeTicks());
}

}  // namespace blink
