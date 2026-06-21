#include "smartspectra_publisher.hpp"

#include <chrono>
#include <optional>
#include <string>

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
}  // namespace

SmartSpectraPublisher::SmartSpectraPublisher()
    : Node("smartspectra_publisher"),
      metrics_pub_(create_publisher<std_msgs::msg::String>("smartspectra/metrics", 10)),
      client_([this](const presage::smartspectra::Metrics & m, int64_t) { PublishMetrics(m); }) {
        // SensorDataQoS (best-effort) is compatible with both reliable and
        // best-effort image publishers, so it works with any camera driver.
        sub_ = create_subscription<sensor_msgs::msg::Image>(
                "/image_raw", rclcpp::SensorDataQoS(),
                [this](const sensor_msgs::msg::Image::ConstSharedPtr msg) { OnImage(msg); });
        RCLCPP_INFO(get_logger(),
                "SmartSpectra subscribed to /image_raw, publishing metrics on smartspectra/metrics");
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
        // Fires on an SDK worker thread; rclcpp publish is thread-safe. The JSON
        // carries whatever metric groups the config requested (breathing by
        // default — see SmartSpectraConfig in smartspectra_client.hpp).
        const auto json = presage::smartspectra::MetricsToJsonSoA(metrics);
        if (!json.ok()) {
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                        "MetricsToJsonSoA failed: %s", std::string(json.status().message()).c_str());
                return;
        }
        std_msgs::msg::String msg;
        msg.data = *json;
        metrics_pub_->publish(msg);
}
