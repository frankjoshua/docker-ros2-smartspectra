#include "smartspectra_publisher.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <smartspectra/messages/metrics.h>  // MetricsToJsonSoA

namespace {
// Map a ROS sensor_msgs/Image encoding string to a SmartSpectra PixelFormat.
// Returns nullopt for encodings the SDK can't consume (e.g. mono8, bayer).
std::optional<presage::smartspectra::PixelFormat> ToPixelFormat(const std::string & encoding) {
        using PF = presage::smartspectra::PixelFormat;
        if (encoding == "rgb8")  return PF::kRGB;
        if (encoding == "bgr8")  return PF::kBGR;
        if (encoding == "rgba8") return PF::kRGBA;
        if (encoding == "bgra8") return PF::kBGRA;
        if (encoding == "nv12")  return PF::kNV12;
        if (encoding == "nv21")  return PF::kNV21;
        if (encoding == "yuv422_yuy2" || encoding == "yuyv") return PF::kYUYV;
        return std::nullopt;
}

// Map a protobuf repeated field to a std::vector via a per-element converter.
// `const auto&` in the converter means we never have to spell the proto types.
template <typename ProtoRepeated, typename Fn>
auto MapRepeated(const ProtoRepeated & src, Fn fn) {
        std::vector<std::decay_t<decltype(fn(*src.begin()))>> out;
        out.reserve(static_cast<size_t>(src.size()));
        for (const auto & x : src) {
                out.push_back(fn(x));
        }
        return out;
}

// Convert the SDK's protobuf Metrics into the typed smartspectra_msgs/Metrics.
// Absent SDK sub-messages read back as empty defaults, so incremental payloads
// (fields not yet produced) map cleanly to empty arrays.
smartspectra_msgs::msg::Metrics ToRosMetrics(const presage::smartspectra::Metrics & sdk) {
        const auto mwc = [](const auto & m) {
                smartspectra_msgs::msg::MeasurementWithConfidence r;
                r.value = m.value(); r.stable = m.stable();
                r.confidence = m.confidence(); r.timestamp = m.timestamp();
                return r;
        };
        const auto meas = [](const auto & m) {
                smartspectra_msgs::msg::Measurement r;
                r.value = m.value(); r.stable = m.stable(); r.timestamp = m.timestamp();
                return r;
        };
        const auto det = [](const auto & d) {
                smartspectra_msgs::msg::DetectionStatus r;
                r.detected = d.detected(); r.stable = d.stable(); r.timestamp = d.timestamp();
                return r;
        };
        const auto hrv = [](const auto & h) {
                smartspectra_msgs::msg::Hrv r;
                r.rmssd = h.rmssd(); r.mean_nn = h.mean_nn(); r.sdnn = h.sdnn();
                r.baevsky = h.baevsky(); r.timestamp = h.timestamp(); r.confidence = h.confidence();
                return r;
        };

        smartspectra_msgs::msg::Metrics out;

        const auto & b = sdk.breathing();
        out.breathing.rate = MapRepeated(b.rate(), mwc);
        out.breathing.upper_trace = MapRepeated(b.upper_trace(), meas);
        out.breathing.lower_trace = MapRepeated(b.lower_trace(), meas);
        out.breathing.amplitude = MapRepeated(b.amplitude(), meas);
        out.breathing.apnea = MapRepeated(b.apnea(), det);
        out.breathing.respiratory_line_length = MapRepeated(b.respiratory_line_length(), meas);
        out.breathing.baseline = MapRepeated(b.baseline(), meas);
        out.breathing.inhale_exhale_ratio = MapRepeated(b.inhale_exhale_ratio(), meas);
        out.breathing.strict.value = b.strict().value();

        const auto & c = sdk.cardio();
        out.cardio.pulse_rate = MapRepeated(c.pulse_rate(), mwc);
        out.cardio.arterial_pressure_trace = MapRepeated(c.arterial_pressure_trace(), mwc);
        out.cardio.hrv = MapRepeated(c.hrv(), hrv);

        out.eda.trace = MapRepeated(sdk.eda().trace(), meas);

        const auto & f = sdk.face();
        out.face.blinking = MapRepeated(f.blinking(), det);
        out.face.talking = MapRepeated(f.talking(), det);
        out.face.landmarks = MapRepeated(f.landmarks(), [](const auto & l) {
                smartspectra_msgs::msg::Landmarks r;
                r.value = MapRepeated(l.value(), [](const auto & p) {
                        smartspectra_msgs::msg::Point2dFloat q; q.x = p.x(); q.y = p.y(); return q;
                });
                r.stable = l.stable(); r.reset = l.reset(); r.timestamp = l.timestamp();
                return r;
        });
        out.face.expression = MapRepeated(f.expression(), [](const auto & e) {
                smartspectra_msgs::msg::Expression r;
                r.stable = e.stable(); r.timestamp = e.timestamp();
                r.scores = MapRepeated(e.scores(), [](const auto & s) {
                        smartspectra_msgs::msg::ExpressionScore q;
                        q.type.value = static_cast<uint8_t>(s.type());
                        q.confidence = s.confidence();
                        return q;
                });
                return r;
        });

        return out;
}
}  // namespace

std::vector<presage::smartspectra::MetricType> SmartSpectraPublisher::DefaultRequestedMetrics() {
        using Cfg = presage::smartspectra::SmartSpectraConfig;
        auto m = Cfg::BreathingMetrics();
        const auto & c = Cfg::CardioMetrics();
        m.insert(m.end(), c.begin(), c.end());
        return m;
}

std::vector<presage::smartspectra::MetricType>
SmartSpectraPublisher::MapGroups(const std::vector<std::string> & groups) {
        using Cfg = presage::smartspectra::SmartSpectraConfig;
        std::vector<presage::smartspectra::MetricType> out;
        for (const auto & g : groups) {
                const std::vector<presage::smartspectra::MetricType> * set = nullptr;
                if (g == "breathing")  set = &Cfg::BreathingMetrics();
                else if (g == "cardio") set = &Cfg::CardioMetrics();
                else if (g == "eda")    set = &Cfg::EdaMetrics();
                else if (g == "face")   set = &Cfg::FaceMetrics();
                else {
                        RCLCPP_WARN(get_logger(), "set_metrics: unknown group '%s' (ignored)", g.c_str());
                        continue;
                }
                out.insert(out.end(), set->begin(), set->end());
        }
        return out;
}

SmartSpectraPublisher::SmartSpectraPublisher()
    : Node("smartspectra_publisher"),
      metrics_pub_(create_publisher<smartspectra_msgs::msg::Metrics>("smartspectra/metrics", 10)),
      metrics_json_pub_(create_publisher<std_msgs::msg::String>("smartspectra/metrics_json", 10)),
      client_(DefaultRequestedMetrics(),
              [this](const presage::smartspectra::Metrics & m, int64_t) { PublishMetrics(m); }) {
        // SensorDataQoS (best-effort) is compatible with both reliable and
        // best-effort image publishers, so it works with any camera driver.
        sub_ = create_subscription<sensor_msgs::msg::Image>(
                "/image_raw", rclcpp::SensorDataQoS(),
                [this](const sensor_msgs::msg::Image::ConstSharedPtr msg) { OnImage(msg); });
        set_metrics_sub_ = create_subscription<smartspectra_msgs::msg::SetMetrics>(
                "smartspectra/set_metrics", 10,
                [this](const smartspectra_msgs::msg::SetMetrics::ConstSharedPtr msg) { OnSetMetrics(*msg); });
        RCLCPP_INFO(get_logger(),
                "SmartSpectra subscribed to /image_raw; publishing typed metrics on "
                "smartspectra/metrics and JSON on smartspectra/metrics_json; "
                "set groups via smartspectra/set_metrics");
}

void SmartSpectraPublisher::OnImage(const sensor_msgs::msg::Image::ConstSharedPtr & msg) {
        const auto format = ToPixelFormat(msg->encoding);
        if (!format) {
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                        "Unsupported /image_raw encoding '%s'; dropping frame", msg->encoding.c_str());
                return;
        }

        // FrameBuffer is a non-owning view; msg stays alive for this callback,
        // and Send() consumes the bytes synchronously.
        const presage::smartspectra::FrameBuffer frame{
                msg->data.data(),
                static_cast<int>(msg->width),
                static_cast<int>(msg->height),
                static_cast<int>(msg->step),
                *format,
        };

        // SDK demands strictly-monotonic microsecond timestamps from a steady
        // clock — NOT the wall-clock header stamp, whose NTP steps would be
        // rejected as kNonMonotonicTimestamp. Callbacks are serialized by the
        // default single-threaded executor, so steady_clock::now() is monotonic.
        const int64_t ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

        try {
                client_.SendFrame(frame, ts_us);
        } catch (const std::exception & e) {
                // Log-and-continue: a transient timestamp/gap rejection shouldn't
                // tear down the node mid-stream.
                RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "SendFrame failed: %s", e.what());
        }
}

void SmartSpectraPublisher::PublishMetrics(const presage::smartspectra::Metrics & metrics) {
        // Fires on an SDK worker thread; rclcpp publish is thread-safe.
        metrics_pub_->publish(ToRosMetrics(metrics));

        // Also publish the SDK's columnar JSON as a string, for quick inspection.
        const auto json = presage::smartspectra::MetricsToJsonSoA(metrics);
        if (json.ok()) {
                std_msgs::msg::String msg;
                msg.data = *json;
                metrics_json_pub_->publish(msg);
        } else {
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                        "MetricsToJsonSoA failed: %s", std::string(json.status().message()).c_str());
        }
}

void SmartSpectraPublisher::OnSetMetrics(const smartspectra_msgs::msg::SetMetrics & msg) {
        const auto metrics = MapGroups(msg.metrics);
        // Rebuilding the client re-pairs the SDK with the new requested metrics.
        // Runs on the single-threaded executor, so it never overlaps SendFrame();
        // the old client's destruction joins its worker threads before we swap.
        try {
                client_ = SmartSpectraClient(metrics,
                        [this](const presage::smartspectra::Metrics & m, int64_t) { PublishMetrics(m); });
                RCLCPP_INFO(get_logger(), "Reconfigured SmartSpectra: %zu group(s), %zu metric(s)",
                        msg.metrics.size(), metrics.size());
        } catch (const std::exception & e) {
                RCLCPP_ERROR(get_logger(), "set_metrics reconfigure failed (kept previous): %s", e.what());
        }
}
