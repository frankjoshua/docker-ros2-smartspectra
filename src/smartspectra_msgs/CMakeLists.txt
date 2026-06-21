cmake_minimum_required(VERSION 3.20)
project(smartspectra_msgs)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  # enums (constant-holder messages)
  "msg/InsightType.msg"
  "msg/FeatureType.msg"
  "msg/MetricType.msg"
  "msg/ExpressionType.msg"
  # base / wrapper measurement types
  "msg/Measurement.msg"
  "msg/DetectionStatus.msg"
  "msg/MeasurementWithConfidence.msg"
  "msg/Strict.msg"
  # expression
  "msg/ExpressionScore.msg"
  "msg/Expression.msg"
  # heart-rate variability
  "msg/Hrv.msg"
  # points
  "msg/Point2dInt32.msg"
  "msg/Point2dFloat.msg"
  "msg/Point3dFloat.msg"
  # landmarks
  "msg/Landmarks.msg"
  # metric group containers
  "msg/Pulse.msg"
  "msg/Breathing.msg"
  "msg/Face.msg"
  "msg/Eda.msg"
  "msg/Cardio.msg"
  # top-level container + insights
  "msg/Metrics.msg"
  "msg/Insight.msg"
  "msg/RequestedMetrics.msg"
  # control
  "msg/SetMetrics.msg"
)

ament_export_dependencies(rosidl_default_runtime)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
