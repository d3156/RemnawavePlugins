cmake_minimum_required(VERSION 3.16)
project(RemnawaveNodeMarker VERSION 1.0.0 LANGUAGES CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/workspace.cmake")
create_target(plugin)

dependency("../../EasyHttpLib" EasyHttpLib)
dependency("../../PingNode/PingNodeModel" PingNodeModel)
dependency("../../PluginConfigurator/ConfiguratorModel" ConfiguratorModel)

find_package(Boost 1.8 REQUIRED COMPONENTS json thread system)
target_link_libraries(RemnawaveNodeMarker PRIVATE Boost::system Boost::thread Boost::json)

find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)
target_link_libraries(RemnawaveNodeMarker PUBLIC OpenSSL::SSL OpenSSL::Crypto)