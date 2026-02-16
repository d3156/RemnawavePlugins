#include <PluginCore/IModel>
#include <PluginCore/IPlugin>
#include <EasyHttpLib/AsyncHttpClient>
#include <boost/thread/thread.hpp>
#include <PingNodeModel>
#include <BaseConfig>

namespace HostStates
{
    inline constexpr std::string_view EMPTY_NODES      = "⚪";
    inline constexpr std::string_view XRAY_ERROR       = "🟡";
    inline constexpr std::string_view NODE_UNAVAILABLE = "🔴";
    inline constexpr std::string_view NODE_AVAILABLE   = "🟢";
}

class RemnawaveNodeMarker final : public d3156::PluginCore::IPlugin
{

    struct Host {
        std::string uuid;
        std::string uuid_node;
        std::string base_name;
        std::string_view state          = HostStates::NODE_AVAILABLE;
        const PingNodeModel::Node *node = nullptr;
        std::string genPatch();
        void removeEmojis();
    };

    std::vector<Host> hosts;

    struct Config : d3156::Config {
        Config() : d3156::Config("") {}
        CONFIG_UINT(interval, 1);  // # Период проверки состояния нод в секундах
        CONFIG_STRING(host, "");   // # Домен панели Remnawave
        CONFIG_STRING(token, "");  // # Токен из настроек панели Remnawave
        CONFIG_STRING(cookie, ""); // # COOKIE из https://{host}/auth/login?{COOKIE}
    } conf;

    std::unique_ptr<d3156::AsyncHttpClient> client;
    boost::asio::io_context io;
    boost::thread thread_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_guard =
        boost::asio::make_work_guard(io);
    std::atomic<bool> stopToken                  = false;
    boost::asio::steady_timer check_hosts_timer_ = boost::asio::steady_timer(io);
    PingNodeModel *node_model;

public:
    void registerArgs(d3156::Args::Builder &bldr) override;
    void registerModels(d3156::PluginCore::ModelsStorage &models) override;
    void postInit() override;
    void runIO();
    net::awaitable<void> loadNodesInfo();
    net::awaitable<void> timer_check_hosts();
    net::awaitable<std::string> resolve_hostname(std::string hostname);

    void runTimer();

    net::awaitable<bool> updateNodesInfo();
    virtual ~RemnawaveNodeMarker();
};
