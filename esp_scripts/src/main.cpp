#include <simpleble/SimpleBLE.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <iostream>
#include <iomanip>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <array>
#include <thread>
#include <cctype>
#include <utility>
#include <fstream>
#include <cstdint>
#include <algorithm>

// Подключение библиотеки Mosquitto
#include <mosquitto.h>

// ============================================================
// НАСТРОЙКИ
// ============================================================

// RSSI на расстоянии 1 метра.
constexpr double RSSI_AT_1M = -66.0;
constexpr double PATH_LOSS_EXPONENT = 2.5;
constexpr size_t HISTORY_SIZE = 5;
constexpr int STALE_SECONDS = 60;
constexpr int TOTAL_WINDOW_MS = 5000;
constexpr int POLL_STEP_MS = 50;
constexpr const char* DEFAULT_LOG_NAME = "ble_log.csv";
constexpr int SCANNER_ID = 1;

constexpr std::array<std::string_view, 2> TARGETS = {
    "POCO X7 Pro",
    "Meeee"
};

constexpr int RSSI_NEAR_ENTER = -40;
constexpr int RSSI_NEAR_EXIT  = -50;
constexpr size_t NEAR_MIN_PACKETS = 3;

// --- НАСТРОЙКИ MQTT ---
constexpr const char* MQTT_BROKER_HOST = "10.115.10.11"; // Адрес брокера
constexpr int MQTT_BROKER_PORT = 1883;                // Порт брокера
constexpr const char* MQTT_CLIENT_ID = "ble_scanner_2";
constexpr const char* MQTT_TOPIC = "ips/scanners/scanner-2/detections"; //1 - id именно этого сканера - для других нужно менять
constexpr int MQTT_QOS = 1;

// ============================================================
// СТРУКТУРЫ ДАННЫХ
// ============================================================

struct DeviceInfo {
    std::string name;
    std::deque<int> rssiHistory;
    std::chrono::steady_clock::time_point lastSeen{};
};

struct RawPacket {
    std::string mac;
    std::string name;
    int rssi = 0;
    int64_t timestampUnixMs = 0;
};

// ============================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ============================================================

std::mutex g_queueMutex;
std::vector<RawPacket> g_rawPackets;
std::map<std::string, DeviceInfo> devices;
std::set<std::string> seenThisWindow;
std::set<std::string> g_nearDevices;
std::string g_logPath;
std::atomic<bool> g_running{true};
std::atomic<bool> g_logging{false};
std::atomic<uint64_t> g_receivedEvents{0};
size_t g_maxBatchSize = 0;

enum class Mode { Work, Detect, Pause, Near };
std::atomic<Mode> g_mode{Mode::Work};

std::ofstream g_logFile;

// Глобальный указатель на клиент Mosquitto
mosquitto* g_mosq = nullptr;
std::atomic<bool> g_mqtt_connected{false};

// ============================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != 
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool isTarget(const std::string& name, const std::string& mac) {
    for (const auto target : TARGETS) {
        if (iequals(name, target) || iequals(mac, target)) {
            return true;
        }
    }
    return false;
}

int64_t currentUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string csvEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 2);
    bool needsQuotes = false;
    for (char c : value) {
        if (c == '"' || c == ',' || c == '\n' || c == '\r') needsQuotes = true;
        if (c == '"') result += "\"\"";
        else result += c;
    }
    if (needsQuotes) {
        result.insert(result.begin(), '"');
        result.push_back('"');
    }
    return result;
}

// Экранирование строки для JSON
std::string jsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 2);
    for (char c : value) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}


double calculateDistance(double);
double medianRssi(const std::deque<int>&);
// ============================================================
// MQTT ФУНКЦИИ
// ============================================================

void initMqtt() {
    mosquitto_lib_init();
    g_mosq = mosquitto_new(MQTT_CLIENT_ID, true, nullptr);
    if (!g_mosq) {
        std::cerr << "Ошибка: не удалось создать клиент Mosquitto (нет памяти).\n";
        return;
    }

    // Колбэк для отслеживания подключения
    mosquitto_connect_callback_set(g_mosq, [](struct mosquitto* mosq, void* userdata, int rc) {
        if (rc == 0) {
            std::cout << "MQTT подключен к брокеру.\n";
            g_mqtt_connected = true;
        } else {
            std::cerr << "MQTT ошибка подключения (код: " << rc << ")\n";
            g_mqtt_connected = false;
        }
    });

    mosquitto_loop_start(g_mosq);

    int rc = mosquitto_connect_async(g_mosq, MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "Ошибка подключения к MQTT брокеру (код: " << rc << ")\n";
        mosquitto_loop_stop(g_mosq, false);
        mosquitto_destroy(g_mosq);
        g_mosq = nullptr;
        return;
    }

    std::cout << "MQTT инициализирован. Подключение к " << MQTT_BROKER_HOST 
              << ":" << MQTT_BROKER_PORT << "...\n";
}

void cleanupMqtt() {
    if (g_mosq) {
        mosquitto_disconnect(g_mosq);
        mosquitto_loop_stop(g_mosq, false);
        mosquitto_destroy(g_mosq);
        mosquitto_lib_cleanup();
        g_mosq = nullptr;
        g_mqtt_connected = false;
    }
}

void publishMqttReport(
    int windowNo,
    const std::set<std::string>& seen,
    const std::map<std::string, DeviceInfo>& devs,
    Mode currentMode)
{
    if (!g_mosq || !g_mqtt_connected) return;

    // Получаем текущее время в секундах с дробной частью (аналог time.time() в Python)
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    double timestamp = std::chrono::duration<double>(duration).count();

    // Форматируем timestamp, чтобы избежать проблем с локалью (запятая вместо точки)
    std::ostringstream ts_stream;
    ts_stream << std::fixed << std::setprecision(3) << timestamp;
    std::string timestamp_str = ts_stream.str();

    // Проходим по каждому устройству, замеченному в этом окне
    for (const auto& mac : seen) {
        auto it = devs.find(mac);
        if (it == devs.end() || it->second.rssiHistory.empty()) continue;

        const auto& info = it->second;
        int current_rssi = info.rssiHistory.back(); // Берем последнее значение RSSI

        // Формируем JSON строго по вашему шаблону
        std::string payload = "{";
        payload += "\"schema_version\":1,";
        payload += "\"tag_id\":\"" + jsonEscape(info.name.empty() ? "unknown" : info.name) + "\",";
        payload += "\"timestamp\":" + timestamp_str + ",";
        payload += "\"measurements\":{";
        payload += "\"rssi\":" + std::to_string(current_rssi);
        payload += "}}";

        // Публикуем сообщение для каждого устройства отдельно
        int rc = mosquitto_publish(g_mosq, nullptr, MQTT_TOPIC, 
                                   static_cast<int>(payload.length()), 
                                   payload.c_str(), MQTT_QOS, false);
        
        if (rc != MOSQ_ERR_SUCCESS) {
            std::cerr << "[MQTT] Ошибка публикации для " << mac << " (код: " << rc << ")\n";
        }
    }
}

// ============================================================
// ПОЛУЧЕНИЕ СОБЫТИЙ SIMPLEBLE
// ============================================================

void enqueuePacket(SimpleBLE::Peripheral p) {
    RawPacket pkt;
    pkt.mac = p.address();
    pkt.name = p.identifier();
    pkt.rssi = p.rssi();
    pkt.timestampUnixMs = currentUnixMs();

    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_rawPackets.push_back(std::move(pkt));
    }
    ++g_receivedEvents;
}

// ============================================================
// КРОССПЛАТФОРМЕННОЕ ЧТЕНИЕ КЛАВИАТУРЫ
// ============================================================

#ifndef _WIN32
class TerminalMode {
public:
    TerminalMode() {
        if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldState) == 0) {
            termios raw = oldState;
            raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            active = (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0);
        }
    }
    ~TerminalMode() {
        if (active) tcsetattr(STDIN_FILENO, TCSANOW, &oldState);
    }
    TerminalMode(const TerminalMode&) = delete;
    TerminalMode& operator=(const TerminalMode&) = delete;
private:
    termios oldState{};
    bool active = false;
};

int pollKey() {
    if (!isatty(STDIN_FILENO)) return -1;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeval timeout{0, 0};
    if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) <= 0) return -1;
    unsigned char ch = 0;
    return read(STDIN_FILENO, &ch, 1) == 1 ? ch : -1;
}
#else
int pollKey() {
    return _kbhit() ? _getch() : -1;
}
#endif

// ============================================================
// УПРАВЛЕНИЕ КЛАВИАТУРОЙ
// ============================================================

void handleKey(int key) {
    if (key >= 'A' && key <= 'Z') {
        key = std::tolower(static_cast<unsigned char>(key));
    }

    switch (key) {
        case 'p': g_mode = Mode::Pause; std::cout << "\nPause mode requested.\n"; break;
        case 'd': g_mode = Mode::Detect; std::cout << "\nDetect mode requested.\n"; break;
        case 'w': g_mode = Mode::Work; std::cout << "\nWork mode requested.\n"; break;
        case 'n': 
            g_mode = Mode::Near; 
            g_nearDevices.clear();
            std::cout << "\nNear mode requested (enter >= " << RSSI_NEAR_ENTER
                      << ", exit >= " << RSSI_NEAR_EXIT
                      << ", min packets " << NEAR_MIN_PACKETS << ").\n"; 
            break;
        case 'b': g_logging = true; break;
        case 'e': g_logging = false; break;
        case 'q': g_running = false; break;
        default: break;
    }
}

// ============================================================
// ЛОГИРОВАНИЕ В CSV
// ============================================================

void startLogging() {
    std::cout << "Enter log file name [" << DEFAULT_LOG_NAME << "]: ";
    std::cout.flush();
    std::string name;
    std::getline(std::cin, name);
    if (name.empty()) name = DEFAULT_LOG_NAME;
    if (name.find('.') == std::string::npos) name += ".csv";

    g_logFile.open(name, std::ios::out | std::ios::trunc);
    if (!g_logFile.is_open()) {
        std::cerr << "Cannot open file: " << name << "\n";
        g_logging = false;
        return;
    }
    g_logPath = name;
    g_logFile << "scanner_id,timestamp_unix_ms,mac,name,rssi\n";
    g_logFile.flush();
    std::cout << "Logging to " << g_logPath << "\n";
}

void stopLogging() {
    if (!g_logFile.is_open()) return;
    g_logFile.flush();
    g_logFile.close();
    std::cout << "Log saved: " << g_logPath << "\n";
}

void writeBatchToCsv(const std::vector<RawPacket>& batch) {
    if (!g_logFile.is_open()) return;
    for (const auto& pkt : batch) {
        if (!isTarget(pkt.name, pkt.mac)) continue;
        g_logFile << SCANNER_ID << ',' << pkt.timestampUnixMs << ','
                  << csvEscape(pkt.mac) << ',' << csvEscape(pkt.name) << ','
                  << pkt.rssi << '\n';
    }
    g_logFile.flush();
    if (!g_logFile) {
        std::cerr << "Error writing to log file.\n";
        stopLogging();
        g_logging = false;
    }
}

// ============================================================
// РАСЧЁТ РАССТОЯНИЯ И ОБРАБОТКА RSSI
// ============================================================

double calculateDistance(double rssi) {
    if (rssi == 0.0) return -1.0;
    return std::pow(10.0, (RSSI_AT_1M - rssi) / (10.0 * PATH_LOSS_EXPONENT));
}

double medianRssi(const std::deque<int>& history) {
    if (history.empty()) return 0.0;
    std::vector<int> values(history.begin(), history.end());
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n % 2 == 1) return static_cast<double>(values[n / 2]);
    return (static_cast<double>(values[n / 2 - 1]) + static_cast<double>(values[n / 2])) / 2.0;
}

void updateDevice(const RawPacket& pkt, std::chrono::steady_clock::time_point now) {
    auto& info = devices[pkt.mac];
    if (info.name.empty() && !pkt.name.empty()) info.name = pkt.name;
    if (!pkt.name.empty()) info.name = pkt.name;
    
    info.rssiHistory.push_back(pkt.rssi);
    while (info.rssiHistory.size() > HISTORY_SIZE) info.rssiHistory.pop_front();
    info.lastSeen = now;
}

void removeStaleDevices(std::chrono::steady_clock::time_point now, bool filterTargets) {
    for (auto it = devices.begin(); it != devices.end();) {
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastSeen).count();
        const bool stale = age > STALE_SECONDS;
        const bool unwanted = filterTargets && !isTarget(it->second.name, it->first);
        if (stale || unwanted) it = devices.erase(it);
        else ++it;
    }
}

// ============================================================
// РЕЖИМЫ РАБОТЫ
// ============================================================

void working_func(const std::vector<RawPacket>& batch, std::chrono::steady_clock::time_point now) {
    seenThisWindow.clear();
    for (const auto& pkt : batch) {
        if (!isTarget(pkt.name, pkt.mac)) continue;
        updateDevice(pkt, now);
        seenThisWindow.insert(pkt.mac);
    }
    removeStaleDevices(now, true);
}

void near_func(const std::vector<RawPacket>& batch, std::chrono::steady_clock::time_point now) {
    seenThisWindow.clear();
    std::map<std::string, size_t> packetsThisWindow;

    for (const auto& pkt : batch) {
        if (!isTarget(pkt.name, pkt.mac)) continue;
        if (pkt.rssi < RSSI_NEAR_EXIT) continue;
        updateDevice(pkt, now);
        ++packetsThisWindow[pkt.mac];
    }

    for (const auto& [mac, count] : packetsThisWindow) {
        auto it = devices.find(mac);
        if (it == devices.end() || it->second.rssiHistory.empty() || count < NEAR_MIN_PACKETS) continue;

        const double med = medianRssi(it->second.rssiHistory);
        const bool wasNear = (g_nearDevices.count(mac) > 0);
        const int threshold = wasNear ? RSSI_NEAR_EXIT : RSSI_NEAR_ENTER;

        if (med >= threshold) {
            seenThisWindow.insert(mac);
            g_nearDevices.insert(mac);
        } else {
            g_nearDevices.erase(mac);
        }
    }

    removeStaleDevices(now, true);
    for (auto it = g_nearDevices.begin(); it != g_nearDevices.end(); ) {
        if (devices.find(*it) == devices.end()) it = g_nearDevices.erase(it);
        else ++it;
    }
}

void detecting_func(const std::vector<RawPacket>& batch, std::chrono::steady_clock::time_point now) {
    seenThisWindow.clear();
    for (const auto& pkt : batch) {
        updateDevice(pkt, now);
        seenThisWindow.insert(pkt.mac);
    }
    removeStaleDevices(now, false);
}

// ============================================================
// ВЫВОД РЕЗУЛЬТАТОВ
// ============================================================

void printReport(int windowNo, size_t batchSize, size_t maxBatchSize) {
    const Mode currentMode = g_mode.load();
    const char* modeStr = (currentMode == Mode::Detect) ? "DETECT" :
                          (currentMode == Mode::Work)   ? "WORK"   :
                          (currentMode == Mode::Near)   ? "NEAR"   : "PAUSE";

    std::cout << "=== Window " << windowNo << " [" << modeStr << "]"
              << " | events: " << batchSize << " | devices seen: " << seenThisWindow.size()
              << " | tracked: " << devices.size() << " | max batch: " << maxBatchSize
              << " | total callbacks: " << g_receivedEvents.load() << " ===\n";

    if (currentMode == Mode::Pause) {
        std::cout << "  (paused: scanning and event collection continue)\n\n";
        return;
    }
    if (seenThisWindow.empty()) {
        std::cout << "  (no devices in this window)\n\n";
        return;
    }

    for (const auto& mac : seenThisWindow) {
        const auto it = devices.find(mac);
        if (it == devices.end() || it->second.rssiHistory.empty()) continue;

        const auto& info = it->second;
        const double med = medianRssi(info.rssiHistory);
        const double dist = calculateDistance(med);
        const std::string displayName = info.name.empty() ? "<unnamed>" : info.name;

        std::cout << "  " << std::left << std::setw(24) << displayName << " [" << mac << "]"
                  << " RSSI: " << std::setw(5) << info.rssiHistory.back()
                  << " | median: " << std::fixed << std::setprecision(1) << std::setw(6) << med
                  << " | ~" << std::setprecision(2) << dist << " m\n";
    }
    std::cout << "\n";
}

// ============================================================
// MAIN
// ============================================================

int main() {
#ifndef _WIN32
    TerminalMode terminalMode;
#else
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // 1. Инициализация MQTT
    initMqtt();

    // 2. Получаем Bluetooth-адаптер
    auto adapters = SimpleBLE::Adapter::get_adapters();
    if (adapters.empty()) {
        std::cerr << "No Bluetooth adapter found.\n";
        cleanupMqtt();
        return 1;
    }

    auto adapter = adapters[0];
    std::cout << "Adapter: " << adapter.identifier() << " [" << adapter.address() << "]\n\n";
    std::cout << "Commands:\n"
              << "  q - quit\n  p - pause output/tracking\n  d - detect all devices\n"
              << "  w - work mode (target filter)\n  n - near mode\n"
              << "  b - begin CSV logging\n  e - end CSV logging\n\n";

    // 3. Регистрируем колбэки
    adapter.set_callback_on_scan_found([](SimpleBLE::Peripheral p) { enqueuePacket(std::move(p)); });
    adapter.set_callback_on_scan_updated([](SimpleBLE::Peripheral p) { enqueuePacket(std::move(p)); });

    // 4. Запускаем сканирование
    try {
        adapter.scan_start();
    } catch (const std::exception& e) {
        std::cerr << "Failed to start BLE scan: " << e.what() << "\n";
        cleanupMqtt();
        return 1;
    }
    std::cout << "BLE scanning started.\n\n";

    // 5. Основной цикл
    int windowNo = 0;
    while (g_running.load()) {
        const auto windowStart = std::chrono::steady_clock::now();

        while (g_running.load()) {
            int key = pollKey();
            if (key >= 0) handleKey(key);
            if (!g_running.load()) break;

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - windowStart).count();
            if (elapsed >= TOTAL_WINDOW_MS) break;

            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_STEP_MS));
        }

        std::vector<RawPacket> batch;
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            batch.swap(g_rawPackets);
        }
        ++windowNo;
        const size_t batchSize = batch.size();
        if (batchSize > g_maxBatchSize) g_maxBatchSize = batchSize;

        writeBatchToCsv(batch);

        if (g_logging.load() && !g_logFile.is_open()) startLogging();
        else if (!g_logging.load() && g_logFile.is_open()) stopLogging();

        if (!g_running.load()) break;

        const auto now = std::chrono::steady_clock::now();
        switch (g_mode.load()) {
            case Mode::Detect: detecting_func(batch, now); break;
            case Mode::Work: working_func(batch, now); break;
            case Mode::Near: near_func(batch, now); break;
            case Mode::Pause: seenThisWindow.clear(); break;
        }

        printReport(windowNo, batchSize, g_maxBatchSize);
        
        // Публикация данных в MQTT
        publishMqttReport(windowNo, seenThisWindow, devices, g_mode.load());
    }

    // 6. Завершение
    try {
        adapter.scan_stop();
    } catch (const std::exception& e) {
        std::cerr << "Warning: failed to stop BLE scan: " << e.what() << "\n";
    }

    std::vector<RawPacket> finalBatch;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        finalBatch.swap(g_rawPackets);
    }
    writeBatchToCsv(finalBatch);
    if (g_logFile.is_open()) stopLogging();

    cleanupMqtt(); // Очистка ресурсов MQTT

    std::cout << "\nTotal callback events: " << g_receivedEvents.load() << "\n";
    std::cout << "Stopped.\n";

    return 0;
}