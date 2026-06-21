// face_overlay_pub.cpp
// Bridges face.landmarks from /smartspectra/metrics to a visualization_msgs/
// ImageMarker (POINTS) on /smartspectra/face_markers, so Foxglove's Image panel
// can overlay the landmarks on /image_raw. Landmark x,y are treated as pixel
// coordinates in the image. Works with the fake node (enable the "face" group)
// or the real node. No SDK dependency.
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "visualization_msgs/msg/image_marker.hpp"
#include "smartspectra_msgs/msg/metrics.hpp"

class FaceOverlay : public rclcpp::Node {
public:
        FaceOverlay() : Node("face_overlay") {
                pub_ = create_publisher<visualization_msgs::msg::ImageMarker>(
                        "smartspectra/face_markers", 10);
                sub_ = create_subscription<smartspectra_msgs::msg::Metrics>(
                        "smartspectra/metrics", 10,
                        [this](const smartspectra_msgs::msg::Metrics::ConstSharedPtr m) { OnMetrics(*m); });
                RCLCPP_INFO(get_logger(),
                        "Publishing face.landmarks as ImageMarker on smartspectra/face_markers. "
                        "Add it as an annotation to a Foxglove Image panel showing /image_raw.");
        }

private:
        void OnMetrics(const smartspectra_msgs::msg::Metrics & m) {
                visualization_msgs::msg::ImageMarker mk;
                mk.header.stamp = now();
                mk.ns = "face_landmarks";
                mk.id = 0;
                mk.type = visualization_msgs::msg::ImageMarker::POINTS;
                mk.action = visualization_msgs::msg::ImageMarker::ADD;
                mk.scale = 3.0f;  // point radius, pixels
                mk.outline_color.r = 0.0;
                mk.outline_color.g = 1.0;
                mk.outline_color.b = 0.2;
                mk.outline_color.a = 1.0;

                // Use the most recent landmark set; empty points clears the overlay.
                if (!m.face.landmarks.empty()) {
                        for (const auto & p : m.face.landmarks.back().value) {
                                geometry_msgs::msg::Point pt;
                                pt.x = p.x;
                                pt.y = p.y;
                                pt.z = 0.0;
                                mk.points.push_back(pt);
                        }
                }
                pub_->publish(mk);
        }

        rclcpp::Publisher<visualization_msgs::msg::ImageMarker>::SharedPtr pub_;
        rclcpp::Subscription<smartspectra_msgs::msg::Metrics>::SharedPtr sub_;
};

int main(int argc, char ** argv) {
        rclcpp::init(argc, argv);
        rclcpp::spin(std::make_shared<FaceOverlay>());
        rclcpp::shutdown();
        return 0;
}
