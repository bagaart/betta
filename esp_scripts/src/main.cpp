#include <simpleble/SimpleBLE.h>
#include <mosquitto.h>
#include <nlohmann/json.hpp>

#include <windows.h>
#include <conio.h>

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
#include <memory>

using json = nlohmann::json;

// ============================================================
// НАСТРОЙКИ СКАНЕРА
// ============================================================

constexpr double RSSI_AT_1M = -66.0;
constexpr double PATH_LOSS_EXPONENT = 2.5;
constexpr size_t HISTORY_SIZE = 5;
constexpr int STALE_SECONDS = 60;
constexpr int TOTAL_WINDOW_MS = 5000;
constexpr int POLL_STEP_MS = 50;
constexpr const char* DEFAULT_LOG_NAME = "ble_log.csv";

// ID должен совпадать с data/scanners.json и mapping-файлом.
// Для первого сканера: scanner-1
constexpr const char* SCANNER_ID = "scanner-1";

// ============================================================
// НАСТРОЙКИ MQTT
// ============================================================

// IP ноутбука, на котором запущен Mosquitto.
// НЕ localhost: сервер MQTT находится на другом ноутбуке.
constexpr const char* MQTT_HOST = "192.168.1.100";
constexpr int MQTT_PORT = 1883;

constexpr int MQTT_QOS = 1;
constexpr const char* MQTT_TOPIC_PREFIX = "ips/scanners/";
constexpr const char* MQTT_TOPIC_SUFFIX = "/detections";

// В тестовой конфигурации брокера с allow_anonymous true
// логин и пароль не нужны.

// Устройства, которые отслеживаются в режиме WORK.
constexpr std::array<std::string_view, 2> TARGETS = {
    "POCO X7 Pro",
    "Meeee"
};

// ============================================================
// MQTT
// ============================================================

struct mosquitto* g_mqtt = nullptr;
bool g_mqttLibraryInitialized = false;

void onMqttConnect(
    struct mosquitto*,
    void*,
    int rc
) {
    if (rc == 0) {
        std::cout << "[MQTT] Connected to broker\n";
    } else {
        std::cerr << "[MQTT] Connection failed, code: "
                  << rc << "\n";
    }
}

void onMqttDisconnect(
    struct mosquitto*,
    void*,
    int rc
) {
    if (rc != 0) {
        std::cerr << "[MQTT] Disconnected, code: "
                  << rc << "\n";
    }
}

bool initMqtt() {
    int rc = mosquitto_lib_init();

    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] mosquitto_lib_init failed: "
                  << mosquitto_strerror(rc) << "\n";
        return false;
    }

    g_mqttLibraryInitialized = true;

    const std::string clientId =
        std::string("ble-") + SCANNER_ID;

    g_mqtt = mosquitto_new(
        clientId.c_str(),
        true,
        nullptr
    );

    if (!g_mqtt) {
        std::cerr << "[MQTT] mosquitto_new failed\n";
        mosquitto_lib_cleanup();
        g_mqttLibraryInitialized = false;
        return false;
    }

    mosquitto_connect_callback_set(g_mqtt, onMqttConnect);
    mosquitto_disconnect_callback_set(g_mqtt, onMqttDisconnect);

    // Автоматическое переподключение при потере связи.
    mosquitto_reconnect_delay_set(g_mqtt, 1, 10, true);

    // Асинхронное подключение позволяет запустить сетевой цикл,
    // даже если брокер временно недоступен.
    rc = mosquitto_connect_async(
        g_mqtt,
        MQTT_HOST,
        MQTT_PORT,
        30
    );

    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] connect_async failed: "
                  << mosquitto_strerror(rc) << "\n";

        mosquitto_destroy(g_mqtt);
        g_mqtt = nullptr;

        mosquitto_lib_cleanup();
        g_mqttLibraryInitialized = false;
        return false;
    }

    rc = mosquitto_loop_start(g_mqtt);

    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] loop_start failed: "
                  << mosquitto_strerror(rc) << "\n";

        mosquitto_destroy(g_mqtt);
        g_mqtt = nullptr;

        mosquitto_lib_cleanup();
        g_mqttLibraryInitialized = false;
        return false;
    }

    std::cout << "[MQTT] Connecting to "
              << MQTT_HOST << ":" << MQTT_PORT << "\n";

    return true;
}

void stopMqtt() {
    if (g_mqtt) {
        // Останавливаем сетевой поток.
        mosquitto_disconnect(g_mqtt);
        mosquitto_loop_stop(g_mqtt, true);

        mosquitto_destroy(g_mqtt);
        g_mqtt = nullptr;
    }

    if (g_mqttLibraryInitialized) {
        mosquitto_lib_cleanup();
        g_mqttLibraryInitialized = false;
    }

    std::cout << "[MQTT] Stopped\n";
}

bool publishDetection(const std::string& mac, int rssi, int64_t timestampUnixMs) {
    if (!g_mqtt) {
        return false;
    }

    const std::string topic =
        std::string(MQTT_TOPIC_PREFIX) +
        SCANNER_ID +
        MQTT_TOPIC_SUFFIX;

    // Backend ожидает timestamp в секундах Unix,
    // поэтому миллисекунды делим на 1000.
    const double timestampSeconds =
        static_cast<double>(timestampUnixMs) / 1000.0;

    json payload = {
        {"schema_version", 1},
        {"tag_id", mac},
        {"timestamp", timestampSeconds},
        {"measurements", {
            {"rssi", static_cast<double>(rssi)}
        }}
    };

    const std::string payloadString = payload.dump();

    int rc = mosquitto_publish(
        g_mqtt,
        nullptr,
        topic.c_str(),
        static_cast<int>(payloadString.size()),
        payloadString.c_str(),
        MQTT_QOS,
        false
    );

    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[MQTT] Publish failed: "
                  << mosquitto_strerror(rc)
                  << " | tag=" << mac << "\n";
        return false;
    }

    return true;
}

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

std::string g_logPath;

std::atomic<bool> g_running{true};
std::atomic<bool> g_logging{false};
std::atomic<uint64_t> g_receivedEvents{0};

size_t g_maxBatchSize = 0;

enum class Mode {
    Work,
    Detect,
    Pause
};

std::atomic<Mode> g_mode{Mode::Work};

std::ofstream g_logFile;

// ============================================================
// СРАВНЕНИЕ СТРОК БЕЗ УЧЁТА РЕГИСТРА
// ============================================================

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);

        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }

    return true;
}

// ============================================================
// ПРОВЕРКА ЦЕЛЕВЫХ УСТРОЙСТВ
// ============================================================

bool isTarget(const std::string& name, const std::string& mac) {
    for (const auto target : TARGETS) {
        if (iequals(name, target) || iequals(mac, target)) {
            return true;
        }
    }

    return false;
}

// ============================================================
// ВРЕМЯ UNIX В МИЛЛИСЕКУНДАХ
// ============================================================

int64_t currentUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// ============================================================
// CSV: ЭКРАНИРОВАНИЕ ПОЛЕЙ
// ============================================================

std::string csvEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 2);

    bool needsQuotes = false;

    for (char c : value) {
        if (c == '"' || c == ',' || c == '\n' || c == '\r') {
            needsQuotes = true;
        }

        if (c == '"') {
            result += "\"\"";
        } else {
            result += c;
        }
    }

    if (needsQuotes) {
        result.insert(result.begin(), '"');
        result.push_back('"');
    }

    return result;
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
// УПРАВЛЕНИЕ КЛАВИАТУРОЙ
// ============================================================

void handleKey(int key) {
    if (key >= 'A' && key <= 'Z') {
        key = std::tolower(static_cast<unsigned char>(key));
    }

    switch (key) {
        case 'p':
            g_mode = Mode::Pause;
            std::cout << "\nPause mode requested.\n";
            break;

        case 'd':
            g_mode = Mode::Detect;
            std::cout << "\nDetect mode requested.\n";
            break;

        case 'w':
            g_mode = Mode::Work;
            std::cout << "\nWork mode requested.\n";
            break;

        case 'b':
            g_logging = true;
            break;

        case 'e':
            g_logging = false;
            break;

        case 'q':
            g_running = false;
            break;

        default:
            break;
    }
}

// ============================================================
// ЗАПУСК ЛОГИРОВАНИЯ
// ============================================================

void startLogging() {
    std::cout
        << "Enter log file name ["
        << DEFAULT_LOG_NAME
        << "]: ";

    std::cout.flush();

    std::string name;
    std::getline(std::cin, name);

    if (name.empty()) {
        name = DEFAULT_LOG_NAME;
    }

    if (name.find('.') == std::string::npos) {
        name += ".csv";
    }

    g_logFile.open(name, std::ios::out | std::ios::trunc);

    if (!g_logFile.is_open()) {
        std::cerr << "Cannot open file: " << name << "\n";
        g_logging = false;
        return;
    }

    g_logPath = name;

    g_logFile
        << "scanner_id,"
        << "timestamp_unix_ms,"
        << "mac,"
        << "name,"
        << "rssi\n";

    g_logFile.flush();

    std::cout << "Logging to " << g_logPath << "\n";
}

// ============================================================
// ОСТАНОВКА ЛОГИРОВАНИЯ
// ============================================================

void stopLogging() {
    if (!g_logFile.is_open()) {
        return;
    }

    g_logFile.flush();
    g_logFile.close();

    std::cout << "Log saved: " << g_logPath << "\n";
}

// ============================================================
// ЗАПИСЬ СОБЫТИЙ В CSV
// ============================================================

void writeBatchToCsv(const std::vector<RawPacket>& batch) {
    if (!g_logFile.is_open()) {
        return;
    }

    for (const auto& pkt : batch) {
        if (!isTarget(pkt.name, pkt.mac)) {
            continue;
        }

        g_logFile
            << SCANNER_ID << ','
            << pkt.timestampUnixMs << ','
            << csvEscape(pkt.mac) << ','
            << csvEscape(pkt.name) << ','
            << pkt.rssi
            << '\n';
    }

    g_logFile.flush();

    if (!g_logFile) {
        std::cerr << "Error writing to log file.\n";
        stopLogging();
        g_logging = false;
    }
}

// ============================================================
// ОТПРАВКА ПАРТИИ СОБЫТИЙ В MQTT
// ============================================================

// Поведение соответствует режимам сканера:
// WORK   — публикуем только целевые устройства;
// DETECT — публикуем все обнаруженные устройства;
// PAUSE  — не отправляем события в backend.

void publishBatchToMqtt(const std::vector<RawPacket>& batch) {
    const Mode mode = g_mode.load();

    if (mode == Mode::Pause) {
        return;
    }

    for (const auto& pkt : batch) {
        if (mode == Mode::Work &&
            !isTarget(pkt.name, pkt.mac)) {
            continue;
        }

        publishDetection(
            pkt.mac,
            pkt.rssi,
            pkt.timestampUnixMs
        );
    }
}

// ============================================================
// РАСЧЁТ РАССТОЯНИЯ ПО RSSI
// ============================================================

double calculateDistance(double rssi) {
    if (rssi == 0.0) {
        return -1.0;
    }

    return std::pow(
        10.0,
        (RSSI_AT_1M - rssi) / (10.0 * PATH_LOSS_EXPONENT)
    );
}

// ============================================================
// ОБНОВЛЕНИЕ ИСТОРИИ RSSI
// ============================================================

void updateDevice(
    const RawPacket& pkt,
    std::chrono::steady_clock::time_point now
) {
    auto& info = devices[pkt.mac];

    if (!pkt.name.empty()) {
        info.name = pkt.name;
    }

    info.rssiHistory.push_back(pkt.rssi);

    while (info.rssiHistory.size() > HISTORY_SIZE) {
        info.rssiHistory.pop_front();
    }

    info.lastSeen = now;
}

// ============================================================
// УДАЛЕНИЕ УСТАРЕВШИХ УСТРОЙСТВ
// ============================================================

void removeStaleDevices(
    std::chrono::steady_clock::time_point now,
    bool filterTargets
) {
    for (auto it = devices.begin(); it != devices.end();) {
        const auto age =
            std::chrono::duration_cast<std::chrono::seconds>(
                now - it->second.lastSeen
            ).count();

        const bool stale = age > STALE_SECONDS;

        const bool unwanted =
            filterTargets &&
            !isTarget(it->second.name, it->first);

        if (stale || unwanted) {
            it = devices.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================
// РЕЖИМ WORK
// ============================================================

void working_func(
    const std::vector<RawPacket>& batch,
    std::chrono::steady_clock::time_point now
) {
    seenThisWindow.clear();

    for (const auto& pkt : batch) {
        if (!isTarget(pkt.name, pkt.mac)) {
            continue;
        }

        updateDevice(pkt, now);
        seenThisWindow.insert(pkt.mac);
    }

    removeStaleDevices(now, true);
}

// ============================================================
// РЕЖИМ DETECT
// ============================================================

void detecting_func(
    const std::vector<RawPacket>& batch,
    std::chrono::steady_clock::time_point now
) {
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

void printReport(
    int windowNo,
    size_t batchSize,
    size_t maxBatchSize
) {
    const Mode currentMode = g_mode.load();

    const char* modeStr =
        currentMode == Mode::Detect ? "DETECT" :
        currentMode == Mode::Work ? "WORK" : "PAUSE";

    std::cout
        << "=== Window " << windowNo
        << " [" << modeStr << "]"
        << " | events: " << batchSize
        << " | devices seen: " << seenThisWindow.size()
        << " | tracked: " << devices.size()
        << " | max batch: " << maxBatchSize
        << " | total callbacks: " << g_receivedEvents.load()
        << " ===\n";

    if (currentMode == Mode::Pause) {
        std::cout
            << "  (paused: scanning and event collection continue)\n\n";
        return;
    }

    if (seenThisWindow.empty()) {
        std::cout << "  (no devices in this window)\n\n";
        return;
    }

    for (const auto& mac : seenThisWindow) {
        const auto it = devices.find(mac);

        if (it == devices.end()) {
            continue;
        }

        const auto& info = it->second;

        if (info.rssiHistory.empty()) {
            continue;
        }

        double sum = 0.0;

        for (int value : info.rssiHistory) {
            sum += value;
        }

        const double avg =
            sum / static_cast<double>(info.rssiHistory.size());

        const double dist = calculateDistance(avg);

        const std::string displayName =
            info.name.empty() ? "<unnamed>" : info.name;

        std::cout
            << "  "
            << std::left
            << std::setw(24)
            << displayName
            << " [" << mac << "]"
            << " RSSI: "
            << std::setw(5)
            << info.rssiHistory.back()
            << " | avg: "
            << std::fixed
            << std::setprecision(1)
            << std::setw(6)
            << avg
            << " | ~"
            << std::setprecision(2)
            << dist
            << " m\n";
    }

    std::cout << "\n";
}

// ============================================================
// MAIN
// ============================================================

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 1. Получаем Bluetooth-адаптер.

    auto adapters = SimpleBLE::Adapter::get_adapters();

    if (adapters.empty()) {
        std::cerr << "No Bluetooth adapter found.\n";
        return 1;
    }

    auto adapter = adapters[0];

    std::cout
        << "Adapter: "
        << adapter.identifier()
        << " ["
        << adapter.address()
        << "]\n";

    std::cout
        << "Scanner ID: "
        << SCANNER_ID
        << "\n\n";

    // 2. Подключаемся к MQTT.

    if (!initMqtt()) {
        std::cerr << "MQTT initialization failed. Exiting.\n";
        return 1;
    }

    // 3. Команды управления.

    std::cout
        << "Commands:\n"
        << "  q - quit\n"
        << "  p - pause MQTT/output/tracking\n"
        << "  d - detect all devices\n"
        << "  w - work mode (target filter)\n"
        << "  b - begin CSV logging\n"
        << "  e - end CSV logging\n\n";

    // 4. Регистрируем BLE callbacks.
    // В callback только помещаем событие в очередь.

    adapter.set_callback_on_scan_found(
        [](SimpleBLE::Peripheral p) {
            enqueuePacket(std::move(p));
        }
    );

    adapter.set_callback_on_scan_updated(
        [](SimpleBLE::Peripheral p) {
            enqueuePacket(std::move(p));
        }
    );

    // 5. Запускаем сканирование.

    try {
        adapter.scan_start();
    } catch (const std::exception& e) {
        std::cerr
            << "Failed to start BLE scan: "
            << e.what()
            << "\n";

        stopMqtt();
        return 1;
    }

    std::cout
        << "BLE scanning started.\n"
        << "MQTT topic: "
        << MQTT_TOPIC_PREFIX
        << SCANNER_ID
        << MQTT_TOPIC_SUFFIX
        << "\n\n";

    // 6. Основной цикл.

    int windowNo = 0;

    while (g_running.load()) {
        const auto windowStart =
            std::chrono::steady_clock::now();

        while (g_running.load()) {
            if (_kbhit()) {
                handleKey(_getch());
            }

            if (!g_running.load()) {
                break;
            }

            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - windowStart
                ).count();

            if (elapsed >= TOTAL_WINDOW_MS) {
                break;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(POLL_STEP_MS)
            );
        }

        // 7. Извлекаем накопленные события.

        std::vector<RawPacket> batch;

        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            batch.swap(g_rawPackets);
        }

        ++windowNo;

        const size_t batchSize = batch.size();

        if (batchSize > g_maxBatchSize) {
            g_maxBatchSize = batchSize;
        }

        // 8. Записываем события в CSV.

        writeBatchToCsv(batch);

        // 9. Отправляем события в MQTT.

        publishBatchToMqtt(batch);

        // 10. Обрабатываем включение/выключение CSV.

        if (g_logging.load() && !g_logFile.is_open()) {
            startLogging();
        } else if (!g_logging.load() && g_logFile.is_open()) {
            stopLogging();
        }

        if (!g_running.load()) {
            break;
        }

        // 11. Обрабатываем данные согласно режиму.

        const auto now = std::chrono::steady_clock::now();

        switch (g_mode.load()) {
            case Mode::Detect:
                detecting_func(batch, now);
                break;

            case Mode::Work:
                working_func(batch, now);
                break;

            case Mode::Pause:
                seenThisWindow.clear();
                break;
        }

        // 12. Выводим статистику.

        printReport(
            windowNo,
            batchSize,
            g_maxBatchSize
        );
    }

    // 13. Завершение работы BLE.

    try {
        adapter.scan_stop();
    } catch (const std::exception& e) {
        std::cerr
            << "Warning: failed to stop BLE scan: "
            << e.what()
            << "\n";
    }

    // 14. Отправляем последнюю партию и сохраняем CSV.

    std::vector<RawPacket> finalBatch;

    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        finalBatch.swap(g_rawPackets);
    }

    writeBatchToCsv(finalBatch);
    publishBatchToMqtt(finalBatch);

    if (g_logFile.is_open()) {
        stopLogging();
    }

    // 15. Останавливаем MQTT.


    stopMqtt();

    std::cout
        << "\nTotal callback events: "
        << g_receivedEvents.load()
        << "\n";

    std::cout << "Stopped.\n";

    return 0;
}