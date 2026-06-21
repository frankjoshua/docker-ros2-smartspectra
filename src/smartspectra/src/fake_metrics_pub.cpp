// fake_metrics_pub.cpp
// Simulated SmartSpectra metrics for offline development (no SDK, key, keyring,
// or camera needed). Publishes smartspectra_msgs/Metrics on smartspectra/metrics
// and the matching SoA JSON on smartspectra/metrics_json. Which metric groups it
// emits is controlled at runtime via smartspectra/set_metrics (a string[] of
// "breathing"/"cardio"/"eda"/"face"), mirroring the real node. Drop-in for the
// real node: same topics, same formats. Lets you plot vitals (rqt_plot) and
// exercise downstream nodes while out of SDK credits.
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "smartspectra_msgs/msg/metrics.hpp"
#include "smartspectra_msgs/msg/set_metrics.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRateHz = 30.0;          // publish + simulation rate
constexpr double kHrBaseline = 70.0;      // bpm
constexpr double kHrSwing = 8.0;          // bpm, slow-drift amplitude
constexpr double kHrDriftPeriod = 20.0;   // s, drift period
constexpr double kBrBaseline = 15.0;      // breaths/min
constexpr double kBrSwing = 3.0;          // breaths/min
constexpr double kBrDriftPeriod = 30.0;   // s
}  // namespace

// Publishes a fabricated smartspectra_msgs/Metrics stream plus its SoA JSON.
class FakeMetricsPublisher : public rclcpp::Node {
public:
        FakeMetricsPublisher() : Node("fake_metrics_publisher"), rng_(std::random_device{}()) {
                pub_ = create_publisher<smartspectra_msgs::msg::Metrics>("smartspectra/metrics", 10);
                json_pub_ = create_publisher<std_msgs::msg::String>("smartspectra/metrics_json", 10);
                set_metrics_sub_ = create_subscription<smartspectra_msgs::msg::SetMetrics>(
                        "smartspectra/set_metrics", 10,
                        [this](const smartspectra_msgs::msg::SetMetrics::ConstSharedPtr msg) {
                                OnSetMetrics(*msg);
                        });
                timer_ = create_wall_timer(
                        std::chrono::duration<double>(1.0 / kRateHz), [this]() { Tick(); });
                RCLCPP_INFO(get_logger(),
                        "Simulating metrics on smartspectra/metrics (+ JSON) at %.0f Hz; groups "
                        "breathing,cardio (set via smartspectra/set_metrics). No SDK required.",
                        kRateHz);
        }

private:
        void OnSetMetrics(const smartspectra_msgs::msg::SetMetrics & msg) {
                breathing_ = cardio_ = eda_ = face_ = false;
                for (const auto & g : msg.metrics) {
                        if (g == "breathing")  breathing_ = true;
                        else if (g == "cardio") cardio_ = true;
                        else if (g == "eda")    eda_ = true;
                        else if (g == "face")   face_ = true;
                        else RCLCPP_WARN(get_logger(), "set_metrics: unknown group '%s' (ignored)", g.c_str());
                }
                RCLCPP_INFO(get_logger(), "Simulating groups:%s%s%s%s",
                        breathing_ ? " breathing" : "", cardio_ ? " cardio" : "",
                        eda_ ? " eda" : "", face_ ? " face" : "");
        }

        void Tick() {
                const double dt = 1.0 / kRateHz;
                t_ += dt;

                // Slowly-varying rates with a little noise so the line looks alive.
                const double hr = kHrBaseline + kHrSwing * std::sin(2.0 * kPi * t_ / kHrDriftPeriod)
                        + Noise(0.4);
                const double br = kBrBaseline + kBrSwing * std::sin(2.0 * kPi * t_ / kBrDriftPeriod)
                        + Noise(0.2);

                // Integrate phase so each waveform's frequency tracks its (varying) rate.
                pulse_phase_ += 2.0 * kPi * (hr / 60.0) * dt;
                breath_phase_ += 2.0 * kPi * (br / 60.0) * dt;
                const double pulse_wave = std::sin(pulse_phase_);
                const double breath_wave = std::sin(breath_phase_);

                const int64_t ts = NowUs();

                smartspectra_msgs::msg::Metrics m;
                std::string js = "{\"series\":{";
                bool first = true;
                auto series = [&](const char * name, double value) {
                        char buf[160];
                        std::snprintf(buf, sizeof(buf),
                                "%s\"%s\":{\"timestamps\":[%" PRId64 "],\"values\":[%.4f]}",
                                first ? "" : ",", name, ts, value);
                        js += buf;
                        first = false;
                };

                if (breathing_) {
                        m.breathing.rate.push_back(Mwc(static_cast<float>(br), ts, 97.0f));
                        m.breathing.upper_trace.push_back(Meas(static_cast<float>(breath_wave), ts));
                        series("breathing.rate", br);
                        series("breathing.upper_trace", breath_wave);
                }
                if (cardio_) {
                        m.cardio.pulse_rate.push_back(Mwc(static_cast<float>(hr), ts, 98.0f));
                        m.cardio.arterial_pressure_trace.push_back(Mwc(static_cast<float>(pulse_wave), ts, 98.0f));
                        series("cardio.pulse_rate", hr);
                        series("cardio.arterial_pressure_trace", pulse_wave);
                }
                if (eda_) {
                        const double eda = 5.0 + 2.0 * std::sin(2.0 * kPi * t_ / 40.0);  // slow tonic level
                        m.eda.trace.push_back(Meas(static_cast<float>(eda), ts));
                        series("eda.trace", eda);
                }
                if (face_) {
                        const bool blink = std::fmod(t_, 4.0) < 0.15;  // blink ~every 4 s
                        smartspectra_msgs::msg::DetectionStatus d;
                        d.detected = blink; d.stable = true; d.timestamp = ts;
                        m.face.blinking.push_back(d);
                        series("face.blinking", blink ? 1.0 : 0.0);
                }
                js += "}}";

                pub_->publish(m);
                std_msgs::msg::String s;
                s.data = js;
                json_pub_->publish(s);
        }

        static smartspectra_msgs::msg::MeasurementWithConfidence Mwc(float value, int64_t ts, float conf) {
                smartspectra_msgs::msg::MeasurementWithConfidence r;
                r.value = value;
                r.stable = true;
                r.confidence = conf;
                r.timestamp = ts;
                return r;
        }
        static smartspectra_msgs::msg::Measurement Meas(float value, int64_t ts) {
                smartspectra_msgs::msg::Measurement r;
                r.value = value;
                r.stable = true;
                r.timestamp = ts;
                return r;
        }
        double Noise(double stddev) {
                return std::normal_distribution<double>(0.0, stddev)(rng_);
        }
        static int64_t NowUs() {
                return std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        rclcpp::Publisher<smartspectra_msgs::msg::Metrics>::SharedPtr pub_;
        rclcpp::Publisher<std_msgs::msg::String>::SharedPtr json_pub_;
        rclcpp::Subscription<smartspectra_msgs::msg::SetMetrics>::SharedPtr set_metrics_sub_;
        rclcpp::TimerBase::SharedPtr timer_;
        std::mt19937 rng_;
        double t_ = 0.0;
        double pulse_phase_ = 0.0;
        double breath_phase_ = 0.0;
        // Which groups to simulate (default: breathing + cardio, matching the real node).
        bool breathing_ = true;
        bool cardio_ = true;
        bool eda_ = false;
        bool face_ = false;
};

int main(int argc, char ** argv) {
        rclcpp::init(argc, argv);
        rclcpp::spin(std::make_shared<FakeMetricsPublisher>());
        rclcpp::shutdown();
        return 0;
}
