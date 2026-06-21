#pragma once

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/string.hpp"

#include "smartspectra_client.hpp"

class SmartSpectraPublisher : public rclcpp::Node {
public:
        SmartSpectraPublisher();

private:
        void OnImage(const sensor_msgs::msg::Image::ConstSharedPtr & msg);
        void PublishMetrics(const presage::smartspectra::Metrics & metrics);

        // Declared before client_ on purpose: it must be constructed before the
        // client registers the metrics callback that captures it, and destroyed
        // *after* the client tears down its SDK worker threads — so a callback in
        // flight can never publish through a dead publisher.
        rclcpp::Publisher<std_msgs::msg::String>::SharedPtr metrics_pub_;
        SmartSpectraClient client_;
        rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
};
