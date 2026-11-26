// ClashYuka mihomo API 服务层测试
//
// 两层测试：
//   1) Mock 测试：用 ix::HttpServer 模拟 mihomo REST API，验证请求路径/请求体编码、
//      代理组枚举（GLOBAL.all 顺序 + 嵌套组 + Provider 节点合并）、
//      单节点/整组测速接口区分、错误信息保留、配置重载结果。
//   2) 真实内核集成测试：若找到 mihomo 可执行文件（环境变量 YUKA_TEST_MIHOMO，
//      或 <exe>/resources/mihomo.exe），则启动真实 mihomo 验证上述行为；
//      并通过 AppContext 验证「切换配置 -> ApplyConfig -> 代理组立即变化」。
//
// 构建：cmake -DCLASHYUKA_BUILD_TESTS=ON ... 然后编译 clashyuka_api_tests。
// 运行：clashyuka_api_tests [mihomo.exe 路径]
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXHttp.h>
#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXNetSystem.h>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#  include <windows.h>
#endif

#include "core/app_context.h"
#include "core/clash/api_client.h"
#include "core/clash/process.h"
#include "core/config/profile.h"
#include "core/utils/dirs.h"

namespace fs = std::filesystem;
using namespace clash_yuka;

namespace {

int g_checks = 0;
int g_failures = 0;

void CheckImpl(bool ok, const char* expr, const char* file, int line,
               const std::string& detail) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("[FAIL] %s:%d  %s", file, line, expr);
        if (!detail.empty()) std::printf("  (%s)", detail.c_str());
        std::printf("\n");
        std::fflush(stdout);
    }
}

#define CHECK(cond) CheckImpl((cond), #cond, __FILE__, __LINE__, std::string())
#define CHECK_DETAIL(cond, detail) \
    CheckImpl((cond), #cond, __FILE__, __LINE__, std::string(detail))

void Info(const std::string& msg) {
    std::printf("[INFO] %s\n", msg.c_str());
    std::fflush(stdout);
}

std::string UrlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hex(in[i + 1]);
            int l = hex(in[i + 2]);
            if (h >= 0 && l >= 0) {
                out.push_back(static_cast<char>((h << 4) | l));
                i += 2;
                continue;
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

std::string PathOf(const std::string& uri) {
    const auto pos = uri.find('?');
    return pos == std::string::npos ? uri : uri.substr(0, pos);
}

std::string QueryOf(const std::string& uri) {
    const auto pos = uri.find('?');
    return pos == std::string::npos ? std::string() : uri.substr(pos + 1);
}

bool QueryHas(const std::string& query, const std::string& key,
              const std::string& expected_value) {
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string::npos) end = query.size();
        const std::string pair = query.substr(start, end - start);
        const auto eq = pair.find('=');
        if (eq != std::string::npos) {
            if (UrlDecode(pair.substr(0, eq)) == key &&
                UrlDecode(pair.substr(eq + 1)) == expected_value) {
                return true;
            }
        }
        if (end == query.size()) break;
        start = end + 1;
    }
    return false;
}

void WriteFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs << content;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    return std::string((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
}

void SetEnv(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

fs::path MakeTempDir(const std::string& tag) {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec) base = fs::current_path(ec);
    fs::path dir = base / ("clashyuka-test-" + tag + "-" +
                           std::to_string(std::chrono::steady_clock::now()
                                              .time_since_epoch()
                                              .count()));
    fs::create_directories(dir, ec);
    return dir;
}

const clash::ProxyGroup* FindGroup(const std::vector<clash::ProxyGroup>& groups,
                                   const std::string& name) {
    for (const auto& g : groups) {
        if (g.name == name) return &g;
    }
    return nullptr;
}

const clash::Proxy* FindProxy(const std::vector<clash::Proxy>& proxies,
                              const std::string& name) {
    for (const auto& p : proxies) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

// ============================================================
// Mock mihomo API
// ============================================================

struct MockState {
    std::mutex mutex;
    std::string last_method;
    std::string last_uri;
    std::string last_body;
    std::string last_auth;
};

nlohmann::json MockProxiesJson() {
    return nlohmann::json::parse(R"JSON({
      "proxies": {
        "DIRECT": {"name":"DIRECT","type":"Direct","alive":true,"history":[]},
        "REJECT": {"name":"REJECT","type":"Reject","alive":false,"history":[]},
        "手动选择": {"name":"手动选择","type":"Selector","all":["DIRECT","REJECT","自动选择"],"now":"DIRECT","history":[]},
        "自动选择": {"name":"自动选择","type":"URLTest","all":["DIRECT","REJECT"],"now":"DIRECT","history":[]},
        "内层组": {"name":"内层组","type":"Fallback","all":["DIRECT","provider-node"],"now":"DIRECT","history":[]},
        "节点 A": {"name":"节点 A","type":"Shadowsocks","alive":true,"history":[{"time":"t","delay":42}]},
        "GLOBAL": {"name":"GLOBAL","type":"Selector","all":["DIRECT","REJECT","节点 A","手动选择","自动选择","内层组"],"now":"DIRECT","history":[]}
      }
    })JSON");
}

nlohmann::json MockProvidersJson() {
    return nlohmann::json::parse(R"JSON({
      "providers": {
        "provA": {
          "name":"provA","type":"Proxy","vehicleType":"HTTP",
          "proxies":[{"name":"provider-node","type":"Shadowsocks","history":[{"time":"t","delay":55}]}]
        },
        "手动选择": {
          "name":"手动选择","type":"Proxy","vehicleType":"Compatible",
          "proxies":[{"name":"bogus-node","type":"Direct"}]
        }
      }
    })JSON");
}

std::shared_ptr<ix::HttpServer> StartMockServer(int port,
                                               std::shared_ptr<MockState> state) {
    auto server = std::make_shared<ix::HttpServer>(port, "127.0.0.1");
    server->setOnConnectionCallback(
        [state](ix::HttpRequestPtr req,
                std::shared_ptr<ix::ConnectionState>) -> ix::HttpResponsePtr {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->last_method = req->method;
                state->last_uri = req->uri;
                state->last_body = req->body;
                auto it = req->headers.find("Authorization");
                state->last_auth =
                    (it != req->headers.end()) ? it->second : std::string();
            }

            auto make = [](int status, const std::string& body) {
                ix::WebSocketHttpHeaders headers;
                headers["Content-Type"] = "application/json";
                return std::make_shared<ix::HttpResponse>(
                    status, "", ix::HttpErrorCode::Ok, headers, body);
            };

            const std::string path = PathOf(req->uri);
            const std::string query = QueryOf(req->uri);

            if (req->method == "GET" && path == "/version") {
                return make(200, R"({"meta":true,"version":"mock-1.0"})");
            }
            if (req->method == "GET" && path == "/proxies") {
                return make(200, MockProxiesJson().dump());
            }
            if (req->method == "GET" && path == "/providers/proxies") {
                return make(200, MockProvidersJson().dump());
            }
            if (req->method == "GET" && path == "/configs") {
                return make(200, R"({"mode":"rule"})");
            }
            if (req->method == "GET" && path == "/memory") {
                // 模拟流式接口：一次返回多个对象，GetMemory 应只取第一个
                return make(200, R"({"inuse":123,"oslimit":0}{"inuse":999,"oslimit":0})");
            }
            if (req->method == "PUT" && path == "/configs") {
                if (req->body.find("bad.yaml") != std::string::npos) {
                    return make(400, R"({"message":"yaml: unmarshal errors: bad proxy-groups"})");
                }
                return make(204, "");
            }
            if (req->method == "GET" && path == "/generate_204") {
                return make(204, "");
            }
            if (req->method == "PUT" && path.rfind("/proxies/", 0) == 0) {
                const std::string group =
                    UrlDecode(path.substr(std::string("/proxies/").size()));
                if (group == "自动选择") {
                    return make(400, R"({"message":"Selector is required"})");
                }
                return make(204, "");
            }
            if (req->method == "GET" && path.rfind("/proxies/", 0) == 0 &&
                path.size() > std::string("/proxies/").size() + 6 &&
                path.compare(path.size() - 6, 6, "/delay") == 0) {
                const std::string name = UrlDecode(path.substr(
                    std::string("/proxies/").size(),
                    path.size() - std::string("/proxies/").size() - 6));
                if (name == "TimeoutNode") {
                    return make(504, R"({"message":"Timeout"})");
                }
                if (name == "DeadNode") {
                    return make(404, R"({"message":"Resource not found"})");
                }
                if (!QueryHas(query, "url", "https://example.com/generate_204")) {
                    return make(400, R"({"message":"unexpected url"})");
                }
                if (!QueryHas(query, "timeout", "5000")) {
                    return make(400, R"({"message":"unexpected timeout"})");
                }
                return make(200, R"({"delay":123})");
            }
            if (req->method == "GET" && path.rfind("/group/", 0) == 0 &&
                path.compare(path.size() - 6, 6, "/delay") == 0) {
                return make(200, R"({"DIRECT":10,"节点 A":20})");
            }
            return make(404, R"({"message":"Resource not found"})");
        });

    auto listen_result = server->listen();
    if (!listen_result.first) {
        std::printf("[FAIL] mock server listen failed: %s\n",
                    listen_result.second.c_str());
        ++g_failures;
        return nullptr;
    }
    // listen() 只负责 bind/listen，需要 start() 才会启动 accept 线程
    server->start();
    return server;
}

// ============================================================
// Mock 测试
// ============================================================

void RunMockTests(int mock_port, const std::shared_ptr<MockState>& state) {
    Info("== Mock API tests ==");
    const std::string base = "http://127.0.0.1:" + std::to_string(mock_port);
    clash::ApiClient api(base, "mock-secret");

    // 1. 代理组枚举：GLOBAL.all 顺序、嵌套组、GLOBAL 末尾
    auto snapshot = api.GetProxySnapshot();
    CHECK(snapshot.groups.size() == 4);
    if (snapshot.groups.size() == 4) {
        CHECK(snapshot.groups[0].name == "手动选择");
        CHECK(snapshot.groups[1].name == "自动选择");
        CHECK(snapshot.groups[2].name == "内层组");
        CHECK(snapshot.groups[3].name == "GLOBAL");
        CHECK(snapshot.groups[1].type == "URLTest");
        CHECK(snapshot.groups[0].now == "DIRECT");
        CHECK(snapshot.groups[2].all.size() == 2);
    } else {
        for (const auto& g : snapshot.groups) {
            Info("group: " + g.name + " [" + g.type + "]");
        }
    }

    // 2. Provider 节点合并（仅 HTTP/File；Compatible 组的镜像不能当节点）
    const clash::Proxy* provider_node = FindProxy(snapshot.proxies, "provider-node");
    CHECK(provider_node != nullptr);
    if (provider_node) {
        CHECK(provider_node->provider == "provA");
        CHECK(provider_node->delay.value_or(-1) == 55);
    }
    CHECK(FindProxy(snapshot.proxies, "bogus-node") == nullptr);
    // /proxies 中已有节点的 history 延迟
    const clash::Proxy* node_a = FindProxy(snapshot.proxies, "节点 A");
    CHECK(node_a != nullptr);
    if (node_a) CHECK(node_a->delay.value_or(-1) == 42);

    // 3. 节点切换：中文/空格名称编码，路径是组名，body name 是节点名
    auto select_ok = api.SelectProxy("手动选择 组", "节点 A");
    CHECK(select_ok.ok);
    CHECK(select_ok.http_status == 204);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        CHECK(state->last_method == "PUT");
        CHECK(UrlDecode(state->last_uri) == "/proxies/手动选择 组");
        CHECK(state->last_body == "{\"name\":\"节点 A\"}");
    }

    // 4. 切换失败：保留 mihomo 原始错误信息（不统一成「不支持此协议」）
    auto select_fail = api.SelectProxy("自动选择", "DIRECT");
    CHECK(!select_fail.ok);
    CHECK(select_fail.http_status == 400);
    CHECK(select_fail.error == "Selector is required");

    // 5. 单节点测速：正确传递 url/timeout，解析 {"delay":N}
    auto delay_ok = api.GetProxyDelay("节点 A", "https://example.com/generate_204", 5000);
    CHECK(delay_ok.ok);
    CHECK(delay_ok.delay_ms == 123);
    CHECK(delay_ok.error.empty());

    // 6. 测速超时/不存在：区分 HTTP 状态并保留原始 message
    auto delay_timeout = api.GetProxyDelay("TimeoutNode",
                                           "https://example.com/generate_204", 5000);
    CHECK(!delay_timeout.ok);
    CHECK(delay_timeout.http_status == 504);
    CHECK(delay_timeout.error == "Timeout");

    auto delay_missing = api.GetProxyDelay("DeadNode",
                                           "https://example.com/generate_204", 5000);
    CHECK(!delay_missing.ok);
    CHECK(delay_missing.http_status == 404);
    CHECK(delay_missing.error == "Resource not found");

    // 7. 整组测速：GET /group/{name}/delay，解析节点->延迟映射
    auto group_delay = api.GetGroupDelay("手动选择",
                                         "https://example.com/generate_204", 5000);
    CHECK(group_delay.ok);
    CHECK(group_delay.delays.size() == 2);
    bool has_direct = false;
    bool has_node_a = false;
    for (const auto& kv : group_delay.delays) {
        if (kv.first == "DIRECT" && kv.second == 10) has_direct = true;
        if (kv.first == "节点 A" && kv.second == 20) has_node_a = true;
    }
    CHECK(has_direct);
    CHECK(has_node_a);

    // 8. 配置热重载：成功/失败分别返回真实结果
    auto reload_ok = api.ReloadConfig("C:\\tmp\\good.yaml");
    CHECK(reload_ok.ok);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        CHECK(state->last_method == "PUT");
        CHECK(state->last_uri == "/configs");
        CHECK(state->last_body.find("good.yaml") != std::string::npos);
    }
    auto reload_bad = api.ReloadConfig("C:\\tmp\\bad.yaml");
    CHECK(!reload_bad.ok);
    CHECK(reload_bad.http_status == 400);
    CHECK(reload_bad.error.find("yaml: unmarshal") != std::string::npos);

    // 9. 鉴权头传递
    api.GetProxyGroups();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        CHECK(state->last_auth == "Bearer mock-secret");
    }

    // 10. /memory 为流式响应：只解析第一个完整对象
    auto mem = api.GetMemory();
    CHECK(mem.inuse == 123);
}

// ============================================================
// 真实 mihomo 集成测试
// ============================================================

fs::path FindMihomo(const std::string& explicit_path) {
    if (!explicit_path.empty() && fs::exists(explicit_path)) {
        return fs::path(explicit_path);
    }
    if (const char* env = std::getenv("YUKA_TEST_MIHOMO")) {
        if (*env && fs::exists(env)) return fs::path(env);
    }
    for (const char* name : {"mihomo.exe", "mihomo"}) {
        fs::path candidate = utils::app_resources_dir() / name;
        std::error_code ec;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

bool WaitApiReady(clash::ApiClient& api, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    for (;;) {
        if (!api.GetVersion().version.empty()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

std::string RealConfigA() {
    return R"YAML(mixed-port: 0
mode: rule
log-level: warning
proxies: []
proxy-providers:
  local-prov:
    type: file
    path: ./providers.yaml
    health-check:
      enable: false
proxy-groups:
  - name: 手动选择
    type: select
    use:
      - local-prov
    proxies:
      - DIRECT
      - REJECT
      - 自动选择
  - name: 自动选择
    type: url-test
    url: http://www.gstatic.com/generate_204
    interval: 600
    proxies:
      - DIRECT
      - REJECT
rules:
  - MATCH,手动选择
)YAML";
}

std::string RealConfigB() {
    return R"YAML(mode: rule
log-level: warning
proxies: []
proxy-groups:
  - name: 第二次配置组
    type: select
    proxies:
      - DIRECT
      - REJECT
rules:
  - MATCH,第二次配置组
)YAML";
}

std::string RealConfigBad() {
    return R"YAML(mode: rule
proxy-groups: "not-a-list"
rules: 42
)YAML";
}

void RunRealCoreApiTests(const fs::path& mihomo, int mock_port) {
    Info("== Real mihomo API tests: " + mihomo.string() + " ==");
    const fs::path dir = MakeTempDir("real");
    int api_port = ix::getFreePort();
    const std::string secret = "real-secret";

    {
        auto cfg = RealConfigA();
        cfg = "external-controller: 127.0.0.1:" + std::to_string(api_port) + "\n" +
              "secret: " + secret + "\n" + cfg;
        WriteFile(dir / "config.yaml", cfg);
        WriteFile(dir / "providers.yaml",
                  "proxies:\n"
                  "  - name: 提供商节点\n"
                  "    type: socks5\n"
                  "    server: 127.0.0.1\n"
                  "    port: 1080\n");
        WriteFile(dir / "config2.yaml", RealConfigB());
        WriteFile(dir / "bad.yaml", RealConfigBad());
    }

    clash::ClashProcess proc;
    proc.set_on_log([](const std::string&) {});
    std::string start_err;
    CHECK_DETAIL(proc.Start(mihomo, dir, dir / "config.yaml", &start_err), start_err);

    clash::ApiClient api("http://127.0.0.1:" + std::to_string(api_port), secret);
    if (!WaitApiReady(api, 20000)) {
        Info("SKIP: mihomo API 未就绪: " + start_err);
        ++g_failures;
        proc.Stop();
        return;
    }

    // 代理组：配置中的 select 组必须存在（不是只显示自动组），GLOBAL 在末尾
    auto snapshot = api.GetProxySnapshot();
    CHECK(FindGroup(snapshot.groups, "手动选择") != nullptr);
    CHECK(FindGroup(snapshot.groups, "自动选择") != nullptr);
    CHECK(!snapshot.groups.empty() && snapshot.groups.back().name == "GLOBAL");
    if (const auto* g = FindGroup(snapshot.groups, "手动选择")) {
        CHECK(g->type == "Selector");
    }

    // Provider 节点可见（真实 file provider）
    if (FindProxy(snapshot.proxies, "提供商节点") == nullptr) {
        Info("注意: 当前 mihomo 未在 /proxies 暴露 file provider 节点，"
             "检查 /providers/proxies 合并逻辑");
    }
    const auto* provider_node = FindProxy(snapshot.proxies, "提供商节点");
    CHECK(provider_node != nullptr);

    // 节点切换（中文组名/节点名）
    auto select = api.SelectProxy("手动选择", "REJECT");
    CHECK_DETAIL(select.ok, select.error + " HTTP " + std::to_string(select.http_status));
    auto after = api.GetProxySnapshot();
    if (const auto* g = FindGroup(after.groups, "手动选择")) {
        CHECK(g->now == "REJECT");
    } else {
        CHECK(false);
    }

    // 不存在的组：错误必须保留，不能谎报成功
    auto select_missing = api.SelectProxy("不存在的组", "DIRECT");
    CHECK(!select_missing.ok);
    CHECK(!select_missing.error.empty());

    // 单节点测速：用本地 mock 的 204 端点，结果确定
    const std::string test_url =
        "http://127.0.0.1:" + std::to_string(mock_port) + "/generate_204";
    auto delay_direct = api.GetProxyDelay("DIRECT", test_url, 3000);
    CHECK_DETAIL(delay_direct.ok, delay_direct.error);
    CHECK(delay_direct.delay_ms > 0);

    // 不可达地址 -> 超时错误（mihomo 返回 504 Timeout）
    auto delay_timeout = api.GetProxyDelay("DIRECT",
                                           "http://10.255.255.1/generate_204", 1000);
    CHECK(!delay_timeout.ok);
    CHECK(!delay_timeout.error.empty());
    Info("timeout error: HTTP " + std::to_string(delay_timeout.http_status) +
         " " + delay_timeout.error);

    // 整组测速：解析组内节点结果
    auto group_delay = api.GetGroupDelay("手动选择", test_url, 3000);
    CHECK_DETAIL(group_delay.ok, group_delay.error);
    bool has_direct_delay = false;
    for (const auto& kv : group_delay.delays) {
        if (kv.first == "DIRECT") has_direct_delay = true;
    }
    CHECK(has_direct_delay);

    // /memory 是流式接口：必须在短时间内返回第一个采样（不能读满 30s 超时）
    {
        auto t0 = std::chrono::steady_clock::now();
        auto mem = api.GetMemory();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        CHECK(mem.inuse > 0);
        CHECK_DETAIL(ms < 3000, std::to_string(ms) + "ms");
    }

    // 回归：一个进行中的 /memory 流式读取不能阻塞节点切换
    {
        std::thread mem_thread([&api]() { (void)api.GetMemory(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto t0 = std::chrono::steady_clock::now();
        auto concurrent_select = api.SelectProxy("手动选择", "DIRECT");
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        CHECK_DETAIL(concurrent_select.ok, concurrent_select.error);
        CHECK_DETAIL(ms < 2000, std::to_string(ms) + "ms");
        mem_thread.join();
    }

    // 热重载到第二份配置：代理组立即变化（P0-1 的核心机制）
    auto reload = api.ReloadConfig((dir / "config2.yaml").string());
    CHECK_DETAIL(reload.ok, reload.error);
    auto snapshot_b = api.GetProxySnapshot();
    CHECK(FindGroup(snapshot_b.groups, "第二次配置组") != nullptr);
    CHECK(FindGroup(snapshot_b.groups, "手动选择") == nullptr);

    // 非法配置热重载：必须失败，且原有配置保持不变
    auto reload_bad = api.ReloadConfig((dir / "bad.yaml").string());
    CHECK(!reload_bad.ok);
    CHECK(!reload_bad.error.empty());
    Info("bad reload error: HTTP " + std::to_string(reload_bad.http_status) +
         " " + reload_bad.error);
    auto snapshot_after_bad = api.GetProxySnapshot();
    CHECK(FindGroup(snapshot_after_bad.groups, "第二次配置组") != nullptr);

    proc.Stop();
}

// ============================================================
// AppContext 端到端测试（模拟「导入配置 -> 应用 -> 立即看到新节点」）
// ============================================================

std::string ProfileYaml(const std::string& group_name) {
    return "mixed-port: 0\n"
           "mode: rule\n"
           "proxies: []\n"
           "proxy-groups:\n"
           "  - name: " + group_name + "\n"
           "    type: select\n"
           "    proxies:\n"
           "      - DIRECT\n"
           "      - REJECT\n"
           "rules:\n"
           "  - MATCH," + group_name + "\n";
}

void RunAppContextTests(const fs::path& mihomo) {
    Info("== AppContext end-to-end tests ==");

    const fs::path root = MakeTempDir("appctx");
    const fs::path appdata = root / "roaming";
    const fs::path localappdata = root / "local";
    fs::create_directories(appdata / "ClashYuka");
    fs::create_directories(localappdata / "ClashYuka");
    SetEnv("APPDATA", appdata.string());
    SetEnv("LOCALAPPDATA", localappdata.string());

    const int api_port = ix::getFreePort();
    const int mixed_port = ix::getFreePort();
    const std::string secret = "app-secret";

    // 内核可执行文件必须位于 <test-exe>/resources 下（CoreManager 固定查找位置）
    const fs::path resources = utils::app_resources_dir();
    std::error_code ec;
    fs::create_directories(resources, ec);
    fs::path core_target = resources / "mihomo.exe";
    if (!fs::exists(core_target, ec)) {
        fs::copy_file(mihomo, core_target, fs::copy_options::overwrite_existing, ec);
    }
    CHECK(!ec);

    const fs::path config_dir = appdata / "ClashYuka";
    const fs::path profiles_dir = localappdata / "ClashYuka" / "profiles";
    fs::create_directories(profiles_dir, ec);

    WriteFile(config_dir / "verge.yaml",
              "clash_core: mihomo\n"
              "enable_service_mode: false\n"
              "enable_random_port: false\n"
              "verge_mixed_port: " + std::to_string(mixed_port) + "\n"
              "enable_system_proxy: false\n"
              "enable_tun_mode: false\n");
    WriteFile(config_dir / "clash-guard-overrides.yaml",
              "mixed-port: " + std::to_string(mixed_port) + "\n"
              "external-controller: 127.0.0.1:" + std::to_string(api_port) + "\n"
              "secret: " + secret + "\n");
    WriteFile(profiles_dir / "a.yaml", ProfileYaml("首发配置组"));
    WriteFile(profiles_dir / "b.yaml", ProfileYaml("第二配置组"));
    // 合法 YAML、但 mihomo 无法接受的配置（proxy-groups 不是数组）
    WriteFile(profiles_dir / "c.yaml",
              "mode: rule\nproxy-groups: \"not-a-list\"\nrules: 42\n");
    // 合法 YAML、但内核启动时才会失败的配置（未知代理协议）
    WriteFile(profiles_dir / "d.yaml",
              "mode: rule\n"
              "proxies:\n"
              "  - name: broken\n"
              "    type: no-such-protocol\n"
              "    server: 127.0.0.1\n"
              "    port: 1\n"
              "proxy-groups:\n"
              "  - name: 坏节点组\n"
              "    type: select\n"
              "    proxies:\n"
              "      - DIRECT\n"
              "rules:\n"
              "  - MATCH,DIRECT\n");
    WriteFile(config_dir / "profiles.yaml",
              "current:\n"
              "  - A\n"
              "items:\n"
              "  - uid: A\n"
              "    name: A\n"
              "    type: local\n"
              "    file: a.yaml\n"
              "  - uid: B\n"
              "    name: B\n"
              "    type: local\n"
              "    file: b.yaml\n"
              "  - uid: C\n"
              "    name: C\n"
              "    type: local\n"
              "    file: c.yaml\n"
              "  - uid: D\n"
              "    name: D\n"
              "    type: local\n"
              "    file: d.yaml\n");

    auto& ctx = AppContext::Get();
    ctx.Init();
    CHECK(ctx.IsInitialized());
    if (!ctx.WaitForApiReady(25000)) {
        Info("SKIP: 内核未就绪，AppContext 测试终止。last error: " +
             ctx.last_start_error());
        ++g_failures;
        ctx.StopCore();
        return;
    }

    // 1) 初始配置生效
    auto snap_a = ctx.api().GetProxySnapshot();
    CHECK(FindGroup(snap_a.groups, "首发配置组") != nullptr);
    CHECK(FindGroup(snap_a.groups, "第二配置组") == nullptr);

    // 1.1) TUN 配置注入（默认关闭；设备名固定，避免与其它客户端适配器冲突）
    {
        YAML::Node cfg = YAML::LoadFile(ctx.RuntimeConfigPath().string());
        CHECK(cfg["tun"] && cfg["tun"].IsMap());
        CHECK(cfg["tun"]["enable"] && !cfg["tun"]["enable"].as<bool>());
        CHECK(cfg["tun"]["device"] &&
              cfg["tun"]["device"].as<std::string>() == "YukaTun");
    }

    // 1.2) 内存/性能配置注入（find-process-mode / sniffer）
    ctx.verge().mihomo_find_process_mode = "off";
    ctx.verge().mihomo_sniffer_enable = false;
    std::string perf_err = ctx.ApplyConfig();
    CHECK_DETAIL(perf_err.empty(), perf_err);
    {
        YAML::Node cfg = YAML::LoadFile(ctx.RuntimeConfigPath().string());
        CHECK(cfg["find-process-mode"] &&
              cfg["find-process-mode"].as<std::string>() == "off");
        CHECK(cfg["sniffer"] && cfg["sniffer"]["enable"] &&
              !cfg["sniffer"]["enable"].as<bool>());
    }

    // 1.3) Go 内存参数：通过环境变量传给子进程，重启后内核仍可启动
    ctx.verge().mihomo_gogc = 50;
    ctx.verge().mihomo_memlimit_mb = 128;
    std::string env_err = ctx.RestartCore();
    CHECK_DETAIL(env_err.empty(), env_err);
    CHECK(ctx.WaitForApiReady(20000));
    ctx.verge().mihomo_gogc = 0;
    ctx.verge().mihomo_memlimit_mb = 0;

    // 2) 切换到第二份配置 -> ApplyConfig -> 无需重启 GUI 即可看到新组
    ctx.profiles().current = {"B"};
    ctx.profiles().Save(utils::profiles_yaml_path());
    std::string apply_err = ctx.ApplyConfig();
    CHECK_DETAIL(apply_err.empty(), apply_err);
    auto snap_b = ctx.api().GetProxySnapshot();
    CHECK(FindGroup(snap_b.groups, "第二配置组") != nullptr);
    CHECK(FindGroup(snap_b.groups, "首发配置组") == nullptr);

    // 3) 切换节点（中文组名）通过统一 API 层
    auto select = ctx.api().SelectProxy("第二配置组", "REJECT");
    CHECK_DETAIL(select.ok, select.error);
    auto snap_after_select = ctx.api().GetProxySnapshot();
    if (const auto* g = FindGroup(snap_after_select.groups, "第二配置组")) {
        CHECK(g->now == "REJECT");
    } else {
        CHECK(false);
    }

    // 4) 非法配置：ApplyConfig 必须失败并保留运行中的配置（不谎报成功）
    const std::string runtime_config = ReadFile(ctx.RuntimeConfigPath());
    ctx.profiles().current = {"C"};
    ctx.profiles().Save(utils::profiles_yaml_path());
    std::string bad_err = ctx.ApplyConfig();
    CHECK(!bad_err.empty());
    if (bad_err.empty()) {
        Info("预期 ApplyConfig 失败但返回成功");
    } else {
        Info("ApplyConfig 失败信息: " + bad_err);
    }
    auto snap_after_bad = ctx.api().GetProxySnapshot();
    CHECK(FindGroup(snap_after_bad.groups, "第二配置组") != nullptr);
    CHECK(ReadFile(ctx.RuntimeConfigPath()) == runtime_config);

    // 4.1) 恢复为有效配置（B），确认失败后仍能正常继续应用
    ctx.profiles().current = {"B"};
    ctx.profiles().Save(utils::profiles_yaml_path());
    std::string recover_err = ctx.ApplyConfig();
    CHECK_DETAIL(recover_err.empty(), recover_err);

    // 5) 监听端口变更：PUT /configs 不会重建监听器，必须走安全重启
    const int new_mixed_port = ix::getFreePort();
    ctx.verge().verge_mixed_port = static_cast<uint16_t>(new_mixed_port);
    std::string restart_err = ctx.ApplyConfig();
    CHECK_DETAIL(restart_err.empty(), restart_err);
    try {
        const auto cfg_json = nlohmann::json::parse(ctx.api().GetConfig());
        CHECK(cfg_json.value("mixed-port", 0) == new_mixed_port);
    } catch (const std::exception& e) {
        CHECK_DETAIL(false, e.what());
    }

    // 6) 新配置启动失败时必须回滚，且不谎报成功
    //    切到含未知代理协议的配置，并改动监听端口强制走「重启内核」路径；
    //    mihomo 启动即失败 -> ApplyConfig 必须返回错误并回滚到旧配置。
    const int another_port = ix::getFreePort();
    ctx.profiles().current = {"D"};
    ctx.profiles().Save(utils::profiles_yaml_path());
    ctx.verge().verge_mixed_port = static_cast<uint16_t>(another_port);
    std::string failed_restart = ctx.ApplyConfig();
    CHECK(!failed_restart.empty());
    Info("restart failure message: " + failed_restart);
    auto snap_rolled_back = ctx.api().GetProxySnapshot();
    CHECK(FindGroup(snap_rolled_back.groups, "第二配置组") != nullptr);

    // 恢复为可用配置与端口，确认回滚后的内核仍可正常应用配置
    ctx.profiles().current = {"B"};
    ctx.profiles().Save(utils::profiles_yaml_path());
    ctx.verge().verge_mixed_port = static_cast<uint16_t>(new_mixed_port);
    std::string restore_err = ctx.ApplyConfig();
    CHECK_DETAIL(restore_err.empty(), restore_err);

    // 7) 内核停止后：API 不崩溃，返回空数据与错误
    ctx.StopCore();
    CHECK(!ctx.IsCoreRunning());
    auto snap_offline = ctx.api().GetProxySnapshot();
    CHECK(snap_offline.groups.empty());
    auto select_offline = ctx.api().SelectProxy("第二配置组", "DIRECT");
    CHECK(!select_offline.ok);
    CHECK(!select_offline.error.empty());
    Info("offline select error: " + select_offline.error);
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    // IXWebSocket 在 Windows 上要求先初始化 Winsock（HttpClient 内部会初始化，
    // 但测试先启动 HttpServer，因此这里显式初始化）。
    ix::initNetSystem();

    const std::string explicit_core = (argc > 1) ? argv[1] : std::string();

    const int mock_port = ix::getFreePort();
    auto state = std::make_shared<MockState>();
    auto mock_server = StartMockServer(mock_port, state);
    if (!mock_server) return 1;

    RunMockTests(mock_port, state);

    const fs::path mihomo = FindMihomo(explicit_core);
    if (mihomo.empty()) {
        Info("SKIP: 未找到 mihomo 可执行文件（可通过参数或 YUKA_TEST_MIHOMO 指定），"
             "真实内核场景未验证");
    } else {
        RunRealCoreApiTests(mihomo, mock_port);
        RunAppContextTests(mihomo);
    }

    mock_server->stop();

    ix::uninitNetSystem();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
