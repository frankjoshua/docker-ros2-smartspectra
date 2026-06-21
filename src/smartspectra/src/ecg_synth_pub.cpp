// ecg_synth_pub.cpp
// Cosmetic ECG synthesizer. The SmartSpectra SDK is camera-based (rPPG) and does
// NOT measure ECG (it reads the optical blood-volume pulse, not the electrical
// signal). This node fakes a realistic-looking ECG: it reads the measured heart
// rate from cardio.pulse_rate on /smartspectra/metrics and traces out a classic
// PQRST complex (McSharry sum-of-Gaussians) phase-locked to that rate, published
// as std_msgs/Float64 on /smartspectra/ecg so you can plot a "heartbeat" trace.
// Works with the fake or the real node (it only consumes the metrics topic).
#include <chrono>
#include <cmath>
#include <memory>
#include <random>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float64.hpp"
#include "smartspectra_msgs/msg/metrics.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRateHz = 200.0;     // ECG sample rate (smooth QRS)
constexpr double kDefaultHr = 70.0;   // until the first metrics arrive
constexpr double kDeg = kPi / 180.0;
constexpr double kBaselineWander = 0.02;  // slow baseline drift amplitude (fraction of R peak)
constexpr double kNoiseStdDev = 0.006;    // additive sensor-noise std dev

// One PQRST wave as a Gaussian on the cardiac phase circle: {angle, amplitude, width}.
// Amplitudes tuned (McSharry-style) for a clean, recognizable look; scaled so R ~ 1 mV.
struct Wave { double theta; double a; double b; };
constexpr Wave kWaves[] = {
        {-70.0 * kDeg,  2.0, 0.25},  // P
        {-15.0 * kDeg, -5.0, 0.10},  // Q
        {  0.0,        30.0, 0.10},  // R
        { 15.0 * kDeg, -7.5, 0.10},  // S
        { 90.0 * kDeg,  6.0, 0.40},  // T
};

double Wrap(double x) {
        while (x > kPi) x -= 2.0 * kPi;
        while (x < -kPi) x += 2.0 * kPi;
        return x;
}
}  // namespace

class EcgSynth : public rclcpp::Node {
public:
        EcgSynth() : Node("ecg_synth"), rng_(std::random_device{}()) {
                pub_ = create_publisher<std_msgs::msg::Float64>("smartspectra/ecg", 10);
                // hr_ is written here and read in Tick(); the default single-threaded
                // executor serializes both, so no synchronization is needed.
                sub_ = create_subscription<smartspectra_msgs::msg::Metrics>(
                        "smartspectra/metrics", 10,
                        [this](const smartspectra_msgs::msg::Metrics::ConstSharedPtr m) {
                                if (!m->cardio.pulse_rate.empty()) {
                                        hr_ = m->cardio.pulse_rate.back().value;
                                }
                        });
                timer_ = create_wall_timer(
                        std::chrono::duration<double>(1.0 / kRateHz), [this]() { Tick(); });
                RCLCPP_INFO(get_logger(),
                        "Synthesizing a cosmetic ECG on smartspectra/ecg at %.0f Hz from cardio.pulse_rate. "
                        "(The SmartSpectra SDK is camera-based and does NOT measure ECG.)", kRateHz);
        }

private:
        void Tick() {
                const double dt = 1.0 / kRateHz;
                t_ += dt;
                const double hr = (hr_ > 20.0 && hr_ < 240.0) ? hr_ : kDefaultHr;

                // Advance the cardiac phase at the current heart rate, sum the PQRST waves.
                phase_ = Wrap(phase_ + 2.0 * kPi * (hr / 60.0) * dt);
                double z = 0.0;
                for (const auto & w : kWaves) {
                        const double d = Wrap(phase_ - w.theta);
                        z += w.a * std::exp(-(d * d) / (2.0 * w.b * w.b));
                }
                z = z / 30.0;                                            // R peak -> ~1 mV
                z += kBaselineWander * std::sin(2.0 * kPi * 0.25 * t_);   // gentle baseline wander
                z += kNoiseStdDev * std::normal_distribution<double>(0.0, 1.0)(rng_);  // sensor noise

                std_msgs::msg::Float64 msg;
                msg.data = z;
                pub_->publish(msg);
        }

        rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr pub_;
        rclcpp::Subscription<smartspectra_msgs::msg::Metrics>::SharedPtr sub_;
        rclcpp::TimerBase::SharedPtr timer_;
        std::mt19937 rng_;
        double hr_ = kDefaultHr;
        double phase_ = 0.0;
        double t_ = 0.0;
};

int main(int argc, char ** argv) {
        rclcpp::init(argc, argv);
        rclcpp::spin(std::make_shared<EcgSynth>());
        rclcpp::shutdown();
        return 0;
}
