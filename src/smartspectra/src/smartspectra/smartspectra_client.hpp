// smartspectra_client.hpp
// Thin wrapper around the SmartSpectra SDK driven by custom frame push.
// Owns the SDK instance and a CustomInput handle; SendFrame() pushes raw
// frames in. Construction reads the API key from $SMARTSPECTRA_API_KEY,
// applies the requested metric set, optionally registers a metrics callback,
// wires the custom-input source, and starts processing.
//
// To change which metrics are computed at runtime, construct a fresh client
// with a new requested_metrics list and move-assign it (the SDK is re-paired);
// see SmartSpectraPublisher::OnSetMetrics.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <smartspectra/smartspectra.h>

class SmartSpectraClient {
public:
        // requested_metrics: the SDK metric set to compute (empty -> SDK default).
        // on_metrics (optional) fires on an SDK worker thread when vitals are
        // produced; registered before Start(), as the SDK requires.
        explicit SmartSpectraClient(
                std::vector<presage::smartspectra::MetricType> requested_metrics,
                presage::smartspectra::OnMetricsFn on_metrics = {}) {
                const char* key = std::getenv("SMARTSPECTRA_API_KEY");
                if (key == nullptr || *key == '\0') {
                        throw std::runtime_error("SmartSpectra API key missing (set SMARTSPECTRA_API_KEY)");
                }

                presage::smartspectra::SmartSpectraConfig config;
                config.api_key = key;
                config.requested_metrics = std::move(requested_metrics);  // empty -> SDK default
                spectra_ = std::make_unique<presage::smartspectra::SmartSpectra>(config);

                // Callbacks must be registered before Start().
                if (on_metrics) {
                        spectra_->SetOnMetrics(std::move(on_metrics));
                }

                // Custom input must be configured before Start().
                if (const auto err = spectra_->UseCustomInput().Build(handle_); !err.ok()) {
                        throw std::runtime_error("UseCustomInput().Build() failed: " + err.FullMessage());
                }
                if (const auto err = spectra_->Start(); !err.ok()) {
                        throw std::runtime_error("SmartSpectra::Start() failed: " + err.FullMessage());
                }
        }

        // Push one raw frame. timestamp_us must be strictly monotonically increasing
        // in microseconds — use steady_clock; a wall clock's NTP steps get rejected
        // as kNonMonotonicTimestamp, and a forward gap > 2s as kTimestampGap.
        // ponytail: throws on any Send error, killing the stream. If transient
        // per-frame errors (non-monotonic/gap) should be tolerated, return the
        // SmartSpectraError (or bool) instead and let the caller drop the frame.
        void SendFrame(const presage::smartspectra::FrameBuffer& frame, int64_t timestamp_us) {
                if (const auto err = handle_->Send(frame, timestamp_us); !err.ok()) {
                        throw std::runtime_error("CustomInput::Send() failed: " + err.FullMessage());
                }
        }

private:
        std::unique_ptr<presage::smartspectra::SmartSpectra> spectra_;
        std::shared_ptr<presage::smartspectra::CustomInput> handle_;
};
