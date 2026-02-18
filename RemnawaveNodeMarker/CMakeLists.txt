cmake_minimum_required(VERSION 3.16)
project(RemnawaveNodeMarker VERSION 1.0.0 LANGUAGES CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/workspace.cmake")
create_target(plugin)

dependency("../.." EasyHttpLib "https://gitlab.bubki.zip/d3156/EasyHttpLib")
dependency("../.." PingNodeModel "https://gitlab.bubki.zip/d3156/PingNode")
dependency("../.." ConfiguratorModel "https://gitlab.bubki.zip/d3156/PluginConfigurator")

find_package(Boost 1.8 REQUIRED COMPONENTS json thread system)
target_link_libraries(RemnawaveNodeMarker PRIVATE Boost::system Boost::thread Boost::json)

find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)
target_link_libraries(RemnawaveNodeMarker PUBLIC OpenSSL::SSL OpenSSL::Crypto)