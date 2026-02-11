#include "RemnawaveNodeMarker.hpp"
#include <Logger/Log.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/thread/thread.hpp>
#include <filesystem>
#include <memory>
#include <sys/prctl.h>
#include <boost/json.hpp>
#include <boost/algorithm/string.hpp>

void RemnawaveNodeMarker::registerArgs(d3156::Args::Builder &bldr)
{
    bldr.setVersion(FULL_NAME).addOption(configPath, "RemnawaveNodeMarkerPath",
                                         "path to config for RemnawaveNodeMarker.json");
}

void RemnawaveNodeMarker::registerModels(d3156::PluginCore::ModelsStorage &models)
{
    node_model = models.registerModel<PingNodeModel>();
}

void RemnawaveNodeMarker::postInit()
{
    thread_ = boost::thread([this]() { this->runIO(); });
}

// ABI required by d3156::PluginCore::Core (dlsym uses exact names)
extern "C" d3156::PluginCore::IPlugin *create_plugin() { return new RemnawaveNodeMarker(); }

extern "C" void destroy_plugin(d3156::PluginCore::IPlugin *p) { delete p; }

using boost::property_tree::ptree;
namespace json = boost::json;
namespace fs   = std::filesystem;

void RemnawaveNodeMarker::parseSettings()
{
    if (!fs::exists(configPath)) {
        Y_LOG(1, "Config file " << configPath << " not found. Creating default config...");
        fs::create_directories(fs::path(configPath).parent_path());
        ptree pt;
        pt.put("interval", interval);
        pt.put("host", host);
        pt.put("token", token);
        pt.put("cookie", cookie);
        boost::property_tree::write_json(configPath, pt);
        G_LOG(1, " Default config created at " << configPath);
        return;
    }
    try {
        ptree pt;
        read_json(configPath, pt);
        interval = pt.get("interval", interval);
        host     = pt.get("host", host);
        token    = pt.get("token", token);
        cookie   = pt.get("cookie", cookie);
    } catch (std::exception e) {
        R_LOG(1, "error on load config " << configPath << " " << e.what());
    }
}

std::string RemnawaveNodeMarker::Host::genPatch()
{
    return boost::json::serialize(json::value{{"uuid", uuid}, {"remark", base_name + std::string(state)}});
}

net::awaitable<void> RemnawaveNodeMarker::loadNodesInfo()
{
    if (stopToken) co_return;
    auto res = co_await client->getAsync("/api/hosts", "");
    try {
        json::value root     = json::parse(boost::beast::buffers_to_string(res.body().data()));
        const auto &response = root.at("response").as_array();
        for (const auto &host : response) {
            const auto &obj = host.as_object();
            Host new_host;
            new_host.uuid      = json::value_to<std::string>(obj.at("uuid"));
            new_host.base_name = json::value_to<std::string>(obj.at("remark"));
            auto nodes_it      = obj.find("nodes");
            if (nodes_it != obj.end() && nodes_it->value().is_array()) {
                auto &nodes = nodes_it->value().get_array();
                if (!nodes.empty()) new_host.uuid_node = json::value_to<std::string>(nodes[0]);
            }
            if (new_host.uuid_node.empty()) new_host.state = HostStates::EMPTY_NODES;
            new_host.removeEmojis();
            hosts.push_back(new_host);
            G_LOG(1, "Added node " << new_host.uuid_node << ":" << new_host.base_name);
        }

        check_hosts_timer_.expires_after(std::chrono::seconds(interval));
        check_hosts_timer_.async_wait(std::bind(&RemnawaveNodeMarker::timer_check_hosts, this, std::placeholders::_1));
        co_return;
    } catch (const std::exception &e) {
        R_LOG(0, "Error loadNodesInfo: " << e.what());
    }
    /// Если не удалось получить список нод - отложим. Попробуем через 15 секунд
    co_await boost::asio::steady_timer(io, std::chrono::milliseconds(15000)).async_wait(net::use_awaitable);
    net::co_spawn(io, loadNodesInfo(), net::detached);
    co_return;
}

void RemnawaveNodeMarker::runIO()
{
    prctl(PR_SET_NAME, "MetricsModel", 0, 0, 0);
    client = std::make_unique<d3156::AsyncHttpClient>(io, host, cookie, "Bearer " + token);
    net::co_spawn(io, loadNodesInfo(), net::detached);
    io.run();
}

RemnawaveNodeMarker::~RemnawaveNodeMarker()
{
    constexpr boost::chrono::milliseconds stopThreadTimeout = boost::chrono::milliseconds(200);
    try {
        stopToken = true;
        io_guard.reset();
        G_LOG(1, "Io-context guard canceled");
        if (!thread_.joinable()) return;
        G_LOG(1, "Thread joinable, try join in " << stopThreadTimeout.count() << " milliseconds");
        if (thread_.timed_join(stopThreadTimeout)) return;
        Y_LOG(1, "Thread was not terminated, attempting to force stop io_context...");
        io.stop();
        if (thread_.timed_join(stopThreadTimeout)) {
            G_LOG(1, "io_context force stopped successfully");
            return;
        }
        R_LOG(1, "WARNING: Thread cannot be stopped. Thread will be detached (potential resource leak)");
        thread_.detach();
    } catch (std::exception &e) {
        R_LOG(1, "Exception throwed in exit: " << e.what());
    }
}

void RemnawaveNodeMarker::Host::removeEmojis()
{
    boost::algorithm::replace_all(base_name, HostStates::EMPTY_NODES, "");
    boost::algorithm::replace_all(base_name, HostStates::XRAY_ERROR, "");
    boost::algorithm::replace_all(base_name, HostStates::NODE_UNAVAILABLE, "");
    boost::algorithm::replace_all(base_name, HostStates::NODE_AVAILABLE, "");
}

net::awaitable<void> RemnawaveNodeMarker::timer_check_hosts(const boost::system::error_code &ec)
{
    if (ec == boost::asio::error::operation_aborted || stopToken) co_return;
    if (ec) G_LOG(0, "Timer error: " << ec.message());
    for (auto &host : hosts) host.state = HostStates::EMPTY_NODES;
    auto res = boost::beast::buffers_to_string((co_await client->getAsync("/api/nodes", "")).body().data());
    json::value root;
    try {
        root = json::parse(res);
    } catch (std::exception &e) {
        R_LOG(1, "Error parse json responce: " << res << " error: " << e.what());
        check_hosts_timer_.expires_after(std::chrono::seconds(interval));
        check_hosts_timer_.async_wait(std::bind(&RemnawaveNodeMarker::timer_check_hosts, this, std::placeholders::_1));
        co_return;
    }
    for (const auto &node : root.at("response").as_array()) {
        const auto &obj     = node.as_object();
        std::string uuid    = json::value_to<std::string>(obj.at("uuid"));
        std::string address = json::value_to<std::string>(obj.at("address"));
        bool isConnected    = obj.at("isConnected").as_bool();
        auto host           = std::ranges::find_if(hosts, [&](auto &host) { return host.uuid == uuid; });
        if (host == hosts.end()) continue;
        host->state = isConnected ? HostStates::NODE_AVAILABLE : HostStates::XRAY_ERROR;
        if (isConnected) continue;
        if (host->node == nullptr) {
            auto node_ip = resolve_hostname(address);
            G_LOG(50, "Resolving " << address << " → " << node_ip);
            auto node = std::ranges::find_if(node_model->get_nodes(), [&](auto &node) { return node_ip == node->ip; });
            if (node != node_model->get_nodes().end()) host->node = node->get();
        }
        if (host->node && !host->node->available) host->state = HostStates::NODE_UNAVAILABLE;
    }
    for (auto &host : hosts) G_LOG(100, co_await client->patchAsync("/api/hosts", host.genPatch()));
    check_hosts_timer_.expires_after(std::chrono::seconds(interval));
    check_hosts_timer_.async_wait(std::bind(&RemnawaveNodeMarker::timer_check_hosts, this, std::placeholders::_1));
    co_return;
}

std::string RemnawaveNodeMarker::resolve_hostname(std::string hostname)
{
    boost::asio::ip::tcp::resolver resolver(io);
    try {
        return resolver.resolve(hostname, "")->endpoint().address().to_string();
    } catch (std::exception &e) {
        R_LOG(1, "Error in resolve hostname: " << hostname << " error: " << e.what());
        return "";
    }
}
