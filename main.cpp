#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>

extern "C" {
    #include <talloc.h>
    #include <osmocom/gsm/gsm_utils.h>
    #include <osmocom/gsm/protocol/gsm_04_08.h>
    #include <osmocom/gsm/gsm48.h>
    #include <osmocom/core/utils.h>
    #include <osmocom/core/logging.h>
    #include <osmocom/core/application.h>
    #include <osmocom/core/msgb.h>
}

// ANSI цвета (глобальные переменные для возможности отключения)
const char *COLOR_RESET     = "\033[0m";
const char *COLOR_BLUE      = "\033[1;34m";
const char *COLOR_GREEN     = "\033[1;32m";
const char *COLOR_YELLOW    = "\033[1;33m";
const char *COLOR_CYAN      = "\033[1;36m";
const char *COLOR_MAGENTA   = "\033[1;35m";

// Глобальные счетчики для SCCP
static uint32_t sccp_src_local_ref = 0x00000001;
static uint32_t sccp_dst_local_ref = 0x00000000;

// Структура конфигурации
struct Config {
    std::string imsi    = "250991234567890";
    std::string msisdn = "";  // MSISDN привязан к IMSI вручную (в реальной сети — через HLR/MAP)
    uint16_t mcc = 250;
    uint16_t mnc = 99;
    uint16_t lac = 12345;
    
    // A-interface M3UA: один NI для BSC (Reserved = 3)
    uint32_t m3ua_opc = 14001;  // OPC для A-интерфейса
    uint32_t m3ua_dpc = 14002;  // DPC для A-интерфейса
    uint8_t  m3ua_ni  = 3;      // NI=3 (Reserved) для BSC

    uint16_t cell_id = 1;
    std::string local_ip   = "0.0.0.0";
    uint16_t   local_port = 0;
    std::string remote_ip  = "127.0.0.1";
    uint16_t   remote_port = 4729;

    // C-interface: MSC ↔ HLR (MAP over SCCP/M3UA) — один NI
    std::string c_local_ip    = "0.0.0.0";
    uint16_t   c_local_port  = 0;
    std::string c_remote_ip   = "";
    uint16_t   c_remote_port = 0;
    uint32_t   c_opc         = 0;
    uint32_t   c_dpc         = 0;
    uint8_t    c_m3ua_ni     = 3;  // NI для C-интерфейса

    // E-interface: MSC ↔ MSC (междусистемный хендовер, MAP) — три NI варианта
    std::string e_local_ip    = "0.0.0.0";
    uint16_t   e_local_port  = 0;
    std::string e_remote_ip   = "";
    uint16_t   e_remote_port = 0;
    uint32_t   e_opc_ni0 = 0;  uint32_t e_dpc_ni0 = 0;
    uint32_t   e_opc_ni2 = 0;  uint32_t e_dpc_ni2 = 0;
    uint32_t   e_opc_ni3 = 0;  uint32_t e_dpc_ni3 = 0;
    uint8_t    e_m3ua_ni = 3;  // Активный NI для E-интерфейса

    // Nc-interface: MSC-S ↔ MGW (H.248/MEGACO) — три NI варианта
    std::string nc_local_ip    = "0.0.0.0";
    uint16_t   nc_local_port  = 0;
    std::string nc_remote_ip   = "";
    uint16_t   nc_remote_port = 0;
    uint32_t   nc_opc_ni0 = 0;  uint32_t nc_dpc_ni0 = 0;
    uint32_t   nc_opc_ni2 = 0;  uint32_t nc_dpc_ni2 = 0;
    uint32_t   nc_opc_ni3 = 0;  uint32_t nc_dpc_ni3 = 0;
    uint8_t    nc_m3ua_ni = 3;  // Активный NI для Nc-интерфейса

    // ISUP-interface: MSC ↔ PSTN/GW (ISUP over MTP3/M3UA) — два NI варианта
    std::string isup_local_ip   = "0.0.0.0";
    uint16_t   isup_local_port  = 0;
    std::string isup_remote_ip  = "";
    uint16_t   isup_remote_port = 0;
    uint32_t   isup_opc_ni0 = 0;  uint32_t isup_dpc_ni0 = 0;  // NI=0 International
    uint32_t   isup_opc_ni2 = 0;  uint32_t isup_dpc_ni2 = 0;  // NI=2 National
    uint8_t    isup_m3ua_ni = 2;  // Активный NI для ISUP (по умолчанию National)

    // Gs-interface: MSC ↔ SGSN (BSSAP+ over SCCP/M3UA) — NI=2 (National), NI=3 (Reserved)
    std::string gs_local_ip    = "0.0.0.0";
    uint16_t   gs_local_port   = 0;
    std::string gs_remote_ip   = "";
    uint16_t   gs_remote_port  = 0;
    uint32_t   gs_opc_ni2 = 0;  uint32_t gs_dpc_ni2 = 0;  // NI=2 National
    uint32_t   gs_opc_ni3 = 0;  uint32_t gs_dpc_ni3 = 0;  // NI=3 Reserved
    uint8_t    gs_m3ua_ni = 2;  // Активный NI для Gs (по умолчанию National)
};

// Загрузка конфигурации из файла
static bool load_config(const std::string &path, Config &cfg) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    std::string section;
    
    while (std::getline(file, line)) {
        // Убираем пробелы
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // Пропускаем пустые строки и комментарии
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        // Секция
        if (line[0] == '[' && line[line.length()-1] == ']') {
            section = line.substr(1, line.length()-2);
            continue;
        }
        
        // Параметр=Значение
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        
        // Убираем пробелы вокруг ключа и значения
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Парсим значения
        // Новый формат: интерфейсы MSC
        if (section == "subscriber") {
            if      (key == "imsi")   cfg.imsi   = value;
            else if (key == "msisdn") cfg.msisdn = value;
        } else if (section == "a-interface") {
            // Сетевые параметры
            if      (key == "mcc")    cfg.mcc    = std::stoi(value);
            else if (key == "mnc")    cfg.mnc    = std::stoi(value);
            else if (key == "lac")    cfg.lac    = std::stoi(value);
            // BSSMAP
            else if (key == "cell_id") cfg.cell_id = std::stoi(value);
            // M3UA (один NI для BSC)
            else if (key == "opc")  cfg.m3ua_opc = std::stoul(value);
            else if (key == "dpc")  cfg.m3ua_dpc = std::stoul(value);
            else if (key == "ni")   cfg.m3ua_ni  = std::stoul(value);
            // Транспорт
            else if (key == "local_ip")    cfg.local_ip    = value;
            else if (key == "local_port")  cfg.local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.remote_ip   = value;
            else if (key == "remote_port") cfg.remote_port = std::stoi(value);
        } else if (section == "c-interface") {
            if      (key == "local_ip")    cfg.c_local_ip    = value;
            else if (key == "local_port")  cfg.c_local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.c_remote_ip   = value;
            else if (key == "remote_port") cfg.c_remote_port = std::stoi(value);
            else if (key == "opc")         cfg.c_opc         = std::stoul(value);
            else if (key == "dpc")         cfg.c_dpc         = std::stoul(value);
            else if (key == "ni")          cfg.c_m3ua_ni     = std::stoul(value);
        } else if (section == "e-interface") {
            if      (key == "local_ip")    cfg.e_local_ip    = value;
            else if (key == "local_port")  cfg.e_local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.e_remote_ip   = value;
            else if (key == "remote_port") cfg.e_remote_port = std::stoi(value);
            // M3UA (три NI варианта)
            else if (key == "opc_ni0") cfg.e_opc_ni0 = std::stoul(value);
            else if (key == "dpc_ni0") cfg.e_dpc_ni0 = std::stoul(value);
            else if (key == "opc_ni2") cfg.e_opc_ni2 = std::stoul(value);
            else if (key == "dpc_ni2") cfg.e_dpc_ni2 = std::stoul(value);
            else if (key == "opc_ni3") cfg.e_opc_ni3 = std::stoul(value);
            else if (key == "dpc_ni3") cfg.e_dpc_ni3 = std::stoul(value);
            else if (key == "ni")      cfg.e_m3ua_ni  = std::stoul(value);
        } else if (section == "nc-interface") {
            if      (key == "local_ip")    cfg.nc_local_ip    = value;
            else if (key == "local_port")  cfg.nc_local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.nc_remote_ip   = value;
            else if (key == "remote_port") cfg.nc_remote_port = std::stoi(value);
            // M3UA (три NI варианта)
            else if (key == "opc_ni0") cfg.nc_opc_ni0 = std::stoul(value);
            else if (key == "dpc_ni0") cfg.nc_dpc_ni0 = std::stoul(value);
            else if (key == "opc_ni2") cfg.nc_opc_ni2 = std::stoul(value);
            else if (key == "dpc_ni2") cfg.nc_dpc_ni2 = std::stoul(value);
            else if (key == "opc_ni3") cfg.nc_opc_ni3 = std::stoul(value);
            else if (key == "dpc_ni3") cfg.nc_dpc_ni3 = std::stoul(value);
            else if (key == "ni")      cfg.nc_m3ua_ni = std::stoul(value);
        } else if (section == "isup-interface") {
            if      (key == "local_ip")    cfg.isup_local_ip    = value;
            else if (key == "local_port")  cfg.isup_local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.isup_remote_ip   = value;
            else if (key == "remote_port") cfg.isup_remote_port = std::stoi(value);
            else if (key == "opc_ni0") cfg.isup_opc_ni0 = std::stoul(value);
            else if (key == "dpc_ni0") cfg.isup_dpc_ni0 = std::stoul(value);
            else if (key == "opc_ni2") cfg.isup_opc_ni2 = std::stoul(value);
            else if (key == "dpc_ni2") cfg.isup_dpc_ni2 = std::stoul(value);
            else if (key == "ni")      cfg.isup_m3ua_ni = std::stoul(value);
        } else if (section == "gs-interface") {
            if      (key == "local_ip")    cfg.gs_local_ip    = value;
            else if (key == "local_port")  cfg.gs_local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.gs_remote_ip   = value;
            else if (key == "remote_port") cfg.gs_remote_port = std::stoi(value);
            else if (key == "opc_ni2") cfg.gs_opc_ni2 = std::stoul(value);
            else if (key == "dpc_ni2") cfg.gs_dpc_ni2 = std::stoul(value);
            else if (key == "opc_ni3") cfg.gs_opc_ni3 = std::stoul(value);
            else if (key == "dpc_ni3") cfg.gs_dpc_ni3 = std::stoul(value);
            else if (key == "ni")      cfg.gs_m3ua_ni  = std::stoul(value);
        // Обратная совместимость со старым форматом
        } else if (section == "network") {
            if      (key == "mcc") cfg.mcc = std::stoi(value);
            else if (key == "mnc") cfg.mnc = std::stoi(value);
            else if (key == "lac") cfg.lac = std::stoi(value);
        } else if (section == "m3ua") {
            // Старый формат: маппинг opc_ni3/dpc_ni3 в один opc/dpc
            if      (key == "opc_ni3") cfg.m3ua_opc = std::stoul(value);
            else if (key == "dpc_ni3") cfg.m3ua_dpc = std::stoul(value);
            else if (key == "opc_ni0") {} // игнорируем
            else if (key == "dpc_ni0") {}
            else if (key == "opc_ni2") {}
            else if (key == "dpc_ni2") {}
            else if (key == "ni")      cfg.m3ua_ni = std::stoul(value);
        } else if (section == "identity") {
            if      (key == "imsi")   cfg.imsi   = value;
            else if (key == "msisdn") cfg.msisdn = value;
        } else if (section == "bssmap") {
            if (key == "cell_id") cfg.cell_id = std::stoi(value);
        } else if (section == "transport") {
            if      (key == "local_ip")    cfg.local_ip    = value;
            else if (key == "local_port")  cfg.local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.remote_ip   = value;
            else if (key == "remote_port") cfg.remote_port = std::stoi(value);
            else if (key == "udp_host")    cfg.remote_ip   = value;
            else if (key == "udp_port")    cfg.remote_port = std::stoi(value);
        }
    }
    
    return true;
}

// Сохранение конфигурации в файл
static bool save_config(const std::string &path, const Config &cfg) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    const std::string SEP  = "# ================================================================\n";
    const std::string LINE = "# ----------------------------------------------------------------\n";

    auto sec = [&](const std::string &name, const std::string &desc) {
        file << "\n" << SEP;
        // центрируем "[name]  desc" внутри 64 символов
        std::string title = name + "  " + desc;
        int pad = (int)(64 - 2 - title.size()) / 2;
        if (pad < 0) pad = 0;
        file << "#" << std::string(pad + 1, ' ') << title << "\n";
        file << SEP;
        file << name << "\n";
    };

    file << SEP;
    file << "#              vMSC Configuration File\n";
    file << "#              Автоматически создан: " << __DATE__ << "\n";
    file << SEP;

    // --- [subscriber] ---
    sec("[subscriber]", "Абонент");
    file << "imsi="   << cfg.imsi   << "\n";
    if (!cfg.msisdn.empty())
        file << "msisdn=" << cfg.msisdn << "\n";

    // --- [a-interface] ---
    sec("[a-interface]", "MSC ↔ BSC  (GSM A-interface)");
    file << LINE;
    file << "#  Сетевая идентификация (GSM 04.08 / LAI)\n";
    file << LINE;
    file << "mcc="     << cfg.mcc     << "\n";
    file << "mnc="     << cfg.mnc     << "\n";
    file << "lac="     << cfg.lac     << "\n";
    file << LINE;
    file << "#  BSSMAP\n";
    file << LINE;
    file << "cell_id=" << cfg.cell_id << "\n";
    file << LINE;
    file << "#  M3UA  NI=" << (int)cfg.m3ua_ni << " (Reserved) — один NI для BSC\n";
    file << LINE;
    file << "opc=" << cfg.m3ua_opc << "\n";
    file << "dpc=" << cfg.m3ua_dpc << "\n";
    file << "ni="  << (int)cfg.m3ua_ni << "\n";
    file << LINE;
    file << "#  Транспорт (UDP)\n";
    file << LINE;
    file << "local_ip="    << cfg.local_ip    << "\n";
    file << "local_port="  << cfg.local_port  << "\n";
    file << "remote_ip="   << cfg.remote_ip   << "\n";
    file << "remote_port=" << cfg.remote_port << "\n";

    // --- [c-interface] ---
    sec("[c-interface]", "MSC ↔ HLR  (MAP over SCCP/M3UA)");
    file << LINE;
    file << "#  Транспорт\n";
    file << LINE;
    file << "local_ip="    << cfg.c_local_ip    << "\n";
    file << "local_port="  << cfg.c_local_port  << "\n";
    file << "remote_ip="   << cfg.c_remote_ip   << "\n";
    file << "remote_port=" << cfg.c_remote_port << "\n";
    file << LINE;
    file << "#  M3UA  NI=" << (int)cfg.c_m3ua_ni << "\n";
    file << LINE;
    file << "opc=" << cfg.c_opc      << "\n";
    file << "dpc=" << cfg.c_dpc      << "\n";
    file << "ni="  << (int)cfg.c_m3ua_ni << "\n";

    // --- [e-interface] ---
    sec("[e-interface]", "MSC ↔ MSC  (межсистемный хендовер, MAP)");
    file << LINE;
    file << "#  Транспорт\n";
    file << LINE;
    file << "local_ip="    << cfg.e_local_ip    << "\n";
    file << "local_port="  << cfg.e_local_port  << "\n";
    file << "remote_ip="   << cfg.e_remote_ip   << "\n";
    file << "remote_port=" << cfg.e_remote_port << "\n";
    file << LINE;
    file << "#  M3UA  (три NI варианта, активный NI=" << (int)cfg.e_m3ua_ni << ")\n";
    file << LINE;
    file << "#  NI=0  International\n";
    file << "opc_ni0=" << cfg.e_opc_ni0 << "\n";
    file << "dpc_ni0=" << cfg.e_dpc_ni0 << "\n";
    file << "ni=0\n";
    file << "#  NI=2  National\n";
    file << "opc_ni2=" << cfg.e_opc_ni2 << "\n";
    file << "dpc_ni2=" << cfg.e_dpc_ni2 << "\n";
    file << "ni=2\n";
    file << "#  NI=3  Reserved\n";
    file << "opc_ni3=" << cfg.e_opc_ni3 << "\n";
    file << "dpc_ni3=" << cfg.e_dpc_ni3 << "\n";
    file << "#  Активный NI\n";
    file << "ni=" << (int)cfg.e_m3ua_ni << "\n";

    // --- [nc-interface] ---
    sec("[nc-interface]", "MSC-S ↔ MGW  (H.248/MEGACO)");
    file << LINE;
    file << "#  Транспорт\n";
    file << LINE;
    file << "local_ip="    << cfg.nc_local_ip    << "\n";
    file << "local_port="  << cfg.nc_local_port  << "\n";
    file << "remote_ip="   << cfg.nc_remote_ip   << "\n";
    file << "remote_port=" << cfg.nc_remote_port << "\n";
    file << LINE;
    file << "#  M3UA  (три NI варианта, активный NI=" << (int)cfg.nc_m3ua_ni << ")\n";
    file << LINE;
    file << "#  NI=0  International\n";
    file << "opc_ni0=" << cfg.nc_opc_ni0 << "\n";
    file << "dpc_ni0=" << cfg.nc_dpc_ni0 << "\n";
    file << "ni=0\n";
    file << "#  NI=2  National\n";
    file << "opc_ni2=" << cfg.nc_opc_ni2 << "\n";
    file << "dpc_ni2=" << cfg.nc_dpc_ni2 << "\n";
    file << "ni=2\n";
    file << "#  NI=3  Reserved\n";
    file << "opc_ni3=" << cfg.nc_opc_ni3 << "\n";
    file << "dpc_ni3=" << cfg.nc_dpc_ni3 << "\n";
    file << "#  Активный NI\n";
    file << "ni=" << (int)cfg.nc_m3ua_ni << "\n";

    // --- [isup-interface] ---
    sec("[isup-interface]", "MSC ↔ PSTN/GW  (ISUP over MTP3/M3UA)");
    file << LINE;
    file << "#  Транспорт\n";
    file << LINE;
    file << "local_ip="    << cfg.isup_local_ip    << "\n";
    file << "local_port="  << cfg.isup_local_port  << "\n";
    file << "remote_ip="   << cfg.isup_remote_ip   << "\n";
    file << "remote_port=" << cfg.isup_remote_port << "\n";
    file << LINE;
    file << "#  MTP3/M3UA  SI=5 (ISUP), активный NI=" << (int)cfg.isup_m3ua_ni << "\n";
    file << LINE;
    file << "#  NI=0  International\n";
    file << "opc_ni0=" << cfg.isup_opc_ni0 << "\n";
    file << "dpc_ni0=" << cfg.isup_dpc_ni0 << "\n";
    file << "ni=0\n";
    file << "#  NI=2  National\n";
    file << "opc_ni2=" << cfg.isup_opc_ni2 << "\n";
    file << "dpc_ni2=" << cfg.isup_dpc_ni2 << "\n";
    file << "#  Активный NI\n";
    file << "ni=" << (int)cfg.isup_m3ua_ni << "\n";

    // --- [gs-interface] ---
    sec("[gs-interface]", "MSC ↔ SGSN  (BSSAP+ over SCCP/M3UA)");
    file << LINE;
    file << "#  Транспорт\n";
    file << LINE;
    file << "local_ip="    << cfg.gs_local_ip    << "\n";
    file << "local_port="  << cfg.gs_local_port  << "\n";
    file << "remote_ip="   << cfg.gs_remote_ip   << "\n";
    file << "remote_port=" << cfg.gs_remote_port << "\n";
    file << LINE;
    file << "#  SCCP/M3UA  SI=3 (SCCP), активный NI=" << (int)cfg.gs_m3ua_ni << "\n";
    file << LINE;
    file << "#  NI=2  National\n";
    file << "opc_ni2=" << cfg.gs_opc_ni2 << "\n";
    file << "dpc_ni2=" << cfg.gs_dpc_ni2 << "\n";
    file << "ni=2\n";
    file << "#  NI=3  Reserved\n";
    file << "opc_ni3=" << cfg.gs_opc_ni3 << "\n";
    file << "dpc_ni3=" << cfg.gs_dpc_ni3 << "\n";
    file << "#  Активный NI\n";
    file << "ni=" << (int)cfg.gs_m3ua_ni << "\n";

    file << "\n" << SEP;

    return true;
}

// Получение пути к конфигу по умолчанию
static std::string get_default_config_path() {
    // Приоритет: текущая директория, затем домашняя
    return "./vmsc.conf";
}

// Оборачивание SCCP в M3UA DATA message (SIGTRAN)
static struct msgb *wrap_in_m3ua(struct msgb *sccp_msg, uint32_t opc, uint32_t dpc, uint8_t ni) {
    if (!sccp_msg) return nullptr;

    struct msgb *m3ua_msg = msgb_alloc_headroom(512, 128, "M3UA DATA");
    if (!m3ua_msg) return nullptr;

    // M3UA Common Header (8 bytes)
    *(msgb_put(m3ua_msg, 1)) = 0x01;  // Version
    *(msgb_put(m3ua_msg, 1)) = 0x00;  // Reserved
    *(msgb_put(m3ua_msg, 1)) = 0x01;  // Message Class: Transfer Messages
    *(msgb_put(m3ua_msg, 1)) = 0x01;  // Message Type: DATA
    
    // Message Length (заполним позже)
    uint8_t *len_ptr = msgb_put(m3ua_msg, 4);
    
    // Protocol Data Parameter
    uint16_t proto_data_len = sccp_msg->len + 16;  // 16 = header (4) + OPC (4) + DPC (4) + SI/NI/MP/SLS (4)
    uint16_t param_len = proto_data_len + 4;  // +4 для Tag и Length
    
    // Protocol Data Tag (0x0210)
    *(msgb_put(m3ua_msg, 1)) = 0x02;
    *(msgb_put(m3ua_msg, 1)) = 0x10;
    
    // Protocol Data Length
    *(msgb_put(m3ua_msg, 1)) = (param_len >> 8) & 0xFF;
    *(msgb_put(m3ua_msg, 1)) = param_len & 0xFF;
    
    // OPC (Originating Point Code) - 4 bytes
    *(msgb_put(m3ua_msg, 1)) = (opc >> 24) & 0xFF;
    *(msgb_put(m3ua_msg, 1)) = (opc >> 16) & 0xFF;
    *(msgb_put(m3ua_msg, 1)) = (opc >> 8) & 0xFF;
    *(msgb_put(m3ua_msg, 1)) = opc & 0xFF;
    
    // DPC (Destination Point Code) - 4 bytes
    *(msgb_put(m3ua_msg, 1)) = (dpc >> 24) & 0xFF;
    *(msgb_put(m3ua_msg, 1)) = (dpc >> 16) & 0xFF;
    *(msgb_put(m3ua_msg, 1)) = (dpc >> 8) & 0xFF;
    *(msgb_put(m3ua_msg, 1)) = dpc & 0xFF;
    
    // SI (Service Indicator) = 0x03 (SCCP)
    *(msgb_put(m3ua_msg, 1)) = 0x03;
    
    // NI (Network Indicator)
    *(msgb_put(m3ua_msg, 1)) = ni;
    
    // MP (Message Priority) = 0x00
    *(msgb_put(m3ua_msg, 1)) = 0x00;
    
    // SLS (Signalling Link Selection) = 0x00
    *(msgb_put(m3ua_msg, 1)) = 0x00;
    
    // Копируем SCCP данные
    memcpy(msgb_put(m3ua_msg, sccp_msg->len), sccp_msg->data, sccp_msg->len);
    
    // Padding для выравнивания на 4 байта
    uint8_t padding = (4 - (sccp_msg->len % 4)) % 4;
    for (uint8_t i = 0; i < padding; i++) {
        *(msgb_put(m3ua_msg, 1)) = 0x00;
    }
    
    // Заполняем Message Length в заголовке
    uint32_t total_len = m3ua_msg->len;
    len_ptr[0] = (total_len >> 24) & 0xFF;
    len_ptr[1] = (total_len >> 16) & 0xFF;
    len_ptr[2] = (total_len >> 8) & 0xFF;
    len_ptr[3] = total_len & 0xFF;

    std::cout << COLOR_CYAN << "✓ SCCP обернуто в M3UA DATA message" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OPC: " << COLOR_GREEN << opc << COLOR_RESET 
              << COLOR_BLUE << "   DPC: " << COLOR_GREEN << dpc << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SI: " << COLOR_GREEN << "3 (SCCP)" << COLOR_RESET 
              << COLOR_BLUE << "   NI: " << COLOR_GREEN << (int)ni;
    if (ni == 0) std::cout << " (International)";
    else if (ni == 2) std::cout << " (National)";
    else if (ni == 3) std::cout << " (Reserved)";
    std::cout << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  M3UA размер: " << COLOR_GREEN << m3ua_msg->len << " байт" 
              << COLOR_RESET << " (SCCP: " << sccp_msg->len << " байт)\n\n";

    return m3ua_msg;
}

// Оборачивание BSSAP в SCCP Connection Request (CR)
static struct msgb *wrap_in_sccp_cr(struct msgb *bssap_msg) {
    if (!bssap_msg) return nullptr;

    struct msgb *sccp_msg = msgb_alloc_headroom(512, 128, "SCCP CR");
    if (!sccp_msg) return nullptr;

    // SCCP Connection Request (CR):
    // Message Type: 0x01 (CR)
    // Source Local Reference: 3 bytes
    // Protocol Class: 1 byte (0x02 - Class 2, connection-oriented)
    // Called Party Address (Variable Length)
    // Data (Variable Length) - содержит BSSAP

    *(msgb_put(sccp_msg, 1)) = 0x01;  // SCCP CR Message Type
    
    // Source Local Reference (3 bytes, little-endian)
    *(msgb_put(sccp_msg, 1)) = (sccp_src_local_ref >> 0) & 0xFF;
    *(msgb_put(sccp_msg, 1)) = (sccp_src_local_ref >> 8) & 0xFF;
    *(msgb_put(sccp_msg, 1)) = (sccp_src_local_ref >> 16) & 0xFF;
    
    *(msgb_put(sccp_msg, 1)) = 0x02;  // Protocol Class 2
    
    // Called Party Address (упрощенная версия)
    *(msgb_put(sccp_msg, 1)) = 0x03;  // Pointer to Called Party Address (3 bytes offset)
    *(msgb_put(sccp_msg, 1)) = 0x05 + bssap_msg->len;  // Pointer to Data
    
    // Called Party Address Length и данные (минимальный SSN)
    *(msgb_put(sccp_msg, 1)) = 0x02;  // Length
    *(msgb_put(sccp_msg, 1)) = 0x42;  // Address Indicator (SSN present, no PC)
    *(msgb_put(sccp_msg, 1)) = 0xFE;  // SSN = 254 (MSC)
    
    // Data Parameter
    *(msgb_put(sccp_msg, 1)) = bssap_msg->len;  // Data Length
    memcpy(msgb_put(sccp_msg, bssap_msg->len), bssap_msg->data, bssap_msg->len);

    std::cout << COLOR_CYAN << "✓ BSSAP обернуто в SCCP Connection Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Source Local Ref: " << COLOR_GREEN << "0x" << std::hex 
              << sccp_src_local_ref << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SCCP размер: " << COLOR_GREEN << sccp_msg->len << " байт" 
              << COLOR_RESET << " (BSSAP: " << bssap_msg->len << " байт)\n\n";

    sccp_src_local_ref++;  // Инкрементируем для следующего соединения
    return sccp_msg;
}

// Оборачивание BSSAP в SCCP Data Transfer (DT1) - для уже установленного соединения
static struct msgb *wrap_in_sccp_dt1(struct msgb *bssap_msg, uint32_t dst_ref) {
    if (!bssap_msg) return nullptr;

    struct msgb *sccp_msg = msgb_alloc_headroom(512, 128, "SCCP DT1");
    if (!sccp_msg) return nullptr;

    // SCCP Data Form 1 (DT1):
    // Message Type: 0x06 (DT1)
    // Destination Local Reference: 3 bytes
    // Segmenting/Reassembling: 1 byte
    // Data Length: 1 byte
    // Data: variable

    *(msgb_put(sccp_msg, 1)) = 0x06;  // SCCP DT1 Message Type
    
    // Destination Local Reference (3 bytes)
    *(msgb_put(sccp_msg, 1)) = (dst_ref >> 0) & 0xFF;
    *(msgb_put(sccp_msg, 1)) = (dst_ref >> 8) & 0xFF;
    *(msgb_put(sccp_msg, 1)) = (dst_ref >> 16) & 0xFF;
    
    *(msgb_put(sccp_msg, 1)) = 0x00;  // Segmenting/Reassembling (no segmentation)
    *(msgb_put(sccp_msg, 1)) = bssap_msg->len;  // Data Length
    
    memcpy(msgb_put(sccp_msg, bssap_msg->len), bssap_msg->data, bssap_msg->len);

    std::cout << COLOR_CYAN << "✓ BSSAP обернуто в SCCP Data Transfer (DT1)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Dest Local Ref: " << COLOR_GREEN << "0x" << std::hex 
              << dst_ref << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SCCP размер: " << COLOR_GREEN << sccp_msg->len << " байт" 
              << COLOR_RESET << " (BSSAP: " << bssap_msg->len << " байт)\n\n";

    return sccp_msg;
}

// Оборачивание GSM 04.08 в BSSMAP Complete Layer 3 Information
static struct msgb *wrap_in_bssmap_complete_l3(struct msgb *l3_msg, uint16_t cell_id, uint16_t lac) {
    if (!l3_msg) return nullptr;

    struct msgb *bssmap_msg = msgb_alloc_headroom(512, 128, "BSSMAP Complete L3");
    if (!bssmap_msg) return nullptr;

    // BSSMAP Complete Layer 3 Information:
    // 0x00 - BSSAP discriminator (BSSMAP)
    // Length (1 byte)
    // 0x57 - Message Type (Complete Layer 3 Information)
    // Cell Identifier (IE)
    // Layer 3 Information (IE)
    
    *(msgb_put(bssmap_msg, 1)) = 0x00;  // BSSAP BSSMAP discriminator
    
    // Length (заполним позже)
    uint8_t *len_ptr = msgb_put(bssmap_msg, 1);
    
    *(msgb_put(bssmap_msg, 1)) = 0x57;  // Complete Layer 3 Information
    
    // Cell Identifier IE (Tag-Length-Value)
    *(msgb_put(bssmap_msg, 1)) = 0x05;  // Cell Identifier IE tag
    *(msgb_put(bssmap_msg, 1)) = 0x08;  // Length (8 bytes)
    *(msgb_put(bssmap_msg, 1)) = 0x01;  // Cell ID discriminator (whole CGI)
    
    // MCC/MNC/LAC/CI (упрощенная версия)
    *(msgb_put(bssmap_msg, 1)) = 0x52;  // MCC digit 2,1 (250 -> 52)
    *(msgb_put(bssmap_msg, 1)) = 0xf0;  // MCC digit 3, filler
    *(msgb_put(bssmap_msg, 1)) = 0x99;  // MNC digit 2,1 (99)
    *(msgb_put(bssmap_msg, 1)) = (lac >> 8) & 0xFF;  // LAC high byte
    *(msgb_put(bssmap_msg, 1)) = lac & 0xFF;  // LAC low byte
    *(msgb_put(bssmap_msg, 1)) = (cell_id >> 8) & 0xFF;  // CI high byte
    *(msgb_put(bssmap_msg, 1)) = cell_id & 0xFF;  // CI low byte
    
    // Layer 3 Information IE
    *(msgb_put(bssmap_msg, 1)) = 0x15;  // Layer 3 Information IE tag
    *(msgb_put(bssmap_msg, 1)) = l3_msg->len;  // Length
    memcpy(msgb_put(bssmap_msg, l3_msg->len), l3_msg->data, l3_msg->len);
    
    // Заполняем Length в заголовке
    *len_ptr = bssmap_msg->len - 2;  // Длина без discriminator и length field

    std::cout << COLOR_CYAN << "✓ GSM 04.08 обернуто в BSSMAP Complete Layer 3 Information" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cell ID: " << COLOR_GREEN << cell_id << COLOR_RESET 
              << COLOR_BLUE << "   LAC: " << COLOR_GREEN << lac << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  BSSMAP размер: " << COLOR_GREEN << bssmap_msg->len << " байт" 
              << COLOR_RESET << " (GSM 04.08: " << l3_msg->len << " байт)\n\n";

    return bssmap_msg;
}

// Генерация BSSMAP Clear Command
static struct msgb *generate_bssmap_clear_command(uint8_t cause) {
    struct msgb *bssmap_msg = msgb_alloc_headroom(512, 128, "BSSMAP Clear Command");
    if (!bssmap_msg) return nullptr;

    // BSSMAP Clear Command:
    // 0x00 - BSSAP discriminator (BSSMAP)
    // Length (1 byte)
    // 0x20 - Message Type (Clear Command)
    // Cause (IE)
    
    *(msgb_put(bssmap_msg, 1)) = 0x00;  // BSSAP BSSMAP discriminator
    *(msgb_put(bssmap_msg, 1)) = 0x04;  // Length (4 bytes after this field)
    *(msgb_put(bssmap_msg, 1)) = 0x20;  // Clear Command
    
    // Cause IE
    *(msgb_put(bssmap_msg, 1)) = 0x04;  // Cause IE tag
    *(msgb_put(bssmap_msg, 1)) = 0x01;  // Length (1 byte)
    *(msgb_put(bssmap_msg, 1)) = cause;  // Cause value

    std::cout << COLOR_CYAN << "✓ Создано BSSMAP Clear Command" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << "0x" << std::hex << (int)cause 
              << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  BSSMAP размер: " << COLOR_GREEN << bssmap_msg->len 
              << " байт" << COLOR_RESET << "\n\n";

    return bssmap_msg;
}

// Оборачивание GSM 04.08 в BSSAP DTAP (Direct Transfer Application Part)
static struct msgb *wrap_in_bssap_dtap(struct msgb *l3_msg) {
    if (!l3_msg) return nullptr;

    // Выделяем новый msgb с достаточным headroom и tailroom
    struct msgb *bssap_msg = msgb_alloc_headroom(512, 128, "BSSAP DTAP");
    if (!bssap_msg) return nullptr;

    // BSSAP заголовок для DTAP:
    // 0x00 - BSSAP discriminator (BSSMAP/DTAP)
    // Length (1 byte)
    // 0x01 - DTAP DLCI (Data Link Connection Identifier)
    // Length of L3 info (1 byte)
    // L3 data
    
    *(msgb_put(bssap_msg, 1)) = 0x00;  // BSSAP DTAP discriminator
    *(msgb_put(bssap_msg, 1)) = l3_msg->len + 2;  // Length (включая DLCI и L3 length)
    *(msgb_put(bssap_msg, 1)) = 0x01;  // DLCI
    *(msgb_put(bssap_msg, 1)) = l3_msg->len;  // L3 information length
    
    // Копируем GSM 04.08 данные
    memcpy(msgb_put(bssap_msg, l3_msg->len), l3_msg->data, l3_msg->len);

    std::cout << COLOR_CYAN << "✓ GSM 04.08 обернуто в BSSAP DTAP" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  BSSAP размер: " << COLOR_GREEN << bssap_msg->len << " байт" 
              << COLOR_RESET << " (GSM 04.08: " << l3_msg->len << " байт)\n\n";

    return bssap_msg;
}

// Отправка сообщения по UDP
static bool send_message_udp(const uint8_t *data, size_t len, const char *host, uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << COLOR_YELLOW << "Ошибка создания UDP сокета" << COLOR_RESET << "\n";
        return false;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &dest_addr.sin_addr) <= 0) {
        std::cerr << COLOR_YELLOW << "Неверный адрес: " << host << COLOR_RESET << "\n";
        close(sock);
        return false;
    }

    ssize_t sent = sendto(sock, data, len, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    close(sock);

    if (sent < 0) {
        std::cerr << COLOR_YELLOW << "Ошибка отправки UDP пакета" << COLOR_RESET << "\n";
        return false;
    }

    std::cout << COLOR_GREEN << "✓ Отправлено по UDP на " << host << ":" << port 
              << " (" << sent << " байт)" << COLOR_RESET << "\n\n";
    return true;
}

// Простая функция для генерации BCD из строки номера
static int generate_bcd_number(uint8_t *bcd, size_t max_len, const char *number) {
    size_t len = strlen(number);
    size_t bcd_len = (len + 1) / 2;
    if (bcd_len > max_len) return -1;

    memset(bcd, 0, max_len);

    for (size_t i = 0; i < len; ++i) {
        char digit = number[i] - '0';
        if (digit < 0 || digit > 9) return -1;
        if (i % 2 == 0) {
            bcd[i/2] = digit;
        } else {
            bcd[i/2] |= (digit << 4);
        }
    }
    if (len % 2 == 1) {
        bcd[bcd_len - 1] |= 0xF0;
    }
    return bcd_len;
}

// Простая BCD → строка IMSI
static std::string bcd_to_string(const uint8_t *bcd, size_t len) {
    std::string s;
    for (size_t i = 0; i < len; ++i) {
        uint8_t nibble1 = bcd[i] & 0x0F;
        uint8_t nibble2 = (bcd[i] >> 4) & 0x0F;
        if (nibble1 <= 9) s += '0' + nibble1;
        if (nibble2 <= 9) s += '0' + nibble2;
    }
    return s;
}

// Разбор и красивый цветной вывод
static void parse_and_print_gsm48_msg(struct msgb *msg, const char *msg_name) {
    if (!msg || msg->len < 2) {
        std::cout << COLOR_YELLOW << msg_name << ": сообщение слишком короткое или пустое" << COLOR_RESET << "\n";
        return;
    }

    uint8_t *data = msg->data;
    uint8_t pd = data[0] & 0x0F;
    uint8_t mt = data[1];

    std::cout << "\n" << COLOR_CYAN << "=== " << msg_name << " (len = " << msg->len << " байт) ===" << COLOR_RESET << "\n";
    
    std::cout << COLOR_YELLOW << "Raw hex (для копирования в Wireshark):" << COLOR_RESET << "\n";
    std::cout << "    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";

    std::cout << COLOR_BLUE << "  Protocol Discriminator: " << COLOR_RESET << "0x" << std::hex << (int)pd << " (";
    if (pd == 0x05) std::cout << COLOR_GREEN << "MM (Mobility Management)" << COLOR_RESET;
    else if (pd == 0x06) std::cout << COLOR_GREEN << "RR (Radio Resource)" << COLOR_RESET;
    else std::cout << COLOR_YELLOW << "Unknown" << COLOR_RESET;
    std::cout << ")\n";

    std::cout << COLOR_BLUE << "  Message Type: " << COLOR_RESET << "0x" << std::hex << (int)mt << " (";
    if (pd == 0x05) {
        if (mt == 0x08) std::cout << COLOR_GREEN << "Location Updating Request" << COLOR_RESET;
        else if (mt == 0x27) std::cout << COLOR_GREEN << "Paging Response" << COLOR_RESET;
        else std::cout << COLOR_YELLOW << "Unknown MM message" << COLOR_RESET;
    } else {
        std::cout << COLOR_YELLOW << "Unknown" << COLOR_RESET;
    }
    std::cout << ")\n";

    if (msg->len < 3) return;

    uint8_t cksn_lu = data[2];
    uint8_t cksn = (cksn_lu >> 4) & 0x07;
    uint8_t lu_type = cksn_lu & 0x0F;

    std::cout << COLOR_BLUE << "  Ciphering Key Sequence Number: " << COLOR_RESET << (int)cksn << " (";
    if (cksn == 7) std::cout << COLOR_GREEN << "no key available" << COLOR_RESET;
    else std::cout << COLOR_GREEN << "key " << (int)cksn << COLOR_RESET;
    std::cout << ")\n";

    if (pd == 0x05 && mt == 0x08) {
        std::cout << COLOR_BLUE << "  Location Updating Type: " << COLOR_RESET << (int)lu_type << " (";
        if (lu_type == 0) std::cout << COLOR_GREEN << "normal location updating" << COLOR_RESET;
        else if (lu_type == 1) std::cout << COLOR_GREEN << "periodic updating" << COLOR_RESET;
        else if (lu_type == 3) std::cout << COLOR_GREEN << "IMSI attach" << COLOR_RESET;
        else std::cout << COLOR_YELLOW << "reserved" << COLOR_RESET;
        std::cout << ")\n";

        if (msg->len < 9) return;

        // Правильный разбор LAI
        uint8_t *lai = data + 3;
        uint16_t mcc = ((lai[0] & 0x0F) * 100) + ((lai[0] >> 4) & 0x0F) * 10 + (lai[1] & 0x0F);
        uint16_t mnc = ((lai[2] & 0x0F) * 10) + ((lai[2] >> 4) & 0x0F);
        if ((lai[1] >> 4) != 0xF) {
            mnc = mnc * 10 + ((lai[1] >> 4) & 0x0F);
        }
        uint16_t lac = (lai[3] << 8) | lai[4];

        std::cout << COLOR_BLUE << "  Location Area Identification:\n" << COLOR_RESET;
        std::cout << "    MCC: " << COLOR_GREEN << std::dec << mcc << COLOR_RESET << "\n";
        std::cout << "    MNC: " << COLOR_GREEN << std::dec << mnc << COLOR_RESET << "\n";
        std::cout << "    LAC: " << COLOR_GREEN << std::dec << lac << COLOR_RESET << " (0x" << std::hex << lac << COLOR_RESET << ")\n";
    }

    // Mobile Identity
    uint8_t *mi_ptr = data + (mt == 0x08 ? 9 : 3);
    if (msg->len < (mi_ptr - data + 1)) return;

    uint8_t mi_len = mi_ptr[0];
    uint8_t mi_type = mi_ptr[1] >> 4;
    bool odd_digits = (mi_ptr[1] & 0x08) != 0;

    std::cout << COLOR_BLUE << "  Mobile Identity:\n" << COLOR_RESET;
    std::cout << "    Length: " << (int)mi_len << "\n";
    std::cout << "    Type: ";
    if (mi_type == 0x01) std::cout << COLOR_GREEN << "IMSI" << COLOR_RESET;
    else if (mi_type == 0x00) std::cout << COLOR_GREEN << "TMSI/P-TMSI" << COLOR_RESET;
    else std::cout << COLOR_YELLOW << "Reserved (" << (int)mi_type << ")" << COLOR_RESET;
    std::cout << "\n";

    if (mi_type == 0x01 && mi_len >= 2) {
        std::string imsi = bcd_to_string(mi_ptr + 2, mi_len - 1);
        std::cout << "    IMSI: " << COLOR_GREEN << imsi << COLOR_RESET;
        if (odd_digits) std::cout << COLOR_YELLOW << " (odd digits)" << COLOR_RESET;
        std::cout << "\n";
    } else if (mi_type == 0x00 && mi_len >= 5) {
        uint32_t tmsi = (mi_ptr[2] << 24) | (mi_ptr[3] << 16) | (mi_ptr[4] << 8) | mi_ptr[5];
        std::cout << "    TMSI: " << COLOR_GREEN << "0x" << std::hex << tmsi << COLOR_RESET << "\n";
    }

    std::cout << "\n";
}

static struct msgb *generate_location_update_request(const char *imsi_str, uint16_t mcc, uint16_t mnc, uint16_t lac) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "LU Request");
    if (!msg) return nullptr;

    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = GSM48_MT_MM_LOC_UPD_REQUEST;
    *(msgb_put(msg, 1)) = (0x07 << 4) | 0x00;

    uint8_t lai[6];
    // MCC: digit1 (low), digit2 (high) в lai[0]; digit3 (low) в lai[1]
    lai[0] = (mcc / 100) | ((mcc / 10 % 10) << 4);
    lai[1] = (mcc % 10) | (0xF << 4);  // digit3 + fill для 2-значного MNC
    // MNC: digit1 (low), digit2 (high) в lai[2]
    lai[2] = (mnc / 10 % 10) | ((mnc % 10) << 4);
    lai[3] = (lac >> 8) & 0xFF;
    lai[4] = lac & 0xFF;
    lai[5] = 0x00;
    memcpy(msgb_put(msg, 6), lai, 6);

    uint8_t bcd[9];
    int imsi_len = generate_bcd_number(bcd + 1, sizeof(bcd) - 1, imsi_str);
    if (imsi_len <= 0) {
        msgb_free(msg);
        return nullptr;
    }

    uint8_t mi_len = imsi_len + 1;
    uint8_t *mi = msgb_put(msg, mi_len + 1);
    mi[0] = mi_len;
    mi[1] = (0x01 << 4) | 0x01;  // IMSI + odd
    memcpy(mi + 2, bcd + 1, imsi_len);

    std::cout << "Сгенерировано Location Update Request (raw hex):\n";
    osmo_hexdump(msg->data, msg->len);

    return msg;
}

static struct msgb *generate_paging_response(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "Paging Response");
    if (!msg) return nullptr;

    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_PAG_RESP;
    *(msgb_put(msg, 1)) = (0x07 << 4) | 0x00;

    uint8_t bcd[9];
    int imsi_len = generate_bcd_number(bcd + 1, sizeof(bcd) - 1, imsi_str);
    if (imsi_len <= 0) {
        msgb_free(msg);
        return nullptr;
    }

    uint8_t mi_len = imsi_len + 1;
    uint8_t *mi = msgb_put(msg, mi_len + 1);
    mi[0] = mi_len;
    mi[1] = (0x01 << 4) | 0x01;
    memcpy(mi + 2, bcd + 1, imsi_len);

    std::cout << "Сгенерировано Paging Response (raw hex):\n";
    osmo_hexdump(msg->data, msg->len);

    return msg;
}

int main(int argc, char** argv) {
    void *ctx = talloc_named_const(NULL, 0, "vmsc_context");

    struct log_info info = {};
    int rc = osmo_init_logging2(ctx, &info);
    if (rc < 0) {
        std::cerr << "Ошибка инициализации лога\n";
        talloc_free(ctx);
        return 1;
    }

    struct log_target *tgt = log_target_find(LOG_TGT_TYPE_STDERR, NULL);
    if (tgt) {
        log_set_print_filename2(tgt, LOG_FILENAME_NONE);
        log_set_use_color(tgt, 1);
    }

    log_set_category_filter(tgt, DLGLOBAL, 1, LOGL_INFO);

    // Загружаем конфигурацию по умолчанию
    Config cfg;
    std::string config_path = get_default_config_path();
    bool config_loaded = false;
    
    // Приоритет поиска конфигурации:
    // 1. ./vmsc.conf (текущая директория)
    // 2. ../vmsc.conf (родительская директория, если запуск из build/)
    // 3. ~/.vmsc.conf (домашняя директория)
    
    if (load_config("./vmsc.conf", cfg)) {
        config_loaded = true;
        config_path = "./vmsc.conf";
        std::cout << COLOR_GREEN << "✓ Конфигурация загружена из ./vmsc.conf" << COLOR_RESET << "\n\n";
    } else if (load_config("../vmsc.conf", cfg)) {
        config_loaded = true;
        config_path = "../vmsc.conf";
        std::cout << COLOR_GREEN << "✓ Конфигурация загружена из ../vmsc.conf" << COLOR_RESET << "\n\n";
    } else {
        const char *home = getenv("HOME");
        if (home) {
            std::string home_config = std::string(home) + "/.vmsc.conf";
            if (load_config(home_config, cfg)) {
                config_loaded = true;
                config_path = home_config;
                std::cout << COLOR_GREEN << "✓ Конфигурация загружена из " << config_path << COLOR_RESET << "\n\n";
            }
        }
    }

    std::string imsi   = cfg.imsi;
    std::string msisdn = cfg.msisdn;  // MSISDN: номер абонента, привязан к IMSI
    uint16_t mcc = cfg.mcc;
    uint16_t mnc = cfg.mnc;
    uint16_t lac = cfg.lac;
    bool do_lu = true;
    bool do_paging = true;
    bool color = true;
    bool send_udp = false;
    // A-interface transport
    std::string local_ip   = cfg.local_ip;
    uint16_t   local_port = cfg.local_port;
    std::string remote_ip  = cfg.remote_ip;
    uint16_t   remote_port = cfg.remote_port;

    // C-interface: MSC ↔ HLR (один NI)
    std::string c_local_ip    = cfg.c_local_ip;
    uint16_t   c_local_port  = cfg.c_local_port;
    std::string c_remote_ip   = cfg.c_remote_ip;
    uint16_t   c_remote_port = cfg.c_remote_port;
    uint32_t   c_opc         = cfg.c_opc;
    uint32_t   c_dpc         = cfg.c_dpc;
    uint8_t    c_m3ua_ni     = cfg.c_m3ua_ni;

    // E-interface: MSC ↔ MSC (три NI варианта)
    std::string e_local_ip    = cfg.e_local_ip;
    uint16_t   e_local_port  = cfg.e_local_port;
    std::string e_remote_ip   = cfg.e_remote_ip;
    uint16_t   e_remote_port = cfg.e_remote_port;
    uint32_t   e_opc_ni0 = cfg.e_opc_ni0;  uint32_t e_dpc_ni0 = cfg.e_dpc_ni0;
    uint32_t   e_opc_ni2 = cfg.e_opc_ni2;  uint32_t e_dpc_ni2 = cfg.e_dpc_ni2;
    uint32_t   e_opc_ni3 = cfg.e_opc_ni3;  uint32_t e_dpc_ni3 = cfg.e_dpc_ni3;
    uint8_t    e_m3ua_ni = cfg.e_m3ua_ni;

    // Nc-interface: MSC-S ↔ MGW
    std::string nc_local_ip    = cfg.nc_local_ip;
    uint16_t   nc_local_port  = cfg.nc_local_port;
    std::string nc_remote_ip   = cfg.nc_remote_ip;
    uint16_t   nc_remote_port = cfg.nc_remote_port;
    bool use_bssap = true;  // По умолчанию оборачиваем в BSSAP
    bool use_sccp = false;  // SCCP обертка (опционально)
    bool use_sccp_dt1 = false;  // Использовать DT1 вместо CR
    uint32_t sccp_dst_ref = 0x00000001;  // Destination Local Reference для DT1
    bool use_m3ua = false;  // M3UA обертка (SIGTRAN)

    // ISUP-interface: MSC ↔ PSTN/GW
    std::string isup_local_ip   = cfg.isup_local_ip;
    uint16_t   isup_local_port  = cfg.isup_local_port;
    std::string isup_remote_ip  = cfg.isup_remote_ip;
    uint16_t   isup_remote_port = cfg.isup_remote_port;
    uint32_t   isup_opc_ni0 = cfg.isup_opc_ni0;  uint32_t isup_dpc_ni0 = cfg.isup_dpc_ni0;
    uint32_t   isup_opc_ni2 = cfg.isup_opc_ni2;  uint32_t isup_dpc_ni2 = cfg.isup_dpc_ni2;
    uint8_t    isup_m3ua_ni = cfg.isup_m3ua_ni;

    // Gs-interface: MSC ↔ SGSN
    std::string gs_local_ip    = cfg.gs_local_ip;
    uint16_t   gs_local_port   = cfg.gs_local_port;
    std::string gs_remote_ip   = cfg.gs_remote_ip;
    uint16_t   gs_remote_port  = cfg.gs_remote_port;
    uint32_t   gs_opc_ni2 = cfg.gs_opc_ni2;  uint32_t gs_dpc_ni2 = cfg.gs_dpc_ni2;
    uint32_t   gs_opc_ni3 = cfg.gs_opc_ni3;  uint32_t gs_dpc_ni3 = cfg.gs_dpc_ni3;
    uint8_t    gs_m3ua_ni = cfg.gs_m3ua_ni;
    
    // A-interface M3UA: один NI для BSC (Reserved = 3)
    uint32_t m3ua_opc = cfg.m3ua_opc;
    uint32_t m3ua_dpc = cfg.m3ua_dpc;
    uint8_t  m3ua_ni  = cfg.m3ua_ni;
    // Nc-interface M3UA: три NI варианта
    uint32_t nc_opc_ni0 = cfg.nc_opc_ni0;  uint32_t nc_dpc_ni0 = cfg.nc_dpc_ni0;
    uint32_t nc_opc_ni2 = cfg.nc_opc_ni2;  uint32_t nc_dpc_ni2 = cfg.nc_dpc_ni2;
    uint32_t nc_opc_ni3 = cfg.nc_opc_ni3;  uint32_t nc_dpc_ni3 = cfg.nc_dpc_ni3;
    uint8_t  nc_m3ua_ni = cfg.nc_m3ua_ni;  // Активный NI для Nc
    
    bool use_sctp = false;  // Использовать SCTP вместо UDP
    bool use_bssmap_complete_l3 = false;  // Использовать BSSMAP Complete Layer 3 вместо DTAP
    bool send_clear_command = false;  // Отправить BSSMAP Clear Command
    uint16_t cell_id = cfg.cell_id;  // Cell Identity
    uint8_t clear_cause = 0x00;  // Cause для Clear Command
    bool save_config_flag = false;  // Сохранить конфигурацию
    
    // Флаги для выбора секций вывода
    bool show_all = true;
    bool show_identity = false;
    bool show_network = false;
    bool show_m3ua = false;
    bool show_bssmap = false;
    bool show_transport = false;
    bool show_encapsulation = false;
    // Флаги по интерфейсам
    bool show_subscriber    = false;
    bool show_a_interface   = false;
    bool show_c_interface   = false;
    bool show_e_interface   = false;
    bool show_nc_interface  = false;
    bool show_isup_interface = false;
    bool show_gs_interface   = false;

    // Простой парсинг аргументов
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--imsi" && i+1 < argc) imsi = argv[++i];
        else if (arg == "--mcc" && i+1 < argc) mcc = std::stoi(argv[++i]);
        else if (arg == "--mnc" && i+1 < argc) mnc = std::stoi(argv[++i]);
        else if (arg == "--lac" && i+1 < argc) lac = std::stoi(argv[++i]);
        else if (arg == "--no-lu") do_lu = false;
        else if (arg == "--no-paging") do_paging = false;
        else if (arg == "--no-color") color = false;
        else if (arg == "--send-udp") {
            send_udp = true;
            if (i+1 < argc && argv[i+1][0] != '-') {
                std::string addr = argv[++i];
                size_t colon = addr.find(':');
                if (colon != std::string::npos) {
                    remote_ip   = addr.substr(0, colon);
                    remote_port = std::stoi(addr.substr(colon + 1));
                }
            }
        }
        else if (arg == "--no-bssap") use_bssap = false;
        else if (arg == "--use-sccp") use_sccp = true;
        else if (arg == "--use-sccp-dt1") {
            use_sccp = true;
            use_sccp_dt1 = true;
            if (i+1 < argc && argv[i+1][0] != '-') {
                sccp_dst_ref = std::stoul(argv[++i], nullptr, 16);
            }
        }
        else if (arg == "--use-m3ua") {
            use_m3ua = true;
            use_sccp = true;  // M3UA требует SCCP
        }
        else if (arg == "--opc" && i+1 < argc) m3ua_opc = std::stoul(argv[++i]);
        else if (arg == "--dpc" && i+1 < argc) m3ua_dpc = std::stoul(argv[++i]);
        else if (arg == "--ni"  && i+1 < argc) m3ua_ni  = std::stoul(argv[++i]);
        else if (arg == "--use-sctp") use_sctp = true;
        else if (arg == "--use-bssmap-l3") use_bssmap_complete_l3 = true;
        else if (arg == "--cell-id" && i+1 < argc) cell_id = std::stoi(argv[++i]);
        else if (arg == "--send-clear") {
            send_clear_command = true;
            if (i+1 < argc && argv[i+1][0] != '-') {
                clear_cause = std::stoul(argv[++i], nullptr, 16);
            }
        }
        else if (arg == "--config" && i+1 < argc) {
            config_path = argv[++i];
            if (load_config(config_path, cfg)) {
                config_loaded = true;
                // Применяем загруженные значения
                imsi = cfg.imsi;
                mcc = cfg.mcc;
                mnc = cfg.mnc;
                lac = cfg.lac;
                m3ua_opc = cfg.m3ua_opc;
                m3ua_dpc = cfg.m3ua_dpc;
                m3ua_ni  = cfg.m3ua_ni;
                nc_opc_ni0 = cfg.nc_opc_ni0;  nc_dpc_ni0 = cfg.nc_dpc_ni0;
                nc_opc_ni2 = cfg.nc_opc_ni2;  nc_dpc_ni2 = cfg.nc_dpc_ni2;
                nc_opc_ni3 = cfg.nc_opc_ni3;  nc_dpc_ni3 = cfg.nc_dpc_ni3;
                nc_m3ua_ni = cfg.nc_m3ua_ni;
                isup_opc_ni0 = cfg.isup_opc_ni0;  isup_dpc_ni0 = cfg.isup_dpc_ni0;
                isup_opc_ni2 = cfg.isup_opc_ni2;  isup_dpc_ni2 = cfg.isup_dpc_ni2;
                isup_m3ua_ni = cfg.isup_m3ua_ni;
                isup_local_ip   = cfg.isup_local_ip;
                isup_local_port = cfg.isup_local_port;
                isup_remote_ip  = cfg.isup_remote_ip;
                isup_remote_port= cfg.isup_remote_port;
                gs_opc_ni2 = cfg.gs_opc_ni2;  gs_dpc_ni2 = cfg.gs_dpc_ni2;
                gs_opc_ni3 = cfg.gs_opc_ni3;  gs_dpc_ni3 = cfg.gs_dpc_ni3;
                gs_m3ua_ni = cfg.gs_m3ua_ni;
                gs_local_ip   = cfg.gs_local_ip;
                gs_local_port = cfg.gs_local_port;
                gs_remote_ip  = cfg.gs_remote_ip;
                gs_remote_port= cfg.gs_remote_port;
                cell_id = cfg.cell_id;
                local_ip    = cfg.local_ip;
                local_port  = cfg.local_port;
                remote_ip   = cfg.remote_ip;
                remote_port = cfg.remote_port;
                std::cout << COLOR_GREEN << "✓ Конфигурация загружена из " << config_path << COLOR_RESET << "\n\n";
            } else {
                std::cerr << COLOR_YELLOW << "⚠ Не удалось загрузить конфигурацию из " << config_path << COLOR_RESET << "\n\n";
            }
        }
        else if (arg == "--save-config") {
            save_config_flag = true;
            if (i+1 < argc && argv[i+1][0] != '-') {
                config_path = argv[++i];
            }
        }
        else if (arg == "--show-identity") {
            if (show_all) { show_all = false; }
            show_identity = true;
            do_lu = false;  // Отключаем генерацию сообщений
            do_paging = false;
        }
        else if (arg == "--show-network") {
            if (show_all) { show_all = false; }
            show_network = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-m3ua") {
            if (show_all) { show_all = false; }
            show_m3ua = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-bssmap") {
            if (show_all) { show_all = false; }
            show_bssmap = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-transport") {
            if (show_all) { show_all = false; }
            show_transport = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-encapsulation") {
            if (show_all) { show_all = false; }
            show_encapsulation = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-subscriber") {
            if (show_all) { show_all = false; }
            show_subscriber = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-a-interface") {
            if (show_all) { show_all = false; }
            show_a_interface = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-c-interface") {
            if (show_all) { show_all = false; }
            show_c_interface = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-e-interface") {
            if (show_all) { show_all = false; }
            show_e_interface = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-nc-interface") {
            if (show_all) { show_all = false; }
            show_nc_interface = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-isup-interface") {
            if (show_all) { show_all = false; }
            show_isup_interface = true;
            do_lu = false;
            do_paging = false;
        }
        else if (arg == "--show-gs-interface") {
            if (show_all) { show_all = false; }
            show_gs_interface = true;
            do_lu = false;
            do_paging = false;
        }
    }

    // Отключаем цвета если запрошено
    if (!color) {
        COLOR_RESET = COLOR_BLUE = COLOR_GREEN = COLOR_YELLOW = COLOR_CYAN = COLOR_MAGENTA = "";
    }

    // Сохраняем конфигурацию если запрошено
    if (save_config_flag) {
        Config new_cfg;
        new_cfg.imsi = imsi;
        new_cfg.mcc = mcc;
        new_cfg.mnc = mnc;
        new_cfg.lac = lac;
        new_cfg.m3ua_opc = m3ua_opc;
        new_cfg.m3ua_dpc = m3ua_dpc;
        new_cfg.m3ua_ni  = m3ua_ni;
        new_cfg.nc_opc_ni0 = nc_opc_ni0;  new_cfg.nc_dpc_ni0 = nc_dpc_ni0;
        new_cfg.nc_opc_ni2 = nc_opc_ni2;  new_cfg.nc_dpc_ni2 = nc_dpc_ni2;
        new_cfg.nc_opc_ni3 = nc_opc_ni3;  new_cfg.nc_dpc_ni3 = nc_dpc_ni3;
        new_cfg.nc_m3ua_ni = nc_m3ua_ni;
        new_cfg.cell_id = cell_id;
        new_cfg.msisdn = msisdn;
        new_cfg.local_ip = local_ip;
        new_cfg.local_port = local_port;
        new_cfg.remote_ip = remote_ip;
        new_cfg.remote_port = remote_port;
        new_cfg.c_local_ip = c_local_ip;
        new_cfg.c_local_port = c_local_port;
        new_cfg.c_remote_ip = c_remote_ip;
        new_cfg.c_remote_port = c_remote_port;
        new_cfg.c_opc = c_opc;
        new_cfg.c_dpc = c_dpc;
        new_cfg.c_m3ua_ni = c_m3ua_ni;
        new_cfg.e_local_ip = e_local_ip;
        new_cfg.e_local_port = e_local_port;
        new_cfg.e_remote_ip = e_remote_ip;
        new_cfg.e_remote_port = e_remote_port;
        new_cfg.e_opc_ni0 = e_opc_ni0;  new_cfg.e_dpc_ni0 = e_dpc_ni0;
        new_cfg.e_opc_ni2 = e_opc_ni2;  new_cfg.e_dpc_ni2 = e_dpc_ni2;
        new_cfg.e_opc_ni3 = e_opc_ni3;  new_cfg.e_dpc_ni3 = e_dpc_ni3;
        new_cfg.e_m3ua_ni = e_m3ua_ni;
        new_cfg.nc_local_ip = nc_local_ip;
        new_cfg.nc_local_port = nc_local_port;
        new_cfg.nc_remote_ip = nc_remote_ip;
        new_cfg.nc_remote_port = nc_remote_port;
        new_cfg.isup_local_ip   = isup_local_ip;
        new_cfg.isup_local_port = isup_local_port;
        new_cfg.isup_remote_ip  = isup_remote_ip;
        new_cfg.isup_remote_port= isup_remote_port;
        new_cfg.isup_opc_ni0 = isup_opc_ni0;  new_cfg.isup_dpc_ni0 = isup_dpc_ni0;
        new_cfg.isup_opc_ni2 = isup_opc_ni2;  new_cfg.isup_dpc_ni2 = isup_dpc_ni2;
        new_cfg.isup_m3ua_ni = isup_m3ua_ni;
        new_cfg.gs_local_ip   = gs_local_ip;
        new_cfg.gs_local_port = gs_local_port;
        new_cfg.gs_remote_ip  = gs_remote_ip;
        new_cfg.gs_remote_port= gs_remote_port;
        new_cfg.gs_opc_ni2 = gs_opc_ni2;  new_cfg.gs_dpc_ni2 = gs_dpc_ni2;
        new_cfg.gs_opc_ni3 = gs_opc_ni3;  new_cfg.gs_dpc_ni3 = gs_dpc_ni3;
        new_cfg.gs_m3ua_ni = gs_m3ua_ni;
        
        if (save_config(config_path, new_cfg)) {
            std::cout << COLOR_GREEN << "✓ Конфигурация сохранена в " << config_path << COLOR_RESET << "\n\n";
        } else {
            std::cerr << COLOR_YELLOW << "⚠ Не удалось сохранить конфигурацию в " << config_path << COLOR_RESET << "\n\n";
        }
    }

    std::cout << "vMSC инициализирован\n\n";
    
    std::cout << COLOR_MAGENTA << "=== Параметры генерации ===" << COLOR_RESET << "\n\n";

    // Заголовок секции: жирный, по центру, с разделителями
    auto print_section_header = [](const std::string &name, const std::string &comment = "") {
        int W = 60;
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
            W = ws.ws_col < 100 ? (int)ws.ws_col : 100;
        auto rep = [](int n) -> std::string {
            std::string r; for (int i = 0; i < n; i++) r += "─"; return r;
        };
        auto ucols = [](const std::string &s) -> int {
            int n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n; return n;
        };
        int inner = ucols(name) + 2;
        int dash_total = W - inner;
        int ld = dash_total / 2; if (ld < 1) ld = 1;
        int rd = dash_total - ld; if (rd < 1) rd = 1;
        std::cout << COLOR_YELLOW << rep(ld) << " " << name << " " << rep(rd) << COLOR_RESET << "\n";
        if (!comment.empty()) {
            int cpad = (W - ucols(comment)) / 2;
            if (cpad < 0) cpad = 0;
            std::cout << COLOR_CYAN << std::string(cpad, ' ') << comment << COLOR_RESET << "\n";
        }
    };

    auto print_separator = []() {
        int W = 60;
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
            W = ws.ws_col < 100 ? (int)ws.ws_col : 100;
        std::string line;
        for (int i = 0; i < W; i++) line += "─";
        std::cout << COLOR_YELLOW << line << COLOR_RESET << "\n";
    };

    // [subscriber] / старый [identity]
    if (show_all || show_subscriber || show_identity) {
        print_section_header("[subscriber]");
        std::cout << "  IMSI:   " << COLOR_GREEN << imsi   << COLOR_RESET << "\n";
        if (!msisdn.empty())
            std::cout << "  MSISDN: " << COLOR_GREEN << msisdn << COLOR_RESET << "\n";
        std::cout << "\n";
    }

    // [a-interface]: сеть + BSSMAP + M3UA + транспорт
    if (show_all || show_a_interface || show_network || show_m3ua || show_bssmap || show_transport) {
        print_section_header("[a-interface]");

        if (show_all || show_a_interface || show_network) {
            std::cout << "  " << COLOR_CYAN << "GSM 04.08 / LAI:" << COLOR_RESET << "\n";
            std::cout << "    MCC: " << COLOR_GREEN << std::dec << mcc << COLOR_RESET << "\n";
            std::cout << "    MNC: " << COLOR_GREEN << mnc  << COLOR_RESET << "\n";
            std::cout << "    LAC: " << COLOR_GREEN << std::dec << lac << COLOR_RESET
                      << " (0x" << std::hex << lac << std::dec << COLOR_RESET << ")\n";
        }
        if (show_all || show_a_interface || show_bssmap) {
            std::cout << "  " << COLOR_CYAN << "BSSMAP:" << COLOR_RESET << "\n";
            std::cout << "    Cell ID: " << COLOR_GREEN << cell_id << COLOR_RESET << "\n";
        }
        if (show_all || show_a_interface || show_m3ua) {
            std::cout << "  " << COLOR_CYAN << "M3UA:" << COLOR_RESET << "\n";
            std::cout << "    NI=" << (int)m3ua_ni << " (Reserved):\n";
            std::cout << "      OPC=" << COLOR_GREEN << m3ua_opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << m3ua_dpc << COLOR_RESET << "\n";
        }
        if (show_all || show_a_interface || show_transport) {
            std::cout << "  " << COLOR_CYAN << "Transport (UDP):" << COLOR_RESET << "\n";
            std::cout << "    LIP: " << COLOR_GREEN << local_ip << COLOR_RESET;
            if (local_port) std::cout << "  LP: " << COLOR_GREEN << local_port << COLOR_RESET;
            std::cout << "\n";
            std::cout << "    RIP: " << COLOR_GREEN << remote_ip  << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << remote_port << COLOR_RESET << "\n";
        }
        std::cout << "\n";
    }

    // [c-interface]: MSC ↔ HLR
    if (show_all || show_c_interface) {
        print_section_header("[c-interface]", "MSC ↔ HLR (MAP over SCCP/M3UA)");
        // M3UA (один NI)
        std::cout << "  " << COLOR_CYAN << "M3UA:" << COLOR_RESET << "\n";
        {
            const char *ni_label = (c_m3ua_ni == 0) ? "International" :
                                   (c_m3ua_ni == 2) ? "National" : "Reserved";
            std::cout << "    NI=" << (int)c_m3ua_ni << " (" << ni_label << "):\n";
            std::cout << "      OPC=" << COLOR_GREEN << c_opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << c_dpc << COLOR_RESET << "\n";
        }
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << c_local_ip << COLOR_RESET;
        if (c_local_port) std::cout << "  LP: " << COLOR_GREEN << c_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!c_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << c_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << c_remote_port << COLOR_RESET << "\n";
        std::cout << "\n";
    }

    // [e-interface]: MSC ↔ MSC
    if (show_all || show_e_interface) {
        print_section_header("[e-interface]", "MSC ↔ MSC (межсистемный хендовер, MAP)");
        // M3UA (три NI варианта)
        std::cout << "  " << COLOR_CYAN << "M3UA:" << COLOR_RESET << "\n";
        auto print_e_ni_row = [&](uint8_t ni, uint32_t opc, uint32_t dpc, const char *label) {
            std::string marker = (ni == e_m3ua_ni) ? COLOR_GREEN + std::string(" ◄ active") + COLOR_RESET : "";
            std::cout << "    NI=" << (int)ni << " (" << label << "):" << marker << "\n";
            std::cout << "      OPC=" << COLOR_GREEN << opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << dpc << COLOR_RESET << "\n";
        };
        print_e_ni_row(0, e_opc_ni0, e_dpc_ni0, "International");
        print_e_ni_row(2, e_opc_ni2, e_dpc_ni2, "National");
        print_e_ni_row(3, e_opc_ni3, e_dpc_ni3, "Reserved");
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << e_local_ip << COLOR_RESET;
        if (e_local_port) std::cout << "  LP: " << COLOR_GREEN << e_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!e_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << e_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << e_remote_port << COLOR_RESET << "\n";
        std::cout << "\n";
    }

    // [nc-interface]: MSC-S ↔ MGW
    if (show_all || show_nc_interface) {
        print_section_header("[nc-interface]", "MSC-S ↔ MGW (H.248/MEGACO)");
        // M3UA (три NI варианта)
        std::cout << "  " << COLOR_CYAN << "M3UA:" << COLOR_RESET << "\n";
        auto print_nc_ni_row = [&](uint8_t ni, uint32_t opc, uint32_t dpc, const char *label) {
            std::string marker = (ni == nc_m3ua_ni) ? COLOR_GREEN + std::string(" ◄ active") + COLOR_RESET : "";
            std::cout << "    NI=" << (int)ni << " (" << label << "):" << marker << "\n";
            std::cout << "      OPC=" << COLOR_GREEN << opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << dpc << COLOR_RESET << "\n";
        };
        print_nc_ni_row(0, nc_opc_ni0, nc_dpc_ni0, "International");
        print_nc_ni_row(2, nc_opc_ni2, nc_dpc_ni2, "National");
        print_nc_ni_row(3, nc_opc_ni3, nc_dpc_ni3, "Reserved");
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << nc_local_ip << COLOR_RESET;
        if (nc_local_port) std::cout << "  LP: " << COLOR_GREEN << nc_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!nc_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << nc_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << nc_remote_port << COLOR_RESET << "\n";
        std::cout << "\n";
    }

    // [isup-interface]: MSC ↔ PSTN/GW
    if (show_all || show_isup_interface) {
        print_section_header("[isup-interface]", "MSC ↔ PSTN/GW (ISUP over MTP3/M3UA)");
        std::cout << "  " << COLOR_CYAN << "MTP3/M3UA:" << COLOR_RESET
                  << "  SI=" << COLOR_GREEN << "5 (ISUP)" << COLOR_RESET << "\n";
        auto print_isup_ni_row = [&](uint8_t ni, uint32_t opc, uint32_t dpc, const char *label) {
            std::string marker = (ni == isup_m3ua_ni) ? COLOR_GREEN + std::string(" ◄ active") + COLOR_RESET : "";
            std::cout << "    NI=" << (int)ni << " (" << label << "):" << marker << "\n";
            std::cout << "      OPC=" << COLOR_GREEN << opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << dpc << COLOR_RESET << "\n";
        };
        print_isup_ni_row(0, isup_opc_ni0, isup_dpc_ni0, "International");
        print_isup_ni_row(2, isup_opc_ni2, isup_dpc_ni2, "National");
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << isup_local_ip << COLOR_RESET;
        if (isup_local_port) std::cout << "  LP: " << COLOR_GREEN << isup_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!isup_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << isup_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << isup_remote_port << COLOR_RESET << "\n";
        std::cout << "\n";
    }

    // [gs-interface]: MSC ↔ SGSN
    if (show_all || show_gs_interface) {
        print_section_header("[gs-interface]", "MSC ↔ SGSN (BSSAP+ over SCCP/M3UA)");
        std::cout << "  " << COLOR_CYAN << "SCCP/M3UA:" << COLOR_RESET
                  << "  SI=" << COLOR_GREEN << "3 (SCCP/BSSAP+)" << COLOR_RESET << "\n";
        auto print_gs_ni_row = [&](uint8_t ni, uint32_t opc, uint32_t dpc, const char *label) {
            std::string marker = (ni == gs_m3ua_ni) ? COLOR_GREEN + std::string(" ◄ active") + COLOR_RESET : "";
            std::cout << "    NI=" << (int)ni << " (" << label << "):" << marker << "\n";
            std::cout << "      OPC=" << COLOR_GREEN << opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << dpc << COLOR_RESET << "\n";
        };
        print_gs_ni_row(2, gs_opc_ni2, gs_dpc_ni2, "National");
        print_gs_ni_row(3, gs_opc_ni3, gs_dpc_ni3, "Reserved");
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << gs_local_ip << COLOR_RESET;
        if (gs_local_port) std::cout << "  LP: " << COLOR_GREEN << gs_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!gs_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << gs_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << gs_remote_port << COLOR_RESET << "\n";
        std::cout << "\n";
    }

    if (show_encapsulation || (show_all && send_udp)) {
        print_section_header("[encapsulation]");
        std::cout << "  " << COLOR_CYAN;
        if (use_m3ua) {
            std::cout << "M3UA → ";
        }
        if (use_sccp) {
            if (use_sccp_dt1) {
                std::cout << "SCCP DT1 → ";
            } else {
                std::cout << "SCCP CR → ";
            }
        }
        if (use_bssap) {
            if (use_bssmap_complete_l3) {
                std::cout << "BSSMAP Complete L3 → ";
            } else {
                std::cout << "BSSAP DTAP → ";
            }
        }
        std::cout << "GSM 04.08" << COLOR_RESET << "\n";
    }
    std::cout << "\n";
    print_section_header("[Location Update Request]");
    std::cout << "\n";

    if (do_lu) {
        struct msgb *lu_msg = generate_location_update_request(imsi.c_str(), mcc, mnc, lac);
        if (lu_msg) {
            parse_and_print_gsm48_msg(lu_msg, "Location Update Request");
            if (send_udp) {
                if (use_bssap) {
                    struct msgb *bssap_msg;
                    // Выбираем между BSSMAP Complete Layer 3 и DTAP
                    if (use_bssmap_complete_l3) {
                        bssap_msg = wrap_in_bssmap_complete_l3(lu_msg, cell_id, lac);
                    } else {
                        bssap_msg = wrap_in_bssap_dtap(lu_msg);
                    }
                    if (bssap_msg) {
                        if (use_sccp) {
                            struct msgb *sccp_msg;
                            if (use_sccp_dt1) {
                                sccp_msg = wrap_in_sccp_dt1(bssap_msg, sccp_dst_ref);
                            } else {
                                sccp_msg = wrap_in_sccp_cr(bssap_msg);
                            }
                            if (sccp_msg) {
                                if (use_m3ua) {
                                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni);
                                    if (m3ua_msg) {
                                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                                        msgb_free(m3ua_msg);
                                    }
                                } else {
                                    send_message_udp(sccp_msg->data, sccp_msg->len, remote_ip.c_str(), remote_port);
                                }
                                msgb_free(sccp_msg);
                            }
                        } else {
                            send_message_udp(bssap_msg->data, bssap_msg->len, remote_ip.c_str(), remote_port);
                        }
                        msgb_free(bssap_msg);
                    }
                } else {
                    send_message_udp(lu_msg->data, lu_msg->len, remote_ip.c_str(), remote_port);
                }
            }
            msgb_free(lu_msg);
        }
    }

    if (do_paging) {
        print_section_header("[Paging Response]");
        std::cout << "\n";
        struct msgb *pg_resp = generate_paging_response(imsi.c_str());
        if (pg_resp) {
            parse_and_print_gsm48_msg(pg_resp, "Paging Response");
            if (send_udp) {
                if (use_bssap) {
                    struct msgb *bssap_msg;
                    // Для Paging Response используем только DTAP
                    bssap_msg = wrap_in_bssap_dtap(pg_resp);
                    if (bssap_msg) {
                        if (use_sccp) {
                            struct msgb *sccp_msg;
                            if (use_sccp_dt1) {
                                sccp_msg = wrap_in_sccp_dt1(bssap_msg, sccp_dst_ref);
                            } else {
                                sccp_msg = wrap_in_sccp_cr(bssap_msg);
                            }
                            if (sccp_msg) {
                                if (use_m3ua) {
                                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni);
                                    if (m3ua_msg) {
                                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                                        msgb_free(m3ua_msg);
                                    }
                                } else {
                                    send_message_udp(sccp_msg->data, sccp_msg->len, remote_ip.c_str(), remote_port);
                                }
                                msgb_free(sccp_msg);
                            }
                        } else {
                            send_message_udp(bssap_msg->data, bssap_msg->len, remote_ip.c_str(), remote_port);
                        }
                        msgb_free(bssap_msg);
                    }
                } else {
                    send_message_udp(pg_resp->data, pg_resp->len, remote_ip.c_str(), remote_port);
                }
            }
            msgb_free(pg_resp);
        }
    }

    // Отправка BSSMAP Clear Command если запрошено
    if (send_clear_command && send_udp) {
        struct msgb *clear_msg = generate_bssmap_clear_command(clear_cause);
        if (clear_msg) {
            if (use_sccp) {
                struct msgb *sccp_msg;
                if (use_sccp_dt1) {
                    sccp_msg = wrap_in_sccp_dt1(clear_msg, sccp_dst_ref);
                } else {
                    sccp_msg = wrap_in_sccp_cr(clear_msg);
                }
                if (sccp_msg) {
                    if (use_m3ua) {
                        struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni);
                        if (m3ua_msg) {
                            send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                            msgb_free(m3ua_msg);
                        }
                    } else {
                        send_message_udp(sccp_msg->data, sccp_msg->len, remote_ip.c_str(), remote_port);
                    }
                    msgb_free(sccp_msg);
                }
            } else {
                send_message_udp(clear_msg->data, clear_msg->len, remote_ip.c_str(), remote_port);
            }
            msgb_free(clear_msg);
        }
    }

    talloc_free(ctx);
    return 0;
}
