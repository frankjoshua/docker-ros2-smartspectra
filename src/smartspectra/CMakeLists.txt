cmake_minimum_required(VERSION 3.20)
project(smartspectra)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(smartspectra_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(SmartSpectra REQUIRED)

add_executable(smartspectra
  src/smartspectra_pub.cpp
  src/smartspectra/smartspectra_publisher.cpp
)

target_link_libraries(smartspectra PUBLIC rclcpp::rclcpp SmartSpectra::SDK)
ament_target_dependencies(smartspectra PUBLIC sensor_msgs std_msgs smartspectra_msgs)

# Simulated metrics publisher for offline development — no SDK, key, or camera.
# Links only rclcpp + smartspectra_msgs, so it builds and runs without the SDK.
add_executable(fake_metrics src/fake_metrics_pub.cpp)
target_link_libraries(fake_metrics PUBLIC rclcpp::rclcpp)
ament_target_dependencies(fake_metrics PUBLIC smartspectra_msgs std_msgs)

# Cosmetic ECG synthesizer: traces a PQRST waveform from cardio.pulse_rate in
# /smartspectra/metrics. No SDK — works with the fake or the real node.
add_executable(ecg_synth src/ecg_synth_pub.cpp)
target_link_libraries(ecg_synth PUBLIC rclcpp::rclcpp)
ament_target_dependencies(ecg_synth PUBLIC smartspectra_msgs std_msgs)

# Face-landmark overlay: face.landmarks -> visualization_msgs/ImageMarker for the
# Foxglove Image panel. No SDK — works with the fake or the real node.
add_executable(face_overlay src/face_overlay_pub.cpp)
target_link_libraries(face_overlay PUBLIC rclcpp::rclcpp)
ament_target_dependencies(face_overlay PUBLIC smartspectra_msgs visualization_msgs)

install(TARGETS
  smartspectra
  fake_metrics
  ecg_synth
  face_overlay
  DESTINATION lib/${PROJECT_NAME})

# uncomment the following section in order to fill in
# further dependencies manually.
# find_package(<dependency> REQUIRED)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
