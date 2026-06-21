#pragma once

#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/string.hpp"
#include "smartspectra_msgs/msg/metrics.hpp"
#include "smartspectra_msgs/msg/set_metrics.hpp"

#include "smartspectra_client.hpp"

class SmartSpectraPublisher : public rclcpp::Node {
public:
        SmartSpectraPublisher();

private:
        void OnImage(const sensor_msgs::msg::Image::ConstSharedPtr & msg);
        void PublishMetrics(const presage::smartspectra::Metrics & metrics);
        // Rebuild the SDK client to compute a new metric set (re-pairs).
        void OnSetMetrics(const smartspectra_msgs::msg::SetMetrics & msg);
        // Map metric-group names ("breathing"/"cardio"/"eda"/"face") to SDK metrics.
        std::vector<presage::smartspectra::MetricType> MapGroups(const std::vector<std::string> & groups);
        static std::vector<presage::smartspectra::MetricType> DefaultRequestedMetrics();

        // Declared before client_ on purpose: the publishers must be constructed
        // before the client registers the metrics callback that captures them, and
        // destroyed *after* the client tears down its SDK worker threads — so a
        // callback in flight can never publish through a dead publisher.
        rclcpp::Publisher<smartspectra_msgs::msg::Metrics>::SharedPtr metrics_pub_;
        rclcpp::Publisher<std_msgs::msg::String>::SharedPtr metrics_json_pub_;
        SmartSpectraClient client_;
        rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
        rclcpp::Subscription<smartspectra_msgs::msg::SetMetrics>::SharedPtr set_metrics_sub_;
};
