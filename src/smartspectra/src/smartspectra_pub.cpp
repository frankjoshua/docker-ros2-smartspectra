#include "rclcpp/rclcpp.hpp"
#include "smartspectra/smartspectra_publisher.hpp"


int main(int argc, char * argv[]){
        rclcpp::init(argc, argv);
        rclcpp::spin(std::make_shared<SmartSpectraPublisher>());
        rclcpp::shutdown();
        return 0;
};