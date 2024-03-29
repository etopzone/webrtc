/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_AUDIO_PROCESSING_AEC3_SUBTRACTOR_H_
#define MODULES_AUDIO_PROCESSING_AEC3_SUBTRACTOR_H_

#include <math.h>
#include <stddef.h>

#include <array>
#include <vector>

#include "api/array_view.h"
#include "api/audio/echo_canceller3_config.h"
#include "modules/audio_processing/aec3/adaptive_fir_filter.h"
#include "modules/audio_processing/aec3/aec3_common.h"
#include "modules/audio_processing/aec3/aec3_fft.h"
#include "modules/audio_processing/aec3/aec_state.h"
#include "modules/audio_processing/aec3/echo_path_variability.h"
#include "modules/audio_processing/aec3/main_filter_update_gain.h"
#include "modules/audio_processing/aec3/render_buffer.h"
#include "modules/audio_processing/aec3/render_signal_analyzer.h"
#include "modules/audio_processing/aec3/shadow_filter_update_gain.h"
#include "modules/audio_processing/aec3/subtractor_output.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/checks.h"

namespace webrtc {

// Proves linear echo cancellation functionality
class Subtractor {
 public:
  Subtractor(const EchoCanceller3Config& config,
             size_t num_render_channels,
             size_t num_capture_channels,
             ApmDataDumper* data_dumper,
             Aec3Optimization optimization);
  ~Subtractor();
  Subtractor(const Subtractor&) = delete;
  Subtractor& operator=(const Subtractor&) = delete;

  // Performs the echo subtraction.
  void Process(const RenderBuffer& render_buffer,
               const std::vector<std::vector<float>>& capture,
               const RenderSignalAnalyzer& render_signal_analyzer,
               const AecState& aec_state,
               rtc::ArrayView<SubtractorOutput> outputs);

  void HandleEchoPathChange(const EchoPathVariability& echo_path_variability);

  // Exits the initial state.
  void ExitInitialState();

  // Returns the block-wise frequency responses for the main adaptive filters.
  // TODO(bugs.webrtc.org/10913): Return the frequency responses for all capture
  // channels.
  const std::vector<std::array<float, kFftLengthBy2Plus1>>&
  FilterFrequencyResponse() const {
    return main_frequency_response_[0];
  }

  // Returns the estimates of the impulse responses for the main adaptive
  // filters.
  // TODO(bugs.webrtc.org/10913): Return the impulse responses for all capture
  // channels.
  const std::vector<float>& FilterImpulseResponse() const {
    return main_impulse_response_[0];
  }

  void DumpFilters() {
    size_t current_size = main_impulse_response_[0].size();
    main_impulse_response_[0].resize(main_impulse_response_[0].capacity());
    data_dumper_->DumpRaw("aec3_subtractor_h_main", main_impulse_response_[0]);
    main_impulse_response_[0].resize(current_size);

    main_filter_[0]->DumpFilter("aec3_subtractor_H_main");
    shadow_filter_[0]->DumpFilter("aec3_subtractor_H_shadow");
  }

 private:
  class FilterMisadjustmentEstimator {
   public:
    FilterMisadjustmentEstimator() = default;
    ~FilterMisadjustmentEstimator() = default;
    // Update the misadjustment estimator.
    void Update(const SubtractorOutput& output);
    // GetMisadjustment() Returns a recommended scale for the filter so the
    // prediction error energy gets closer to the energy that is seen at the
    // microphone input.
    float GetMisadjustment() const {
      RTC_DCHECK_GT(inv_misadjustment_, 0.0f);
      // It is not aiming to adjust all the estimated mismatch. Instead,
      // it adjusts half of that estimated mismatch.
      return 2.f / sqrtf(inv_misadjustment_);
    }
    // Returns true if the prediciton error energy is significantly larger
    // than the microphone signal energy and, therefore, an adjustment is
    // recommended.
    bool IsAdjustmentNeeded() const { return inv_misadjustment_ > 10.f; }
    void Reset();
    void Dump(ApmDataDumper* data_dumper) const;

   private:
    const int n_blocks_ = 4;
    int n_blocks_acum_ = 0;
    float e2_acum_ = 0.f;
    float y2_acum_ = 0.f;
    float inv_misadjustment_ = 0.f;
    int overhang_ = 0.f;
  };

  const Aec3Fft fft_;
  ApmDataDumper* data_dumper_;
  const Aec3Optimization optimization_;
  const EchoCanceller3Config config_;
  const size_t num_capture_channels_;

  std::vector<std::unique_ptr<AdaptiveFirFilter>> main_filter_;
  std::vector<std::unique_ptr<AdaptiveFirFilter>> shadow_filter_;
  std::vector<std::unique_ptr<MainFilterUpdateGain>> G_main_;
  std::vector<std::unique_ptr<ShadowFilterUpdateGain>> G_shadow_;
  std::vector<FilterMisadjustmentEstimator> filter_misadjustment_estimator_;
  std::vector<size_t> poor_shadow_filter_counter_;
  std::vector<std::vector<std::array<float, kFftLengthBy2Plus1>>>
      main_frequency_response_;
  std::vector<std::vector<float>> main_impulse_response_;
};

}  // namespace webrtc

#endif  // MODULES_AUDIO_PROCESSING_AEC3_SUBTRACTOR_H_
