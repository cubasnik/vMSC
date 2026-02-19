#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>
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
struct GtRoute {
    std::string prefix;       // E.164-префикс (например: 7916)
    std::string iface;        // интерфейс: a, c, f, e, nc, isup, gs
    uint32_t    dpc = 0;      // DPC назначения (point code)
    std::string description;  // произвольное описание
    std::string spid;         // SPID метка (явная; иначе — авто-корреляция по DPC)
};

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
    uint8_t  a_ssn    = 254;    // SCCP SSN A-интерфейса (BSSAP called party, 0xFE=254)
    uint8_t  a_si     = 3;      // SI=3 (SCCP) для A-interface

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
    uint8_t    c_si          = 3;  // SI=3 (SCCP)
    uint8_t    c_ssn_local  = 8;   // Calling SSN (MSC/VLR → HLR) [ITU Q.713 Annex B]
    uint8_t    c_ssn_remote = 6;   // Called SSN  (HLR)
    // Примечание: B-интерфейс (MSC↔VLR) внутренний — MSC и VLR совмещены на одном узле

    // F-interface: MSC ↔ EIR (MAP CheckIMEI over SCCP/M3UA) — один NI
    // MAP opCode=43 (CheckIMEI), AC: checkIMEI-context-v2 (3GPP TS 29.002)
    std::string f_local_ip    = "0.0.0.0";
    uint16_t   f_local_port  = 0;
    std::string f_remote_ip   = "";
    uint16_t   f_remote_port = 0;
    uint32_t   f_opc         = 0;
    uint32_t   f_dpc         = 0;
    uint8_t    f_m3ua_ni     = 3;   // NI для F-интерфейса
    uint8_t    f_si          = 3;   // SI=3 (SCCP)
    uint8_t    f_ssn_local  = 8;    // Calling SSN (MSC)
    uint8_t    f_ssn_remote = 11;   // Called SSN  (EIR, оператор-специфично)

    // E-interface: MSC ↔ MSC (междусистемный хендовер, MAP) — три NI варианта
    std::string e_local_ip    = "0.0.0.0";
    uint16_t   e_local_port  = 0;
    std::string e_remote_ip   = "";
    uint16_t   e_remote_port = 0;
    uint32_t   e_opc_ni0 = 0;  uint32_t e_dpc_ni0 = 0;
    uint32_t   e_opc_ni2 = 0;  uint32_t e_dpc_ni2 = 0;
    uint32_t   e_opc_ni3 = 0;  uint32_t e_dpc_ni3 = 0;
    uint8_t    e_m3ua_ni = 3;  // Активный NI для E-интерфейса
    uint8_t    e_si      = 3;  // SI=3 (SCCP)
    uint8_t    e_ssn_local  = 8;   // Calling SSN (MSC calling)
    uint8_t    e_ssn_remote = 8;   // Called SSN  (MSC called)

    // Nc-interface: MSC-S ↔ MGW (H.248/MEGACO) — три NI варианта
    std::string nc_local_ip    = "0.0.0.0";
    uint16_t   nc_local_port  = 0;
    std::string nc_remote_ip   = "";
    uint16_t   nc_remote_port = 0;
    uint32_t   nc_opc_ni0 = 0;  uint32_t nc_dpc_ni0 = 0;
    uint32_t   nc_opc_ni2 = 0;  uint32_t nc_dpc_ni2 = 0;
    uint32_t   nc_opc_ni3 = 0;  uint32_t nc_dpc_ni3 = 0;
    uint8_t    nc_m3ua_ni = 3;  // Активный NI для Nc-интерфейса
    uint8_t    nc_si      = 3;  // SI=3 (SCCP/H.248)

    // ISUP-interface: MSC ↔ PSTN/GW (ISUP over MTP3/M3UA) — два NI варианта
    std::string isup_local_ip   = "0.0.0.0";
    uint16_t   isup_local_port  = 0;
    std::string isup_remote_ip  = "";
    uint16_t   isup_remote_port = 0;
    uint32_t   isup_opc_ni0 = 0;  uint32_t isup_dpc_ni0 = 0;  // NI=0 International
    uint32_t   isup_opc_ni2 = 0;  uint32_t isup_dpc_ni2 = 0;  // NI=2 National
    uint8_t    isup_m3ua_ni = 2;  // Активный NI для ISUP (по умолчанию National)
    uint8_t    isup_si      = 5;  // SI=5 (ISUP) — отличается от всех остальных интерфейсов

    // Gs-interface: MSC ↔ SGSN (BSSAP+ over SCCP/M3UA) — NI=2 (National), NI=3 (Reserved)
    std::string gs_local_ip    = "0.0.0.0";
    uint16_t   gs_local_port   = 0;
    std::string gs_remote_ip   = "";
    uint16_t   gs_remote_port  = 0;
    uint32_t   gs_opc_ni2 = 0;  uint32_t gs_dpc_ni2 = 0;  // NI=2 National
    uint32_t   gs_opc_ni3 = 0;  uint32_t gs_dpc_ni3 = 0;  // NI=3 Reserved
    uint8_t    gs_m3ua_ni = 2;  // Активный NI для Gs (по умолчанию National)
    uint8_t    gs_si      = 3;   // SI=3 (SCCP/BSSAP+)
    uint8_t    gs_ssn_local  = 254;  // Calling SSN (MSC,  BSSAP+)
    uint8_t    gs_ssn_remote = 254;  // Called SSN  (SGSN, BSSAP+)
    // Глобальные MTP3 параметры
    uint8_t sls = 0;  // Signalling Link Selection (балансировка по SS7-линкам, 0-15)
    uint8_t mp  = 0;  // Message Priority: 0=normal, 1=important, 2=overload, 3=NM
    // Глобальный Title (SCCP GT-маршрутизация) — ITU-T Q.713 GTI=4
    // Используется в реальных сетях оператора для маршрутизации MAP-сообщений к HLR/EIR/MSC
    std::string msc_gt  = "";  // E.164 адрес MSC (используется как Calling GT)
    uint8_t     gt_tt   = 0;   // Translation Type: 0=нет трансляции
    uint8_t     gt_np   = 1;   // Numbering Plan: 1=E.164, 6=E.212 (land mobile/IMSI)
    uint8_t     gt_nai  = 4;   // NAI: 1=subscriber, 3=national, 4=international
    // Пер-интерфейсные: GTI (признак включения, 0=выкл, 4=GTI-4) + E.164 адрес целевого узла
    uint8_t     c_gt_ind  = 0;  std::string c_gt_called  = "";  // HLR E.164
    uint8_t     f_gt_ind  = 0;  std::string f_gt_called  = "";  // EIR E.164
    uint8_t     e_gt_ind  = 0;  std::string e_gt_called  = "";  // Remote MSC E.164
    uint8_t     gs_gt_ind = 0;  std::string gs_gt_called = "";  // SGSN E.164
    // Сетевые параметры узла
    std::string local_netmask  = "255.255.255.248"; // Маска локальной подсети
    std::string remote_netmask = "255.255.255.0";   // Маска удалённой подсети
    std::string gateway        = "100.100.100.1";   // Шлюз по умолчанию
    std::string ntp_primary    = "";                 // Основной NTP-сервер (источник синхронизации)
    std::string ntp_secondary  = "";                 // Резервный NTP-сервер
    // SPID — текстовые метки сигнальных точек (Signaling Point ID)
    std::string a_local_spid    = "";  // OPC A-interface (MSC)
    std::string a_remote_spid   = "";  // DPC A-interface (BSC)
    std::string c_local_spid    = "";  // OPC C-interface (MSC)
    std::string c_remote_spid   = "";  // DPC C-interface (HLR)
    std::string f_local_spid    = "";  // OPC F-interface (MSC)
    std::string f_remote_spid   = "";  // DPC F-interface (EIR)
    std::string e_local_spid    = "";  // OPC E-interface (MSC)
    std::string e_remote_spid   = "";  // DPC E-interface (удалённый MSC)
    std::string nc_local_spid   = "";  // OPC Nc-interface (MSC-S)
    std::string nc_remote_spid  = "";  // DPC Nc-interface (MGW)
    std::string isup_local_spid = "";  // OPC ISUP-interface (MSC)
    std::string isup_remote_spid= "";  // DPC ISUP-interface (PSTN/GW)
    std::string gs_local_spid   = "";  // OPC Gs-interface (MSC)
    std::string gs_remote_spid  = "";  // DPC Gs-interface (SGSN)
    // Таблица GT-маршрутизации SCCP
    std::vector<GtRoute> gt_routes;
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
            std::transform(section.begin(), section.end(), section.begin(), ::tolower);
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
            else if (key == "msc_gt") cfg.msc_gt = value;
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
            else if (key == "si")   cfg.a_si     = (uint8_t)std::stoul(value);
            else if (key == "sls")  cfg.sls      = (uint8_t)std::stoul(value);
            else if (key == "mp")   cfg.mp       = (uint8_t)std::stoul(value);
            else if (key == "ssn")  cfg.a_ssn    = (uint8_t)std::stoul(value);
            // Транспорт
            else if (key == "local_ip")    cfg.local_ip    = value;
            else if (key == "local_port")  cfg.local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.remote_ip   = value;
            else if (key == "remote_port") cfg.remote_port = std::stoi(value);
            else if (key == "local_spid")  cfg.a_local_spid  = value;
            else if (key == "remote_spid") cfg.a_remote_spid = value;
        } else if (section == "c-interface") {
            if      (key == "local_ip")    cfg.c_local_ip    = value;
            else if (key == "local_port")  cfg.c_local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.c_remote_ip   = value;
            else if (key == "remote_port") cfg.c_remote_port = std::stoi(value);
            else if (key == "opc")         cfg.c_opc         = std::stoul(value);
            else if (key == "dpc")         cfg.c_dpc         = std::stoul(value);
            else if (key == "ni")          cfg.c_m3ua_ni     = std::stoul(value);
            else if (key == "si")          cfg.c_si          = (uint8_t)std::stoul(value);
            else if (key == "gt_ind")      cfg.c_gt_ind      = (uint8_t)std::stoul(value);
            else if (key == "gt_called")   cfg.c_gt_called   = value;
            else if (key == "ssn_local")   cfg.c_ssn_local  = (uint8_t)std::stoul(value);
            else if (key == "ssn_remote")  cfg.c_ssn_remote = (uint8_t)std::stoul(value);
            else if (key == "local_spid")  cfg.c_local_spid  = value;
            else if (key == "remote_spid") cfg.c_remote_spid = value;
        } else if (section == "f-interface") {
            if      (key == "local_ip")    cfg.f_local_ip    = value;
            else if (key == "local_port")  cfg.f_local_port  = std::stoi(value);
            else if (key == "remote_ip")   cfg.f_remote_ip   = value;
            else if (key == "remote_port") cfg.f_remote_port = std::stoi(value);
            else if (key == "opc")         cfg.f_opc         = std::stoul(value);
            else if (key == "dpc")         cfg.f_dpc         = std::stoul(value);
            else if (key == "ni")          cfg.f_m3ua_ni     = (uint8_t)std::stoul(value);
            else if (key == "si")          cfg.f_si          = (uint8_t)std::stoul(value);
            else if (key == "gt_ind")      cfg.f_gt_ind      = (uint8_t)std::stoul(value);
            else if (key == "gt_called")   cfg.f_gt_called   = value;
            else if (key == "ssn_local")   cfg.f_ssn_local  = (uint8_t)std::stoul(value);
            else if (key == "ssn_remote")  cfg.f_ssn_remote = (uint8_t)std::stoul(value);
            else if (key == "local_spid")  cfg.f_local_spid  = value;
            else if (key == "remote_spid") cfg.f_remote_spid = value;
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
            else if (key == "si")      cfg.e_si       = (uint8_t)std::stoul(value);
            else if (key == "gt_ind")  cfg.e_gt_ind   = (uint8_t)std::stoul(value);
            else if (key == "gt_called") cfg.e_gt_called = value;
            else if (key == "ssn_local")   cfg.e_ssn_local  = (uint8_t)std::stoul(value);
            else if (key == "ssn_remote")  cfg.e_ssn_remote = (uint8_t)std::stoul(value);
            else if (key == "local_spid")  cfg.e_local_spid  = value;
            else if (key == "remote_spid") cfg.e_remote_spid = value;
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
            else if (key == "si")      cfg.nc_si      = (uint8_t)std::stoul(value);
            else if (key == "local_spid")  cfg.nc_local_spid  = value;
            else if (key == "remote_spid") cfg.nc_remote_spid = value;
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
            else if (key == "si")      cfg.isup_si      = (uint8_t)std::stoul(value);
            else if (key == "local_spid")  cfg.isup_local_spid  = value;
            else if (key == "remote_spid") cfg.isup_remote_spid = value;
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
            else if (key == "si")      cfg.gs_si       = (uint8_t)std::stoul(value);
            else if (key == "gt_ind")  cfg.gs_gt_ind   = (uint8_t)std::stoul(value);
            else if (key == "gt_called") cfg.gs_gt_called = value;
            else if (key == "ssn_local")   cfg.gs_ssn_local  = (uint8_t)std::stoul(value);
            else if (key == "ssn_remote")  cfg.gs_ssn_remote = (uint8_t)std::stoul(value);
            else if (key == "local_spid")  cfg.gs_local_spid  = value;
            else if (key == "remote_spid") cfg.gs_remote_spid = value;
        // Секция [gt]: общие параметры Global Title
        } else if (section == "gt") {
            if      (key == "msc_gt") cfg.msc_gt  = value;
            else if (key == "tt")  cfg.gt_tt  = (uint8_t)std::stoul(value);
            else if (key == "np")  cfg.gt_np  = (uint8_t)std::stoul(value);
            else if (key == "nai") cfg.gt_nai = (uint8_t)std::stoul(value);
        } else if (section == "gt-route") {
            // Формат: route=prefix:interface:dpc:description
            if (key == "route") {
                std::istringstream ss(value);
                std::string part;
                std::vector<std::string> parts;
                while (std::getline(ss, part, ':')) parts.push_back(part);
                if (parts.size() >= 3) {
                    GtRoute r;
                    r.prefix = parts[0];
                    r.iface  = parts[1];
                    try { r.dpc = std::stoul(parts[2]); } catch (...) { r.dpc = 0; }
                    if (parts.size() >= 4) r.description = parts[3];
                    if (parts.size() >= 5) r.spid        = parts[4];
                    cfg.gt_routes.push_back(r);
                }
            }
        // Обратная совместимость со старым форматом
        } else if (section == "network") {
            if      (key == "mcc") cfg.mcc = std::stoi(value);
            else if (key == "mnc") cfg.mnc = std::stoi(value);
            else if (key == "lac") cfg.lac = std::stoi(value);
            else if (key == "local_netmask")  cfg.local_netmask  = value;
            else if (key == "remote_netmask") cfg.remote_netmask = value;
            else if (key == "gateway")        cfg.gateway        = value;
            else if (key == "ntp_primary")    cfg.ntp_primary    = value;
            else if (key == "ntp_secondary")  cfg.ntp_secondary  = value;
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
    sec("[A-interface]", "MSC ↔ BSC  (GSM A-interface)");
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
    file << "si="  << (int)cfg.a_si    << "\n";
    file << "sls=" << (int)cfg.sls     << "\n";
    file << "mp="  << (int)cfg.mp      << "\n";
    file << LINE;
    file << "#  SCCP SSN\n";
    file << LINE;
    file << "ssn=" << (int)cfg.a_ssn << "\n";
    if (!cfg.a_local_spid.empty() || !cfg.a_remote_spid.empty()) {
        file << LINE;
        file << "#  SPID (Signaling Point ID)\n";
        file << LINE;
        if (!cfg.a_local_spid.empty())  file << "local_spid="  << cfg.a_local_spid  << "\n";
        if (!cfg.a_remote_spid.empty()) file << "remote_spid=" << cfg.a_remote_spid << "\n";
    }
    file << LINE;
    file << "#  Транспорт (UDP)\n";
    file << LINE;
    file << "local_ip="    << cfg.local_ip    << "\n";
    file << "local_port="  << cfg.local_port  << "\n";
    file << "remote_ip="   << cfg.remote_ip   << "\n";
    file << "remote_port=" << cfg.remote_port << "\n";

    // --- [c-interface] ---
    sec("[C-interface]", "MSC ↔ HLR  (MAP over SCCP/M3UA)");
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
    file << "si="  << (int)cfg.c_si      << "\n";
    file << LINE;
    file << "#  SCCP SSN\n";
    file << LINE;
    file << "ssn_local="  << (int)cfg.c_ssn_local  << "\n";
    file << "ssn_remote=" << (int)cfg.c_ssn_remote << "\n";
    if (!cfg.c_local_spid.empty() || !cfg.c_remote_spid.empty()) {
        file << LINE;
        file << "#  SPID (Signaling Point ID)\n";
        file << LINE;
        if (!cfg.c_local_spid.empty())  file << "local_spid="  << cfg.c_local_spid  << "\n";
        if (!cfg.c_remote_spid.empty()) file << "remote_spid=" << cfg.c_remote_spid << "\n";
    }

    // --- [f-interface] ---
    sec("[F-interface]", "MSC \u2194 EIR  (MAP CheckIMEI over SCCP/M3UA)");
    file << LINE;
    file << "#  Транспорт\n";
    file << LINE;
    file << "local_ip="    << cfg.f_local_ip    << "\n";
    file << "local_port="  << cfg.f_local_port  << "\n";
    file << "remote_ip="   << cfg.f_remote_ip   << "\n";
    file << "remote_port=" << cfg.f_remote_port << "\n";
    file << LINE;
    file << "#  M3UA  NI=" << (int)cfg.f_m3ua_ni << "\n";
    file << LINE;
    file << "opc=" << cfg.f_opc          << "\n";
    file << "dpc=" << cfg.f_dpc          << "\n";
    file << "ni="  << (int)cfg.f_m3ua_ni << "\n";
    file << "si="  << (int)cfg.f_si      << "\n";
    file << LINE;
    file << "#  SCCP SSN\n";
    file << LINE;
    file << "ssn_local="  << (int)cfg.f_ssn_local  << "\n";
    file << "ssn_remote=" << (int)cfg.f_ssn_remote << "\n";
    if (!cfg.f_local_spid.empty() || !cfg.f_remote_spid.empty()) {
        file << LINE;
        file << "#  SPID (Signaling Point ID)\n";
        file << LINE;
        if (!cfg.f_local_spid.empty())  file << "local_spid="  << cfg.f_local_spid  << "\n";
        if (!cfg.f_remote_spid.empty()) file << "remote_spid=" << cfg.f_remote_spid << "\n";
    }

    // --- [e-interface] ---
    sec("[E-interface]", "MSC ↔ MSC  (межсистемный хендовер, MAP)");
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
    file << "si=" << (int)cfg.e_si      << "\n";
    file << LINE;
    file << "#  SCCP SSN\n";
    file << LINE;
    file << "ssn_local="  << (int)cfg.e_ssn_local  << "\n";
    file << "ssn_remote=" << (int)cfg.e_ssn_remote << "\n";
    if (!cfg.e_local_spid.empty() || !cfg.e_remote_spid.empty()) {
        file << LINE;
        file << "#  SPID (Signaling Point ID)\n";
        file << LINE;
        if (!cfg.e_local_spid.empty())  file << "local_spid="  << cfg.e_local_spid  << "\n";
        if (!cfg.e_remote_spid.empty()) file << "remote_spid=" << cfg.e_remote_spid << "\n";
    }

    // --- [nc-interface] ---
    sec("[Nc-interface]", "MSC-S ↔ MGW  (H.248/MEGACO)");
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
    file << "si=" << (int)cfg.nc_si      << "\n";
    if (!cfg.nc_local_spid.empty() || !cfg.nc_remote_spid.empty()) {
        file << LINE;
        file << "#  SPID (Signaling Point ID)\n";
        file << LINE;
        if (!cfg.nc_local_spid.empty())  file << "local_spid="  << cfg.nc_local_spid  << "\n";
        if (!cfg.nc_remote_spid.empty()) file << "remote_spid=" << cfg.nc_remote_spid << "\n";
    }

    // --- [isup-interface] ---
    sec("[ISUP-interface]", "MSC ↔ PSTN/GW  (ISUP over MTP3/M3UA)");
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
    file << "si=" << (int)cfg.isup_si      << "\n";
    if (!cfg.isup_local_spid.empty() || !cfg.isup_remote_spid.empty()) {
        file << LINE;
        file << "#  SPID (Signaling Point ID)\n";
        file << LINE;
        if (!cfg.isup_local_spid.empty())  file << "local_spid="  << cfg.isup_local_spid  << "\n";
        if (!cfg.isup_remote_spid.empty()) file << "remote_spid=" << cfg.isup_remote_spid << "\n";
    }

    // --- [gs-interface] ---
    sec("[Gs-interface]", "MSC ↔ SGSN  (BSSAP+ over SCCP/M3UA)");
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
    file << "si=" << (int)cfg.gs_si      << "\n";
    file << LINE;
    file << "#  SCCP SSN\n";
    file << LINE;
    file << "ssn_local="  << (int)cfg.gs_ssn_local  << "\n";
    file << "ssn_remote=" << (int)cfg.gs_ssn_remote << "\n";
    if (!cfg.gs_local_spid.empty() || !cfg.gs_remote_spid.empty()) {
        file << LINE;
        file << "#  SPID (Signaling Point ID)\n";
        file << LINE;
        if (!cfg.gs_local_spid.empty())  file << "local_spid="  << cfg.gs_local_spid  << "\n";
        if (!cfg.gs_remote_spid.empty()) file << "remote_spid=" << cfg.gs_remote_spid << "\n";
    }

    // --- [gt] ---
    sec("[gt]", "Общие параметры Global Title (SCCP GT-маршрутизация)");
    if (!cfg.msc_gt.empty())
        file << "msc_gt=" << cfg.msc_gt << "\n";
    file << "tt="  << (int)cfg.gt_tt  << "\n";
    file << "np="  << (int)cfg.gt_np  << "\n";
    file << "nai=" << (int)cfg.gt_nai << "\n";

    if (!cfg.gt_routes.empty()) {
        sec("[gt-route]", "Таблица GT-маршрутизации SCCP");
        file << "# Формат: prefix:interface:dpc:описание[:spid]\n";
        for (const auto &r : cfg.gt_routes) {
            file << "route=" << r.prefix << ":" << r.iface << ":" << r.dpc
                 << ":" << r.description;
            if (!r.spid.empty()) file << ":" << r.spid;
            file << "\n";
        }
    }

    // --- [network] ---
    sec("[network]", "Сетевые параметры узла");
    file << "local_netmask="  << cfg.local_netmask  << "\n";
    file << "remote_netmask=" << cfg.remote_netmask << "\n";
    file << "gateway="        << cfg.gateway        << "\n";
    if (!cfg.ntp_primary.empty())
        file << "ntp_primary="    << cfg.ntp_primary    << "\n";
    if (!cfg.ntp_secondary.empty())
        file << "ntp_secondary="  << cfg.ntp_secondary  << "\n";

    file << "\n" << SEP;

    return true;
}

// Получение пути к конфигу по умолчанию
static std::string get_default_config_path() {
    // Приоритет: текущая директория, затем домашняя
    return "./vmsc.conf";
}

// Оборачивание SCCP в M3UA DATA message (SIGTRAN)
static struct msgb *wrap_in_m3ua(struct msgb *sccp_msg, uint32_t opc, uint32_t dpc, uint8_t ni,
                                   uint8_t si = 0x03, uint8_t mp = 0x00, uint8_t sls = 0x00) {
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
    
    // SI (Service Indicator): 0x03=SCCP, 0x05=ISUP, 0x04=TUP, 0x00=SNM
    *(msgb_put(m3ua_msg, 1)) = si;
    
    // NI (Network Indicator)
    *(msgb_put(m3ua_msg, 1)) = ni;
    
    // MP (Message Priority): 0=normal, 1=important, 2=overload, 3=NM
    *(msgb_put(m3ua_msg, 1)) = mp;
    
    // SLS (Signalling Link Selection): балансировка нагрузки по SS7-линкам
    *(msgb_put(m3ua_msg, 1)) = sls;
    
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
    const char *si_name = (si == 0x03) ? "SCCP" : (si == 0x05) ? "ISUP" :
                          (si == 0x04) ? "TUP"  : (si == 0x00) ? "SNM"  : "?";
    std::cout << COLOR_BLUE << "  SI: " << COLOR_GREEN << (int)si << " (" << si_name << ")" << COLOR_RESET
              << COLOR_BLUE << "   NI: " << COLOR_GREEN << (int)ni;
    if (ni == 0) std::cout << " (International)";
    else if (ni == 2) std::cout << " (National)";
    else if (ni == 3) std::cout << " (Reserved)";
    std::cout << COLOR_BLUE << "   SLS: " << COLOR_GREEN << (int)sls
              << COLOR_BLUE << "   MP: "  << COLOR_GREEN << (int)mp << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  M3UA размер: " << COLOR_GREEN << m3ua_msg->len << " байт" 
              << COLOR_RESET << " (SCCP: " << sccp_msg->len << " байт)\n\n";

    return m3ua_msg;
}

// Оборачивание BSSAP в SCCP Connection Request (CR)
static struct msgb *wrap_in_sccp_cr(struct msgb *bssap_msg, uint8_t ssn = 0xFE) {
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
    *(msgb_put(sccp_msg, 1)) = ssn;   // SSN (из конфига [a-interface] ssn=)
    
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

// ============================================================
// C-интерфейс: MAP over SCCP UDT (connectionless)
// Стек: MAP/TCAP → SCCP UDT → M3UA DATA → UDP
// SSN: MSC=8, VLR=7, HLR=6  [ITU Q.713 Annex B / 3GPP TS 29.002]
// TCAP: ITU-T Q.773, BER ASN.1 (ручное кодирование)
// MAP:  3GPP TS 29.002
// ============================================================

// SCCP адрес — SSN-маршрутизация или GT-маршрутизация (ITU-T Q.713)
// GTI=0: route-on-SSN (Address Indicator = 0x42)
// GTI=4: route-on-GT  (ITU GTI=4: TT + NP + ES + NAI + BCD-digits)
struct ScpAddr {
    uint8_t     ssn    = 0;     // SSN (передается если SSI=1 в AI)
    uint8_t     gti    = 0;     // GTI: 0=нет GT (route-on-SSN), 4=GTI-4
    uint8_t     tt     = 0;     // Translation Type (0 = no translation)
    uint8_t     np     = 1;     // Numbering Plan: 1=E.164, 6=E.212 (IMSI)
    uint8_t     nai    = 4;     // NAI: 1=subscriber, 3=national, 4=international
    std::string digits;         // GT цифры E.164 (без '+', только цифры)
    bool has_gt() const { return gti != 0 && !digits.empty(); }
};

// Вспомогательная функция: кодирование одного BER TLV в буфер (short form, len<=127)
// Возвращает количество записанных байт (2 + vlen)
static int ber_tlv(uint8_t *buf, uint8_t tag, const uint8_t *val, uint8_t vlen) {
    buf[0] = tag;
    buf[1] = vlen;
    if (vlen > 0 && val) memcpy(buf + 2, val, vlen);
    return 2 + vlen;
}

// ──────────────────────────────────────────────────────────────
// Кодирование SCCP-адреса (Called/Calling Party Address)
// ITU-T Q.713 §3.4
//
// GTI=0 (route-on-SSN): [AI=0x42][SSN]  → 2 байта
// GTI=4 (route-on-GT):  [AI][SSN?][TT][NP|ES][OE|NAI][BCD...] → variable
//
// Address Indicator (AI):
//   bit8: reserved=0
//   bit7: RI  (Routing Indicator): 0=route-on-GT, 1=route-on-SSN/PC
//   bits6-3: GTI (4 бита): 0000=нет GT, 0100=GTI-4
//   bit2: SSI (SSN Indicator): 1=SSN присутствует
//   bit1: PCI (PC Indicator):  0=нет Point Code
// ──────────────────────────────────────────────────────────────
static int encode_sccp_addr(uint8_t *buf, const ScpAddr &a) {
    int pos = 0;
    if (a.has_gt()) {
        // AI: RI=0 (route-on-GT), GTI в битах[6:3], SSI, no PC
        buf[pos++] = (uint8_t)(((a.gti & 0x0F) << 2) | (a.ssn ? 0x02 : 0x00));
        if (a.ssn) buf[pos++] = a.ssn;
        buf[pos++] = a.tt;
        size_t nd  = a.digits.size();
        uint8_t es = (nd % 2 == 1) ? 0x01 : 0x02;  // 1=odd BCD, 2=even BCD
        buf[pos++] = (uint8_t)((a.np << 4) | es);
        // NAI байт: бит8=OE (1=нечётное кол-во цифр), биты7-1=NAI
        buf[pos++] = (uint8_t)((nd % 2 == 1 ? 0x80 : 0x00) | (a.nai & 0x7F));
        // BCD: два знака на байт, LSB-first; если нечётное — старший нибл=0xF
        for (size_t i = 0; i < nd; i += 2) {
            uint8_t lo = (uint8_t)(a.digits[i]   - '0');
            uint8_t hi = (i+1 < nd) ? (uint8_t)(a.digits[i+1] - '0') : 0x0F;
            buf[pos++] = (uint8_t)((hi << 4) | lo);
        }
    } else {
        // Route on SSN: AI=0x42 (RI=1, GTI=0, SSN present, no PC)
        buf[pos++] = 0x42;
        buf[pos++] = a.ssn;
    }
    return pos;
}

// ──────────────────────────────────────────────────────────────
// SCCP UDT (Unitdata, connectionless, ITU-T Q.713 §4.9)
//
// Формат:
//   MsgType(0x09) | PC(0x01) | Ptr_Called | Ptr_Calling | Ptr_Data
//   | CalledLen | CalledAddr(N) | CallingLen | CallingAddr(M) | DataLen | Data
//
// Pointer values (от позиции pointer-байта до length-байта поля):
//   ptr_called  = 3          (constant)
//   ptr_calling = called_len + 3
//   ptr_data    = calling_len + 2
// ──────────────────────────────────────────────────────────────
static struct msgb *wrap_in_sccp_udt(struct msgb *map_msg,
                                      const ScpAddr &called,
                                      const ScpAddr &calling) {
    if (!map_msg) return nullptr;

    struct msgb *sccp = msgb_alloc_headroom(512, 128, "SCCP UDT");
    if (!sccp) return nullptr;

    uint8_t called_buf[24], calling_buf[24];
    int called_len  = encode_sccp_addr(called_buf,  called);
    int calling_len = encode_sccp_addr(calling_buf, calling);

    uint8_t ptr_called  = 3;
    uint8_t ptr_calling = (uint8_t)(called_len  + 3);
    uint8_t ptr_data    = (uint8_t)(calling_len + 2);

    *(msgb_put(sccp, 1)) = 0x09;   // SCCP UDT
    *(msgb_put(sccp, 1)) = 0x01;   // Protocol Class 1 (return on error)
    *(msgb_put(sccp, 1)) = ptr_called;
    *(msgb_put(sccp, 1)) = ptr_calling;
    *(msgb_put(sccp, 1)) = ptr_data;

    *(msgb_put(sccp, 1)) = (uint8_t)called_len;
    memcpy(msgb_put(sccp, called_len), called_buf, called_len);

    *(msgb_put(sccp, 1)) = (uint8_t)calling_len;
    memcpy(msgb_put(sccp, calling_len), calling_buf, calling_len);

    *(msgb_put(sccp, 1)) = (uint8_t)map_msg->len;
    memcpy(msgb_put(sccp, map_msg->len), map_msg->data, map_msg->len);

    std::cout << COLOR_CYAN << "✓ MAP обернуто в SCCP UDT (connectionless)" << COLOR_RESET << "\n";
    auto print_sccp_addr = [&](const char *label, const ScpAddr &a) {
        if (a.has_gt()) {
            std::cout << COLOR_BLUE << "  " << label << " GT:  " << COLOR_GREEN << a.digits
                      << COLOR_RESET << " (GTI=" << (int)a.gti
                      << " TT=" << (int)a.tt << " NP=" << (int)a.np << " NAI=" << (int)a.nai << ")";
            if (a.ssn) std::cout << "  SSN=" << (int)a.ssn;
            std::cout << "\n";
        } else {
            std::cout << COLOR_BLUE << "  " << label << " SSN: " << COLOR_GREEN << (int)a.ssn
                      << COLOR_RESET << "\n";
        }
    };
    print_sccp_addr("Called ", called);
    print_sccp_addr("Calling", calling);
    std::cout << COLOR_BLUE << "  SCCP UDT размер: " << COLOR_GREEN << sccp->len
              << " байт" << COLOR_RESET << " (MAP/TCAP: " << map_msg->len << " байт)\n\n";

    return sccp;
}

// ──────────────────────────────────────────────────────────────
// MAP SendAuthenticationInfo (SAI) — 3GPP TS 29.002 §8.5.2
// Направление: MSC → HLR
// TCAP Begin, Invoke, opCode=56 (0x38), arg=IMSI
//
// Application Context OID (sendAuthInfoContext-v3):
//   0.4.0.0.1.0.57.3  → BER: 04 00 00 01 00 39 03
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_send_auth_info(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP SAI");
    if (!msg) return nullptr;

    // ── IMSI: BCD-кодирование (GSM-формат, тип IMSI=1, odd bit)
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0] - '0') << 4) | 0x09); // digit0 | odd | type=1
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i] - '0');
        uint8_t hi = (i + 1 < imsi_slen) ? (uint8_t)(imsi_str[i+1] - '0') : 0x0F;
        bcd_imsi[bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    // IMSI arg: IMPLICIT OCTET STRING (тег 0x04) в MAP SAI
    uint8_t imsi_ie[12];
    uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);

    // ── Invoke Component (тег 0xA1)
    uint8_t invoke_id[] = { 0x02, 0x01, 0x01 };        // INTEGER InvokeID=1
    uint8_t opcode[]    = { 0x02, 0x01, 0x38 };        // INTEGER OpCode=56 (SAI)
    uint8_t invoke_body[32]; uint8_t ib_len = 0;
    memcpy(invoke_body + ib_len, invoke_id, 3); ib_len += 3;
    memcpy(invoke_body + ib_len, opcode,    3); ib_len += 3;
    memcpy(invoke_body + ib_len, imsi_ie, imsi_ie_len); ib_len += imsi_ie_len;

    uint8_t invoke[48];
    uint8_t invoke_len = (uint8_t)ber_tlv(invoke, 0xA1, invoke_body, ib_len);

    // ── Component Portion (тег 0x6C)
    uint8_t comp_portion[56];
    uint8_t comp_len = (uint8_t)ber_tlv(comp_portion, 0x6C, invoke, invoke_len);

    // ── Dialogue Portion (тег 0x6B)
    // Application Context OID: sendAuthInfoContext-v3 = {0.4.0.0.1.0.57.3}
    // BER OID: первые два {0,4} → 0*40+4=0x04; далее 0,0,1,0,57,3
    uint8_t sai_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x39, 0x03 };
    uint8_t oid_tlv[12];
    uint8_t oid_tlv_len = (uint8_t)ber_tlv(oid_tlv, 0x06, sai_ac_oid, (uint8_t)sizeof(sai_ac_oid));

    // protocol-version: BIT STRING (version1 present = 0x80)
    uint8_t proto_ver[] = { 0x80, 0x02, 0x07, 0x80 };
    // application-context-name [1] EXPLICIT OID
    uint8_t acn[16];
    uint8_t acn_len = (uint8_t)ber_tlv(acn, 0xA1, oid_tlv, oid_tlv_len);

    // dialogueRequest (AARQ-apdu) [APPLICATION 0] (тег 0x60)
    uint8_t dr_body[40]; uint8_t dr_len = 0;
    memcpy(dr_body + dr_len, proto_ver, 4);     dr_len += 4;
    memcpy(dr_body + dr_len, acn, acn_len);     dr_len += acn_len;
    uint8_t dialogue_req[48];
    uint8_t dialogue_req_len = (uint8_t)ber_tlv(dialogue_req, 0x60, dr_body, dr_len);

    // single-ASN1-type [0] (тег 0xA0)
    uint8_t single_asn1[56];
    uint8_t single_asn1_len = (uint8_t)ber_tlv(single_asn1, 0xA0, dialogue_req, dialogue_req_len);

    // EXTERNAL (тег 0x28): OID диалогового AC + PDU
    // MAP Dialogue OID: {0.4.0.0.1.1.1.1} → BER: 04 00 00 01 01 01 01
    uint8_t dlg_oid_val[] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    uint8_t dlg_oid[12];
    uint8_t dlg_oid_len = (uint8_t)ber_tlv(dlg_oid, 0x06, dlg_oid_val, (uint8_t)sizeof(dlg_oid_val));

    uint8_t ext_body[80]; uint8_t ext_body_len = 0;
    memcpy(ext_body + ext_body_len, dlg_oid, dlg_oid_len);           ext_body_len += dlg_oid_len;
    memcpy(ext_body + ext_body_len, single_asn1, single_asn1_len);   ext_body_len += single_asn1_len;
    uint8_t ext_tlv[90];
    uint8_t ext_len = (uint8_t)ber_tlv(ext_tlv, 0x28, ext_body, ext_body_len);

    uint8_t dial_portion[100];
    uint8_t dial_len = (uint8_t)ber_tlv(dial_portion, 0x6B, ext_tlv, ext_len);

    // ── OTID (Originating Transaction ID), тег 0x48, 4 байта
    static uint32_t map_sai_tid = 0x00000001;
    uint8_t otid_val[4] = {
        (uint8_t)((map_sai_tid >> 24) & 0xFF), (uint8_t)((map_sai_tid >> 16) & 0xFF),
        (uint8_t)((map_sai_tid >>  8) & 0xFF), (uint8_t)( map_sai_tid        & 0xFF)
    };
    uint8_t otid[8];
    uint8_t otid_len = (uint8_t)ber_tlv(otid, 0x48, otid_val, 4);
    map_sai_tid++;

    // ── TCAP Begin (тег 0x62)
    uint8_t begin_body[256]; uint8_t bb_len = 0;
    memcpy(begin_body + bb_len, otid,         otid_len); bb_len += otid_len;
    memcpy(begin_body + bb_len, dial_portion, dial_len); bb_len += dial_len;
    memcpy(begin_body + bb_len, comp_portion, comp_len); bb_len += comp_len;

    *(msgb_put(msg, 1)) = 0x62;
    *(msgb_put(msg, 1)) = bb_len;
    memcpy(msgb_put(msg, bb_len), begin_body, bb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP SendAuthenticationInfo (SAI)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:    " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "56 (0x38) SendAuthInfo" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:     " << COLOR_GREEN << "0x" << std::hex
              << (map_sai_tid - 1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MAP/TCAP размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP SAI:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";

    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP UpdateLocation (UL) — 3GPP TS 29.002 §8.1.2
// Направление: MSC/VLR → HLR
// TCAP Begin, Invoke, opCode=2 (0x02)
// arg: UpdateLocationArg ::= SEQUENCE { imsi, msc-Number, vlr-Number }
//
// Application Context OID: networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
// BER OID: 04 00 00 01 00 01 03
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_update_location(const char *imsi_str,
                                                   const char *vlr_msisdn) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP UL");
    if (!msg) return nullptr;

    // ── IMSI (тег 0x04, BCD)
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0] - '0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i] - '0');
        uint8_t hi = (i + 1 < imsi_slen) ? (uint8_t)(imsi_str[i+1] - '0') : 0x0F;
        bcd_imsi[bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t imsi_ie[12];
    uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);

    // ── VLR-Number (тег 0x81 IMPLICIT OCTET STRING): TON/NPI + BCD номер
    size_t vlr_slen = strlen(vlr_msisdn);
    uint8_t bcd_vlr[9]; memset(bcd_vlr, 0, sizeof(bcd_vlr));
    bcd_vlr[0] = 0x91;  // TON=international, NPI=E.164
    size_t bv_len = 1;
    for (size_t i = 0; i < vlr_slen && bv_len < 9; i += 2) {
        uint8_t lo = (uint8_t)(vlr_msisdn[i] - '0');
        uint8_t hi = (i + 1 < vlr_slen) ? (uint8_t)(vlr_msisdn[i+1] - '0') : 0x0F;
        bcd_vlr[bv_len++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t vlr_ie[12];
    uint8_t vlr_ie_len = (uint8_t)ber_tlv(vlr_ie, 0x81, bcd_vlr, (uint8_t)bv_len);

    // ── MSC-Number = тот же номер (тег 0x82 IMPLICIT OCTET STRING)
    uint8_t msc_ie[12];
    uint8_t msc_ie_len = (uint8_t)ber_tlv(msc_ie, 0x82, bcd_vlr, (uint8_t)bv_len);

    // ── Invoke Component (0xA1): InvokeID + OpCode + SEQUENCE{imsi,msc,vlr}
    uint8_t invoke_id[] = { 0x02, 0x01, 0x01 };
    uint8_t opcode[]    = { 0x02, 0x01, 0x02 };  // OpCode=2 (UpdateLocation)
    // arg = UpdateLocationArg ::= SEQUENCE
    uint8_t seq_body[48]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie, imsi_ie_len); sq_len += imsi_ie_len;
    memcpy(seq_body + sq_len, msc_ie,  msc_ie_len);  sq_len += msc_ie_len;
    memcpy(seq_body + sq_len, vlr_ie,  vlr_ie_len);  sq_len += vlr_ie_len;
    uint8_t seq_tlv[56];
    uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    uint8_t invoke_body[72]; uint8_t ib_len = 0;
    memcpy(invoke_body + ib_len, invoke_id, 3);     ib_len += 3;
    memcpy(invoke_body + ib_len, opcode,    3);     ib_len += 3;
    memcpy(invoke_body + ib_len, seq_tlv, seq_len); ib_len += seq_len;
    uint8_t invoke[80];
    uint8_t invoke_len = (uint8_t)ber_tlv(invoke, 0xA1, invoke_body, ib_len);

    uint8_t comp_portion[88];
    uint8_t comp_len = (uint8_t)ber_tlv(comp_portion, 0x6C, invoke, invoke_len);

    // ── Dialogue Portion: networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
    uint8_t ul_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    uint8_t oid_tlv[12];
    uint8_t oid_tlv_len = (uint8_t)ber_tlv(oid_tlv, 0x06, ul_ac_oid, (uint8_t)sizeof(ul_ac_oid));

    uint8_t proto_ver[] = { 0x80, 0x02, 0x07, 0x80 };
    uint8_t acn[16];
    uint8_t acn_len = (uint8_t)ber_tlv(acn, 0xA1, oid_tlv, oid_tlv_len);

    uint8_t dr_body[40]; uint8_t dr_len = 0;
    memcpy(dr_body + dr_len, proto_ver, 4);  dr_len += 4;
    memcpy(dr_body + dr_len, acn, acn_len);  dr_len += acn_len;
    uint8_t dialogue_req[48];
    uint8_t dialogue_req_len = (uint8_t)ber_tlv(dialogue_req, 0x60, dr_body, dr_len);

    uint8_t single_asn1[56];
    uint8_t single_asn1_len = (uint8_t)ber_tlv(single_asn1, 0xA0, dialogue_req, dialogue_req_len);

    uint8_t dlg_oid_val[] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    uint8_t dlg_oid[12];
    uint8_t dlg_oid_len = (uint8_t)ber_tlv(dlg_oid, 0x06, dlg_oid_val, (uint8_t)sizeof(dlg_oid_val));

    uint8_t ext_body[80]; uint8_t ext_body_len = 0;
    memcpy(ext_body + ext_body_len, dlg_oid, dlg_oid_len);          ext_body_len += dlg_oid_len;
    memcpy(ext_body + ext_body_len, single_asn1, single_asn1_len);  ext_body_len += single_asn1_len;
    uint8_t ext_tlv[90];
    uint8_t ext_len = (uint8_t)ber_tlv(ext_tlv, 0x28, ext_body, ext_body_len);

    uint8_t dial_portion[100];
    uint8_t dial_len = (uint8_t)ber_tlv(dial_portion, 0x6B, ext_tlv, ext_len);

    // ── OTID
    static uint32_t map_ul_tid = 0x00000100;
    uint8_t otid_val[4] = {
        (uint8_t)((map_ul_tid >> 24) & 0xFF), (uint8_t)((map_ul_tid >> 16) & 0xFF),
        (uint8_t)((map_ul_tid >>  8) & 0xFF), (uint8_t)( map_ul_tid        & 0xFF)
    };
    uint8_t otid[8];
    uint8_t otid_len = (uint8_t)ber_tlv(otid, 0x48, otid_val, 4);
    map_ul_tid++;

    // ── TCAP Begin
    uint8_t begin_body[300]; uint8_t bb_len = 0;
    memcpy(begin_body + bb_len, otid,          otid_len); bb_len += otid_len;
    memcpy(begin_body + bb_len, dial_portion,  dial_len); bb_len += dial_len;
    memcpy(begin_body + bb_len, comp_portion,  comp_len); bb_len += comp_len;

    *(msgb_put(msg, 1)) = 0x62;
    *(msgb_put(msg, 1)) = bb_len;
    memcpy(msgb_put(msg, bb_len), begin_body, bb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP UpdateLocation (UL)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:       " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  VLR-Number: " << COLOR_GREEN << vlr_msisdn << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:     " << COLOR_GREEN << "2 (0x02) UpdateLocation" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:        " << COLOR_GREEN << "0x" << std::hex
              << (map_ul_tid - 1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MAP/TCAP размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP UL:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";

    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP CheckIMEI — 3GPP TS 29.002 §7.5.1
// Направление: MSC/VLR → EIR  (F-interface)
// TCAP Begin, Invoke, opCode=43 (0x2B)
// arg: CheckIMEI-Arg ::= SEQUENCE { imei, requested-equipment-info }
// Application Context OID: checkIMEI-context-v2 = {0.4.0.0.1.0.0.2.2}
// BER OID: 04 00 00 01 00 00 02 02
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_check_imei(const char *imei_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP CheckIMEI");
    if (!msg) return nullptr;

    // IMEI: BCD-кодирование (15 цифр, тег 0x04 IMPLICIT OCTET STRING)
    size_t imei_slen = strlen(imei_str);
    uint8_t bcd_imei[9]; memset(bcd_imei, 0, sizeof(bcd_imei));
    // IMEI type=IMEI(0xA0)|odd bit: первые две цифры → первый байт
    bcd_imei[0] = (uint8_t)(((imei_str[0] - '0') << 4) | 0x0A); // type=IMEI(2), odd
    size_t bcd_len = 1;
    for (size_t i = 1; i < imei_slen && bcd_len < 9; i += 2) {
        uint8_t lo = (uint8_t)(imei_str[i] - '0');
        uint8_t hi = (i + 1 < imei_slen) ? (uint8_t)(imei_str[i+1] - '0') : 0x0F;
        bcd_imei[bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t imei_ie[12];
    uint8_t imei_ie_len = (uint8_t)ber_tlv(imei_ie, 0x04, bcd_imei, (uint8_t)bcd_len);

    // requested-equipment-info: BIT STRING (equipmentStatus) = 0x00
    uint8_t req_info[] = { 0x03, 0x02, 0x07, 0x00 }; // BIT STRING, 1 байт, 7 неиспользованных бит

    // arg SEQUENCE
    uint8_t seq_body[32]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imei_ie, imei_ie_len); sq_len += imei_ie_len;
    memcpy(seq_body + sq_len, req_info, sizeof(req_info)); sq_len += sizeof(req_info);
    uint8_t seq_tlv[40];
    uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    // Invoke Component
    uint8_t invoke_id[] = { 0x02, 0x01, 0x01 };
    uint8_t opcode[]    = { 0x02, 0x01, 0x2B };  // opCode=43
    uint8_t invoke_body[64]; uint8_t ib_len = 0;
    memcpy(invoke_body + ib_len, invoke_id, 3);      ib_len += 3;
    memcpy(invoke_body + ib_len, opcode,    3);      ib_len += 3;
    memcpy(invoke_body + ib_len, seq_tlv, seq_len);  ib_len += seq_len;
    uint8_t invoke[72];
    uint8_t invoke_len = (uint8_t)ber_tlv(invoke, 0xA1, invoke_body, ib_len);
    uint8_t comp_portion[80];
    uint8_t comp_len = (uint8_t)ber_tlv(comp_portion, 0x6C, invoke, invoke_len);

    // Dialogue Portion: checkIMEI-context-v2 = {0.4.0.0.1.0.0.2.2}
    uint8_t ci_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02 };
    uint8_t oid_tlv[14];
    uint8_t oid_tlv_len = (uint8_t)ber_tlv(oid_tlv, 0x06, ci_ac_oid, (uint8_t)sizeof(ci_ac_oid));
    uint8_t proto_ver[] = { 0x80, 0x02, 0x07, 0x80 };
    uint8_t acn[18];
    uint8_t acn_len = (uint8_t)ber_tlv(acn, 0xA1, oid_tlv, oid_tlv_len);
    uint8_t dr_body[40]; uint8_t dr_len = 0;
    memcpy(dr_body + dr_len, proto_ver, 4);  dr_len += 4;
    memcpy(dr_body + dr_len, acn, acn_len);  dr_len += acn_len;
    uint8_t dialogue_req[48];
    uint8_t dialogue_req_len = (uint8_t)ber_tlv(dialogue_req, 0x60, dr_body, dr_len);
    uint8_t single_asn1[56];
    uint8_t single_asn1_len = (uint8_t)ber_tlv(single_asn1, 0xA0, dialogue_req, dialogue_req_len);
    uint8_t dlg_oid_val[] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    uint8_t dlg_oid[12];
    uint8_t dlg_oid_len = (uint8_t)ber_tlv(dlg_oid, 0x06, dlg_oid_val, sizeof(dlg_oid_val));
    uint8_t ext_body[80]; uint8_t ext_body_len = 0;
    memcpy(ext_body + ext_body_len, dlg_oid, dlg_oid_len);         ext_body_len += dlg_oid_len;
    memcpy(ext_body + ext_body_len, single_asn1, single_asn1_len); ext_body_len += single_asn1_len;
    uint8_t ext_tlv[90];
    uint8_t ext_len = (uint8_t)ber_tlv(ext_tlv, 0x28, ext_body, ext_body_len);
    uint8_t dial_portion[100];
    uint8_t dial_len = (uint8_t)ber_tlv(dial_portion, 0x6B, ext_tlv, ext_len);

    // OTID + TCAP Begin
    static uint32_t map_ci_tid = 0x00000200;
    uint8_t otid_val[4] = {
        (uint8_t)((map_ci_tid >> 24) & 0xFF), (uint8_t)((map_ci_tid >> 16) & 0xFF),
        (uint8_t)((map_ci_tid >>  8) & 0xFF), (uint8_t)( map_ci_tid        & 0xFF)
    };
    uint8_t otid[8];
    uint8_t otid_len = (uint8_t)ber_tlv(otid, 0x48, otid_val, 4);
    map_ci_tid++;

    uint8_t begin_body[300]; uint8_t bb_len = 0;
    memcpy(begin_body + bb_len, otid,         otid_len); bb_len += otid_len;
    memcpy(begin_body + bb_len, dial_portion, dial_len); bb_len += dial_len;
    memcpy(begin_body + bb_len, comp_portion, comp_len); bb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x62;
    *(msgb_put(msg, 1)) = bb_len;
    memcpy(msgb_put(msg, bb_len), begin_body, bb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP CheckIMEI" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMEI:    " << COLOR_GREEN << imei_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "43 (0x2B) CheckIMEI" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:     " << COLOR_GREEN << "0x" << std::hex
              << (map_ci_tid - 1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MAP/TCAP размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP CheckIMEI:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP PrepareHandover — 3GPP TS 29.002 §19.1.1
// Направление: Anchor MSC → Relay MSC  (E-interface)
// TCAP Begin, Invoke, opCode=68 (0x44)
// arg: PrepareHO-Arg ::= SEQUENCE { targetCellId, integrityProtectionInfo, ... }
// Application Context OID: handoverContext-v3 = {0.4.0.0.1.0.4.3}
// BER OID: 04 00 00 01 00 04 03
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_prepare_ho(uint16_t target_lac, uint16_t target_cell_id) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP PrepHO");
    if (!msg) return nullptr;

    // targetCellId [0] IMPLICIT OCTET STRING (2 байта: lac high/low)
    uint8_t cell_id_val[4] = {
        (uint8_t)(target_lac >> 8), (uint8_t)(target_lac & 0xFF),
        (uint8_t)(target_cell_id >> 8), (uint8_t)(target_cell_id & 0xFF)
    };
    uint8_t cell_ie[8];
    uint8_t cell_ie_len = (uint8_t)ber_tlv(cell_ie, 0x80, cell_id_val, 4);  // [0] IMPLICIT

    // SEQUENCE arg
    uint8_t seq_tlv[16];
    uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, cell_ie, cell_ie_len);

    // Invoke Component
    uint8_t invoke_id[] = { 0x02, 0x01, 0x01 };
    uint8_t opcode[]    = { 0x02, 0x01, 0x44 };  // opCode=68
    uint8_t invoke_body[32]; uint8_t ib_len = 0;
    memcpy(invoke_body + ib_len, invoke_id, 3);     ib_len += 3;
    memcpy(invoke_body + ib_len, opcode,    3);     ib_len += 3;
    memcpy(invoke_body + ib_len, seq_tlv, seq_len); ib_len += seq_len;
    uint8_t invoke[40];
    uint8_t invoke_len = (uint8_t)ber_tlv(invoke, 0xA1, invoke_body, ib_len);
    uint8_t comp_portion[48];
    uint8_t comp_len = (uint8_t)ber_tlv(comp_portion, 0x6C, invoke, invoke_len);

    // Dialogue Portion: handoverContext-v3 = {0.4.0.0.1.0.4.3}
    uint8_t ho_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x04, 0x03 };
    uint8_t oid_tlv[12];
    uint8_t oid_tlv_len = (uint8_t)ber_tlv(oid_tlv, 0x06, ho_ac_oid, sizeof(ho_ac_oid));
    uint8_t proto_ver[] = { 0x80, 0x02, 0x07, 0x80 };
    uint8_t acn[16];
    uint8_t acn_len = (uint8_t)ber_tlv(acn, 0xA1, oid_tlv, oid_tlv_len);
    uint8_t dr_body[40]; uint8_t dr_len = 0;
    memcpy(dr_body + dr_len, proto_ver, 4);  dr_len += 4;
    memcpy(dr_body + dr_len, acn, acn_len);  dr_len += acn_len;
    uint8_t dialogue_req[48];
    uint8_t dialogue_req_len = (uint8_t)ber_tlv(dialogue_req, 0x60, dr_body, dr_len);
    uint8_t single_asn1[56];
    uint8_t single_asn1_len = (uint8_t)ber_tlv(single_asn1, 0xA0, dialogue_req, dialogue_req_len);
    uint8_t dlg_oid_val[] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    uint8_t dlg_oid[12];
    uint8_t dlg_oid_len = (uint8_t)ber_tlv(dlg_oid, 0x06, dlg_oid_val, sizeof(dlg_oid_val));
    uint8_t ext_body[80]; uint8_t ext_body_len = 0;
    memcpy(ext_body + ext_body_len, dlg_oid, dlg_oid_len);         ext_body_len += dlg_oid_len;
    memcpy(ext_body + ext_body_len, single_asn1, single_asn1_len); ext_body_len += single_asn1_len;
    uint8_t ext_tlv[90];
    uint8_t ext_len = (uint8_t)ber_tlv(ext_tlv, 0x28, ext_body, ext_body_len);
    uint8_t dial_portion[100];
    uint8_t dial_len = (uint8_t)ber_tlv(dial_portion, 0x6B, ext_tlv, ext_len);

    // OTID + TCAP Begin
    static uint32_t map_ho_tid = 0x00000300;
    uint8_t otid_val[4] = {
        (uint8_t)((map_ho_tid >> 24) & 0xFF), (uint8_t)((map_ho_tid >> 16) & 0xFF),
        (uint8_t)((map_ho_tid >>  8) & 0xFF), (uint8_t)( map_ho_tid        & 0xFF)
    };
    uint8_t otid[8];
    uint8_t otid_len = (uint8_t)ber_tlv(otid, 0x48, otid_val, 4);
    map_ho_tid++;

    uint8_t begin_body[300]; uint8_t bb_len = 0;
    memcpy(begin_body + bb_len, otid,         otid_len); bb_len += otid_len;
    memcpy(begin_body + bb_len, dial_portion, dial_len); bb_len += dial_len;
    memcpy(begin_body + bb_len, comp_portion, comp_len); bb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x62;
    *(msgb_put(msg, 1)) = bb_len;
    memcpy(msgb_put(msg, bb_len), begin_body, bb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP PrepareHandover" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Target LAC:     " << COLOR_GREEN << target_lac     << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Target Cell ID: " << COLOR_GREEN << target_cell_id << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:         " << COLOR_GREEN << "68 (0x44) PrepareHO" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:            " << COLOR_GREEN << "0x" << std::hex
              << (map_ho_tid - 1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MAP/TCAP размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP PrepareHO:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP SendEndSignal — 3GPP TS 29.002 §19.1.5
// Направление: Relay MSC → Anchor MSC  (E-interface)
// TCAP Begin, Invoke, opCode=29 (0x1D)
// arg: SendEndSignal-Arg — NULL (пустой)
// Application Context OID: handoverContext-v3 = {0.4.0.0.1.0.4.3}
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_send_end_signal() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP SendEndSignal");
    if (!msg) return nullptr;

    // Invoke: opCode=29, arg=NULL
    uint8_t invoke_id[] = { 0x02, 0x01, 0x01 };
    uint8_t opcode[]    = { 0x02, 0x01, 0x1D };  // opCode=29
    uint8_t null_arg[]  = { 0x05, 0x00 };         // NULL
    uint8_t invoke_body[16]; uint8_t ib_len = 0;
    memcpy(invoke_body + ib_len, invoke_id, 3);       ib_len += 3;
    memcpy(invoke_body + ib_len, opcode,    3);       ib_len += 3;
    memcpy(invoke_body + ib_len, null_arg,  2);       ib_len += 2;
    uint8_t invoke[24];
    uint8_t invoke_len = (uint8_t)ber_tlv(invoke, 0xA1, invoke_body, ib_len);
    uint8_t comp_portion[32];
    uint8_t comp_len = (uint8_t)ber_tlv(comp_portion, 0x6C, invoke, invoke_len);

    // Dialogue Portion: handoverContext-v3
    uint8_t ho_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x04, 0x03 };
    uint8_t oid_tlv[12];
    uint8_t oid_tlv_len = (uint8_t)ber_tlv(oid_tlv, 0x06, ho_ac_oid, sizeof(ho_ac_oid));
    uint8_t proto_ver[] = { 0x80, 0x02, 0x07, 0x80 };
    uint8_t acn[16];
    uint8_t acn_len = (uint8_t)ber_tlv(acn, 0xA1, oid_tlv, oid_tlv_len);
    uint8_t dr_body[40]; uint8_t dr_len = 0;
    memcpy(dr_body + dr_len, proto_ver, 4);  dr_len += 4;
    memcpy(dr_body + dr_len, acn, acn_len);  dr_len += acn_len;
    uint8_t dialogue_req[48];
    uint8_t dialogue_req_len = (uint8_t)ber_tlv(dialogue_req, 0x60, dr_body, dr_len);
    uint8_t single_asn1[56];
    uint8_t single_asn1_len = (uint8_t)ber_tlv(single_asn1, 0xA0, dialogue_req, dialogue_req_len);
    uint8_t dlg_oid_val[] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    uint8_t dlg_oid[12];
    uint8_t dlg_oid_len = (uint8_t)ber_tlv(dlg_oid, 0x06, dlg_oid_val, sizeof(dlg_oid_val));
    uint8_t ext_body[80]; uint8_t ext_body_len = 0;
    memcpy(ext_body + ext_body_len, dlg_oid, dlg_oid_len);         ext_body_len += dlg_oid_len;
    memcpy(ext_body + ext_body_len, single_asn1, single_asn1_len); ext_body_len += single_asn1_len;
    uint8_t ext_tlv[90];
    uint8_t ext_len = (uint8_t)ber_tlv(ext_tlv, 0x28, ext_body, ext_body_len);
    uint8_t dial_portion[100];
    uint8_t dial_len = (uint8_t)ber_tlv(dial_portion, 0x6B, ext_tlv, ext_len);

    static uint32_t map_ses_tid = 0x00000400;
    uint8_t otid_val[4] = {
        (uint8_t)((map_ses_tid >> 24) & 0xFF), (uint8_t)((map_ses_tid >> 16) & 0xFF),
        (uint8_t)((map_ses_tid >>  8) & 0xFF), (uint8_t)( map_ses_tid        & 0xFF)
    };
    uint8_t otid[8];
    uint8_t otid_len = (uint8_t)ber_tlv(otid, 0x48, otid_val, 4);
    map_ses_tid++;

    uint8_t begin_body[300]; uint8_t bb_len = 0;
    memcpy(begin_body + bb_len, otid,         otid_len); bb_len += otid_len;
    memcpy(begin_body + bb_len, dial_portion, dial_len); bb_len += dial_len;
    memcpy(begin_body + bb_len, comp_portion, comp_len); bb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x62;
    *(msgb_put(msg, 1)) = bb_len;
    memcpy(msgb_put(msg, bb_len), begin_body, bb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP SendEndSignal" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "29 (0x1D) SendEndSignal" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:     " << COLOR_GREEN << "0x" << std::hex
              << (map_ses_tid - 1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MAP/TCAP размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP SendEndSignal:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP PrepareSubsequentHandover — 3GPP TS 29.002 §19.1.2
// Направление: Anchor MSC → Target MSC  (E-interface, второй хендовер)
// TCAP Begin, Invoke, opCode=69 (0x45)
// Application Context OID: handoverContext-v3 = {0.4.0.0.1.0.4.3}
// BER OID: 04 00 00 01 00 04 03
// arg: PrepSubsequentHO-Arg SEQUENCE (empty — минимальная реализация)
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_prep_subsequent_ho() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP PrepSubseqHO");
    if (!msg) return nullptr;

    // arg: empty SEQUENCE
    uint8_t empty_seq[4];
    uint8_t seq_len = (uint8_t)ber_tlv(empty_seq, 0x30, nullptr, 0);

    // Invoke Component: invokeId=1, opCode=69
    uint8_t invoke_id[] = { 0x02, 0x01, 0x01 };
    uint8_t opcode[]    = { 0x02, 0x01, 0x45 };  // opCode=69
    uint8_t invoke_body[32]; uint8_t ib_len = 0;
    memcpy(invoke_body + ib_len, invoke_id, 3);         ib_len += 3;
    memcpy(invoke_body + ib_len, opcode,    3);         ib_len += 3;
    memcpy(invoke_body + ib_len, empty_seq, seq_len);   ib_len += seq_len;
    uint8_t invoke[40];
    uint8_t invoke_len = (uint8_t)ber_tlv(invoke, 0xA1, invoke_body, ib_len);
    uint8_t comp_portion[48];
    uint8_t comp_len = (uint8_t)ber_tlv(comp_portion, 0x6C, invoke, invoke_len);

    // Dialogue Portion: handoverContext-v3 = {0.4.0.0.1.0.4.3}
    uint8_t ho_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x04, 0x03 };
    uint8_t oid_tlv[12];
    uint8_t oid_tlv_len = (uint8_t)ber_tlv(oid_tlv, 0x06, ho_ac_oid, sizeof(ho_ac_oid));
    uint8_t proto_ver[] = { 0x80, 0x02, 0x07, 0x80 };
    uint8_t acn[16];
    uint8_t acn_len = (uint8_t)ber_tlv(acn, 0xA1, oid_tlv, oid_tlv_len);
    uint8_t dr_body[40]; uint8_t dr_len = 0;
    memcpy(dr_body + dr_len, proto_ver, 4);  dr_len += 4;
    memcpy(dr_body + dr_len, acn, acn_len);  dr_len += acn_len;
    uint8_t dialogue_req[48];
    uint8_t dialogue_req_len = (uint8_t)ber_tlv(dialogue_req, 0x60, dr_body, dr_len);
    uint8_t single_asn1[56];
    uint8_t single_asn1_len = (uint8_t)ber_tlv(single_asn1, 0xA0, dialogue_req, dialogue_req_len);
    uint8_t dlg_oid_val[] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    uint8_t dlg_oid[12];
    uint8_t dlg_oid_len = (uint8_t)ber_tlv(dlg_oid, 0x06, dlg_oid_val, sizeof(dlg_oid_val));
    uint8_t ext_body[80]; uint8_t ext_body_len = 0;
    memcpy(ext_body + ext_body_len, dlg_oid, dlg_oid_len);         ext_body_len += dlg_oid_len;
    memcpy(ext_body + ext_body_len, single_asn1, single_asn1_len); ext_body_len += single_asn1_len;
    uint8_t ext_tlv[90];
    uint8_t ext_len = (uint8_t)ber_tlv(ext_tlv, 0x28, ext_body, ext_body_len);
    uint8_t dial_portion[100];
    uint8_t dial_len = (uint8_t)ber_tlv(dial_portion, 0x6B, ext_tlv, ext_len);

    static uint32_t map_psho_tid = 0x00000D00;
    uint8_t otid_val[4] = {
        (uint8_t)((map_psho_tid >> 24) & 0xFF), (uint8_t)((map_psho_tid >> 16) & 0xFF),
        (uint8_t)((map_psho_tid >>  8) & 0xFF), (uint8_t)( map_psho_tid        & 0xFF)
    };
    uint8_t otid[8];
    uint8_t otid_len = (uint8_t)ber_tlv(otid, 0x48, otid_val, 4);
    map_psho_tid++;

    uint8_t begin_body[300]; uint8_t bb_len = 0;
    memcpy(begin_body + bb_len, otid,         otid_len); bb_len += otid_len;
    memcpy(begin_body + bb_len, dial_portion, dial_len); bb_len += dial_len;
    memcpy(begin_body + bb_len, comp_portion, comp_len); bb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x62;
    *(msgb_put(msg, 1)) = bb_len;
    memcpy(msgb_put(msg, bb_len), begin_body, bb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP PrepareSubsequentHandover" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:          " << COLOR_GREEN << "69 (0x45) PrepSubseqHO" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:             " << COLOR_GREEN << "0x" << std::hex << (map_psho_tid - 1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MAP/TCAP размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP PrepSubseqHO:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP ProcessAccessSignalling — 3GPP TS 29.002 §19.1.3
// Направление: Target MSC → Anchor MSC  (E-interface)
// TCAP Begin, Invoke, opCode=33 (0x21)
// Application Context OID: handoverContext-v3 = {0.4.0.0.1.0.4.3}
// arg: ProcessAccessSignalling-Arg SEQUENCE {
//        an-APDU SEQUENCE { an-APDUType INTEGER 0, signalInfo OCTET STRING }
//      }
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_process_access_signalling() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ProcAccSig");
    if (!msg) return nullptr;

    // signalInfo: dummy BSSMAP-like OCTET STRING (4 bytes)
    uint8_t sig_data[]   = { 0x00, 0x08, 0x00, 0x01 };
    uint8_t sig_tlv[8];
    uint8_t sig_len = (uint8_t)ber_tlv(sig_tlv, 0x04, sig_data, sizeof(sig_data));

    // an-APDUType: INTEGER 0 = BSSMAP
    uint8_t apdu_type[] = { 0x02, 0x01, 0x00 };

    // an-APDU SEQUENCE
    uint8_t apdu_body[32]; uint8_t ab_len = 0;
    memcpy(apdu_body + ab_len, apdu_type, 3);     ab_len += 3;
    memcpy(apdu_body + ab_len, sig_tlv, sig_len); ab_len += sig_len;
    uint8_t apdu_seq[40];
    uint8_t apdu_seq_len = (uint8_t)ber_tlv(apdu_seq, 0x30, apdu_body, ab_len);

    // outer arg SEQUENCE
    uint8_t outer_seq[48];
    uint8_t outer_len = (uint8_t)ber_tlv(outer_seq, 0x30, apdu_seq, apdu_seq_len);

    // Invoke Component: invokeId=1, opCode=33
    uint8_t invoke_id[] = { 0x02, 0x01, 0x01 };
    uint8_t opcode[]    = { 0x02, 0x01, 0x21 };  // opCode=33
    uint8_t invoke_body[80]; uint8_t ib_len = 0;
    memcpy(invoke_body + ib_len, invoke_id,  3);         ib_len += 3;
    memcpy(invoke_body + ib_len, opcode,     3);         ib_len += 3;
    memcpy(invoke_body + ib_len, outer_seq, outer_len);  ib_len += outer_len;
    uint8_t invoke[96];
    uint8_t invoke_len = (uint8_t)ber_tlv(invoke, 0xA1, invoke_body, ib_len);
    uint8_t comp_portion[112];
    uint8_t comp_len = (uint8_t)ber_tlv(comp_portion, 0x6C, invoke, invoke_len);

    // Dialogue Portion: handoverContext-v3
    uint8_t ho_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x04, 0x03 };
    uint8_t oid_tlv[12];
    uint8_t oid_tlv_len = (uint8_t)ber_tlv(oid_tlv, 0x06, ho_ac_oid, sizeof(ho_ac_oid));
    uint8_t proto_ver[] = { 0x80, 0x02, 0x07, 0x80 };
    uint8_t acn[16];
    uint8_t acn_len = (uint8_t)ber_tlv(acn, 0xA1, oid_tlv, oid_tlv_len);
    uint8_t dr_body[40]; uint8_t dr_len = 0;
    memcpy(dr_body + dr_len, proto_ver, 4);  dr_len += 4;
    memcpy(dr_body + dr_len, acn, acn_len);  dr_len += acn_len;
    uint8_t dialogue_req[48];
    uint8_t dialogue_req_len = (uint8_t)ber_tlv(dialogue_req, 0x60, dr_body, dr_len);
    uint8_t single_asn1[56];
    uint8_t single_asn1_len = (uint8_t)ber_tlv(single_asn1, 0xA0, dialogue_req, dialogue_req_len);
    uint8_t dlg_oid_val[] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    uint8_t dlg_oid[12];
    uint8_t dlg_oid_len = (uint8_t)ber_tlv(dlg_oid, 0x06, dlg_oid_val, sizeof(dlg_oid_val));
    uint8_t ext_body[80]; uint8_t ext_body_len = 0;
    memcpy(ext_body + ext_body_len, dlg_oid, dlg_oid_len);         ext_body_len += dlg_oid_len;
    memcpy(ext_body + ext_body_len, single_asn1, single_asn1_len); ext_body_len += single_asn1_len;
    uint8_t ext_tlv[90];
    uint8_t ext_len = (uint8_t)ber_tlv(ext_tlv, 0x28, ext_body, ext_body_len);
    uint8_t dial_portion[100];
    uint8_t dial_len = (uint8_t)ber_tlv(dial_portion, 0x6B, ext_tlv, ext_len);

    static uint32_t map_pas_tid = 0x00000E00;
    uint8_t otid_val[4] = {
        (uint8_t)((map_pas_tid >> 24) & 0xFF), (uint8_t)((map_pas_tid >> 16) & 0xFF),
        (uint8_t)((map_pas_tid >>  8) & 0xFF), (uint8_t)( map_pas_tid        & 0xFF)
    };
    uint8_t otid[8];
    uint8_t otid_len = (uint8_t)ber_tlv(otid, 0x48, otid_val, 4);
    map_pas_tid++;

    uint8_t begin_body[400]; uint8_t bb_len = 0;
    memcpy(begin_body + bb_len, otid,         otid_len); bb_len += otid_len;
    memcpy(begin_body + bb_len, dial_portion, dial_len); bb_len += dial_len;
    memcpy(begin_body + bb_len, comp_portion, comp_len); bb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x62;
    *(msgb_put(msg, 1)) = bb_len;
    memcpy(msgb_put(msg, bb_len), begin_body, bb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP ProcessAccessSignalling" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:          " << COLOR_GREEN << "33 (0x21) ProcessAccessSignalling" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  AN-APDU Type:    " << COLOR_GREEN << "0 (BSSMAP)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:             " << COLOR_GREEN << "0x" << std::hex << (map_pas_tid - 1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MAP/TCAP размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ProcessAccessSignalling:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// Forward declaration for MAP TCAP helpers (defined later)
static uint8_t build_tcap_begin(uint8_t *out, uint32_t otid,
                                const uint8_t *ac_oid, uint8_t ac_oid_len,
                                uint8_t invoke_id, uint8_t op_code,
                                const uint8_t *invoke_arg, uint8_t invoke_arg_len);

// MAP SendIdentification (3GPP TS 29.002 §7.5.4)
// Новый MSC/VLR → Старый MSC/VLR  (E-interface, во время межузлового HO)
// Запрашивает AUTH vectors для абонента по IMSI
// opCode=55 (0x37), AC: handoverContext-v3 = {0.4.0.0.1.0.4.3}
static struct msgb *generate_map_send_identification(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP SendIdent");
    if (!msg) return nullptr;

    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    }
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);
    // numberOfRequestedVectors [3] IMPLICIT INTEGER = 3
    uint8_t num_vec[] = { 0x83, 0x01, 0x03 };
    uint8_t seq_body[32]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie, imsi_ie_len); sq_len += imsi_ie_len;
    memcpy(seq_body + sq_len, num_vec, 3); sq_len += 3;
    uint8_t seq_tlv[40]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    uint8_t ho_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x04, 0x03 };
    static uint32_t si_tid = 0x00000E00;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, si_tid++, ho_ac_oid, sizeof(ho_ac_oid), 0x01, 0x37, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP SendIdentification" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "55 (0x37)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:    " << COLOR_GREEN << "0x" << std::hex << (si_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP SendIdentification:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// MAP RestoreData (3GPP TS 29.002 §7.4.1)
// VLR → HLR  — VLR запрашивает восстановление данных после сбоя (restart)
// opCode=57 (0x39), AC: networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
static struct msgb *generate_map_restore_data(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP RestoreData");
    if (!msg) return nullptr;

    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    }
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);
    uint8_t seq_tlv[20]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, imsi_ie, imsi_ie_len);

    uint8_t lu_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    static uint32_t rd_tid = 0x00000F00;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, rd_tid++, lu_ac_oid, sizeof(lu_ac_oid), 0x01, 0x39, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP RestoreData" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "57 (0x39)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:    " << COLOR_GREEN << "0x" << std::hex << (rd_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP RestoreData:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// MAP ForwardCheckSS-Indication (3GPP TS 29.002 §10.4.1)
// HLR → VLR/SGSN  — уведомление о наличии ожидающих SS операций
// opCode=38 (0x26), пустой аргумент (NULL)
// AC: networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
static struct msgb *generate_map_forward_check_ss(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP FwdCheckSS");
    if (!msg) return nullptr;

    // Arg: NULL (no argument — just an empty SEQUENCE)
    uint8_t null_arg[4]; uint8_t null_len = (uint8_t)ber_tlv(null_arg, 0x30, nullptr, 0);
    uint8_t lu_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    static uint32_t fcs_tid = 0x00001000;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, fcs_tid++, lu_ac_oid, sizeof(lu_ac_oid), 0x01, 0x26, null_arg, null_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP ForwardCheckSS-Indication" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "38 (0x26)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:    " << COLOR_GREEN << "0x" << std::hex << (fcs_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ForwardCheckSS:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// MAP NoteSubscriberPresent (3GPP TS 29.002 §7.3.2)
// MSC/VLR → HLR  — уведомление о появлении абонента (после MSRN получен)
// opCode=48 (0x30), AC: networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
// Arg: IMSI
static struct msgb *generate_map_note_subscriber_present(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP NoteSubsPresent");
    if (!msg) return nullptr;

    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2)
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);
    uint8_t seq_tlv[20]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, imsi_ie, imsi_ie_len);

    uint8_t lu_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    static uint32_t nsp_tid = 0x00001100;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, nsp_tid++, lu_ac_oid, sizeof(lu_ac_oid), 0x01, 0x30, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP NoteSubscriberPresent" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "48 (0x30)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP NoteSubscriberPresent:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// MAP ReadyForSM (3GPP TS 29.002 §13.4.2)
// MSC/VLR → HLR  — сигнал о готовности к получению короткого сообщения
// opCode=66 (0x42), AC: shortMsgAlertContext-v2 = {0.4.0.0.1.0.23.2}
static struct msgb *generate_map_ready_for_sm(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ReadyForSM");
    if (!msg) return nullptr;

    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2)
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);
    // alertReason [1] IMPLICIT INTEGER (msPresent=0)
    uint8_t alert_reason[] = { 0x81, 0x01, 0x00 };
    uint8_t seq_body[32]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie, imsi_ie_len); sq_len += imsi_ie_len;
    memcpy(seq_body + sq_len, alert_reason, 3); sq_len += 3;
    uint8_t seq_tlv[40]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    uint8_t sms_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x17, 0x02 };
    static uint32_t rfsm_tid = 0x00001200;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, rfsm_tid++, sms_ac_oid, sizeof(sms_ac_oid), 0x01, 0x42, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP ReadyForSM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "66 (0x42)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ReadyForSM:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// MAP ProvideSubscriberInfo (3GPP TS 29.002 §7.5.2)
// HLR → VLR — запрос информации о местонахождении абонента
// opCode=70 (0x46), AC: networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
static struct msgb *generate_map_provide_subscriber_info(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP PSI");
    if (!msg) return nullptr;

    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2)
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);
    // RequestedInfo [1] IMPLICIT SEQUENCE = {locationInformation [0], subscriberState [1]}
    uint8_t req_info[] = { 0xa1, 0x04, 0x80, 0x00, 0x81, 0x00 };  // loc + state
    uint8_t seq_body[40]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie, imsi_ie_len); sq_len += imsi_ie_len;
    memcpy(seq_body + sq_len, req_info, sizeof(req_info)); sq_len += (uint8_t)sizeof(req_info);
    uint8_t seq_tlv[50]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    uint8_t lu_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    static uint32_t psi_tid = 0x00001300;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, psi_tid++, lu_ac_oid, sizeof(lu_ac_oid), 0x01, 0x46, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP ProvideSubscriberInfo" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "70 (0x46)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ProvideSubscriberInfo:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// MAP SendIMSI (3GPP TS 29.002 §7.5.5)
// VLR → HLR — получить IMSI по MSISDN (для incoming call routing)
// opCode=58 (0x3A), AC: mobileInfoContext-v3 = {0.4.0.0.1.0.9.3}
static struct msgb *generate_map_send_imsi(const char *msisdn_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP SendIMSI");
    if (!msg) return nullptr;

    // MSISDN in BCD (E.164)
    size_t mslen = strlen(msisdn_str);
    uint8_t bcd_ms[10]; memset(bcd_ms, 0xff, sizeof(bcd_ms));
    bcd_ms[0] = 0x91;  // ToN=international, NPI=ISDN
    size_t bcd_len = 1;
    for (size_t i = 0; i < mslen && bcd_len < 10; i += 2) {
        uint8_t lo = (uint8_t)(msisdn_str[i] - '0');
        uint8_t hi = (i+1 < mslen) ? (uint8_t)(msisdn_str[i+1] - '0') : 0x0F;
        bcd_ms[bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t ms_ie[14]; uint8_t ms_ie_len = (uint8_t)ber_tlv(ms_ie, 0x04, bcd_ms, (uint8_t)bcd_len);
    uint8_t seq_tlv[20]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, ms_ie, ms_ie_len);

    uint8_t mi_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x09, 0x03 };
    static uint32_t si_tid2 = 0x00001400;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, si_tid2++, mi_ac_oid, sizeof(mi_ac_oid), 0x01, 0x3A, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP SendIMSI" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MSISDN: " << COLOR_GREEN << msisdn_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "58 (0x3A)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP SendIMSI:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// MAP CheckIMEI Result (3GPP TS 29.002 §7.5.3.2)
// F-interface: MSC/VLR → EIR — ответ EIR на запрос CheckIMEI (opCode=43)
// TCAP ReturnResult с EquipmentStatus (0=whiteListed, 1=blackListed, 2=greyListed)
// MAP CheckIMEI Result (3GPP TS 29.002 §7.5.3.2)
// F-interface: EIR → MSC/VLR  — ответ EIR на запрос CheckIMEI (opCode=43)
// TCAP End с ReturnResult-Last, EquipmentStatus
static struct msgb *generate_map_check_imei_res(uint8_t equip_status) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP CheckIMEI Res");
    if (!msg) return nullptr;
    // Encode EquipmentStatus [0x0A tag=ENUMERATED, len=1, value]
    uint8_t eq_tlv[4]; uint8_t eq_len = (uint8_t)ber_tlv(eq_tlv, 0x0A, &equip_status, 1);
    // Sequence wrapping opCode + EquipmentStatus
    uint8_t opcode_val = 0x2B;  // 43 decimal
    uint8_t op_tlv[4]; uint8_t op_len = (uint8_t)ber_tlv(op_tlv, 0x02, &opcode_val, 1);
    uint8_t seq_body[20]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, op_tlv, op_len); sq_len += op_len;
    memcpy(seq_body + sq_len, eq_tlv, eq_len); sq_len += eq_len;
    uint8_t seq_tlv_buf[30]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv_buf, 0x30, seq_body, sq_len);

    // Use mobileInfoContext-v3 as application context for check-IMEI result
    static const uint8_t ci_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x03 };
    static uint32_t ci_tid = 0x00001500;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, ci_tid++, ci_ac_oid, sizeof(ci_ac_oid), 0x01, 0x2B, seq_tlv_buf, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    const char *es = (equip_status==0)?"White-listed":(equip_status==1)?"Black-listed":"Grey-listed";
    std::cout << COLOR_CYAN << "✓ MAP CheckIMEI Result" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=43  EquipmentStatus: " << COLOR_GREEN << es
              << " (" << (int)equip_status << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP AnyTimeModification (3GPP TS 29.002 §7.5.9.3)
// C-interface: MSC/VLR → HLR  — изменение услуг абонента в реальном времени
// opCode=65 (0x41), ApplicationContext: anyTimeInfoHandlingContext-v3
// MAP AnyTimeModification (3GPP TS 29.002 §7.5.9.3)
// C-interface: MSC/VLR → HLR  — изменение услуг абонента в реальном времени
// opCode=65 (0x41), ApplicationContext: anyTimeInfoHandlingContext-v3
static struct msgb *generate_map_any_time_modification(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP AnyTimeMod");
    if (!msg) return nullptr;
    // Encode IMSI as TBCD
    size_t slen = strlen(imsi_str);
    uint8_t bcd_imsi[8]; memset(bcd_imsi, 0xFF, sizeof(bcd_imsi));
    size_t blen = 0;
    for (size_t i = 0; i < slen && blen < 8; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i] - '0');
        uint8_t hi = (i+1 < slen) ? (uint8_t)(imsi_str[i+1] - '0') : 0x0F;
        bcd_imsi[blen++] = (uint8_t)((hi << 4) | lo);
    }
    // subscriberIdentity [0] CHOICE { imsi IMSI }  — IMSI wrapped in [0]
    uint8_t imsi_os[12]; uint8_t imsi_os_len = (uint8_t)ber_tlv(imsi_os, 0x04, bcd_imsi, (uint8_t)blen);
    // [0] IMPLICIT wrapping for imsi choice
    uint8_t imsi_ctx[14]; uint8_t imsi_ctx_len = (uint8_t)ber_tlv(imsi_ctx, 0xA0, imsi_os, imsi_os_len);
    uint8_t seq_tlv[20]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, imsi_ctx, imsi_ctx_len);
    // OID: anyTimeInfoHandlingContext-v3 {0.4.0.0.1.0.64.3}
    static const uint8_t oid_atm[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x40, 0x03 };
    static uint32_t atm_tid = 0x00001600;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, atm_tid++, oid_atm, sizeof(oid_atm), 0x01, 0x41, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);
    std::cout << COLOR_CYAN << "✓ MAP AnyTimeModification" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=65(0x41)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP ActivateTraceMode (3GPP TS 29.002 §7.5.12.1)
// C-interface: HLR → VLR  — активация трассировки для абонента
// opCode=50 (0x32), ApplicationContext: tracingContext-v3
// MAP ActivateTraceMode (3GPP TS 29.002 §7.5.12.1)
// C-interface: HLR → VLR  — активация трассировки для абонента
// opCode=50 (0x32), ApplicationContext: tracingContext-v3
static struct msgb *generate_map_activate_trace_mode(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ActivTrace");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd_imsi[8]; memset(bcd_imsi, 0xFF, sizeof(bcd_imsi));
    size_t blen = 0;
    for (size_t i = 0; i < slen && blen < 8; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i] - '0');
        uint8_t hi = (i+1 < slen) ? (uint8_t)(imsi_str[i+1] - '0') : 0x0F;
        bcd_imsi[blen++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t imsi_os[12]; uint8_t imsi_os_len = (uint8_t)ber_tlv(imsi_os, 0x04, bcd_imsi, (uint8_t)blen);
    uint8_t imsi_ctx[14]; uint8_t imsi_ctx_len = (uint8_t)ber_tlv(imsi_ctx, 0xA0, imsi_os, imsi_os_len);
    // TraceId: INTEGER = 1
    uint8_t trace_id_val = 0x01;
    uint8_t tid_tlv[4]; uint8_t tid_len = (uint8_t)ber_tlv(tid_tlv, 0x02, &trace_id_val, 1);
    // TraceType: INTEGER = 1 (ISDN-PBX)
    uint8_t trace_type_val = 0x01;
    uint8_t ttype_tlv[4]; uint8_t ttype_len = (uint8_t)ber_tlv(ttype_tlv, 0x02, &trace_type_val, 1);
    uint8_t seq_body[40]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ctx, imsi_ctx_len); sq_len += imsi_ctx_len;
    memcpy(seq_body + sq_len, tid_tlv, tid_len); sq_len += tid_len;
    memcpy(seq_body + sq_len, ttype_tlv, ttype_len); sq_len += ttype_len;
    uint8_t seq_tlv[50]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);
    // OID: tracingContext-v3 {0.4.0.0.1.0.36.3}
    static const uint8_t oid_trace[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x24, 0x03 };
    static uint32_t trace_tid = 0x00001700;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, trace_tid++, oid_trace, sizeof(oid_trace), 0x01, 0x32, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);
    std::cout << COLOR_CYAN << "✓ MAP ActivateTraceMode" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=50(0x32)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP NotifySubscriberData (TS 29.002 §7.5.2b — informational)
// C-interface: HLR → VLR — уведомление об изменении данных подписчика
// opCode=120 (0x78), AC: networkLocUpContext-v3
static struct msgb *generate_map_notify_subscriber_data(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP NtfySubData");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd_imsi[8]; memset(bcd_imsi, 0xFF, sizeof(bcd_imsi));
    size_t blen = 0;
    for (size_t i = 0; i < slen && blen < 8; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i]-'0');
        uint8_t hi = (i+1<slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F;
        bcd_imsi[blen++] = (uint8_t)((hi<<4)|lo);
    }
    uint8_t imsi_os[12]; uint8_t imsi_os_len = (uint8_t)ber_tlv(imsi_os, 0x04, bcd_imsi, (uint8_t)blen);
    uint8_t imsi_ctx[14]; uint8_t imsi_ctx_len = (uint8_t)ber_tlv(imsi_ctx, 0xA0, imsi_os, imsi_os_len);
    uint8_t seq_tlv[20]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, imsi_ctx, imsi_ctx_len);
    // networkLocUpContext-v3 {0.4.0.0.1.0.1.3}
    static const uint8_t oid_nlu[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    static uint32_t nsd_tid = 0x00001800;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, nsd_tid++, oid_nlu, sizeof(oid_nlu), 0x01, 0x78, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);
    std::cout << COLOR_CYAN << "✓ MAP NotifySubscriberData" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=120(0x78)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP ForwardAccessSignalling (3GPP TS 29.002 §7.4.3)
// E-interface: новый MSC → старый MSC  — пересылка сигнальных данных при HO
// opCode=33 (0x21), AC: handoverContext-v3 = {0.4.0.0.1.0.4.3}
static struct msgb *generate_map_forward_access_signalling(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP FwdAccSig");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd_imsi[8]; memset(bcd_imsi, 0xFF, sizeof(bcd_imsi));
    size_t blen = 0;
    for (size_t i = 0; i < slen && blen < 8; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i]-'0');
        uint8_t hi = (i+1<slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F;
        bcd_imsi[blen++] = (uint8_t)((hi<<4)|lo);
    }
    uint8_t imsi_os[12]; uint8_t imsi_os_len = (uint8_t)ber_tlv(imsi_os, 0x04, bcd_imsi, (uint8_t)blen);
    // SignalInfo: OCTET STRING with dummy content (BSS Map Info)
    uint8_t sig_data[] = {0x00, 0x05, 0x04, 0x01, 0x11};  // dummy DTAP signal info
    uint8_t sig_os[10]; uint8_t sig_os_len = (uint8_t)ber_tlv(sig_os, 0x04, sig_data, sizeof(sig_data));
    uint8_t seq_body[40]; uint8_t sq_len = 0;
    memcpy(seq_body+sq_len, imsi_os, imsi_os_len); sq_len += imsi_os_len;
    memcpy(seq_body+sq_len, sig_os, sig_os_len); sq_len += sig_os_len;
    uint8_t seq_tlv[50]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);
    // handoverContext-v3 {0.4.0.0.1.0.4.3}
    static const uint8_t oid_ho[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x04, 0x03 };
    static uint32_t fas_tid = 0x00001900;
    uint8_t pdu[256];
    uint8_t pdu_len = build_tcap_begin(pdu, fas_tid++, oid_ho, sizeof(oid_ho), 0x01, 0x21, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);
    std::cout << COLOR_CYAN << "✓ MAP ForwardAccessSignalling" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=33(0x21)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP UpdateGPRSLocation (3GPP TS 29.002 §7.3.2.3)
// Gs-interface: SGSN → HLR — регистрация GPRS-местоположения
// opCode=68 (0x44), AC: gprsLocationUpdateContext-v1 {0.4.0.0.1.0.0x1e.1}
static struct msgb *generate_map_update_gprs_location(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP UpdGPRSLoc");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd[8]; memset(bcd, 0xFF, 8);
    for (size_t i=0,b=0; i<slen && b<8; i+=2)
        bcd[b++] = (uint8_t)( (imsi_str[i]-'0') | (((i+1<slen)?imsi_str[i+1]-'0':0x0F)<<4) );
    uint8_t imsi_os[12]; uint8_t iol = (uint8_t)ber_tlv(imsi_os, 0x04, bcd, (uint8_t)((slen+1)/2));
    // SGSN number: dummy E.164 as OCTET STRING
    uint8_t sgsn_nb[] = {0x91, 0x79, 0x21, 0x43, 0x65, 0x87};
    uint8_t sgsn_os[10]; uint8_t sol = (uint8_t)ber_tlv(sgsn_os, 0x04, sgsn_nb, sizeof(sgsn_nb));
    uint8_t seq_body[40]; uint8_t sq = 0;
    memcpy(seq_body+sq, imsi_os, iol); sq += iol;
    memcpy(seq_body+sq, sgsn_os, sol); sq += sol;
    uint8_t seq[50]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, seq_body, sq);
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x1e,0x01};
    static uint32_t tid = 0x00001A00;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x44, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP UpdateGPRSLocation" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=68(0x44)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP SendRoutingInfoForGPRS (3GPP TS 29.002 §7.3.2.2)
// Gs-interface: SGSN ↔ HLR — запрос маршрутизации пакетных данных
// opCode=24 (0x18), AC: gprsLocationInfoRetrievalContext-v3 {0.4.0.0.1.0.0x1f.3}
static struct msgb *generate_map_send_routeing_info_gprs(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP SriGPRS");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd[8]; memset(bcd, 0xFF, 8);
    for (size_t i=0,b=0; i<slen && b<8; i+=2)
        bcd[b++] = (uint8_t)( (imsi_str[i]-'0') | (((i+1<slen)?imsi_str[i+1]-'0':0x0F)<<4) );
    uint8_t imsi_os[12]; uint8_t iol = (uint8_t)ber_tlv(imsi_os, 0x04, bcd, (uint8_t)((slen+1)/2));
    uint8_t seq[20]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, imsi_os, iol);
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x1f,0x03};
    static uint32_t tid = 0x00001B00;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x18, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP SendRoutingInfoForGPRS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=24(0x18)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP Reset (3GPP TS 29.002 §7.2.1)
// C-interface: HLR → VLR — перезапуск узла (при рестарте)
// opCode=37 (0x25), AC: networkLocUpContext-v3 {0.4.0.0.1.0.1.3}
// Argument: HLR-Id (NULL)
static struct msgb *generate_map_reset(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP Reset");
    if (!msg) return nullptr;
    // Empty SEQUENCE argument
    uint8_t seq[4]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, nullptr, 0);
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x01,0x03};
    static uint32_t tid = 0x00001C00;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x25, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP Reset" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=37(0x25)  (HLR → VLR)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP BeginSubscriberActivity (3GPP TS 29.002 §7.3.1.6)
// C-interface: VLR → HLR — уведомление о начале активности подписчика
// opCode=131 (0x83), AC: subscriptionDataMgmt-v3 {0.4.0.0.1.0.14.3}
static struct msgb *generate_map_begin_subscriber_activity(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP BeginSubAct");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd[8]; memset(bcd, 0xFF, 8);
    for (size_t i=0,b=0; i<slen && b<8; i+=2)
        bcd[b++] = (uint8_t)( (imsi_str[i]-'0') | (((i+1<slen)?imsi_str[i+1]-'0':0x0F)<<4) );
    uint8_t imsi_os[12]; uint8_t iol = (uint8_t)ber_tlv(imsi_os, 0x04, bcd, (uint8_t)((slen+1)/2));
    uint8_t seq[20]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, imsi_os, iol);
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x0e,0x03};
    static uint32_t tid = 0x00001D00;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x83, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP BeginSubscriberActivity" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=131(0x83)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP DeactivateTraceMode (3GPP TS 29.002 §7.6.7)
// C-interface: MSC/VLR → HLR  — остановка активной трассировки
// opCode=51 (0x33), AC: tracingContext-v3 {0.4.0.0.1.0.36.3}
static struct msgb *generate_map_deactivate_trace_mode(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP DeactTrace");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd[8]; memset(bcd, 0xFF, 8);
    for (size_t i=0,b=0; i<slen && b<8; i+=2)
        bcd[b++] = (uint8_t)( (imsi_str[i]-'0') | (((i+1<slen)?imsi_str[i+1]-'0':0x0F)<<4) );
    uint8_t imsi_os[12]; uint8_t iol = (uint8_t)ber_tlv(imsi_os, 0x04, bcd, (uint8_t)((slen+1)/2));
    // TraceReference: INTEGER, dummy value 42
    uint8_t tref_val = 42;
    uint8_t tref[4]; uint8_t tl = (uint8_t)ber_tlv(tref, 0x02, &tref_val, 1);
    uint8_t seq_body[32]; uint8_t sq = 0;
    memcpy(seq_body+sq, imsi_os, iol); sq += iol;
    memcpy(seq_body+sq, tref, tl); sq += tl;
    uint8_t seq[40]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, seq_body, sq);
    // tracingContext-v3 OID: {0.4.0.0.1.0.36.3}
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x24,0x03};
    static uint32_t tid = 0x00001E00;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x33, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP DeactivateTraceMode" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=51(0x33)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP ProcessUnstructuredSS-Request (3GPP TS 29.002 §7.7.3)
// A-interface: MS → MSC — инициированный абонентом USSD-запрос (pull)
// opCode=59 (0x3B), AC: networkUnstructuredSsContext-v2 {0.4.0.0.1.0.19.2}
static struct msgb *generate_map_process_unstructured_ss_req(const char *ussd_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ProcUSSD");
    if (!msg) return nullptr;
    // USSD DataCodingScheme: 0x0F (GSM default alphabet, language unspecified)
    uint8_t dcs = 0x0F;
    uint8_t dcs_os[4]; uint8_t dl = (uint8_t)ber_tlv(dcs_os, 0x04, &dcs, 1);
    // USSD String: raw bytes of GSM7 packed text
    size_t slen = strlen(ussd_str);
    uint8_t ussd_os[64]; uint8_t ul = (uint8_t)ber_tlv(ussd_os, 0x04,
        (const uint8_t*)ussd_str, (uint8_t)(slen > 30 ? 30 : slen));
    uint8_t seq_body[80]; uint8_t sq = 0;
    memcpy(seq_body+sq, dcs_os, dl); sq += dl;
    memcpy(seq_body+sq, ussd_os, ul); sq += ul;
    uint8_t seq[90]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, seq_body, sq);
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x13,0x02};
    static uint32_t tid = 0x00001F00;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x3B, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP ProcessUnstructuredSS-Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=59(0x3B)  USSD: " << COLOR_GREEN << ussd_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP UnstructuredSS-Request (3GPP TS 29.002 §7.7.5)
// A-interface: MSC → MS — сетевой USSD-запрос (push, ожидает ответа)
// opCode=60 (0x3C), AC: networkUnstructuredSsContext-v2
static struct msgb *generate_map_unstructured_ss_request(const char *ussd_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP UssRequest");
    if (!msg) return nullptr;
    uint8_t dcs = 0x0F;
    uint8_t dcs_os[4]; uint8_t dl = (uint8_t)ber_tlv(dcs_os, 0x04, &dcs, 1);
    size_t slen = strlen(ussd_str);
    uint8_t ussd_os[64]; uint8_t ul = (uint8_t)ber_tlv(ussd_os, 0x04,
        (const uint8_t*)ussd_str, (uint8_t)(slen > 30 ? 30 : slen));
    uint8_t seq_body[80]; uint8_t sq = 0;
    memcpy(seq_body+sq, dcs_os, dl); sq += dl;
    memcpy(seq_body+sq, ussd_os, ul); sq += ul;
    uint8_t seq[90]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, seq_body, sq);
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x13,0x02};
    static uint32_t tid = 0x00002000;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x3C, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP UnstructuredSS-Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=60(0x3C)  USSD: " << COLOR_GREEN << ussd_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP SubscriberDataModificationNotification (3GPP TS 29.002 §7.3.2.6)
// C-interface: HLR → VLR — уведомление VLR об изменении данных подписчика
// opCode=100 (0x64), AC: subscriptionDataMgmt-v3 {0.4.0.0.1.0.14.3}
static struct msgb *generate_map_subscriber_data_modification(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP SubDataMod");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd[8]; memset(bcd, 0xFF, 8);
    for (size_t i=0,b=0; i<slen && b<8; i+=2)
        bcd[b++] = (uint8_t)( (imsi_str[i]-'0') | (((i+1<slen)?imsi_str[i+1]-'0':0x0F)<<4) );
    uint8_t imsi_os[12]; uint8_t iol = (uint8_t)ber_tlv(imsi_os, 0x04, bcd, (uint8_t)((slen+1)/2));
    uint8_t seq[20]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, imsi_os, iol);
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x0e,0x03};
    static uint32_t tid = 0x00002100;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x64, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP SubscriberDataModificationNotification" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=100(0x64)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ─────────────────────────────────────────────────────────────────────────────
// P31: MAP AnyTimeInterrogation / NoteMM-Event / InformSC / AlertSC
// ─────────────────────────────────────────────────────────────────────────────

// MAP AnyTimeInterrogation (3GPP TS 29.002 §7.3.7)
// opCode=71 (0x47)  HLR/VLR  — запрос данных абонента в реальном времени
// AC: anyTimeInfoEnquiryContext-v3 = {0.4.0.0.1.0.26.3}
static struct msgb *generate_map_any_time_interrogation(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ATInterrog");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd[8]; memset(bcd, 0xFF, 8);
    for (size_t i=0,b=0; i<slen && b<8; i+=2)
        bcd[b++] = (uint8_t)( (imsi_str[i]-'0') | (((i+1<slen)?imsi_str[i+1]-'0':0x0F)<<4) );
    uint8_t imsi_os[12]; uint8_t iol = (uint8_t)ber_tlv(imsi_os, 0x04, bcd, (uint8_t)((slen+1)/2));
    // RequestedInfo: empty SEQUENCE (context[1]) — request all available info
    uint8_t req_info[2]; req_info[0] = 0xA1; req_info[1] = 0x00;
    uint8_t seq_body[32]; uint8_t sq = 0;
    memcpy(seq_body+sq, imsi_os, iol); sq += iol;
    memcpy(seq_body+sq, req_info, 2); sq += 2;
    uint8_t seq[48]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, seq_body, sq);
    // anyTimeInfoEnquiryContext-v3 OID: {0.4.0.0.1.0.26.3}
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x1A,0x03};
    static uint32_t tid = 0x00002200;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x47, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP AnyTimeInterrogation" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=71(0x47)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP NoteMM-Event (3GPP TS 29.002 §7.6.11)
// opCode=99 (0x63)  VLR → HLR  — уведомление о событии MM (attach/detach)
// AC: mmEventReportingContext-v3 = {0.4.0.0.1.0.34.3}
static struct msgb *generate_map_note_mm_event(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP NoteMM");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd[8]; memset(bcd, 0xFF, 8);
    for (size_t i=0,b=0; i<slen && b<8; i+=2)
        bcd[b++] = (uint8_t)( (imsi_str[i]-'0') | (((i+1<slen)?imsi_str[i+1]-'0':0x0F)<<4) );
    uint8_t imsi_os[12]; uint8_t iol = (uint8_t)ber_tlv(imsi_os, 0x04, bcd, (uint8_t)((slen+1)/2));
    // serviceKey: INTEGER 0 (context [0])
    uint8_t skey[] = {0x80, 0x01, 0x00};
    // eventData: OCTET STRING dummy (context [1])
    uint8_t evdata[] = {0x81, 0x01, 0x01};
    uint8_t seq_body[32]; uint8_t sq = 0;
    memcpy(seq_body+sq, imsi_os, iol); sq += iol;
    memcpy(seq_body+sq, skey, 3); sq += 3;
    memcpy(seq_body+sq, evdata, 3); sq += 3;
    uint8_t seq[48]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, seq_body, sq);
    // mmEventReportingContext-v3 OID: {0.4.0.0.1.0.34.3}
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x22,0x03};
    static uint32_t tid = 0x00002300;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x63, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP NoteMM-Event" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=99(0x63)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP InformServiceCentre (3GPP TS 29.002 §7.7.16)
// opCode=63 (0x3F)  MSC/VLR → SC  — уведомление SMSC о наличии абонента
// AC: mwdMngtContext-v3 = {0.4.0.0.1.0.5.3}
static struct msgb *generate_map_inform_service_centre(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP InformSC");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd[8]; memset(bcd, 0xFF, 8);
    for (size_t i=0,b=0; i<slen && b<8; i+=2)
        bcd[b++] = (uint8_t)( (imsi_str[i]-'0') | (((i+1<slen)?imsi_str[i+1]-'0':0x0F)<<4) );
    uint8_t msisdn_os[12]; uint8_t mol = (uint8_t)ber_tlv(msisdn_os, 0x04, bcd, (uint8_t)((slen+1)/2));
    // MobileSubscriberNotReachableReason: INTEGER (context [0]) — 0=msPurged
    uint8_t reason[] = {0x80, 0x01, 0x00};
    uint8_t seq_body[32]; uint8_t sq = 0;
    memcpy(seq_body+sq, msisdn_os, mol); sq += mol;
    memcpy(seq_body+sq, reason, 3); sq += 3;
    uint8_t seq[48]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, seq_body, sq);
    // mwdMngtContext-v3 OID: {0.4.0.0.1.0.5.3}
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x05,0x03};
    static uint32_t tid = 0x00002400;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x3F, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP InformServiceCentre" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=63(0x3F)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MAP AlertServiceCentre (3GPP TS 29.002 §7.7.17)
// opCode=64 (0x40)  HLR → SC  — оповещение SMSC, что абонент снова доступен
// AC: mwdMngtContext-v3 = {0.4.0.0.1.0.5.3}
static struct msgb *generate_map_alert_service_centre(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP AlertSC");
    if (!msg) return nullptr;
    size_t slen = strlen(imsi_str);
    uint8_t bcd[8]; memset(bcd, 0xFF, 8);
    for (size_t i=0,b=0; i<slen && b<8; i+=2)
        bcd[b++] = (uint8_t)( (imsi_str[i]-'0') | (((i+1<slen)?imsi_str[i+1]-'0':0x0F)<<4) );
    uint8_t msisdn_os[12]; uint8_t mol = (uint8_t)ber_tlv(msisdn_os, 0x04, bcd, (uint8_t)((slen+1)/2));
    // AlertReason: INTEGER (context [0]) — 0=ms-Present
    uint8_t alert_reason[] = {0x80, 0x01, 0x00};
    uint8_t seq_body[32]; uint8_t sq = 0;
    memcpy(seq_body+sq, msisdn_os, mol); sq += mol;
    memcpy(seq_body+sq, alert_reason, 3); sq += 3;
    uint8_t seq[48]; uint8_t seql = (uint8_t)ber_tlv(seq, 0x30, seq_body, sq);
    // mwdMngtContext-v3 OID: {0.4.0.0.1.0.5.3}
    static const uint8_t oid[] = {0x04,0x00,0x00,0x01,0x00,0x05,0x03};
    static uint32_t tid = 0x00002500;
    uint8_t pdu[256]; uint8_t plen = build_tcap_begin(pdu, tid++, oid, sizeof(oid), 0x01, 0x40, seq, seql);
    memcpy(msgb_put(msg, plen), pdu, plen);
    std::cout << COLOR_CYAN << "✓ MAP AlertServiceCentre" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  opCode=64(0x40)  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// BSSAP+ Location Update  — 3GPP TS 29.018 §9.2.1
// Направление: SGSN → MSC  (Gs-interface)  — здесь симулируем MSC→SGSN
// Сообщение: BSSAP+ LOCATION-UPDATE-REQUEST (msgType=0x01)
// Содержит: IMSI + LAI
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_bssap_plus_lu(const char *imsi_str,
                                            uint16_t mcc, uint16_t mnc, uint16_t lac) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSAP+ LU");
    if (!msg) return nullptr;

    // BSSAP+ header: discriminator=0x00 (BSSAP+), length, msgType=0x01
    *(msgb_put(msg, 1)) = 0x00;  // BSSAP+ discriminator
    *(msgb_put(msg, 1)) = 0x01;  // LOCATION-UPDATE-REQUEST

    // IMSI IE (тег=0x01, BCD)
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0] - '0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i] - '0');
        uint8_t hi = (i + 1 < imsi_slen) ? (uint8_t)(imsi_str[i+1] - '0') : 0x0F;
        bcd_imsi[bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t *imsi_ie = msgb_put(msg, 2 + bcd_len);
    imsi_ie[0] = 0x01;          // IE Identifier: IMSI
    imsi_ie[1] = (uint8_t)bcd_len;
    memcpy(imsi_ie + 2, bcd_imsi, bcd_len);

    // LAI IE (тег=0x04, MCC/MNC/LAC)
    uint8_t lai[6];
    lai[0] = 0x04;  // IE: LAI
    lai[1] = 0x05;  // length
    lai[2] = (uint8_t)((mcc / 100) | ((mcc / 10 % 10) << 4));
    lai[3] = (uint8_t)((mcc % 10) | 0xF0);
    lai[4] = (uint8_t)((mnc / 10 % 10) | ((mnc % 10) << 4));
    lai[5] = (uint8_t)(lac >> 8);
    memcpy(msgb_put(msg, 4), lai, 4);
    *(msgb_put(msg, 1)) = (uint8_t)(lac & 0xFF);

    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSAP+ Location Update Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:    " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MCC/MNC: " << COLOR_GREEN << mcc << "/" << mnc << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  LAC:     " << COLOR_GREEN << lac << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MsgType: " << COLOR_GREEN << "0x01 LOCATION-UPDATE-REQUEST" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex BSSAP+ LU:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ============================================================
// P10: Gs-interface BSSAP+ — Paging, Detach, Reset
// ============================================================

// BSSAP+ Paging (3GPP TS 29.018 §9.2)
// MSC → SGSN  msgType=0x01 подождите — 0x01 это LU. Paging=0x09
// msgType=0x09 MS-PAGING-REQUEST (§9.2.12)
// Содержит: IMSI IE(0x01) + VLR-Number IE(0x05, E.164 MSC номер)
static struct msgb *generate_bssap_plus_paging(const char *imsi_str,
                                                const char *vlr_number) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSAP+ Paging");
    if (!msg) return nullptr;

    *(msgb_put(msg, 1)) = 0x00;  // BSSAP+ discriminator
    *(msgb_put(msg, 1)) = 0x09;  // MS-PAGING-REQUEST

    // IMSI IE: tag=0x01
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0] - '0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i] - '0');
        uint8_t hi = (i + 1 < imsi_slen) ? (uint8_t)(imsi_str[i + 1] - '0') : 0x0F;
        bcd_imsi[bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t *imsi_ie = msgb_put(msg, 2 + bcd_len);
    imsi_ie[0] = 0x01;
    imsi_ie[1] = (uint8_t)bcd_len;
    memcpy(imsi_ie + 2, bcd_imsi, bcd_len);

    // VLR-Number IE: tag=0x05, E.164 BCD (международный, тип=0x91)
    size_t vlr_slen = strlen(vlr_number);
    uint8_t bcd_vlr[12]; memset(bcd_vlr, 0, sizeof(bcd_vlr));
    bcd_vlr[0] = 0x91;  // TON=international, NPI=E.164
    size_t vlr_bcd_len = 1;
    for (size_t i = 0; i < vlr_slen && vlr_bcd_len < 11; i += 2) {
        uint8_t lo = (uint8_t)(vlr_number[i] - '0');
        uint8_t hi = (i + 1 < vlr_slen) ? (uint8_t)(vlr_number[i + 1] - '0') : 0x0F;
        bcd_vlr[vlr_bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    *(msgb_put(msg, 1)) = 0x05;                 // IE: VLR-Number
    *(msgb_put(msg, 1)) = (uint8_t)vlr_bcd_len;
    memcpy(msgb_put(msg, vlr_bcd_len), bcd_vlr, vlr_bcd_len);

    // TMSI IE (optional, tag=0x06): не добавляем — только по IMSI

    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSAP+ MS Paging Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:       " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  VLR-Number: " << COLOR_GREEN << vlr_number << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MsgType:    " << COLOR_GREEN << "0x09 MS-PAGING-REQUEST" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:     " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSAP+ IMSI-Detach-Indication (3GPP TS 29.018 §9.2.7)
// MSC → SGSN  msgType=0x05  — MS выполнила IMSI Detach (выключение)
// Содержит: IMSI IE(0x01) + SGSN-Number IE(0x07)
// detach_type: 0=power-off 1=reattach-required 2=GPRS-detach
static struct msgb *generate_bssap_plus_imsi_detach(const char *imsi_str,
                                                     const char *sgsn_number,
                                                     uint8_t detach_type) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSAP+ Detach");
    if (!msg) return nullptr;

    *(msgb_put(msg, 1)) = 0x00;  // BSSAP+ discriminator
    *(msgb_put(msg, 1)) = 0x05;  // IMSI-DETACH-INDICATION

    // IMSI IE: tag=0x01
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0] - '0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i] - '0');
        uint8_t hi = (i + 1 < imsi_slen) ? (uint8_t)(imsi_str[i + 1] - '0') : 0x0F;
        bcd_imsi[bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t *imsi_ie = msgb_put(msg, 2 + bcd_len);
    imsi_ie[0] = 0x01;
    imsi_ie[1] = (uint8_t)bcd_len;
    memcpy(imsi_ie + 2, bcd_imsi, bcd_len);

    // SGSN-Number IE: tag=0x07, E.164 BCD
    size_t sgsn_slen = strlen(sgsn_number);
    uint8_t bcd_sgsn[12]; memset(bcd_sgsn, 0, sizeof(bcd_sgsn));
    bcd_sgsn[0] = 0x91;
    size_t sgsn_bcd_len = 1;
    for (size_t i = 0; i < sgsn_slen && sgsn_bcd_len < 11; i += 2) {
        uint8_t lo = (uint8_t)(sgsn_number[i] - '0');
        uint8_t hi = (i + 1 < sgsn_slen) ? (uint8_t)(sgsn_number[i + 1] - '0') : 0x0F;
        bcd_sgsn[sgsn_bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    *(msgb_put(msg, 1)) = 0x07;
    *(msgb_put(msg, 1)) = (uint8_t)sgsn_bcd_len;
    memcpy(msgb_put(msg, sgsn_bcd_len), bcd_sgsn, sgsn_bcd_len);

    // Detach Type IE: tag=0x08, len=1
    *(msgb_put(msg, 1)) = 0x08;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = detach_type;

    const char *dt_name = (detach_type == 0) ? "power-off"
                        : (detach_type == 1) ? "reattach-required"
                        : (detach_type == 2) ? "GPRS-detach" : "unknown";
    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSAP+ IMSI Detach Indication" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:        " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SGSN-Number: " << COLOR_GREEN << sgsn_number << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DetachType:  " << COLOR_GREEN << (int)detach_type
              << " (" << dt_name << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MsgType:     " << COLOR_GREEN << "0x05 IMSI-DETACH-INDICATION" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:      " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSAP+ Reset (3GPP TS 29.018 §9.2.15)
// MSC → SGSN  msgType=0x0B  — сброс интерфейса Gs
// Содержит: Cause IE (tag=0x09)
// reset_cause: 0=power-on 1=om-intervention 2=load-control
static struct msgb *generate_bssap_plus_reset(uint8_t reset_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSAP+ Reset");
    if (!msg) return nullptr;

    *(msgb_put(msg, 1)) = 0x00;  // BSSAP+ discriminator
    *(msgb_put(msg, 1)) = 0x0B;  // RESET

    // Cause IE: tag=0x09, len=1
    *(msgb_put(msg, 1)) = 0x09;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = reset_cause;

    const char *c_name = (reset_cause == 0) ? "power-on"
                       : (reset_cause == 1) ? "om-intervention"
                       : (reset_cause == 2) ? "load-control" : "unknown";
    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSAP+ Reset" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN << (int)reset_cause
              << " (" << c_name << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MsgType:" << COLOR_GREEN << " 0x0B RESET" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSAP+ Reset-Acknowledge (3GPP TS 29.018 §9.2.16)
// SGSN → MSC  msgType=0x0C  — подтверждение Reset
static struct msgb *generate_bssap_plus_reset_ack() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSAP+ ResetAck");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;  // BSSAP+ discriminator
    *(msgb_put(msg, 1)) = 0x0C;  // RESET-ACKNOWLEDGE
    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSAP+ Reset Acknowledge" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MsgType:" << COLOR_GREEN << " 0x0C RESET-ACKNOWLEDGE" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ============================================================
// P21: Gs-interface BSSAP+ SMS-related messages (3GPP TS 29.018 §9.2)
//
// READY-FOR-SM   msgType=0x0E  SGSN→MSC — MS ready to receive SM
//   IEs: IMSI (tag=0x01), Alert-Reason (tag=0x0D): 0=MS-Present, 1=MemCapExceeded
//
// ALERT-REQUEST  msgType=0x0F  MSC→SGSN — alert MSC when MS available
//   IEs: IMSI (tag=0x01)
//
// ALERT-ACKNOWLEDGE msgType=0x10  SGSN→MSC — MS now available
//   No IEs
//
// ALERT-REJECT   msgType=0x11  SGSN→MSC — alert rejected
//   IEs: Cause (tag=0x09): 0x01=IMSI-detached, 0x03=unidentified-subscriber
// ============================================================

// Inline IMSI BCD helper (shared pattern with paging/detach generators)
static uint8_t bssap_encode_imsi(const char *imsi_str, uint8_t *bcd_out) {
    size_t slen = strlen(imsi_str);
    bcd_out[0] = (uint8_t)(((imsi_str[0] - '0') << 4) | 0x09);  // identity digit1 + IMSI type
    uint8_t bcd_len = 1;
    for (size_t i = 1; i < slen && bcd_len < 9; i += 2) {
        uint8_t lo = (uint8_t)(imsi_str[i] - '0');
        uint8_t hi = (i + 1 < slen) ? (uint8_t)(imsi_str[i + 1] - '0') : 0x0F;
        bcd_out[bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    return bcd_len;
}

// BSSAP+ READY-FOR-SM (3GPP TS 29.018 §9.2.14)
// SGSN → MSC  msgType=0x0E — MS is ready to receive short message
static struct msgb *generate_bssap_plus_ready_for_sm(const char *imsi_str, uint8_t alert_reason) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSAP+ ReadyForSM");
    if (!msg) return nullptr;

    *(msgb_put(msg, 1)) = 0x00;  // BSSAP+ discriminator
    *(msgb_put(msg, 1)) = 0x0E;  // READY-FOR-SM

    // IMSI IE: tag=0x01
    uint8_t bcd[9];
    uint8_t bcd_len = bssap_encode_imsi(imsi_str, bcd);
    uint8_t *ie = msgb_put(msg, 2 + bcd_len);
    ie[0] = 0x01;  ie[1] = bcd_len;
    memcpy(ie + 2, bcd, bcd_len);

    // Alert-Reason IE: tag=0x0D, len=1
    *(msgb_put(msg, 1)) = 0x0D;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = alert_reason & 0x01;

    const char *ar_str = (alert_reason == 0) ? "MS-Present" : "Memory-Capacity-Exceeded";
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e BSSAP+ Ready-For-SM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:         " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Alert-Reason: " << COLOR_GREEN << (int)alert_reason
              << " (" << ar_str << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MsgType:      " << COLOR_GREEN << "0x0E READY-FOR-SM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440:      " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex READY-FOR-SM:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// BSSAP+ ALERT-REQUEST (3GPP TS 29.018 §9.2.10)
// MSC → SGSN  msgType=0x0F — request notification when MS becomes available
// Triggers the Alert-SC procedure (MSC→SMSC MAP alertServiceCentre)
static struct msgb *generate_bssap_plus_alert_sc(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSAP+ AlertSC");
    if (!msg) return nullptr;

    *(msgb_put(msg, 1)) = 0x00;  // BSSAP+ discriminator
    *(msgb_put(msg, 1)) = 0x0F;  // ALERT-REQUEST

    // IMSI IE: tag=0x01
    uint8_t bcd[9];
    uint8_t bcd_len = bssap_encode_imsi(imsi_str, bcd);
    uint8_t *ie = msgb_put(msg, 2 + bcd_len);
    ie[0] = 0x01;  ie[1] = bcd_len;
    memcpy(ie + 2, bcd, bcd_len);

    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e BSSAP+ Alert-Request (Alert-SC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:    " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MsgType: " << COLOR_GREEN << "0x0F ALERT-REQUEST" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ALERT-REQUEST:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// BSSAP+ ALERT-ACKNOWLEDGE (3GPP TS 29.018 §9.2.11)
// SGSN → MSC  msgType=0x10 — MS is now available (no IEs)
static struct msgb *generate_bssap_plus_alert_ack() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSAP+ AlertAck");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;  // BSSAP+ discriminator
    *(msgb_put(msg, 1)) = 0x10;  // ALERT-ACKNOWLEDGE
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e BSSAP+ Alert-Acknowledge" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MsgType: " << COLOR_GREEN << "0x10 ALERT-ACKNOWLEDGE" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSAP+ ALERT-REJECT (3GPP TS 29.018 §9.2.12)
// SGSN → MSC  msgType=0x11 — alert rejected
// Cause IE (tag=0x09, 1 byte): 0x01=IMSI-detached 0x03=unidentified-subscriber
static struct msgb *generate_bssap_plus_alert_reject(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSAP+ AlertReject");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;  // BSSAP+ discriminator
    *(msgb_put(msg, 1)) = 0x11;  // ALERT-REJECT
    *(msgb_put(msg, 1)) = 0x09;  // Cause IE tag
    *(msgb_put(msg, 1)) = 0x01;  // Cause IE length
    *(msgb_put(msg, 1)) = cause;
    const char *c_str = (cause == 0x01) ? "IMSI-detached"
                      : (cause == 0x03) ? "unidentified-subscriber"
                      : (cause == 0x0F) ? "MSC-not-reachable" : "unknown";
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e BSSAP+ Alert-Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:   " << COLOR_GREEN << "0x" << std::hex << (int)cause
              << std::dec << " (" << c_str << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MsgType: " << COLOR_GREEN << "0x11 ALERT-REJECT" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    return msg;
}

// ============================================================
// ──────────────────────────────────────────────────────────────
// TCAP/MAP диалог: вспомогательные функции
// ──────────────────────────────────────────────────────────────
// Строит полный Dialogue Portion (0x6B) с AARQ-apdu (запрос Begin).
// Тот же паттерн, что и в generate_map_send_auth_info(), вынесен
// в хелпер для повторного использования в P2-генераторах.
// ac_oid / ac_oid_len — BER-кодированный OID application context.
// out[] должен быть >= 112 байт.
static uint8_t build_aarq_dialogue_portion(uint8_t *out,
                                            const uint8_t *ac_oid, uint8_t ac_oid_len) {
    uint8_t oid_tlv[16];
    uint8_t oid_tlv_len = (uint8_t)ber_tlv(oid_tlv, 0x06, ac_oid, ac_oid_len);
    uint8_t proto_ver[] = { 0x80, 0x02, 0x07, 0x80 };
    uint8_t acn[20];
    uint8_t acn_len = (uint8_t)ber_tlv(acn, 0xA1, oid_tlv, oid_tlv_len);
    uint8_t dr_body[40]; uint8_t dr_len = 0;
    memcpy(dr_body + dr_len, proto_ver, 4);  dr_len += 4;
    memcpy(dr_body + dr_len, acn, acn_len);  dr_len += acn_len;
    uint8_t dialogue_req[48];
    uint8_t dialogue_req_len = (uint8_t)ber_tlv(dialogue_req, 0x60, dr_body, dr_len);
    uint8_t single_asn1[56];
    uint8_t single_asn1_len = (uint8_t)ber_tlv(single_asn1, 0xA0, dialogue_req, dialogue_req_len);
    uint8_t dlg_oid_val[] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    uint8_t dlg_oid[12];
    uint8_t dlg_oid_len = (uint8_t)ber_tlv(dlg_oid, 0x06, dlg_oid_val, 7);
    uint8_t ext_body[80]; uint8_t ext_body_len = 0;
    memcpy(ext_body + ext_body_len, dlg_oid, dlg_oid_len);          ext_body_len += dlg_oid_len;
    memcpy(ext_body + ext_body_len, single_asn1, single_asn1_len);  ext_body_len += single_asn1_len;
    uint8_t ext_tlv[92];
    uint8_t ext_len = (uint8_t)ber_tlv(ext_tlv, 0x28, ext_body, ext_body_len);
    return (uint8_t)ber_tlv(out, 0x6B, ext_tlv, ext_len);
}

// Строит TCAP Begin (0x62) с диалоговой частью и компонентой Invoke.
// otid        — Originating TID (4 байта, big-endian).
// ac_oid      — BER-OID application context.
// invoke_id   — Invoke ID (обычно 1).
// op_code     — MAP opCode (1 байт).
// invoke_arg  — байты аргумента после opCode (SEQUENCE, OCTET STRING, etc.).
// out[]       — должен быть >= 300 байт. Возвращает длину PDU.
static uint8_t build_tcap_begin(uint8_t *out,
                                  uint32_t otid,
                                  const uint8_t *ac_oid, uint8_t ac_oid_len,
                                  uint8_t invoke_id, uint8_t op_code,
                                  const uint8_t *invoke_arg, uint8_t invoke_arg_len) {
    // OTID
    uint8_t otid_val[4] = { (uint8_t)(otid>>24),(uint8_t)(otid>>16),
                             (uint8_t)(otid>>8), (uint8_t)(otid) };
    uint8_t otid_tlv[8]; uint8_t otid_len = (uint8_t)ber_tlv(otid_tlv, 0x48, otid_val, 4);

    // Dialogue Portion (AARQ)
    uint8_t dial[112];
    uint8_t dial_len = build_aarq_dialogue_portion(dial, ac_oid, ac_oid_len);

    // Invoke Component
    uint8_t inv_id_ie[]  = { 0x02, 0x01, invoke_id };
    uint8_t opcode_ie[]  = { 0x02, 0x01, op_code };
    uint8_t inv_body[200]; uint8_t ib_len = 0;
    memcpy(inv_body + ib_len, inv_id_ie, 3);              ib_len += 3;
    memcpy(inv_body + ib_len, opcode_ie, 3);              ib_len += 3;
    memcpy(inv_body + ib_len, invoke_arg, invoke_arg_len); ib_len += invoke_arg_len;
    uint8_t invoke[210];
    uint8_t invoke_len = (uint8_t)ber_tlv(invoke, 0xA1, inv_body, ib_len);
    uint8_t comp[220];
    uint8_t comp_len = (uint8_t)ber_tlv(comp, 0x6C, invoke, invoke_len);

    // TCAP Begin body
    uint8_t begin_body[256]; uint8_t bb_len = 0;
    memcpy(begin_body + bb_len, otid_tlv, otid_len); bb_len += otid_len;
    memcpy(begin_body + bb_len, dial, dial_len);      bb_len += dial_len;
    memcpy(begin_body + bb_len, comp, comp_len);      bb_len += comp_len;

    out[0] = 0x62;
    out[1] = bb_len;
    memcpy(out + 2, begin_body, bb_len);
    return (uint8_t)(2 + bb_len);
}

// TCAP/MAP диалог: вспомогательные функции
// ──────────────────────────────────────────────────────────────
// Строит Dialogue Portion (0x6B) с AARE-apdu (accepted).
// Используется в TCAP End для ответа на входящий диалог.
// ITU-T Q.773 §3.2.2, 3GPP TS 29.002 Annex A
// Возвращает число байт записанных в out[].
// out[] должен быть >= 64 байт.
static uint8_t build_aare_dialogue_portion(uint8_t *out,
                                            const uint8_t *ac_oid, uint8_t ac_oid_len) {
    // application-context-name OID TLV
    uint8_t oid_tlv[16];
    uint8_t oid_tlv_len = (uint8_t)ber_tlv(oid_tlv, 0x06, ac_oid, ac_oid_len);
    // [1] application-context-name
    uint8_t acn[20];
    uint8_t acn_len = (uint8_t)ber_tlv(acn, 0xA1, oid_tlv, oid_tlv_len);
    // [2] result ENUMERATED = 0 (accepted)
    uint8_t result_val[] = { 0x0A, 0x01, 0x00 };   // ENUMERATED, 1 byte, value=0
    uint8_t result_ie[8];
    uint8_t result_ie_len = (uint8_t)ber_tlv(result_ie, 0xA2, result_val, 3);
    // [3] result-source-diagnostic → service-user [1] INTEGER = 0 (null)
    uint8_t rsd_inner[] = { 0x02, 0x01, 0x00 };
    uint8_t rsd_su[8];
    uint8_t rsd_su_len = (uint8_t)ber_tlv(rsd_su, 0xA1, rsd_inner, 3);
    uint8_t rsd[12];
    uint8_t rsd_len = (uint8_t)ber_tlv(rsd, 0xA3, rsd_su, rsd_su_len);
    // protocol-version [0] BIT STRING = version1
    uint8_t proto_ver[] = { 0x80, 0x02, 0x07, 0x80 };
    // AARE-apdu [APPLICATION 1] = 0x61
    uint8_t aare_body[64]; uint8_t ab_len = 0;
    memcpy(aare_body + ab_len, proto_ver,  4);          ab_len += 4;
    memcpy(aare_body + ab_len, acn, acn_len);           ab_len += acn_len;
    memcpy(aare_body + ab_len, result_ie, result_ie_len); ab_len += result_ie_len;
    memcpy(aare_body + ab_len, rsd, rsd_len);           ab_len += rsd_len;
    uint8_t aare[80];
    uint8_t aare_len = (uint8_t)ber_tlv(aare, 0x61, aare_body, ab_len);
    // single-ASN1-type [0] = 0xA0
    uint8_t single[88];
    uint8_t single_len = (uint8_t)ber_tlv(single, 0xA0, aare, aare_len);
    // MAP Dialogue OID: {0.4.0.0.1.1.1.1}
    uint8_t dlg_oid_val[] = { 0x04, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    uint8_t dlg_oid[12];
    uint8_t dlg_oid_len = (uint8_t)ber_tlv(dlg_oid, 0x06, dlg_oid_val, 7);
    // EXTERNAL (0x28)
    uint8_t ext_body[112]; uint8_t ext_body_len = 0;
    memcpy(ext_body + ext_body_len, dlg_oid, dlg_oid_len);   ext_body_len += dlg_oid_len;
    memcpy(ext_body + ext_body_len, single, single_len);     ext_body_len += single_len;
    uint8_t ext[120];
    uint8_t ext_len = (uint8_t)ber_tlv(ext, 0x28, ext_body, ext_body_len);
    // Dialogue Portion (0x6B)
    return (uint8_t)ber_tlv(out, 0x6B, ext, ext_len);
}

// Строит Component Portion (0x6C) с ReturnResultLast (0xA2).
// invoke_id: как в исходном Invoke; op_code: тот же opCode;
// result_arg: байты аргумента результата (после тега opCode).
// out[] должен быть >= result_arg_len + 24 байт.
static uint8_t build_rrl_component(uint8_t *out,
                                    uint8_t invoke_id, uint8_t op_code,
                                    const uint8_t *result_arg, uint8_t result_arg_len) {
    uint8_t invoke_id_ie[] = { 0x02, 0x01, invoke_id };
    uint8_t opcode_ie[]    = { 0x02, 0x01, op_code };
    // SEQUENCE { opCode, result }
    uint8_t seq_body[160]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, opcode_ie, 3);              sq_len += 3;
    memcpy(seq_body + sq_len, result_arg, result_arg_len); sq_len += result_arg_len;
    uint8_t seq[168];
    uint8_t seq_tlv_len = (uint8_t)ber_tlv(seq, 0x30, seq_body, sq_len);
    // ReturnResultLast [2] = 0xA2
    uint8_t rrl_body[180]; uint8_t rrl_len = 0;
    memcpy(rrl_body + rrl_len, invoke_id_ie, 3);    rrl_len += 3;
    memcpy(rrl_body + rrl_len, seq, seq_tlv_len);   rrl_len += seq_tlv_len;
    uint8_t rrl[192];
    uint8_t rrl_tlv_len = (uint8_t)ber_tlv(rrl, 0xA2, rrl_body, rrl_len);
    // Component Portion (0x6C)
    return (uint8_t)ber_tlv(out, 0x6C, rrl, rrl_tlv_len);
}

// ──────────────────────────────────────────────────────────────
// TCAP Continue — ITU-T Q.773 §3.2.3
// Используется для промежуточных шагов диалога (например, USSD).
// Содержит: OTID (наш) + DTID (удалённого) без компонентов.
// otid — наш TID из Begin; dtid — TID из ответа партнёра.
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_tcap_continue(uint32_t otid, uint32_t dtid) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "TCAP Continue");
    if (!msg) return nullptr;

    uint8_t otid_val[4] = { (uint8_t)(otid>>24), (uint8_t)(otid>>16),
                             (uint8_t)(otid>>8),  (uint8_t)(otid) };
    uint8_t dtid_val[4] = { (uint8_t)(dtid>>24), (uint8_t)(dtid>>16),
                             (uint8_t)(dtid>>8),  (uint8_t)(dtid) };
    uint8_t otid_tlv[8]; uint8_t otid_len = (uint8_t)ber_tlv(otid_tlv, 0x48, otid_val, 4);
    uint8_t dtid_tlv[8]; uint8_t dtid_len = (uint8_t)ber_tlv(dtid_tlv, 0x49, dtid_val, 4);

    uint8_t cont_body[20]; uint8_t cb_len = 0;
    memcpy(cont_body + cb_len, otid_tlv, otid_len); cb_len += otid_len;
    memcpy(cont_body + cb_len, dtid_tlv, dtid_len); cb_len += dtid_len;

    *(msgb_put(msg, 1)) = 0x65;  // TCAP Continue
    *(msgb_put(msg, 1)) = cb_len;
    memcpy(msgb_put(msg, cb_len), cont_body, cb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерирован TCAP Continue" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OTID:   " << COLOR_GREEN << "0x" << std::hex << otid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DTID:   " << COLOR_GREEN << "0x" << std::hex << dtid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex TCAP Continue:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// TCAP End (пустой) — ITU-T Q.773 §3.2.4
// Закрывает диалог без компонентов.
// dtid — Destination TID (= OTID из входящего Begin от партнёра).
// ac_oid / ac_oid_len — OID приложения (из исходного Begin).
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_tcap_end_ack(uint32_t dtid,
                                           const uint8_t *ac_oid, uint8_t ac_oid_len) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "TCAP End");
    if (!msg) return nullptr;

    uint8_t dtid_val[4] = { (uint8_t)(dtid>>24), (uint8_t)(dtid>>16),
                             (uint8_t)(dtid>>8),  (uint8_t)(dtid) };
    uint8_t dtid_tlv[8]; uint8_t dtid_len = (uint8_t)ber_tlv(dtid_tlv, 0x49, dtid_val, 4);

    uint8_t dial[128];
    uint8_t dial_len = build_aare_dialogue_portion(dial, ac_oid, ac_oid_len);

    uint8_t end_body[160]; uint8_t eb_len = 0;
    memcpy(end_body + eb_len, dtid_tlv, dtid_len); eb_len += dtid_len;
    memcpy(end_body + eb_len, dial, dial_len);      eb_len += dial_len;

    *(msgb_put(msg, 1)) = 0x64;  // TCAP End
    *(msgb_put(msg, 1)) = eb_len;
    memcpy(msgb_put(msg, eb_len), end_body, eb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерирован TCAP End (ack)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DTID:   " << COLOR_GREEN << "0x" << std::hex << dtid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт (AARE accepted, без компонентов)" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex TCAP End:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP SendAuthInfo End — 3GPP TS 29.002 §8.5.2 ReturnResultLast
// Направление: HLR → MSC/VLR  (C-interface, симуляция ответа HLR)
// TCAP End, ReturnResultLast, opCode=56
// Результат: один фиктивный AuthenticationTriplet {RAND, SRES, Kc}
// RAND: 16 байт, SRES: 4 байта, Kc: 8 байт
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_sai_end(uint32_t dtid) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP SAI End");
    if (!msg) return nullptr;

    // AuthenticationTriplet ::= SEQUENCE { rand, sres, kc }
    uint8_t rand_val[] = { 0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
                            0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10 };
    uint8_t sres_val[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    uint8_t kc_val[]   = { 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88 };

    uint8_t rand_ie[20]; uint8_t rand_ie_len = (uint8_t)ber_tlv(rand_ie, 0x04, rand_val, 16);
    uint8_t sres_ie[8];  uint8_t sres_ie_len = (uint8_t)ber_tlv(sres_ie, 0x04, sres_val, 4);
    uint8_t kc_ie[12];   uint8_t kc_ie_len   = (uint8_t)ber_tlv(kc_ie,   0x04, kc_val,   8);

    uint8_t trip_body[40]; uint8_t tb_len = 0;
    memcpy(trip_body + tb_len, rand_ie, rand_ie_len); tb_len += rand_ie_len;
    memcpy(trip_body + tb_len, sres_ie, sres_ie_len); tb_len += sres_ie_len;
    memcpy(trip_body + tb_len, kc_ie,   kc_ie_len);   tb_len += kc_ie_len;
    uint8_t triplet[48];
    uint8_t triplet_len = (uint8_t)ber_tlv(triplet, 0x30, trip_body, tb_len);

    // AuthenticationSetList ::= SEQUENCE OF (1 triplet)
    uint8_t auth_list[56];
    uint8_t auth_list_len = (uint8_t)ber_tlv(auth_list, 0x30, triplet, triplet_len);

    // Component Portion: ReturnResultLast, opCode=56 (0x38)
    uint8_t comp[80];
    uint8_t comp_len = build_rrl_component(comp, 0x01, 0x38, auth_list, auth_list_len);

    // Dialogue Portion: sendAuthInfoContext-v3
    uint8_t sai_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x39, 0x03 };
    uint8_t dial[128];
    uint8_t dial_len = build_aare_dialogue_portion(dial, sai_ac_oid, sizeof(sai_ac_oid));

    // DTID
    uint8_t dtid_val[4] = { (uint8_t)(dtid>>24),(uint8_t)(dtid>>16),
                             (uint8_t)(dtid>>8), (uint8_t)(dtid) };
    uint8_t dtid_tlv[8]; uint8_t dtid_len = (uint8_t)ber_tlv(dtid_tlv, 0x49, dtid_val, 4);

    // TCAP End (0x64)
    uint8_t end_body[230]; uint8_t eb_len = 0;
    memcpy(end_body + eb_len, dtid_tlv, dtid_len); eb_len += dtid_len;
    memcpy(end_body + eb_len, dial, dial_len);      eb_len += dial_len;
    memcpy(end_body + eb_len, comp, comp_len);      eb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x64;
    *(msgb_put(msg, 1)) = eb_len;
    memcpy(msgb_put(msg, eb_len), end_body, eb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерирован MAP SAI End (ReturnResultLast)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DTID:    " << COLOR_GREEN << "0x" << std::hex << dtid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "56 (0x38) SendAuthInfo" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  RAND:    " << COLOR_GREEN << "0102030405060708090a0b0c0d0e0f10" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SRES:    " << COLOR_GREEN << "aabbccdd" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Kc:      " << COLOR_GREEN << "1122334455667788" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP SAI End:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP UpdateLocation End — 3GPP TS 29.002 §8.1.2 ReturnResultLast
// Направление: HLR → MSC/VLR  (C-interface, симуляция ответа HLR)
// TCAP End, ReturnResultLast, opCode=2
// Результат: UpdateLocationRes ::= SEQUENCE { hlr-Number }
// hlr-Number = фиктивный ISDN-номер HLR (BCD E.164)
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_ul_end(uint32_t dtid) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP UL End");
    if (!msg) return nullptr;

    // hlr-Number [0] IMPLICIT ISDNAddressString:
    // "79161234567" → 0x91 + BCD{97,61,21,43,65,F7}
    uint8_t hlr_val[] = { 0x91, 0x97, 0x61, 0x21, 0x43, 0x65, 0xF7 };
    uint8_t hlr_ie[12]; uint8_t hlr_ie_len = (uint8_t)ber_tlv(hlr_ie, 0x80, hlr_val, 7);

    // UpdateLocationRes SEQUENCE
    uint8_t ul_res[16];
    uint8_t ul_res_len = (uint8_t)ber_tlv(ul_res, 0x30, hlr_ie, hlr_ie_len);

    // Component Portion: ReturnResultLast, opCode=2 (0x02)
    uint8_t comp[48];
    uint8_t comp_len = build_rrl_component(comp, 0x01, 0x02, ul_res, ul_res_len);

    // Dialogue Portion: networkLocUpContext-v3
    uint8_t ul_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    uint8_t dial[128];
    uint8_t dial_len = build_aare_dialogue_portion(dial, ul_ac_oid, sizeof(ul_ac_oid));

    // DTID
    uint8_t dtid_val[4] = { (uint8_t)(dtid>>24),(uint8_t)(dtid>>16),
                             (uint8_t)(dtid>>8), (uint8_t)(dtid) };
    uint8_t dtid_tlv[8]; uint8_t dtid_len = (uint8_t)ber_tlv(dtid_tlv, 0x49, dtid_val, 4);

    // TCAP End (0x64)
    uint8_t end_body[230]; uint8_t eb_len = 0;
    memcpy(end_body + eb_len, dtid_tlv, dtid_len); eb_len += dtid_len;
    memcpy(end_body + eb_len, dial, dial_len);      eb_len += dial_len;
    memcpy(end_body + eb_len, comp, comp_len);      eb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x64;
    *(msgb_put(msg, 1)) = eb_len;
    memcpy(msgb_put(msg, eb_len), end_body, eb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерирован MAP UL End (ReturnResultLast)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DTID:       " << COLOR_GREEN << "0x" << std::hex << dtid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:     " << COLOR_GREEN << "2 (0x02) UpdateLocation" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  HLR-Number: " << COLOR_GREEN << "79161234567 (0x91 97 61 21 43 65 F7)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:     " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP UL End:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// ISUP IAM (Initial Address Message) — ITU-T Q.763/Q.764
// Направление: MSC → PSTN/GW  (ISUP-interface, SI=5, без SCCP)
// Структура (big-endian CIC + MTP3 routing label в M3UA):
//   CIC [2 байта little-endian]  Message Type 0x01 (IAM)
//   Mandatory Fixed:
//     Nature of Connection Indicators [1]: 0x00
//     Forward Call Indicators [2]: 0x60 0x00  (ISDN acc, ISDN req)
//     Calling Party's Category [1]: 0x0A (ordinary subscriber)
//     Transmission Medium Requirement [1]: 0x00 (speech)
//   Mandatory Variable:
//     Called Party Number (pointer + length + digits)
//   Optional:
//     Calling Party Number (param code 0x0A)
//     End of Optional Parameters 0x00
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_iam(const char *called_num,
                                       const char *calling_num,
                                       uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP IAM");
    if (!msg) return nullptr;

    // helper: encode E.164 digits into ISUP BCD number field
    // Returns byte count written into out[]. out[] needs len/2+2 bytes.
    // is_called: called=true (no screening), calling=false (with screening).
    auto encode_isup_number = [](uint8_t *out, const char *digits, bool is_called) -> uint8_t {
        size_t dlen = strlen(digits);
        bool odd = (dlen % 2) != 0;
        // Byte 0: odd/even | NAI = 4 (international E.164)
        out[0] = (uint8_t)((odd ? 0x80 : 0x00) | 0x04);
        // Byte 1: called: INN=0 | NPI=1 (E.164); calling: NI=0 | NPI=1 | Presentation=0 | Screening=3
        out[1] = is_called ? 0x10 : 0x13;
        uint8_t pos = 2;
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t d1 = (uint8_t)(digits[i] - '0');
            uint8_t d2 = (i+1 < dlen) ? (uint8_t)(digits[i+1] - '0') : 0x0F;
            out[pos++] = (uint8_t)((d2 << 4) | d1);
        }
        return pos;
    };

    // CIC [2 bytes little-endian]
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    // Message Type: IAM = 0x01
    *(msgb_put(msg, 1)) = 0x01;

    // ── Mandatory Fixed Parameters (4 bytes total)
    *(msgb_put(msg, 1)) = 0x00;         // Nature of Connection Indicators
    uint8_t *fci = msgb_put(msg, 2);
    fci[0] = 0x60; fci[1] = 0x00;      // Forward Call Indicators
    *(msgb_put(msg, 1)) = 0x0A;         // Calling Party's Category = ordinary subscriber
    *(msgb_put(msg, 1)) = 0x00;         // Transmission Medium Requirement = speech

    // ── Encode number fields first (needed for pointer arithmetic)
    // Called Party Number
    uint8_t cpn[20];
    uint8_t cpn_len = encode_isup_number(cpn, called_num, true);

    // Calling Party Number (optional, param 0x0A)
    uint8_t cgpn[20];
    uint8_t cgpn_len = encode_isup_number(cgpn, calling_num, false);

    // ── Mandatory Variable Parameters
    // Pointer section: ptr1 → CalledPartyNumber, ptr2 → Optional
    // ptr_value = pos(target) - pos(ptr_byte)   (Q.763 §1.4.3)
    // ptr1 at offset 8, CPN length at offset 10  → value = 2  (2 ptr bytes in section)
    // ptr2 at offset 9, optional at offset 10+1+cpn_len → value = 2+cpn_len
    uint8_t *ptrs = msgb_put(msg, 2);
    ptrs[0] = 0x02;                           // → CalledPN.length
    ptrs[1] = (uint8_t)(2 + cpn_len);         // → optional start (past CPN length + data)

    // Called Party Number: length octet + digits
    *(msgb_put(msg, 1)) = cpn_len;
    memcpy(msgb_put(msg, cpn_len), cpn, cpn_len);

    // ── Optional Parameters
    // Calling Party Number: param 0x0A, length, digits
    *(msgb_put(msg, 1)) = 0x0A;         // Parameter Code: Calling Party Number
    *(msgb_put(msg, 1)) = cgpn_len;
    memcpy(msgb_put(msg, cgpn_len), cgpn, cgpn_len);
    *(msgb_put(msg, 1)) = 0x00;         // End of Optional Parameters

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP IAM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:     " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Called:  " << COLOR_GREEN << called_num << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Calling: " << COLOR_GREEN << calling_num << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP IAM:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// ISUP REL (Release Message) — ITU-T Q.763/Q.764
// Направление: MSC ↔ PSTN/GW  (ISUP-interface, SI=5, без SCCP)
// Структура:
//   CIC [2 байта little-endian]  Message Type 0x0C (REL)
//   Mandatory Variable:
//     Cause Indicators (pointer + length + cause)
//       Location: 0x82 (local-private-ISDN = user)
//       Cause value: 3GPP TS 24.008 / Q.850
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_rel(uint16_t cic, uint8_t cause_value) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP REL");
    if (!msg) return nullptr;

    // CIC [2 bytes little-endian]
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    // Message Type: REL = 0x0C
    *(msgb_put(msg, 1)) = 0x0C;

    // Mandatory Variable: Cause Indicators
    // Cause Indicators field: 2 bytes
    //   Byte 0: ext=1 | coding=0 (ITU) | spare=0 | location=0x0A (network beyond interworking)
    //   Byte 1: ext=1 | cause value (Q.850)
    uint8_t cause_field[2] = { 0x82, (uint8_t)(0x80 | (cause_value & 0x7F)) };

    // Pointer to Cause Indicators (1 byte) + pointer to optional (1 byte)
    // Pointer 1: 0x02 (skip 2 pointer bytes → point to length)
    // Pointer 2: 0x00 (no optional parameters)
    *(msgb_put(msg, 1)) = 0x02;  // pointer to cause indicators length
    *(msgb_put(msg, 1)) = 0x00;  // pointer to optional params (0 = none)

    // Cause Indicators: length + field
    *(msgb_put(msg, 1)) = sizeof(cause_field);
    memcpy(msgb_put(msg, sizeof(cause_field)), cause_field, sizeof(cause_field));

    static const struct { uint8_t code; const char *name; } causes[] = {
        {1,"unallocated"}, {16,"normal-clearing"}, {17,"user-busy"},
        {18,"no-answer"}, {19,"no-answer-v2"}, {20,"subscriber-absent"},
        {21,"call-rejected"}, {27,"destination-out-of-order"},
        {28,"invalid-number"}, {34,"no-circuit"}, {41,"temporary-failure"},
        {47,"resource-unavailable"}, {63,"service-unavailable"},
        {65,"bearer-capability-not-implemented"}, {79,"service-not-impl"},
        {88,"incompatible-destination"}, {0, nullptr}
    };
    const char *cause_str = "unknown";
    for (int k = 0; causes[k].name; ++k)
        if (causes[k].code == cause_value) { cause_str = causes[k].name; break; }

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP REL" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN << (int)cause_value << " – " << cause_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP REL:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// ISUP ACM (Address Complete Message) — ITU-T Q.763 §3.1
// Направление: PSTN/GW → MSC  (ответ на IAM: абонент вызывается)
// Структура:
//   CIC [2 байта LE]  MT=0x06
//   Mandatory Fixed:
//     Backward Call Indicators [2]:
//       BCI[0]: 0x12 — called party status=subscriber free (01), charge=no ind (00)
//       BCI[1]: 0x14 — ISDN user part used all the way (bit3=1), ISDN access (bit5=1)
//   Optional pointer: 0x00 (нет optional-параметров)
// Итого: 6 байт
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_acm(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP ACM");
    if (!msg) return nullptr;

    // CIC [2 bytes little-endian]
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    // Message Type: ACM = 0x06
    *(msgb_put(msg, 1)) = 0x06;

    // Backward Call Indicators (mandatory fixed, 2 bytes)
    // BCI[0]: bits 1-2=charge ind(00=no ind), bits 3-4=called status(01=sub free),
    //         bits 5-6=called category(00=no ind), bits 7-8=end-to-end(00=no end-to-end)
    // 0b00010000 | 0b00000010 = 0x12 (LSB first in Q.763 bit ordering)
    // BCI[1]: bit1=interworking(0), bit2=e2e info(0), bit3=ISUP all-way(1),
    //         bit4=holding(0), bit5=ISDN access(1), bit6=echo(0), bits7-8=SCCP(00)
    // 0b00010100 = 0x14
    uint8_t *bci = msgb_put(msg, 2);
    bci[0] = 0x12;
    bci[1] = 0x14;

    // Optional pointer = 0x00 (no optional parameters)
    *(msgb_put(msg, 1)) = 0x00;

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP ACM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  BCI:    " << COLOR_GREEN << "0x12 0x14 (subscriber free, ISUP all-way)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP ACM:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// ISUP ANM (Answer Message) — ITU-T Q.763 §3.2
// Направление: PSTN/GW → MSC  (вызываемый ответил, начало тарификации)
// Структура:
//   CIC [2 байта LE]  MT=0x09
//   Optional pointer: 0x00 (нет optional-параметров)
// Итого: 4 байта
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_anm(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP ANM");
    if (!msg) return nullptr;

    // CIC [2 bytes little-endian]
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    // Message Type: ANM = 0x09
    *(msgb_put(msg, 1)) = 0x09;

    // Optional pointer = 0x00 (no optional parameters)
    *(msgb_put(msg, 1)) = 0x00;

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP ANM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP ANM:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// ISUP RLC (Release Complete) — ITU-T Q.763 §3.14
// Направление: PSTN/GW → MSC  (подтверждение REL, канал освобождён)
// Структура:
//   CIC [2 байта LE]  MT=0x10
//   (нет параметров)
// Итого: 3 байта
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_rlc(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP RLC");
    if (!msg) return nullptr;

    // CIC [2 bytes little-endian]
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    // Message Type: RLC = 0x10
    *(msgb_put(msg, 1)) = 0x10;

    // No parameters (Q.763 §3.14 — RLC has no mandatory or optional parameters)

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP RLC" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP RLC:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// ISUP CON (Connect) — ITU-T Q.763 §3.6
// Направление: PSTN/GW → MSC  (вызываемый ответил, до ANM; снятие контроля)
// Структура:
//   CIC [2 байта LE]  MT=0x07
//   Mandatory Fixed:
//     Backward Call Indicators [2]:
//       BCI[0]: 0x12 — subscriber free (01), no charge ind (00)
//       BCI[1]: 0x14 — ISUP all-way (bit3=1), ISDN access (bit5=1)
//   Optional pointer: 0x00
// Итого: 6 байт
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_con(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP CON");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    *(msgb_put(msg, 1)) = 0x07;  // MT=CON

    uint8_t *bci = msgb_put(msg, 2);
    bci[0] = 0x12;
    bci[1] = 0x14;

    *(msgb_put(msg, 1)) = 0x00;  // Optional pointer = none

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP CON" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  BCI:    " << COLOR_GREEN << "0x12 0x14 (subscriber free, ISUP all-way)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP CON:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// ISUP CPG (Call Progress) — ITU-T Q.763 §3.7
// Направление: PSTN/GW ↔ MSC  (промежуточные события вызова)
// Структура:
//   CIC [2 байта LE]  MT=0x2C
//   Mandatory Fixed:
//     Event Information [1]:
//       0x01 = Alerting  0x02 = Progress  0x03 = In-band info
//       Bit8=0: event presentation restricted indicator not set
//   Optional pointer: 0x00
// Итого: 5 байт
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_cpg(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP CPG");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    *(msgb_put(msg, 1)) = 0x2C;  // MT=CPG

    // Event Information: 0x01 = alerting (ring-back)
    *(msgb_put(msg, 1)) = 0x01;

    *(msgb_put(msg, 1)) = 0x00;  // Optional pointer = none

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP CPG" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:          " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Event Info:   " << COLOR_GREEN << "0x01 (Alerting)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP CPG:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// ISUP SUS (Suspend) — ITU-T Q.763 §3.18
// Направление: MSC ↔ PSTN/GW  (приостановка B-канала без разрыва)
// Структура:
//   CIC [2 байта LE]  MT=0x0D
//   Mandatory Fixed:
//     Suspend/Resume Indicators [1]:
//       Bit1: 0=network initiated, 1=ISDN subscriber initiated
//   Optional pointer: 0x00
// Итого: 5 байт
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_sus(uint16_t cic, uint8_t sus_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP SUS");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    *(msgb_put(msg, 1)) = 0x0D;  // MT=SUS

    // Suspend/Resume Indicators: bit1 = initiating side (0=network, 1=user)
    *(msgb_put(msg, 1)) = (uint8_t)(sus_cause & 0x01);

    *(msgb_put(msg, 1)) = 0x00;  // Optional pointer = none

    const char *cause_str = (sus_cause & 0x01) ? "ISDN subscriber" : "network initiated";
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP SUS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:      " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Причина:  " << COLOR_GREEN << cause_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:   " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP SUS:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ISUP SAM — Subsequent Address Message (ITU-T Q.763 §3.27)
// Используется при overlap dialling: передаёт дополнительные цифры после IAM
// MT=0x02, mandatory variable: Subsequent Number (1+ BCD-digits)
static struct msgb *generate_isup_sam(uint16_t cic, const char *digits) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP SAM");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x02;   // MT=SAM
    // Mandatory variable pointer to SubsNum (1 byte, offset from pointer byte)
    *(msgb_put(msg, 1)) = 0x01;
    // SubsNum: len + nature_byte + BCD digits
    size_t dlen = strlen(digits);
    uint8_t nature = (uint8_t)(((dlen & 1) ? 0x01 : 0x00) | 0x10); // odd|plan=ISDN
    uint8_t bcd_bytes = (uint8_t)((dlen + 1) / 2);
    *(msgb_put(msg, 1)) = (uint8_t)(1 + bcd_bytes);   // SubsNum length
    *(msgb_put(msg, 1)) = nature;
    for (size_t i = 0; i < bcd_bytes; i++) {
        uint8_t lo = (uint8_t)(digits[2*i] - '0');
        uint8_t hi = (2*i+1 < dlen) ? (uint8_t)(digits[2*i+1] - '0') : 0x0F;
        *(msgb_put(msg, 1)) = (uint8_t)((hi << 4) | lo);
    }
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP SAM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Digits: " << COLOR_GREEN << digits << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP SAM:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ISUP CCR — Continuity Check Request (ITU-T Q.763 §3.6)
// MSC запрашивает проверку непрерывности речевого тракта (loop test)
// MT=0x11, нет обязательных фиксированных параметров
static struct msgb *generate_isup_ccr(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP CCR");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x11;   // MT=CCR
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP CCR" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP CCR:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ISUP COT — Continuity (ITU-T Q.763 §3.5)
// Ответ на CCR: результат проверки непрерывности (success или fail)
// MT=0x05, Continuity Indicators: bit0=1→успех, bit0=0→отказ
static struct msgb *generate_isup_cot(uint16_t cic, bool success) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP COT");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x05;   // MT=COT
    *(msgb_put(msg, 1)) = success ? 0x01 : 0x00;  // Continuity Indicators
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP COT" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:     " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Статус:  " << COLOR_GREEN << (success ? "Successful" : "Failed") << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP COT:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ISUP FOT — Forward Transfer (ITU-T Q.763 §3.10)
// Сигнал о передаче вызова вперёд (применяется в overlap signalling)
// MT=0x08, нет обязательных параметров
static struct msgb *generate_isup_fot(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP FOT");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x08;   // MT=FOT
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP FOT" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP FOT:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ISUP INR — Information Request (ITU-T Q.763 §3.22)
// Запрос информации о вызывающем абоненте (CLIR, категория и т.д.)
// MT=0x0A, Information Request Indicators: 2 байта
// byte1: bit0=calling party address, bit2=category; byte2: bit0=charge info
static struct msgb *generate_isup_inr(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP INR");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x0A;   // MT=INR
    // Information Request Indicators: request calling party address + category
    *(msgb_put(msg, 1)) = 0x05;   // byte1: bit0=addr, bit2=category
    *(msgb_put(msg, 1)) = 0x00;   // byte2: no charge info requested
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP INR" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Запрос: " << COLOR_GREEN << "Calling Party Address + Category" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP INR:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ISUP INF — Information (ITU-T Q.763 §3.21)
// Ответ на INR: предоставляет информацию о вызывающем абоненте
// MT=0x04, Information Indicators: 2 байта
// byte1 bits1-0: calling party addr (10=included); bit3: holding ind
static struct msgb *generate_isup_inf(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP INF");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x04;   // MT=INF
    // Information Indicators
    *(msgb_put(msg, 1)) = 0x02;   // byte1: bits1-0=10 (calling party addr included)
    *(msgb_put(msg, 1)) = 0x00;   // byte2
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP INF" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Info:   " << COLOR_GREEN << "Calling Party Address included" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP INF:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ISUP CFN — Confusion (ITU-T Q.763 §3.3)
// Отправляется при получении нераспознанного/некорректного сообщения
// MT=0x2F, mandatory variable: Cause Indicators
static struct msgb *generate_isup_cfn(uint16_t cic, uint8_t cause_val) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP CFN");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x2F;   // MT=CFN
    // Mandatory variable pointer (offset 1 from here = pointer byte)
    *(msgb_put(msg, 1)) = 0x01;
    // Cause Indicators: len=2, location=0x0A(inter-exchange), cause_val
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x80;                          // ext=1 coding=ITU location=0 (user)
    *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (cause_val & 0x7F));
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP CFN" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:   " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << (int)cause_val << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP CFN:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ISUP OVL — Overload (ITU-T Q.763 §3.12)
// Уведомление о перегрузке (не вводит канал в блокировку, только сигнал)
// MT=0x31, нет обязательных параметров кроме конца optional list
static struct msgb *generate_isup_ovl(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP OVL");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x31;   // MT=OVL
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP OVL" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP OVL:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ISUP UPT — User Part Test (ITU-T Q.763 §3.32)
// Тест доступности пользовательской части на удалённом узле
// MT=0x34, нет обязательных параметров
static struct msgb *generate_isup_upt(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP UPT");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x34;   // MT=UPT
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP UPT" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP UPT:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ISUP UPA — User Part Available (ITU-T Q.763 §3.33)
// Ответ на UPT: подтверждает доступность UP
// MT=0x35, нет обязательных параметров
static struct msgb *generate_isup_upa(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP UPA");
    if (!msg) return nullptr;
    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF); p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x35;   // MT=UPA
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP UPA" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP UPA:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ISUP Facility (FAR) (ITU-T Q.764 / ETSI EN 300 356)
// MT=0x1F  — Facility message (GSM ISUP: передача Facility IE)
// No mandatory fixed parameters; optional: Facility IE
static struct msgb *generate_isup_far(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP FAR");
    if (!msg) return nullptr;
    // CIC (2 bytes LE)
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    // Message Type
    *(msgb_put(msg, 1)) = 0x1F;
    // Optional parameters end
    *(msgb_put(msg, 1)) = 0x00;
    std::cout << COLOR_CYAN << "✓ ISUP Facility (FAR)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x1F  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Identification Request (IDR) (ITU-T Q.764 / ETSI EN 300 356)
// MT=0x3B  — запрос идентификации (CAMEL: billing/fraud detection)
// No mandatory fixed parameters
static struct msgb *generate_isup_idr(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP IDR");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x3B;  // IDR
    *(msgb_put(msg, 1)) = 0x00;  // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Identification Request (IDR)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x3B  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Identification Response (IRS) (ITU-T Q.764 / ETSI EN 300 356)
// MT=0x3C  — ответ на Identification Request (IDR)
// No mandatory fixed parameters beyond optional end
static struct msgb *generate_isup_irs(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP IRS");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x3C;   // IRS
    *(msgb_put(msg, 1)) = 0x00;   // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Identification Response (IRS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x3C  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Loop Back Acknowledgement (LPA) (ITU-T Q.764 §2.9.5)
// MT=0x24  — подтверждение петли (loop test ответ на LPP)
// No mandatory parameters
static struct msgb *generate_isup_lpa(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP LPA");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x24;   // LPA
    *(msgb_put(msg, 1)) = 0x00;   // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Loop Back Acknowledgement (LPA)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x24  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Circuit Group Query (CQM) (ITU-T Q.764)
// MT=0x2C  — запрос состояния группы цепей
// Mandatory: Range and Status (fixed part: 2 bytes: range, status bitmap)
static struct msgb *generate_isup_cqm(uint16_t cic, uint8_t range) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP CQM");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x2C;   // CQM
    // Range and Status: pointer to mandatory param, 1 byte range
    *(msgb_put(msg, 1)) = 0x01;   // pointer to mandatory variable part
    *(msgb_put(msg, 1)) = 0x01;   // length=1
    *(msgb_put(msg, 1)) = range;  // range (0=1 circuit, 7=8 circuits)
    *(msgb_put(msg, 1)) = 0x00;   // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Circuit Group Query (CQM)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2C  CIC: " << COLOR_GREEN << cic
              << COLOR_RESET << COLOR_BLUE << "  Range: " << COLOR_GREEN << (int)range << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Circuit Group Query Response (CQR) (ITU-T Q.764)
// MT=0x2D  — ответ на запрос состояния группы цепей
// Mandatory: Range and Status
static struct msgb *generate_isup_cqr(uint16_t cic, uint8_t range) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP CQR");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x2D;   // CQR
    // Range and Status
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = range;
    *(msgb_put(msg, 1)) = 0x00;
    std::cout << COLOR_CYAN << "✓ ISUP Circuit Group Query Response (CQR)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2D  CIC: " << COLOR_GREEN << cic
              << COLOR_RESET << COLOR_BLUE << "  Range: " << COLOR_GREEN << (int)range << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Loop Prevention (LOP) (ITU-T Q.764 §2.9.5.3)
// MT=0x23  — предотвращение зацикливания при loop-back тестировании
// No mandatory parameters after CIC
static struct msgb *generate_isup_lop(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP LOP");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x23;   // LOP
    *(msgb_put(msg, 1)) = 0x00;   // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Loop Prevention (LOP)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x23  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Pass-Along (PAM) (ITU-T Q.764 §2.3.8)
// MT=0x28  — транспортное сообщение для вложенного ISUP-сообщения (редко)
// Mandatory: Message Compatibility Information + enclosed message
static struct msgb *generate_isup_pam(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP PAM");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x28;   // PAM
    // Enclosed message: dummy INF (Information) 0x04
    static const uint8_t inner[] = {0x00, 0x00, 0x04, 0x00};  // CIC=0, MT=INF
    *(msgb_put(msg, 1)) = 0x01;   // pointer to variable part
    *(msgb_put(msg, 1)) = (uint8_t)sizeof(inner);
    memcpy(msgb_put(msg, sizeof(inner)), inner, sizeof(inner));
    *(msgb_put(msg, 1)) = 0x00;   // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Pass-Along (PAM)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x28  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Network Resource Management (NRM) (ITU-T Q.764)
// MT=0x32  — управление сетевыми ресурсами (эхо-контроль и др.)
// No mandatory parameters
static struct msgb *generate_isup_nrm(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP NRM");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x32;   // NRM
    *(msgb_put(msg, 1)) = 0x00;   // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Network Resource Management (NRM)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x32  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Overload (OLM) (ITU-T Q.764)
// MT=0x30  — уведомление об исчерпании ресурсов (перегрузка)
// No mandatory parameters
static struct msgb *generate_isup_olm(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP OLM");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x30;   // OLM
    *(msgb_put(msg, 1)) = 0x00;   // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Overload (OLM)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x30  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Exit (EXM) (ITU-T Q.764 §2.1.3)
// MT=0x31  — сообщение выхода (перед передачей управления другой сети)
// No mandatory parameters
static struct msgb *generate_isup_exm(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP EXM");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x31;   // EXM
    *(msgb_put(msg, 1)) = 0x00;   // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Exit (EXM)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x31  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Application Transport (APM) (ITU-T Q.765.5 / 3GPP TS 29.415)
// MT=0x63  — транспортировка данных приложений (Bearer Independent Call Control)
// Mandatory: Application transport parameter IE (variable, dummy content)
static struct msgb *generate_isup_apm(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP APM");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x63;   // APM
    // Pointer to variable part
    *(msgb_put(msg, 1)) = 0x01;
    // Application Transport IE: tag=0x78, length=3, dummy content
    static const uint8_t atp_data[] = {0x78, 0x03, 0x01, 0x00, 0x00};
    *(msgb_put(msg, 1)) = (uint8_t)sizeof(atp_data);
    memcpy(msgb_put(msg, sizeof(atp_data)), atp_data, sizeof(atp_data));
    *(msgb_put(msg, 1)) = 0x00;   // optional end
    std::cout << COLOR_CYAN << "✓ ISUP Application Transport (APM)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x63  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ─────────────────────────────────────────────────────────────────────────────
// P31: ISUP FAA / FRJ / CRM / CRA
// ─────────────────────────────────────────────────────────────────────────────

// ISUP Facility Accepted (FAA) (ITU-T Q.763 §3.17)
// MT=0x20  — подтверждение запроса услуги (Facility)
// Нет обязательных параметров кроме конца опциональных
static struct msgb *generate_isup_faa(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP FAA");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x20;   // MT=FAA (Facility Accepted)
    *(msgb_put(msg, 1)) = 0x00;   // End of optional parameters
    std::cout << COLOR_CYAN << "✓ ISUP Facility Accepted (FAA)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x20  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Facility Reject (FRJ) (ITU-T Q.763 §3.18)
// MT=0x21  — отклонение запроса услуги, содержит Cause Indicators
// Mandatory: Cause Indicators (variable, min 2 bytes)
static struct msgb *generate_isup_frj(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP FRJ");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x21;   // MT=FRJ (Facility Reject)
    // Mandatory variable part: pointer + Cause IE
    *(msgb_put(msg, 1)) = 0x01;   // pointer to cause
    *(msgb_put(msg, 1)) = 0x02;   // length = 2
    *(msgb_put(msg, 1)) = 0x80;   // location=user (0x80), coding=ITU
    *(msgb_put(msg, 1)) = 0x94;   // cause=20 (Subscriber absent)
    *(msgb_put(msg, 1)) = 0x00;   // End of optional
    std::cout << COLOR_CYAN << "✓ ISUP Facility Reject (FRJ)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x21  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Circuit Reservation (CRM) (ITU-T Q.763 §3.12)
// MT=0x1C  — резервирование цепи перед хэндовером (сети 2G→2G или 2G→3G)
// Mandatory: Nature of Connection Indicators (1 byte)
static struct msgb *generate_isup_crm(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP CRM");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x1C;   // MT=CRM (Circuit Reservation)
    // Nature of Connection Indicators: 1 octet
    // bits 1-2: satellite indicator (0=none), bits 3-4: continuity check (0=none), bit5: echo control (0)
    *(msgb_put(msg, 1)) = 0x00;   // no satellite, no cont.check, no echo ctrl
    *(msgb_put(msg, 1)) = 0x00;   // End of optional
    std::cout << COLOR_CYAN << "✓ ISUP Circuit Reservation (CRM)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x1C  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ISUP Circuit Reservation Acknowledgement (CRA) (ITU-T Q.763 §3.13)
// MT=0x1D  — подтверждение резервирования цепи
// Нет обязательных параметров
static struct msgb *generate_isup_cra(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP CRA");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(cic & 0xFF);
    *(msgb_put(msg, 1)) = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x1D;   // MT=CRA (Circuit Reservation Ack)
    *(msgb_put(msg, 1)) = 0x00;   // End of optional
    std::cout << COLOR_CYAN << "✓ ISUP Circuit Reservation Ack (CRA)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x1D  CIC: " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// ISUP RES (Resume) — ITU-T Q.763 §3.15
// Направление: MSC ↔ PSTN/GW  (возобновление B-канала после SUS)
// Структура:
//   CIC [2 байта LE]  MT=0x0E
//   Mandatory Fixed:
//     Suspend/Resume Indicators [1]: (те же биты что у SUS)
//   Optional pointer: 0x00
// Итого: 5 байт
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_res(uint16_t cic, uint8_t sus_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP RES");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    *(msgb_put(msg, 1)) = 0x0E;  // MT=RES

    *(msgb_put(msg, 1)) = (uint8_t)(sus_cause & 0x01);

    *(msgb_put(msg, 1)) = 0x00;  // Optional pointer = none

    const char *cause_str = (sus_cause & 0x01) ? "ISDN subscriber" : "network initiated";
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP RES" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:      " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Причина:  " << COLOR_GREEN << cause_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:   " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP RES:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P16: ISUP BLO — Blocking (ITU-T Q.763 §3.4)
// CIC [2 LE] + MT=0x13 + EOP=0x00  →  4 байта
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_blo(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP BLO");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    *(msgb_put(msg, 1)) = 0x13;  // MT=BLO
    *(msgb_put(msg, 1)) = 0x00;  // EOP

    std::cout << COLOR_CYAN << "\u2713 Сгенерировано ISUP BLO (Blocking)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x13" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP BLO:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P16: ISUP UBL — Unblocking (ITU-T Q.763 §3.34)
// CIC [2 LE] + MT=0x14 + EOP=0x00  →  4 байта
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_ubl(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP UBL");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    *(msgb_put(msg, 1)) = 0x14;  // MT=UBL
    *(msgb_put(msg, 1)) = 0x00;  // EOP

    std::cout << COLOR_CYAN << "\u2713 Сгенерировано ISUP UBL (Unblocking)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x14" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP UBL:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P16: ISUP RSC — Reset Circuit (ITU-T Q.763 §3.26)
// CIC [2 LE] + MT=0x12 + EOP=0x00  →  4 байта
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_rsc(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP RSC");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    *(msgb_put(msg, 1)) = 0x12;  // MT=RSC
    *(msgb_put(msg, 1)) = 0x00;  // EOP

    std::cout << COLOR_CYAN << "\u2713 Сгенерировано ISUP RSC (Reset Circuit)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x12" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP RSC:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P16: ISUP GRS — Group Reset (ITU-T Q.763 §3.16)
// CIC [2 LE] + MT=0x17
// + Mandatory variable part pointer = 0x01
// + Range and Status: len=0x01 + range_value
// + EOP=0x00  →  7 байт
// range_value: 0 = 1 цепь, N = N+1 цепей (умолч. 7 = 8 цепей)
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_grs(uint16_t cic, uint8_t range_value) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP GRS");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);

    *(msgb_put(msg, 1)) = 0x17;  // MT=GRS
    *(msgb_put(msg, 1)) = 0x01;  // pointer to Range and Status (1 byte forward)
    *(msgb_put(msg, 1)) = 0x01;  // Range and Status length
    *(msgb_put(msg, 1)) = range_value & 0x7F;  // Range (7 bits)
    *(msgb_put(msg, 1)) = 0x00;  // EOP

    std::cout << COLOR_CYAN << "\u2713 Сгенерировано ISUP GRS (Group Reset)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:     " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Range:   " << COLOR_GREEN << (int)range_value << " (" << (int)(range_value + 1) << " цепей: CIC " << cic << ".." << (cic + range_value) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:      " << COLOR_GREEN << "0x17" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:   " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP GRS:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P19: ISUP BLA — Blocking Acknowledgement (ITU-T Q.763 §3.5)
// Response to BLO.  MT=0x15.  CIC [2 LE] + MT + EOP = 4 байта
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_bla(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP BLA");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x15;  // MT=BLA
    *(msgb_put(msg, 1)) = 0x00;  // EOP

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP BLA (Blocking Acknowledgement)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x15 (BLA)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP BLA:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P19: ISUP UBA — Unblocking Acknowledgement (ITU-T Q.763 §3.35)
// Response to UBL.  MT=0x16.  CIC [2 LE] + MT + EOP = 4 байта
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_uba(uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP UBA");
    if (!msg) return nullptr;

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x16;  // MT=UBA
    *(msgb_put(msg, 1)) = 0x00;  // EOP

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP UBA (Unblocking Acknowledgement)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x16 (UBA)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP UBA:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P19: ISUP GRA — Group Reset Acknowledgement (ITU-T Q.763 §3.17)
// Response to GRS.  MT=0x29.
// Structure: CIC[2LE] + MT + ptr(1) + RS.len + range + status_bitmap + EOP
//   RS.len = 1 + ceil((range+1)/8)
//   status_bitmap: all zeros = all circuits successfully reset
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_isup_gra(uint16_t cic, uint8_t range_value) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "ISUP GRA");
    if (!msg) return nullptr;

    uint8_t bitmap_bytes = (uint8_t)((range_value + 8) / 8);  // ceil((range+1)/8)
    uint8_t rs_len       = (uint8_t)(1 + bitmap_bytes);        // range(1) + status_bitmap

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = 0x29;              // MT=GRA
    *(msgb_put(msg, 1)) = 0x01;              // pointer to Range and Status (1 octet fwd)
    *(msgb_put(msg, 1)) = rs_len;            // Range and Status: length field
    *(msgb_put(msg, 1)) = range_value & 0x7F; // Range (7 bits, bit8=spare)
    uint8_t *bm = msgb_put(msg, bitmap_bytes);
    memset(bm, 0x00, bitmap_bytes);          // Status: all zeros = all circuits reset OK
    *(msgb_put(msg, 1)) = 0x00;              // EOP

    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP GRA (Group Reset Acknowledgement)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Range:  " << COLOR_GREEN << (int)range_value
              << " (" << (int)(range_value + 1) << " цепей: CIC " << cic << ".." << (cic + range_value) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Status: " << COLOR_GREEN << "все нули (все цепи сброшены успешно)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x29 (GRA)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP GRA:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P20: ISUP Circuit Group Blocking/Unblocking (ITU-T Q.763 §3.12–3.14)
//
// CGB  MT=0x18  Circuit Group Blocking
// CGU  MT=0x19  Circuit Group Unblocking
// CGBA MT=0x1A  Circuit Group Blocking Acknowledgement
// CGUA MT=0x1B  Circuit Group Unblocking Acknowledgement
//
// Structure: CIC[2LE] + MT + ptr_CGS + ptr_RS
//   + CGS.len(1) + cgs_byte(1)
//   + RS.len(1+bitmap_bytes) + range(1) + status_bitmap[N] + EOP
// ptr_CGS = 2  (distance from ptr_CGS byte to CGS.len)
// ptr_RS  = 3  (distance from ptr_RS byte to RS.len)
// cgs_byte bits 0-1: 0x00=maintenance, 0x01=hardware failure
// ──────────────────────────────────────────────────────────────
static struct msgb *build_isup_cg_msg(uint16_t cic, uint8_t mt, uint8_t cgs_byte,
                                       uint8_t range_value, uint8_t status_fill,
                                       const char *label) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, label);
    if (!msg) return nullptr;
    uint8_t bitmap_bytes = (uint8_t)((range_value + 8) / 8);
    uint8_t rs_len       = (uint8_t)(1 + bitmap_bytes);

    uint8_t *p = msgb_put(msg, 2);
    p[0] = (uint8_t)(cic & 0xFF);
    p[1] = (uint8_t)(cic >> 8);
    *(msgb_put(msg, 1)) = mt;             // Message Type
    *(msgb_put(msg, 1)) = 0x02;           // ptr_CGS: 2 bytes to CGS.len
    *(msgb_put(msg, 1)) = 0x03;           // ptr_RS:  3 bytes to RS.len
    *(msgb_put(msg, 1)) = 0x01;           // CGS.len = 1
    *(msgb_put(msg, 1)) = cgs_byte & 0x03; // Circuit Group Supervision indicator
    *(msgb_put(msg, 1)) = rs_len;         // RS.len = 1 + bitmap
    *(msgb_put(msg, 1)) = range_value & 0x7F; // Range (7 bits)
    uint8_t *bm = msgb_put(msg, bitmap_bytes);
    memset(bm, status_fill, bitmap_bytes);
    *(msgb_put(msg, 1)) = 0x00;           // EOP
    return msg;
}

static struct msgb *generate_isup_cgb(uint16_t cic, uint8_t range_value, uint8_t cause) {
    struct msgb *msg = build_isup_cg_msg(cic, 0x18, cause, range_value, 0x00, "ISUP CGB");
    if (!msg) return nullptr;
    const char *cause_str = (cause & 0x03) == 0 ? "maintenance" : "hardware failure";
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP CGB (Circuit Group Blocking)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Range:  " << COLOR_GREEN << (int)range_value
              << " (" << (int)(range_value+1) << " цепей: CIC " << cic << ".." << (cic+range_value) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN << cause_str << " (" << (int)(cause&3) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x18 (CGB)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP CGB:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

static struct msgb *generate_isup_cgu(uint16_t cic, uint8_t range_value, uint8_t cause) {
    struct msgb *msg = build_isup_cg_msg(cic, 0x19, cause, range_value, 0x00, "ISUP CGU");
    if (!msg) return nullptr;
    const char *cause_str = (cause & 0x03) == 0 ? "maintenance" : "hardware failure";
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP CGU (Circuit Group Unblocking)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Range:  " << COLOR_GREEN << (int)range_value
              << " (" << (int)(range_value+1) << " цепей: CIC " << cic << ".." << (cic+range_value) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN << cause_str << " (" << (int)(cause&3) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x19 (CGU)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP CGU:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

static struct msgb *generate_isup_cgba(uint16_t cic, uint8_t range_value, uint8_t cause) {
    struct msgb *msg = build_isup_cg_msg(cic, 0x1A, cause, range_value, 0xFF, "ISUP CGBA");
    if (!msg) return nullptr;
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP CGBA (Circuit Group Blocking Ack)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Range:  " << COLOR_GREEN << (int)range_value
              << " (" << (int)(range_value+1) << " цепей)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x1A (CGBA)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP CGBA:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

static struct msgb *generate_isup_cgua(uint16_t cic, uint8_t range_value, uint8_t cause) {
    struct msgb *msg = build_isup_cg_msg(cic, 0x1B, cause, range_value, 0xFF, "ISUP CGUA");
    if (!msg) return nullptr;
    std::cout << COLOR_CYAN << "✓ Сгенерировано ISUP CGUA (Circuit Group Unblocking Ack)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:    " << COLOR_GREEN << cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Range:  " << COLOR_GREEN << (int)range_value
              << " (" << (int)(range_value+1) << " цепей)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT:     " << COLOR_GREEN << "0x1B (CGUA)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ISUP CGUA:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P17: M3UA ASP Management (RFC 4666 §3.5 / §3.6)
//
// M3UA Common Header (8 bytes):
//   Version(1) Reserved(1) Class(1) Type(1) Length(4 BE, includes header)
//
// ASPSM (Class=0x03):
//   ASPUP     Type=0x01  — ASP Up             (SGP→ASP или ASP→SGP)
//   ASPUP-ACK Type=0x04  — ASP Up Ack
//   ASPDN     Type=0x02  — ASP Down
// ASPTM (Class=0x04):
//   ASPAC     Type=0x01  — ASP Active         (+ Traffic Mode Type param)
//   ASPAC-ACK Type=0x03  — ASP Active Ack
//   ASPIA     Type=0x02  — ASP Inactive
//
// Traffic Mode Type (Tag=0x000B, Len=0x0008): 1=Override 2=Loadshare 3=Broadcast
// Routing Context  (Tag=0x0006, Len=0x0008): 4-byte RC value (skip if rc==0)
// ──────────────────────────────────────────────────────────────

// helper: append a 4-byte uint32 TLV param to an msgb
static void m3ua_put_param(struct msgb *msg, uint16_t tag, uint32_t val) {
    *(msgb_put(msg, 1)) = (tag >> 8) & 0xFF;
    *(msgb_put(msg, 1)) =  tag       & 0xFF;
    *(msgb_put(msg, 1)) = 0x00;  // Length high = 0
    *(msgb_put(msg, 1)) = 0x08;  // Length = 8 (T+L+V)
    *(msgb_put(msg, 1)) = (val >> 24) & 0xFF;
    *(msgb_put(msg, 1)) = (val >> 16) & 0xFF;
    *(msgb_put(msg, 1)) = (val >>  8) & 0xFF;
    *(msgb_put(msg, 1)) =  val        & 0xFF;
}

// helper: build M3UA common header, return pointer to length field for fixup
static uint8_t *m3ua_put_header(struct msgb *msg, uint8_t msg_class, uint8_t msg_type) {
    *(msgb_put(msg, 1)) = 0x01;         // Version
    *(msgb_put(msg, 1)) = 0x00;         // Reserved
    *(msgb_put(msg, 1)) = msg_class;
    *(msgb_put(msg, 1)) = msg_type;
    uint8_t *len_ptr = msgb_put(msg, 4); // Length — fill after
    return len_ptr;
}

static void m3ua_fix_len(struct msgb *msg, uint8_t *len_ptr) {
    uint32_t l = msg->len;
    len_ptr[0] = (l >> 24) & 0xFF;
    len_ptr[1] = (l >> 16) & 0xFF;
    len_ptr[2] = (l >>  8) & 0xFF;
    len_ptr[3] =  l        & 0xFF;
}

// ── ASPUP ─────────────────────────────────────────────────────
static struct msgb *generate_m3ua_aspup() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA ASPUP");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x03, 0x01);
    m3ua_fix_len(msg, lp);   // 8 bytes — no params
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e M3UA ASPUP (ASP Up)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x03 (ASPSM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: "  << COLOR_GREEN << "0x01" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ASPUP:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ── ASPUP-ACK ─────────────────────────────────────────────────
static struct msgb *generate_m3ua_aspup_ack() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA ASPUP-ACK");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x03, 0x04);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e M3UA ASPUP-ACK" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x03 (ASPSM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: "  << COLOR_GREEN << "0x04" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ASPUP-ACK:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ── ASPDN ─────────────────────────────────────────────────────
static struct msgb *generate_m3ua_aspdn() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA ASPDN");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x03, 0x02);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e M3UA ASPDN (ASP Down)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x03 (ASPSM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: "  << COLOR_GREEN << "0x02" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ASPDN:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ── ASPAC ─────────────────────────────────────────────────────
static struct msgb *generate_m3ua_aspac(uint8_t tmt, uint32_t rc) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA ASPAC");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x04, 0x01);
    m3ua_put_param(msg, 0x000B, (uint32_t)tmt);  // Traffic Mode Type
    if (rc != 0) m3ua_put_param(msg, 0x0006, rc); // Routing Context (optional)
    m3ua_fix_len(msg, lp);
    const char *tmt_str = (tmt==1)?"Override":(tmt==2)?"Loadshare":(tmt==3)?"Broadcast":"?";
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e M3UA ASPAC (ASP Active)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class:  " << COLOR_GREEN << "0x04 (ASPTM)" << COLOR_RESET
              << COLOR_BLUE << "  Type:   " << COLOR_GREEN << "0x01" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TMT:    " << COLOR_GREEN << (int)tmt << " (" << tmt_str << ")" << COLOR_RESET << "\n";
    if (rc) std::cout << COLOR_BLUE << "  RC:     " << COLOR_GREEN << rc << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440:  " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ASPAC:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ── ASPAC-ACK ─────────────────────────────────────────────────
static struct msgb *generate_m3ua_aspac_ack(uint8_t tmt, uint32_t rc) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA ASPAC-ACK");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x04, 0x03);
    m3ua_put_param(msg, 0x000B, (uint32_t)tmt);
    if (rc != 0) m3ua_put_param(msg, 0x0006, rc);
    m3ua_fix_len(msg, lp);
    const char *tmt_str = (tmt==1)?"Override":(tmt==2)?"Loadshare":(tmt==3)?"Broadcast":"?";
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e M3UA ASPAC-ACK" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class:  " << COLOR_GREEN << "0x04 (ASPTM)" << COLOR_RESET
              << COLOR_BLUE << "  Type:   " << COLOR_GREEN << "0x03" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TMT:    " << COLOR_GREEN << (int)tmt << " (" << tmt_str << ")" << COLOR_RESET << "\n";
    if (rc) std::cout << COLOR_BLUE << "  RC:     " << COLOR_GREEN << rc << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440:  " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ASPAC-ACK:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ── ASPIA ─────────────────────────────────────────────────────
static struct msgb *generate_m3ua_aspia() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA ASPIA");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x04, 0x02);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e M3UA ASPIA (ASP Inactive)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x04 (ASPTM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: "  << COLOR_GREEN << "0x02" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ASPIA:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// P19: M3UA ACK + Heartbeat messages (RFC 4666 §3.3)
//
// ASPSM (Class=0x03):
//   ASPDN-ACK  Type=0x05  — ASP Down Ack
//   BEAT       Type=0x03  — Heartbeat (no mandatory params)
//   BEAT-ACK   Type=0x06  — Heartbeat Ack
// ASPTM (Class=0x04):
//   ASPIA-ACK  Type=0x04  — ASP Inactive Ack
// ──────────────────────────────────────────────────────────────

// ── ASPDN-ACK ─────────────────────────────────────────────────
static struct msgb *generate_m3ua_aspdn_ack() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA ASPDN-ACK");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x03, 0x05);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "✓ Сгенерировано M3UA ASPDN-ACK (ASP Down Ack)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x03 (ASPSM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: " << COLOR_GREEN << "0x05" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ASPDN-ACK:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ── ASPIA-ACK ─────────────────────────────────────────────────
static struct msgb *generate_m3ua_aspia_ack() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA ASPIA-ACK");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x04, 0x04);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "✓ Сгенерировано M3UA ASPIA-ACK (ASP Inactive Ack)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x04 (ASPTM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: " << COLOR_GREEN << "0x04" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex ASPIA-ACK:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ── BEAT (Heartbeat) ──────────────────────────────────────────
static struct msgb *generate_m3ua_beat() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA BEAT");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x03, 0x03);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "✓ Сгенерировано M3UA BEAT (Heartbeat)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x03 (ASPSM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: " << COLOR_GREEN << "0x03" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex BEAT:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ── BEAT-ACK (Heartbeat Ack) ──────────────────────────────────
static struct msgb *generate_m3ua_beat_ack() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA BEAT-ACK");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x03, 0x06);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "✓ Сгенерировано M3UA BEAT-ACK (Heartbeat Ack)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x03 (ASPSM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: " << COLOR_GREEN << "0x06" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex BEAT-ACK:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// M3UA ERR — Error (RFC 4666 §3.8.1)
// Отправляется при обнаружении ошибки в принятом сообщении M3UA
// Class=0x00, Type=0x00
// Error Code parameter: Tag=0x000C, Len=8, Value=4-byte error code
// Коды: 0x01=Invalid Version, 0x03=Unsupported Msg Class, 0x13=Unsupported Msg Type
static struct msgb *generate_m3ua_err(uint32_t err_code) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA ERR");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x00, 0x00);  // Management Class=0, Type=0
    // Error Code parameter: Tag=0x000C, Length=8
    uint8_t *ep = msgb_put(msg, 8);
    ep[0] = 0x00; ep[1] = 0x0C;   // Tag = 0x000C
    ep[2] = 0x00; ep[3] = 0x08;   // Length = 8
    ep[4] = (uint8_t)(err_code >> 24); ep[5] = (uint8_t)(err_code >> 16);
    ep[6] = (uint8_t)(err_code >>  8); ep[7] = (uint8_t)(err_code      );
    m3ua_fix_len(msg, lp);
    const char *code_str = (err_code==0x01)?"Invalid Version":
                           (err_code==0x03)?"Unsupported Msg Class":
                           (err_code==0x13)?"Unsupported Msg Type":"Other";
    std::cout << COLOR_CYAN << "✓ Сгенерировано M3UA ERR (Error)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x00 (Management)" << COLOR_RESET
              << COLOR_BLUE << "  Type: " << COLOR_GREEN << "0x00" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Error Code: " << COLOR_GREEN
              << "0x" << std::hex << err_code << " (" << code_str << ")" << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex M3UA ERR:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// M3UA NTFY — Notify (RFC 4666 §3.8.2)
// Уведомление о смене состояния ASP/AS
// Class=0x00, Type=0x01
// Status parameter: Tag=0x000D, Len=8, StatusType(2)+StatusInfo(2)
// StatusType: 1=AS State Change, 2=Other
// StatusInfo (AS State): 1=Reserved, 2=AS-INACTIVE, 3=AS-ACTIVE, 4=AS-PENDING
static struct msgb *generate_m3ua_ntfy(uint16_t status_type, uint16_t status_info) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA NTFY");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x00, 0x01);  // Management Class=0, Type=1
    // Status parameter: Tag=0x000D, Length=8
    uint8_t *sp = msgb_put(msg, 8);
    sp[0] = 0x00; sp[1] = 0x0D;   // Tag = 0x000D
    sp[2] = 0x00; sp[3] = 0x08;   // Length = 8
    sp[4] = (uint8_t)(status_type >> 8); sp[5] = (uint8_t)(status_type);
    sp[6] = (uint8_t)(status_info >> 8); sp[7] = (uint8_t)(status_info);
    m3ua_fix_len(msg, lp);
    const char *type_str = (status_type==1)?"AS State Change":(status_type==2)?"Other":"Unknown";
    const char *info_str = (status_type==1&&status_info==2)?"AS-INACTIVE":
                           (status_type==1&&status_info==3)?"AS-ACTIVE":
                           (status_type==1&&status_info==4)?"AS-PENDING":"?";
    std::cout << COLOR_CYAN << "✓ Сгенерировано M3UA NTFY (Notify)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x00 (Management)" << COLOR_RESET
              << COLOR_BLUE << "  Type: " << COLOR_GREEN << "0x01" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Status: " << COLOR_GREEN
              << type_str << " / " << info_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex M3UA NTFY:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ── P22: M3UA SSNM — Destination State Messages (RFC 4666 §3.4) ─────────────
// Class=0x02 (SSNM), Affected Point Code param: Tag=0x012D, Len=8
//   Value: mask(1 byte, 0x00) + reserved(2 bytes) + PC_BE(3 bytes) — actually
//   RFC 4666 §3.4.1: the Affected Point Code parameter is 4-byte aligned:
//     Tag=0x012D, Length=0x0008, mask=0x00, reserved=0x00, PC[3 BE]
//   (one Affected PC entry = 4 bytes: mask(1)+PC[3 BE])
static struct msgb *generate_m3ua_duna(uint32_t aff_pc) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA DUNA");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x02, 0x01);  // SSNM Class=2 Type=1
    // Affected Point Code param: Tag=0x012D, Len=8, mask(1)+PC(3 BE)
    uint8_t aff[4] = { 0x00,
                       (uint8_t)((aff_pc >> 16) & 0xFF),
                       (uint8_t)((aff_pc >>  8) & 0xFF),
                       (uint8_t)( aff_pc        & 0xFF) };
    uint16_t tag = 0x012D;
    uint16_t plen = 8;  // tag(2)+len(2)+value(4)
    uint8_t *p = msgb_put(msg, 8);
    p[0] = (tag >> 8) & 0xFF;  p[1] = tag & 0xFF;
    p[2] = (plen >> 8) & 0xFF; p[3] = plen & 0xFF;
    memcpy(p + 4, aff, 4);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e M3UA DUNA (Destination Unavailable)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x02 (SSNM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: " << COLOR_GREEN << "0x01" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Affected PC: " << COLOR_GREEN << aff_pc << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex DUNA:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

static struct msgb *generate_m3ua_dava(uint32_t aff_pc) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA DAVA");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x02, 0x02);  // SSNM Class=2 Type=2
    uint8_t aff[4] = { 0x00,
                       (uint8_t)((aff_pc >> 16) & 0xFF),
                       (uint8_t)((aff_pc >>  8) & 0xFF),
                       (uint8_t)( aff_pc        & 0xFF) };
    uint16_t tag = 0x012D;
    uint16_t plen = 8;
    uint8_t *p = msgb_put(msg, 8);
    p[0] = (tag >> 8) & 0xFF;  p[1] = tag & 0xFF;
    p[2] = (plen >> 8) & 0xFF; p[3] = plen & 0xFF;
    memcpy(p + 4, aff, 4);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e M3UA DAVA (Destination Available)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x02 (SSNM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: " << COLOR_GREEN << "0x02" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Affected PC: " << COLOR_GREEN << aff_pc << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex DAVA:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

static struct msgb *generate_m3ua_daud(uint32_t aff_pc) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "M3UA DAUD");
    if (!msg) return nullptr;
    uint8_t *lp = m3ua_put_header(msg, 0x02, 0x03);  // SSNM Class=2 Type=3
    uint8_t aff[4] = { 0x00,
                       (uint8_t)((aff_pc >> 16) & 0xFF),
                       (uint8_t)((aff_pc >>  8) & 0xFF),
                       (uint8_t)( aff_pc        & 0xFF) };
    uint16_t tag = 0x012D;
    uint16_t plen = 8;
    uint8_t *p = msgb_put(msg, 8);
    p[0] = (tag >> 8) & 0xFF;  p[1] = tag & 0xFF;
    p[2] = (plen >> 8) & 0xFF; p[3] = plen & 0xFF;
    memcpy(p + 4, aff, 4);
    m3ua_fix_len(msg, lp);
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e M3UA DAUD (Destination State Audit)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Class: " << COLOR_GREEN << "0x02 (SSNM)" << COLOR_RESET
              << COLOR_BLUE << "  Type: " << COLOR_GREEN << "0x03" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Affected PC: " << COLOR_GREEN << aff_pc << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex DAUD:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// Вспомогательная функция: кодирование текста в GSM-7 (3GPP TS 23.038 §6.2.1)
// in: ASCII-строка до 160 символов
// out: буфер GSM7-packed данных, возвращает длину в байтах
// Кодируем только базовый GSM7-алфавит (прямое ASCII→GSM7 отображение
// для символов 0x20–0x7E). Символы вне алфавита заменяются пробелом.
// ──────────────────────────────────────────────────────────────
static uint8_t gsm7_pack(const char *text, uint8_t *out, uint8_t *num_septets) {
    static const char *gsm7_table =
        "@£$¥èéùìòÇ\nØø\rÅåΔ_ΦΓΛΩΠΨΣΘΞ\x1bÆæßÉ !\"#¤%&'()*+,-./0123456789:;<=>?"
        "¡ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÑÜ§¿abcdefghijklmnopqrstuvwxyzäöñüà";
    // Упрощённое кодирование: используем прямое ASCII→GSM7 для printable ASCII
    size_t len = strlen(text);
    if (len > 160) len = 160;
    *num_septets = (uint8_t)len;

    // pack: 7 bits per character → 8-bit bytes
    uint8_t byte_len = (uint8_t)(((size_t)len * 7 + 7) / 8);
    memset(out, 0, byte_len);
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)text[i];
        // GSM7 basic table offset for ' '=0x20 → 0x11 (17 in GSM7), etc.
        // For simplicity: if printable ASCII and in range, use value directly
        // (GSM7 space=0x10, digits=0x30+, uppercase=0x41+, lowercase=0x61+)
        // We'll use a simple mapping: just pass char as-is for printable ASCII
        // (standard subset; real encoding would need full table lookup)
        uint8_t g = (c >= 0x20 && c <= 0x7A) ? c : 0x20;
        // actual GSM7 codes for printable:
        // 0x20=space(0x10), digits/upper/lower map near 1:1 except offset
        // For testing purposes, treat ASCII value as GSM7 value
        // (close enough for simulator — in production use libiconv)
        size_t bit_offset = i * 7;
        size_t byte_idx   = bit_offset / 8;
        size_t bit_idx    = bit_offset % 8;
        out[byte_idx]   |= (uint8_t)(g << bit_idx);
        if (bit_idx > 1 && byte_idx + 1 < byte_len)
            out[byte_idx + 1] |= (uint8_t)(g >> (8 - bit_idx));
    }
    (void)gsm7_table;
    return byte_len;
}

// ──────────────────────────────────────────────────────────────
// MAP MO-ForwardSM — 3GPP TS 29.002 §12.2 / §10.5.6.6
// Направление: MSC → SMSC  (C-interface)
// TCAP Begin, Invoke, opCode=46 (0x2E)
// arg: MO-ForwardSM-Arg ::= SEQUENCE {
//        sm-RP-DA   [0] SM-RP-DA     (SMSC address)
//        sm-RP-OA   [1] SM-RP-OA     (MS address / IMSI)
//        sm-RP-UI   [2] SignalInfo    (TPDU: TP-Submit PDU)
//      }
// AC OID: shortMsgMO-RelayContext-v3 = {0.4.0.0.1.0.25.3}
// BER OID: 04 00 00 01 00 19 03
// TID base: 0x0800
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_mo_forward_sm(const char *imsi_str,
                                                const char *smsc_str,
                                                const char *sm_text) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP MO-FSM");
    if (!msg) return nullptr;

    // ── SM-RP-DA [0]: SMSC E.164 address
    uint8_t smsc_bcd[11]; uint8_t smsc_bcd_len = 0;
    {
        size_t dlen = strlen(smsc_str);
        bool odd = (dlen % 2) != 0;
        smsc_bcd[smsc_bcd_len++] = (uint8_t)((odd ? 0x80 : 0x00) | 0x91); // TON=international, NPI=E.164
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t d1 = (uint8_t)(smsc_str[i] - '0');
            uint8_t d2 = (i+1<dlen) ? (uint8_t)(smsc_str[i+1]-'0') : 0x0F;
            smsc_bcd[smsc_bcd_len++] = (uint8_t)((d2 << 4) | d1);
        }
    }
    uint8_t rpda_ie[14]; uint8_t rpda_len = (uint8_t)ber_tlv(rpda_ie, 0x80, smsc_bcd, smsc_bcd_len);

    // ── SM-RP-OA [1]: IMSI
    uint8_t imsi_bcd[9]; uint8_t imsi_bcd_len = 0;
    {
        size_t dlen = strlen(imsi_str);
        bool odd = (dlen % 2) != 0;
        imsi_bcd[imsi_bcd_len++] = (uint8_t)((odd ? 0x80 : 0x00) | 0x98); // TON/NPI for IMSI type in RP-OA
        // Actually RP-OA IMSI: tag=IMSI, no TON byte — just BCD IMSI
        // 3GPP TS 29.002: SM-RP-OA CHOICE { msisdn [1], imsi [2], serviceCenter [4] }
        // Use [2] IMSI: tag=0x82
        imsi_bcd_len = 0;  // reset
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t d1 = (uint8_t)(imsi_str[i] - '0');
            uint8_t d2 = (i+1<dlen) ? (uint8_t)(imsi_str[i+1]-'0') : 0x0F;
            imsi_bcd[imsi_bcd_len++] = (uint8_t)((d2 << 4) | d1);
        }
    }
    uint8_t rpoa_inner[12]; uint8_t rpoa_inner_len = (uint8_t)ber_tlv(rpoa_inner, 0x82, imsi_bcd, imsi_bcd_len);
    uint8_t rpoa_ie[14];    uint8_t rpoa_len = (uint8_t)ber_tlv(rpoa_ie, 0x81, rpoa_inner, rpoa_inner_len);

    // ── SM-RP-UI [2]: TP-Submit TPDU (GSM TS 23.040)
    // TP-Submit: MTI=0x01, MR=0, DA(length+TON+BCD), PID=0, DCS=0, VP, UD
    uint8_t tpdu[160]; uint8_t tpdu_len = 0;
    {
        tpdu[tpdu_len++] = 0x11; // TP-MTI=01(Submit), TP-VPF=10(relative), TP-RD=0, TP-SRR=0
        tpdu[tpdu_len++] = 0x00; // TP-MR=0

        // TP-DA: smsc is our DA for TP level
        size_t dlen = strlen(smsc_str);
        tpdu[tpdu_len++] = (uint8_t)dlen;    // TP-DA-Length (number of digits)
        tpdu[tpdu_len++] = 0x91;              // TON=1(international), NPI=1(E.164)
        bool odd = (dlen % 2) != 0;
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t d1 = (uint8_t)(smsc_str[i] - '0');
            uint8_t d2 = (i+1<dlen) ? (uint8_t)(smsc_str[i+1]-'0') : 0x0F;
            tpdu[tpdu_len++] = (uint8_t)((d2 << 4) | d1);
        }
        (void)odd;

        tpdu[tpdu_len++] = 0x00; // TP-PID = 0 (no interworking)
        tpdu[tpdu_len++] = 0x00; // TP-DCS = 0 (GSM7, class unspecified)
        tpdu[tpdu_len++] = 0x00; // TP-VP  = 0 (relative: 5 minutes)

        // TP-UD: pack SMS text
        uint8_t gsm7_buf[140]; uint8_t num_septets = 0;
        uint8_t gsm7_len = gsm7_pack(sm_text, gsm7_buf, &num_septets);
        tpdu[tpdu_len++] = num_septets; // TP-UDL (number of septets)
        memcpy(tpdu + tpdu_len, gsm7_buf, gsm7_len);
        tpdu_len += gsm7_len;
    }
    uint8_t rpui_ie[170]; uint8_t rpui_len = (uint8_t)ber_tlv(rpui_ie, 0x82, tpdu, tpdu_len);

    // ── MAP Invoke argument: SEQUENCE { sm-RP-DA, sm-RP-OA, sm-RP-UI }
    uint8_t seq_body[220]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, rpda_ie, rpda_len); sq_len += rpda_len;
    memcpy(seq_body + sq_len, rpoa_ie, rpoa_len); sq_len += rpoa_len;
    memcpy(seq_body + sq_len, rpui_ie, rpui_len); sq_len += rpui_len;
    uint8_t arg_seq[230]; uint8_t arg_len = (uint8_t)ber_tlv(arg_seq, 0x30, seq_body, sq_len);

    // ── Application Context: shortMsgMO-RelayContext-v3
    uint8_t mo_ac_oid[] = {0x04, 0x00, 0x00, 0x01, 0x00, 0x19, 0x03};
    static uint32_t mo_fsm_tid = 0x00000800;
    uint8_t pdu[512];
    uint8_t pdu_len = build_tcap_begin(pdu, mo_fsm_tid++, mo_ac_oid, sizeof(mo_ac_oid),
                                        0x01, 46 /*MO-ForwardSM*/, arg_seq, arg_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP MO-ForwardSM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:    " << COLOR_GREEN << imsi_str   << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SMSC:    " << COLOR_GREEN << smsc_str   << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Text:    " << COLOR_GREEN << sm_text    << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP MO-ForwardSM:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP MT-ForwardSM — 3GPP TS 29.002 §12.2 / §10.5.6.7
// Направление: SMSC → MSC  (C-interface)
// TCAP Begin, Invoke, opCode=44 (0x2C)
// arg: MT-ForwardSM-Arg ::= SEQUENCE {
//        imsi       [0] IMSI
//        sm-RP-OA   [2] SM-RP-OA    (SMSC address as serviceCenter)
//        sm-RP-UI   [3] SignalInfo   (TPDU: TP-Deliver PDU)
//      }
// AC OID: shortMsgMT-RelayContext-v3 = {0.4.0.0.1.0.26.3}
// BER OID: 04 00 00 01 00 1A 03
// TID base: 0x0900
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_mt_forward_sm(const char *imsi_str,
                                                const char *smsc_str,
                                                const char *sm_text) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP MT-FSM");
    if (!msg) return nullptr;

    // ── IMSI [0]: BCD-encoded IMSI
    uint8_t imsi_bcd[9]; uint8_t imsi_bcd_len = 0;
    {
        size_t dlen = strlen(imsi_str);
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t d1 = (uint8_t)(imsi_str[i] - '0');
            uint8_t d2 = (i+1<dlen) ? (uint8_t)(imsi_str[i+1]-'0') : 0x0F;
            imsi_bcd[imsi_bcd_len++] = (uint8_t)((d2 << 4) | d1);
        }
    }
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x80, imsi_bcd, imsi_bcd_len);

    // ── SM-RP-OA [2]: serviceCenter address (SMSC)
    uint8_t smsc_bcd[11]; uint8_t smsc_bcd_len = 0;
    {
        smsc_bcd[smsc_bcd_len++] = 0x91; // TON=international, NPI=E.164
        size_t dlen = strlen(smsc_str);
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t d1 = (uint8_t)(smsc_str[i] - '0');
            uint8_t d2 = (i+1<dlen) ? (uint8_t)(smsc_str[i+1]-'0') : 0x0F;
            smsc_bcd[smsc_bcd_len++] = (uint8_t)((d2 << 4) | d1);
        }
    }
    uint8_t sc_inner[13]; uint8_t sc_inner_len = (uint8_t)ber_tlv(sc_inner, 0x84, smsc_bcd, smsc_bcd_len);
    uint8_t rpoa_ie[15];  uint8_t rpoa_len = (uint8_t)ber_tlv(rpoa_ie, 0x82, sc_inner, sc_inner_len);

    // ── SM-RP-UI [3]: TP-Deliver TPDU (GSM TS 23.040)
    // TP-Deliver: MTI=0x00, MMS=1 (no more), OA, PID, DCS, SCTS, UD
    uint8_t tpdu[170]; uint8_t tpdu_len = 0;
    {
        tpdu[tpdu_len++] = 0x04; // TP-MTI=00(Deliver), TP-MMS=1, TP-SRI=0, TP-UDHI=0, TP-RP=0

        // TP-OA: SMSC as originating address
        size_t dlen = strlen(smsc_str);
        tpdu[tpdu_len++] = (uint8_t)dlen;    // TP-OA-Length (number of digits)
        tpdu[tpdu_len++] = 0x91;              // TON=international, NPI=E.164
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t d1 = (uint8_t)(smsc_str[i] - '0');
            uint8_t d2 = (i+1<dlen) ? (uint8_t)(smsc_str[i+1]-'0') : 0x0F;
            tpdu[tpdu_len++] = (uint8_t)((d2 << 4) | d1);
        }

        tpdu[tpdu_len++] = 0x00; // TP-PID
        tpdu[tpdu_len++] = 0x00; // TP-DCS = GSM7
        // TP-SCTS: 7 bytes, fixed timestamp 26.02.18 12:00:00 UTC+3
        static const uint8_t scts[] = {0x62,0x20,0x81,0x21,0x00,0x00,0x30};
        memcpy(tpdu + tpdu_len, scts, 7); tpdu_len += 7;

        // TP-UD
        uint8_t gsm7_buf[140]; uint8_t num_septets = 0;
        uint8_t gsm7_len = gsm7_pack(sm_text, gsm7_buf, &num_septets);
        tpdu[tpdu_len++] = num_septets;
        memcpy(tpdu + tpdu_len, gsm7_buf, gsm7_len);
        tpdu_len += gsm7_len;
    }
    uint8_t rpui_ie[180]; uint8_t rpui_len = (uint8_t)ber_tlv(rpui_ie, 0x83, tpdu, tpdu_len);

    // ── MAP Invoke argument: SEQUENCE { imsi, sm-RP-OA, sm-RP-UI }
    uint8_t seq_body[230]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie,  imsi_ie_len); sq_len += imsi_ie_len;
    memcpy(seq_body + sq_len, rpoa_ie,  rpoa_len);    sq_len += rpoa_len;
    memcpy(seq_body + sq_len, rpui_ie,  rpui_len);    sq_len += rpui_len;
    uint8_t arg_seq[240]; uint8_t arg_len = (uint8_t)ber_tlv(arg_seq, 0x30, seq_body, sq_len);

    // ── Application Context: shortMsgMT-RelayContext-v3
    uint8_t mt_ac_oid[] = {0x04, 0x00, 0x00, 0x01, 0x00, 0x1A, 0x03};
    static uint32_t mt_fsm_tid = 0x00000900;
    uint8_t pdu[512];
    uint8_t pdu_len = build_tcap_begin(pdu, mt_fsm_tid++, mt_ac_oid, sizeof(mt_ac_oid),
                                        0x01, 44 /*MT-ForwardSM*/, arg_seq, arg_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP MT-ForwardSM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:    " << COLOR_GREEN << imsi_str  << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SMSC:    " << COLOR_GREEN << smsc_str  << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Text:    " << COLOR_GREEN << sm_text   << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP MT-ForwardSM:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP processUnstructuredSS-Request (USSD) — 3GPP TS 29.002 §14.6.2
// Направление: MSC → HLR  или  VLR → HLR  (C-interface)
// TCAP Begin, Invoke, opCode=59 (0x3B)
// arg: USSD-Arg ::= SEQUENCE {
//        ussd-DataCodingScheme  [IMPLICIT OCTET STRING]  (1 byte)
//        ussd-String            [IMPLICIT OCTET STRING]  (packed GSM7)
//      }
// AC OID: networkUnstructuredSsContext-v2 = {0.4.0.0.1.0.19.2}
// BER OID: 04 00 00 01 00 13 02
// TID base: 0x0A00
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_ussd(const char *msisdn_str,
                                       const char *ussd_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP USSD");
    if (!msg) return nullptr;

    // ── USSD-DataCodingScheme: 0x0F = GSM7, no class
    uint8_t dcs_ie[4]; uint8_t dcs_len = (uint8_t)ber_tlv(dcs_ie, 0x04, (const uint8_t*)"\x0F", 1);

    // ── USSD-String: packed GSM7
    uint8_t gsm7_buf[140]; uint8_t num_septets = 0;
    uint8_t gsm7_len = gsm7_pack(ussd_str, gsm7_buf, &num_septets);
    uint8_t us_ie[150]; uint8_t us_len = (uint8_t)ber_tlv(us_ie, 0x04, gsm7_buf, gsm7_len);

    // ── SEQUENCE { DCS, String }
    uint8_t seq_body[160]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, dcs_ie, dcs_len); sq_len += dcs_len;
    memcpy(seq_body + sq_len, us_ie,  us_len);  sq_len += us_len;
    uint8_t arg_seq[170]; uint8_t arg_len = (uint8_t)ber_tlv(arg_seq, 0x30, seq_body, sq_len);

    // ── Application Context: networkUnstructuredSsContext-v2
    uint8_t ussd_ac_oid[] = {0x04, 0x00, 0x00, 0x01, 0x00, 0x13, 0x02};
    static uint32_t ussd_tid = 0x00000A00;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, ussd_tid++, ussd_ac_oid, sizeof(ussd_ac_oid),
                                        0x01, 59 /*processUnstructuredSS-Request*/, arg_seq, arg_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP processUnstructuredSS-Request (USSD)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MSISDN:  " << COLOR_GREEN << msisdn_str  << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  USSD:    " << COLOR_GREEN << ussd_str    << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Septets: " << COLOR_GREEN << (int)num_septets << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP USSD:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP SendRoutingInfo (SRI) — 3GPP TS 29.002 §8.4.2
// Направление: GMSC → HLR  (C-interface, MO/MT call)
// TCAP Begin, Invoke, opCode=22 (0x16)
// arg: SendRoutingInfoArg ::= SEQUENCE { msisdn, ... }
// AC OID: locationInfoRetrievalContext-v3 = {0.4.0.0.1.0.5.3}
// BER OID: 04 00 00 01 00 05 03
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_send_routing_info(const char *msisdn_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP SRI");
    if (!msg) return nullptr;

    // msisdn [0] IMPLICIT ISDNAddressString: TON=international + BCD
    size_t mslen = strlen(msisdn_str);
    uint8_t bcd_ms[10]; memset(bcd_ms, 0, sizeof(bcd_ms));
    bcd_ms[0] = 0x91;  // TON=international, NPI=E.164
    size_t bl = 1;
    for (size_t i = 0; i < mslen && bl < 10; i += 2) {
        uint8_t lo = (uint8_t)(msisdn_str[i] - '0');
        uint8_t hi = (i+1 < mslen) ? (uint8_t)(msisdn_str[i+1] - '0') : 0x0F;
        bcd_ms[bl++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t msisdn_ie[14]; uint8_t msisdn_ie_len = (uint8_t)ber_tlv(msisdn_ie, 0x80, bcd_ms, (uint8_t)bl);

    // interrogationType [3] ENUMERATED = 0 (basicCall)
    uint8_t itype_val = 0x00;
    uint8_t itype_ie[5]; uint8_t itype_ie_len = (uint8_t)ber_tlv(itype_ie, 0x83, &itype_val, 1);

    // SendRoutingInfoArg SEQUENCE
    uint8_t seq_body[32]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, msisdn_ie, msisdn_ie_len); sq_len += msisdn_ie_len;
    memcpy(seq_body + sq_len, itype_ie,  itype_ie_len);  sq_len += itype_ie_len;
    uint8_t seq_tlv[40]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    // AC OID: locationInfoRetrievalContext-v3
    uint8_t sri_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x05, 0x03 };
    static uint32_t sri_tid = 0x00000400;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, sri_tid++, sri_ac_oid, sizeof(sri_ac_oid),
                                        0x01, 0x16, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP SendRoutingInfo (SRI)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MSISDN:  " << COLOR_GREEN << msisdn_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "22 (0x16) SendRoutingInfo" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:     " << COLOR_GREEN << "0x" << std::hex << (sri_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP SRI:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP ProvideRoamingNumber (PRN) — 3GPP TS 29.002 §8.4.3
// Направление: GMSC → HLR  (C-interface, MT-call)
// TCAP Begin, Invoke, opCode=4 (0x04)
// arg: ProvideRoamingNumberArg ::= SEQUENCE { imsi, msc-Number }
// AC OID: roamingNumberEnquiryContext-v3 = {0.4.0.0.1.0.6.3}
// BER OID: 04 00 00 01 00 06 03
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_provide_roaming_number(const char *imsi_str,
                                                          const char *msc_msisdn) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP PRN");
    if (!msg) return nullptr;

    // IMSI BCD
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    }
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);

    // msc-Number [1] IMPLICIT ISDNAddressString
    size_t mslen = strlen(msc_msisdn);
    uint8_t bcd_msc[10]; memset(bcd_msc, 0, sizeof(bcd_msc));
    bcd_msc[0] = 0x91;
    size_t bl = 1;
    for (size_t i = 0; i < mslen && bl < 10; i += 2) {
        bcd_msc[bl++] = (uint8_t)((((i+1<mslen)?(uint8_t)(msc_msisdn[i+1]-'0'):0x0F)<<4)|((uint8_t)(msc_msisdn[i]-'0')));
    }
    uint8_t msc_ie[14]; uint8_t msc_ie_len = (uint8_t)ber_tlv(msc_ie, 0x81, bcd_msc, (uint8_t)bl);

    // ProvideRoamingNumberArg SEQUENCE
    uint8_t seq_body[40]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie, imsi_ie_len); sq_len += imsi_ie_len;
    memcpy(seq_body + sq_len, msc_ie,  msc_ie_len);  sq_len += msc_ie_len;
    uint8_t seq_tlv[48]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    uint8_t prn_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x06, 0x03 };
    static uint32_t prn_tid = 0x00000500;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, prn_tid++, prn_ac_oid, sizeof(prn_ac_oid),
                                        0x01, 0x04, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP ProvideRoamingNumber (PRN)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:       " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MSC-Number: " << COLOR_GREEN << msc_msisdn << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:     " << COLOR_GREEN << "4 (0x04) ProvideRoamingNumber" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:        " << COLOR_GREEN << "0x" << std::hex << (prn_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:     " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP PRN:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP CancelLocation (CL) — 3GPP TS 29.002 §8.1.4
// Направление: HLR → VLR  (C-interface)
// TCAP Begin, Invoke, opCode=3 (0x03)
// arg: CancelLocationArg ::= CHOICE { imsi | imsi-WithLMSI }
//   Используем простой вариант: IMSI (тег 0x04)
// AC OID: networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
// BER OID: 04 00 00 01 00 01 03 (тот же, что UpdateLocation)
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_cancel_location(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP CL");
    if (!msg) return nullptr;

    // IMSI BCD
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    }
    // CancelLocationArg = CHOICE { imsi [0] IMPLICIT IMSI }
    // IMSI IMPLICIT [0]: тег 0x84 (context, primitive, 4)
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x84, bcd_imsi, (uint8_t)bcd_len);

    uint8_t cl_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    static uint32_t cl_tid = 0x00000600;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, cl_tid++, cl_ac_oid, sizeof(cl_ac_oid),
                                        0x01, 0x03, imsi_ie, imsi_ie_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP CancelLocation (CL)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:    " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "3 (0x03) CancelLocation" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:     " << COLOR_GREEN << "0x" << std::hex << (cl_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP CancelLocation:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}


// MAP CancelLocation Result (3GPP TS 29.002 §8.1.3)
// VLR → HLR  —  ответ VLR на CancelLocation (TCAP ReturnResultLast)
// CancelLocation-Res ::= SEQUENCE {} (empty)
// opCode=3, AC: networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
static struct msgb *generate_map_cancel_location_res(uint32_t dtid) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP CL Res");
    if (!msg) return nullptr;

    uint8_t empty_seq[4];
    uint8_t empty_seq_len = (uint8_t)ber_tlv(empty_seq, 0x30, nullptr, 0);

    uint8_t comp[48];
    uint8_t comp_len = build_rrl_component(comp, 0x01, 0x03, empty_seq, empty_seq_len);

    uint8_t cl_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    uint8_t dial[128];
    uint8_t dial_len = build_aare_dialogue_portion(dial, cl_ac_oid, sizeof(cl_ac_oid));

    uint8_t dtid_val[4] = { (uint8_t)(dtid>>24),(uint8_t)(dtid>>16),(uint8_t)(dtid>>8),(uint8_t)(dtid) };
    uint8_t dtid_tlv[8]; uint8_t dtid_len = (uint8_t)ber_tlv(dtid_tlv, 0x49, dtid_val, 4);

    uint8_t end_body[230]; uint8_t eb_len = 0;
    memcpy(end_body + eb_len, dtid_tlv, dtid_len); eb_len += dtid_len;
    memcpy(end_body + eb_len, dial,     dial_len);  eb_len += dial_len;
    memcpy(end_body + eb_len, comp,     comp_len);  eb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x64;   // TCAP End
    *(msgb_put(msg, 1)) = eb_len;
    memcpy(msgb_put(msg, eb_len), end_body, eb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерирован MAP CancelLocation Result (ReturnResultLast)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DTID:   " << COLOR_GREEN << "0x" << std::hex << dtid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "3 (0x03) CancelLocation" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP CancelLocation Result:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}


// ──────────────────────────────────────────────────────────────
// MAP InsertSubscriberData (ISD) — 3GPP TS 29.002 §8.1.3
// Направление: HLR → VLR  (C-interface)
// TCAP Begin, Invoke, opCode=7 (0x07)
// arg: InsertSubscriberData-Arg ::= SEQUENCE { imsi, msisdn, ... }
//   Минимальный набор: imsi + msisdn + teleserviceList
// AC OID: subscriberDataMgmtContext-v3 = {0.4.0.0.1.0.14.3}
// BER OID: 04 00 00 01 00 0E 03
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_insert_subscriber_data(const char *imsi_str,
                                                          const char *msisdn_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ISD");
    if (!msg) return nullptr;

    // IMSI (тег 0x04)
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    }
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);

    // msisdn [1] IMPLICIT ISDNAddressString
    size_t mslen = strlen(msisdn_str);
    uint8_t bcd_ms[10]; memset(bcd_ms, 0, sizeof(bcd_ms));
    bcd_ms[0] = 0x91;
    size_t bl = 1;
    for (size_t i = 0; i < mslen && bl < 10; i += 2) {
        bcd_ms[bl++] = (uint8_t)((((i+1<mslen)?(uint8_t)(msisdn_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(msisdn_str[i]-'0')));
    }
    uint8_t msisdn_ie[14]; uint8_t msisdn_ie_len = (uint8_t)ber_tlv(msisdn_ie, 0x81, bcd_ms, (uint8_t)bl);

    // teleserviceList [6] IMPLICIT SEQUENCE OF TeleserviceCode
    // telephony (0x11) + SMS (0x22) — два базовых телесервиса
    uint8_t ts_vals[] = { 0x04, 0x01, 0x11, 0x04, 0x01, 0x22 };
    uint8_t ts_list[12]; uint8_t ts_list_len = (uint8_t)ber_tlv(ts_list, 0xA6, ts_vals, sizeof(ts_vals));

    // InsertSubscriberData-Arg SEQUENCE
    uint8_t seq_body[64]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie,   imsi_ie_len);   sq_len += imsi_ie_len;
    memcpy(seq_body + sq_len, msisdn_ie, msisdn_ie_len); sq_len += msisdn_ie_len;
    memcpy(seq_body + sq_len, ts_list,   ts_list_len);   sq_len += ts_list_len;
    uint8_t seq_tlv[72]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    uint8_t isd_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x03 };
    static uint32_t isd_tid = 0x00000700;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, isd_tid++, isd_ac_oid, sizeof(isd_ac_oid),
                                        0x01, 0x07, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP InsertSubscriberData (ISD)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:    " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MSISDN:  " << COLOR_GREEN << msisdn_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "7 (0x07) InsertSubscriberData" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:     " << COLOR_GREEN << "0x" << std::hex << (isd_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ISD:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}


// MAP InsertSubscriberData Result (3GPP TS 29.002 §7.6.2)
// VLR → HLR  —  ответ VLR на InsertSubscriberData (TCAP ReturnResultLast)
// Result: InsertSubscriberData-Res (SEQUENCE, may be empty)
// opCode=7, AC: subscriberDataMgmtContext-v3 = {0.4.0.0.1.0.14.3}
static struct msgb *generate_map_insert_subscriber_data_res(uint32_t dtid) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ISD Res");
    if (!msg) return nullptr;

    // Empty result SEQUENCE for ISD-Res
    uint8_t empty_seq[4];
    uint8_t empty_seq_len = (uint8_t)ber_tlv(empty_seq, 0x30, nullptr, 0);

    // Component: ReturnResultLast, opCode=7 (0x07)
    uint8_t comp[48];
    uint8_t comp_len = build_rrl_component(comp, 0x01, 0x07, empty_seq, empty_seq_len);

    // Dialogue: subscriberDataMgmtContext-v3
    uint8_t isd_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x03 };
    uint8_t dial[128];
    uint8_t dial_len = build_aare_dialogue_portion(dial, isd_ac_oid, sizeof(isd_ac_oid));

    uint8_t dtid_val[4] = { (uint8_t)(dtid>>24),(uint8_t)(dtid>>16),(uint8_t)(dtid>>8),(uint8_t)(dtid) };
    uint8_t dtid_tlv[8]; uint8_t dtid_len = (uint8_t)ber_tlv(dtid_tlv, 0x49, dtid_val, 4);

    uint8_t end_body[230]; uint8_t eb_len = 0;
    memcpy(end_body + eb_len, dtid_tlv, dtid_len); eb_len += dtid_len;
    memcpy(end_body + eb_len, dial,     dial_len);  eb_len += dial_len;
    memcpy(end_body + eb_len, comp,     comp_len);  eb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x64;   // TCAP End
    *(msgb_put(msg, 1)) = eb_len;
    memcpy(msgb_put(msg, eb_len), end_body, eb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерирован MAP ISD Result (ReturnResultLast)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DTID:   " << COLOR_GREEN << "0x" << std::hex << dtid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "7 (0x07) InsertSubscriberData" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ISD Result:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}


// MAP DeleteSubscriberData (3GPP TS 29.002 §8.1.3)
// HLR → VLR  —  HLR удаляет часть данных абонента (отзыв сервисов)
// opCode=8 (0x08), AC: subscriberDataMgmtContext-v3 = {0.4.0.0.1.0.14.3}
// Минимальный arg: IMSI + basicServiceList (содержит teleserviceCode)
static struct msgb *generate_map_delete_subscriber_data(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP DSD");
    if (!msg) return nullptr;

    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    }
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);

    // basicServiceList [1] IMPLICIT SEQUENCE — delete telephony (0x11)
    uint8_t ts_body[] = { 0x82, 0x01, 0x11 };  // [2] IMPLICIT TeleserviceCode = telephony
    uint8_t bs_list[12]; uint8_t bs_list_len = (uint8_t)ber_tlv(bs_list, 0xA1, ts_body, sizeof(ts_body));

    uint8_t seq_body[64]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie,  imsi_ie_len);  sq_len += imsi_ie_len;
    memcpy(seq_body + sq_len, bs_list,  bs_list_len);  sq_len += bs_list_len;
    uint8_t seq_tlv[72]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    uint8_t dsd_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x03 };
    static uint32_t dsd_tid = 0x00000D00;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, dsd_tid++, dsd_ac_oid, sizeof(dsd_ac_oid), 0x01, 0x08, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP DeleteSubscriberData (DSD)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "8 (0x08) DeleteSubscriberData" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:    " << COLOR_GREEN << "0x" << std::hex << (dsd_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP DSD:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}


// MAP DeleteSubscriberData Result (3GPP TS 29.002 §8.1.3)
// VLR → HLR  —  подтверждение удаления данных (TCAP ReturnResultLast)
// DeleteSubscriberData-Res ::= SEQUENCE {} (empty)
static struct msgb *generate_map_delete_subscriber_data_res(uint32_t dtid) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP DSD Res");
    if (!msg) return nullptr;

    uint8_t empty_seq[4];
    uint8_t empty_seq_len = (uint8_t)ber_tlv(empty_seq, 0x30, nullptr, 0);
    uint8_t comp[48];
    uint8_t comp_len = build_rrl_component(comp, 0x01, 0x08, empty_seq, empty_seq_len);

    uint8_t dsd_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x03 };
    uint8_t dial[128];
    uint8_t dial_len = build_aare_dialogue_portion(dial, dsd_ac_oid, sizeof(dsd_ac_oid));

    uint8_t dtid_val[4] = { (uint8_t)(dtid>>24),(uint8_t)(dtid>>16),(uint8_t)(dtid>>8),(uint8_t)(dtid) };
    uint8_t dtid_tlv[8]; uint8_t dtid_len = (uint8_t)ber_tlv(dtid_tlv, 0x49, dtid_val, 4);

    uint8_t end_body[230]; uint8_t eb_len = 0;
    memcpy(end_body + eb_len, dtid_tlv, dtid_len); eb_len += dtid_len;
    memcpy(end_body + eb_len, dial,     dial_len);  eb_len += dial_len;
    memcpy(end_body + eb_len, comp,     comp_len);  eb_len += comp_len;
    *(msgb_put(msg, 1)) = 0x64;
    *(msgb_put(msg, 1)) = eb_len;
    memcpy(msgb_put(msg, eb_len), end_body, eb_len);

    std::cout << COLOR_CYAN << "✓ Сгенерирован MAP DSD Result (ReturnResultLast)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DTID:   " << COLOR_GREEN << "0x" << std::hex << dtid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode: " << COLOR_GREEN << "8 (0x08) DeleteSubscriberData" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP DSD Result:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}




// ──────────────────────────────────────────────────────────────
// MAP PurgeMS — 3GPP TS 29.002 §17.7.1
// Направление: VLR/SGSN → HLR  (C-interface)
// TCAP Begin, Invoke, opCode=67 (0x43)
// PurgeMS-Arg ::= SEQUENCE { imsi [0] IMPLICIT IMSI, ... }
// AC OID: networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
// (тот же Application Context, что UpdateLocation и CancelLocation)
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_purge_ms(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP PurgeMS");
    if (!msg) return nullptr;

    // IMSI BCD
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    }
    // imsi [0] IMPLICIT IMSI → context-specific primitive tag 0 = 0x80
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x80, bcd_imsi, (uint8_t)bcd_len);

    // PurgeMS-Arg SEQUENCE (0x30)
    uint8_t seq_body[16]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie, imsi_ie_len); sq_len += imsi_ie_len;
    uint8_t seq_tlv[20]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    // networkLocUpContext-v3 = {0.4.0.0.1.0.1.3}
    uint8_t pm_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03 };
    static uint32_t pm_tid = 0x00000B00;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, pm_tid++, pm_ac_oid, sizeof(pm_ac_oid),
                                        0x01, 0x43, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP PurgeMS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:    " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "67 (0x43) PurgeMS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  AC:      " << COLOR_GREEN << "networkLocUpContext-v3" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:     " << COLOR_GREEN << "0x" << std::hex << (pm_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP PurgeMS:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP AuthenticationFailureReport — 3GPP TS 29.002 §17.3.4
// Направление: VLR → HLR  (C-interface)
// Отправляется после неудачной аутентификации MS.
// TCAP Begin, Invoke, opCode=15 (0x0F)
// AuthenticationFailureReport-Arg ::= SEQUENCE {
//   imsi         IMSI,           -- тег 0x04
//   failureCause FailureCause    -- ENUMERATED 0x0A
//     0=wrongUserResponse
//     1=wrongNetworkSignature
// }
// AC OID: authenticationFailureReportContext-v3 = {0.4.0.0.1.0.28.3}
// BER OID: 04 00 00 01 00 1C 03
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_auth_failure_report(const char *imsi_str,
                                                       uint8_t failure_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP AuthFailRpt");
    if (!msg) return nullptr;

    // IMSI BCD
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_imsi[9]; memset(bcd_imsi, 0, sizeof(bcd_imsi));
    bcd_imsi[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09);
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2) {
        bcd_imsi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    }
    // IMSI: OCTET STRING, tag 0x04
    uint8_t imsi_ie[12]; uint8_t imsi_ie_len = (uint8_t)ber_tlv(imsi_ie, 0x04, bcd_imsi, (uint8_t)bcd_len);

    // FailureCause ENUMERATED, tag 0x0A
    uint8_t fc_val = failure_cause & 0x01;
    uint8_t fc_ie[4]; uint8_t fc_ie_len = (uint8_t)ber_tlv(fc_ie, 0x0A, &fc_val, 1);

    // AuthenticationFailureReport-Arg SEQUENCE (0x30)
    uint8_t seq_body[24]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, imsi_ie, imsi_ie_len); sq_len += imsi_ie_len;
    memcpy(seq_body + sq_len, fc_ie,   fc_ie_len);   sq_len += fc_ie_len;
    uint8_t seq_tlv[32]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    // authenticationFailureReportContext-v3 = {0.4.0.0.1.0.28.3}
    uint8_t afr_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x1C, 0x03 };
    static uint32_t afr_tid = 0x00000C00;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, afr_tid++, afr_ac_oid, sizeof(afr_ac_oid),
                                        0x01, 0x0F, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    const char *fc_str = (fc_val == 0) ? "wrongUserResponse" : "wrongNetworkSignature";
    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP AuthenticationFailureReport" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:         " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  FailureCause: " << COLOR_GREEN << (int)fc_val
              << " (" << fc_str << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:       " << COLOR_GREEN << "15 (0x0F) AuthenticationFailureReport" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  AC:           " << COLOR_GREEN << "authenticationFailureReportContext-v3" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:          " << COLOR_GREEN << "0x" << std::hex << (afr_tid-1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:       " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP AuthFailureReport:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// TCAP Abort — ITU-T Q.773 §3.2.5  (P-Abort)
// Прерывает транзакцию на уровне TCAP без нормального завершения.
// Направление: MSC → HLR/EIR/MSC  или обратное.
// dtid        — Destination TID (= OTID партнёра).
// abort_cause — P-AbortCause ENUMERATED (tag 0x4A):
//   0=unrecognisedMessageType   1=unrecognisedTransactionID
//   2=badlyFormattedTxPortion   3=incorrectTxPortion
//   4=resourceLimitation
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_tcap_abort(uint32_t dtid, uint8_t abort_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "TCAP Abort");
    if (!msg) return nullptr;

    uint8_t dtid_val[4] = { (uint8_t)(dtid>>24), (uint8_t)(dtid>>16),
                             (uint8_t)(dtid>>8),  (uint8_t)(dtid) };
    uint8_t dtid_tlv[8]; uint8_t dtid_len = (uint8_t)ber_tlv(dtid_tlv, 0x49, dtid_val, 4);

    // P-AbortCause [APPLICATION 10] PRIMITIVE = 0x4A
    uint8_t cause_tlv[4]; uint8_t cause_len = (uint8_t)ber_tlv(cause_tlv, 0x4A, &abort_cause, 1);

    uint8_t body[16]; uint8_t b_len = 0;
    memcpy(body + b_len, dtid_tlv, dtid_len); b_len += dtid_len;
    memcpy(body + b_len, cause_tlv, cause_len); b_len += cause_len;

    *(msgb_put(msg, 1)) = 0x67;  // TCAP Abort
    *(msgb_put(msg, 1)) = b_len;
    memcpy(msgb_put(msg, b_len), body, b_len);

    static const char *cause_names[] = {
        "unrecognisedMessageType", "unrecognisedTransactionID",
        "badlyFormattedTxPortion", "incorrectTxPortion", "resourceLimitation"
    };
    const char *cause_str = (abort_cause < 5) ? cause_names[abort_cause] : "unknown";

    std::cout << COLOR_CYAN << "✓ Сгенерирован TCAP Abort (P-Abort)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DTID:    " << COLOR_GREEN << "0x" << std::hex << dtid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Причина: " << COLOR_GREEN << (int)abort_cause << " – " << cause_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex TCAP Abort:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP ReturnError — ITU-T Q.773 + 3GPP TS 29.002 §7.6
// TCAP End (0x64) с компонентом ReturnError (0xA3).
// Симулирует ответ партнёра с ошибкой MAP.
// dtid       — Destination TID (= OTID партнёра из Begin).
// invoke_id  — Invoke ID (соответствует Invoke из Begin партнёра).
// error_code — MAP Local ErrorCode (INTEGER):
//   1=systemFailure       3=dataMissing
//   5=unexpectedDataValue 6=unknownSubscriber
//   8=numberChanged       10=callBarred
//   13=roamingNotAllowed  21=facilityNotSupported
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_return_error(uint32_t dtid, uint8_t invoke_id, uint8_t error_code) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ReturnError");
    if (!msg) return nullptr;

    // DTID
    uint8_t dtid_val[4] = { (uint8_t)(dtid>>24), (uint8_t)(dtid>>16),
                             (uint8_t)(dtid>>8),  (uint8_t)(dtid) };
    uint8_t dtid_tlv[8]; uint8_t dtid_len = (uint8_t)ber_tlv(dtid_tlv, 0x49, dtid_val, 4);

    // InvokeID: INTEGER  02 01 <id>
    uint8_t inv_tlv[4]; uint8_t inv_len = (uint8_t)ber_tlv(inv_tlv, 0x02, &invoke_id, 1);
    // Error code: LOCAL INTEGER  02 01 <code>
    uint8_t err_tlv[4]; uint8_t err_len = (uint8_t)ber_tlv(err_tlv, 0x02, &error_code, 1);

    // ReturnError body: InvokeID + LocalErrorCode
    uint8_t re_body[16]; uint8_t re_len = 0;
    memcpy(re_body + re_len, inv_tlv, inv_len); re_len += inv_len;
    memcpy(re_body + re_len, err_tlv, err_len); re_len += err_len;

    // ReturnError component [3] CONTEXT CONSTRUCTED = 0xA3
    uint8_t re_tlv[24]; uint8_t re_tlv_len = (uint8_t)ber_tlv(re_tlv, 0xA3, re_body, re_len);

    // Component Portion: 6C <len> { ReturnError }
    uint8_t comp[32]; uint8_t comp_len = (uint8_t)ber_tlv(comp, 0x6C, re_tlv, re_tlv_len);

    // TCAP End body: DTID + Component Portion (без Dialogue Portion — ошибка)
    uint8_t end_body[64]; uint8_t eb_len = 0;
    memcpy(end_body + eb_len, dtid_tlv, dtid_len); eb_len += dtid_len;
    memcpy(end_body + eb_len, comp, comp_len);      eb_len += comp_len;

    *(msgb_put(msg, 1)) = 0x64;  // TCAP End
    *(msgb_put(msg, 1)) = eb_len;
    memcpy(msgb_put(msg, eb_len), end_body, eb_len);

    struct { uint8_t code; const char *name; } err_map[] = {
        {1,"systemFailure"}, {3,"dataMissing"}, {5,"unexpectedDataValue"},
        {6,"unknownSubscriber"}, {8,"numberChanged"}, {10,"callBarred"},
        {13,"roamingNotAllowed"}, {21,"facilityNotSupported"}, {0,nullptr}
    };
    const char *err_str = "unknown";
    for (int k = 0; err_map[k].name; ++k)
        if (err_map[k].code == error_code) { err_str = err_map[k].name; break; }

    std::cout << COLOR_CYAN << "✓ Сгенерирован MAP ReturnError (TCAP End)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  DTID:      " << COLOR_GREEN << "0x" << std::hex << dtid << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  InvokeID:  " << COLOR_GREEN << (int)invoke_id << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  ErrorCode: " << COLOR_GREEN << (int)error_code << " – " << err_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:    " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ReturnError:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) {
        printf("%02x ", msg->data[i]);
        if ((i + 1) % 16 == 0) std::cout << "\n    ";
    }
    std::cout << "\n\n";
    return msg;
}

// ============================================================
// P18: MAP Supplementary Services (3GPP TS 29.002 §14)
//   RegisterSS    opCode=10 (0x0A)  MSC/VLR → HLR  (C-interface)
//   EraseSS       opCode=11 (0x0B)  MSC/VLR → HLR
//   ActivateSS    opCode=12 (0x0C)  MSC/VLR → HLR
//   DeactivateSS  opCode=13 (0x0D)  MSC/VLR → HLR
//   InterrogateSS opCode=14 (0x0E)  MSC/VLR → HLR
// AC OID: supplementaryServiceContext-v3 = {0.4.0.0.1.0.50.3}
// BER OID bytes: 04 00 00 01 00 32 03
//
// Common SS-Codes (ITU Q.932 / 3GPP TS 22.030):
//   0x21=CFU  0x28=CFB  0x2A=CFNRy  0x2B=CFNRc  0x20=allForwardingSS
//   0x31=CLIP 0x33=CLIR 0x35=COLP   0x36=COLR
//   0x61=MPTY 0x66=HOLD 0x67=ECT    0x69=CCBS
//   0x91=BAOC 0xA1=BOIC 0xB1=BAIC   0xB2=BIC-Roam
// ============================================================

static const char *ss_name(uint8_t ss_code) {
    switch (ss_code) {
        case 0x10: return "allSS";
        case 0x20: return "allForwardingSS";
        case 0x21: return "CFU";
        case 0x28: return "CFB";
        case 0x2A: return "CFNRy";
        case 0x2B: return "CFNRc";
        case 0x31: return "CLIP";
        case 0x33: return "CLIR";
        case 0x35: return "COLP";
        case 0x36: return "COLR";
        case 0x61: return "MPTY";
        case 0x66: return "HOLD";
        case 0x67: return "ECT";
        case 0x69: return "CCBS";
        case 0x91: return "BAOC";
        case 0xA1: return "BOIC";
        case 0xB1: return "BAIC";
        case 0xB2: return "BIC-Roam";
        default:   return "(unknown)";
    }
}

// ──────────────────────────────────────────────────────────────
// MAP RegisterSS — 3GPP TS 29.002 §14.2.1,  opCode=10
// RegisterSS-Arg ::= SEQUENCE {
//   ss-Code              SS-Code,            -- 0x04 0x01 <code>
//   forwardedToNumber [5] IMPLICIT AddressString OPTIONAL  -- BCD, TON=0x91
// }
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_register_ss(uint8_t ss_code, const char *fwd_num) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP RegisterSS");
    if (!msg) return nullptr;

    uint8_t ssc_ie[4];  uint8_t ssc_len  = (uint8_t)ber_tlv(ssc_ie, 0x04, &ss_code, 1);
    uint8_t seq_body[48]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, ssc_ie, ssc_len); sq_len += ssc_len;

    if (fwd_num && strlen(fwd_num) > 0) {
        uint8_t bcd[12]; memset(bcd, 0xFF, sizeof(bcd));
        bcd[0] = 0x91;  // TON=international, NPI=ISDN
        size_t nlen = strlen(fwd_num); uint8_t bl = 1;
        for (size_t i = 0; i < nlen && bl < 11; i += 2) {
            bcd[bl]  = (uint8_t)(fwd_num[i] - '0') & 0x0F;
            if (i+1 < nlen) bcd[bl] |= (uint8_t)((fwd_num[i+1]-'0') << 4);
            else             bcd[bl] |= 0xF0;
            bl++;
        }
        uint8_t fwd_ie[16]; uint8_t fwd_len = (uint8_t)ber_tlv(fwd_ie, 0x85, bcd, bl); // [5] IMPLICIT
        memcpy(seq_body + sq_len, fwd_ie, fwd_len); sq_len += fwd_len;
    }

    uint8_t arg[64];  uint8_t arg_len  = (uint8_t)ber_tlv(arg, 0x30, seq_body, sq_len);
    uint8_t ss_ac_oid[] = {0x04, 0x00, 0x00, 0x01, 0x00, 0x32, 0x03};
    static uint32_t rss_tid = 0x00001100;
    uint8_t pdu[300]; uint8_t pdu_len = build_tcap_begin(pdu, rss_tid++, ss_ac_oid, sizeof(ss_ac_oid),
                                                          0x01, 10 /*RegisterSS*/, arg, arg_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e MAP RegisterSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SS-Code:  " << COLOR_GREEN << "0x" << std::hex << std::uppercase << (int)ss_code
              << std::dec << " (" << ss_name(ss_code) << ")" << COLOR_RESET << "\n";
    if (fwd_num && strlen(fwd_num) > 0)
        std::cout << COLOR_BLUE << "  FwdTo:    " << COLOR_GREEN << fwd_num << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:   " << COLOR_GREEN << "10 (0x0A) RegisterSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440:   " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP RegisterSS:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP EraseSS — opCode=11  /  ActivateSS — opCode=12
// MAP DeactivateSS — opCode=13  /  InterrogateSS — opCode=14
// SS-ForBS-Code ::= SEQUENCE { ss-Code SS-Code }
// ──────────────────────────────────────────────────────────────
static struct msgb *build_map_ss_simple(uint8_t op_code, uint8_t ss_code,
                                         uint32_t &tid_ref, const char *label) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, label);
    if (!msg) return nullptr;
    uint8_t ssc_ie[4]; uint8_t ssc_len = (uint8_t)ber_tlv(ssc_ie, 0x04, &ss_code, 1);
    uint8_t arg[16];   uint8_t arg_len = (uint8_t)ber_tlv(arg, 0x30, ssc_ie, ssc_len);
    uint8_t ss_ac_oid[] = {0x04, 0x00, 0x00, 0x01, 0x00, 0x32, 0x03};
    uint8_t pdu[300];  uint8_t pdu_len = build_tcap_begin(pdu, tid_ref++, ss_ac_oid, sizeof(ss_ac_oid),
                                                           0x01, op_code, arg, arg_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);
    return msg;
}

static struct msgb *generate_map_erase_ss(uint8_t ss_code) {
    static uint32_t ess_tid = 0x00001200;
    struct msgb *msg = build_map_ss_simple(11, ss_code, ess_tid, "MAP EraseSS");
    if (!msg) return nullptr;
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e MAP EraseSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SS-Code:  " << COLOR_GREEN << "0x" << std::hex << std::uppercase
              << (int)ss_code << std::dec << " (" << ss_name(ss_code) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:   " << COLOR_GREEN << "11 (0x0B) EraseSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440:   " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP EraseSS:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

static struct msgb *generate_map_activate_ss(uint8_t ss_code) {
    static uint32_t ass_tid = 0x00001300;
    struct msgb *msg = build_map_ss_simple(12, ss_code, ass_tid, "MAP ActivateSS");
    if (!msg) return nullptr;
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e MAP ActivateSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SS-Code:  " << COLOR_GREEN << "0x" << std::hex << std::uppercase
              << (int)ss_code << std::dec << " (" << ss_name(ss_code) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:   " << COLOR_GREEN << "12 (0x0C) ActivateSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440:   " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ActivateSS:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

static struct msgb *generate_map_deactivate_ss(uint8_t ss_code) {
    static uint32_t dss_tid = 0x00001400;
    struct msgb *msg = build_map_ss_simple(13, ss_code, dss_tid, "MAP DeactivateSS");
    if (!msg) return nullptr;
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e MAP DeactivateSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SS-Code:  " << COLOR_GREEN << "0x" << std::hex << std::uppercase
              << (int)ss_code << std::dec << " (" << ss_name(ss_code) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:   " << COLOR_GREEN << "13 (0x0D) DeactivateSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440:   " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP DeactivateSS:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

static struct msgb *generate_map_interrogate_ss(uint8_t ss_code) {
    static uint32_t iss_tid = 0x00001500;
    struct msgb *msg = build_map_ss_simple(14, ss_code, iss_tid, "MAP InterrogateSS");
    if (!msg) return nullptr;
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e MAP InterrogateSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SS-Code:  " << COLOR_GREEN << "0x" << std::hex << std::uppercase
              << (int)ss_code << std::dec << " (" << ss_name(ss_code) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:   " << COLOR_GREEN << "14 (0x0E) InterrogateSS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440:   " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP InterrogateSS:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ============================================================
// P20: MAP Password + AnyTimeInterrogation (3GPP TS 29.002)
//   RegisterPassword  opCode=17  MSC/VLR → HLR  (C-interface)
//   GetPassword       opCode=18  MSC/VLR → HLR
//   ATI               opCode=71  MSC/VLR → HLR  (anyTimeInfoEnquiryContext-v3)
// ============================================================

// ──────────────────────────────────────────────────────────────
// MAP RegisterPassword — 3GPP TS 29.002 §14.7.1,  opCode=17
// RegisterPassword-Arg ::= SS-Code  (just the OCTET STRING, no SEQUENCE wrapper)
// AC: supplementaryServiceContext-v3 {0.4.0.0.1.0.50.3}
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_register_password(uint8_t ss_code) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP RegisterPassword");
    if (!msg) return nullptr;

    uint8_t arg[4]; uint8_t arg_len = (uint8_t)ber_tlv(arg, 0x04, &ss_code, 1);
    static const uint8_t ss_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x32, 0x03 };
    static uint32_t rpw_tid = 0x00001600;
    uint8_t pdu[200]; uint8_t pdu_len = build_tcap_begin(pdu, rpw_tid++, ss_ac_oid, sizeof(ss_ac_oid),
                                                          0x01, 17 /*RegisterPassword*/, arg, arg_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP RegisterPassword" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SS-Code:  " << COLOR_GREEN << "0x" << std::hex << std::uppercase
              << (int)ss_code << std::dec << " (" << ss_name(ss_code) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:   " << COLOR_GREEN << "17 (0x11) RegisterPassword" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:   " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP RegisterPassword:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP GetPassword — 3GPP TS 29.002 §14.7.2,  opCode=18
// GetPassword-Arg ::= GuidanceInfo ::= ENUMERATED
//   0=enterPW  1=enterNewPW  2=enterNewPW-Again
// Encoded as: 0x0A 0x01 guidance_value
// AC: supplementaryServiceContext-v3
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_get_password(uint8_t ss_code, uint8_t guidance) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP GetPassword");
    if (!msg) return nullptr;

    // Arg = SS-Code OCTET-STRING + GuidanceInfo ENUMERATED
    uint8_t ssc_ie[4]; uint8_t ssc_len = (uint8_t)ber_tlv(ssc_ie, 0x04, &ss_code, 1);
    uint8_t gui_ie[4]; uint8_t gui_len = (uint8_t)ber_tlv(gui_ie, 0x0A, &guidance, 1);
    uint8_t arg_body[12]; uint8_t ab_len = 0;
    memcpy(arg_body, ssc_ie, ssc_len); ab_len += ssc_len;
    memcpy(arg_body + ab_len, gui_ie, gui_len); ab_len += gui_len;
    uint8_t arg[16]; uint8_t arg_len = (uint8_t)ber_tlv(arg, 0x30, arg_body, ab_len);
    static const uint8_t ss_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x32, 0x03 };
    static uint32_t gpw_tid = 0x00001700;
    uint8_t pdu[200]; uint8_t pdu_len = build_tcap_begin(pdu, gpw_tid++, ss_ac_oid, sizeof(ss_ac_oid),
                                                          0x01, 18 /*GetPassword*/, arg, arg_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    static const char *guidance_str[] = { "enterPW", "enterNewPW", "enterNewPW-Again", "?" };
    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP GetPassword" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SS-Code:  " << COLOR_GREEN << "0x" << std::hex << std::uppercase
              << (int)ss_code << std::dec << " (" << ss_name(ss_code) << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Guidance: " << COLOR_GREEN << (int)guidance
              << " (" << guidance_str[guidance < 3 ? guidance : 3] << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:   " << COLOR_GREEN << "18 (0x12) GetPassword" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:   " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP GetPassword:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP AnyTimeInterrogation (ATI) — 3GPP TS 29.002 §7.3.3, opCode=71 (0x47)
// Направление: MSC/VLR → HLR  (C-interface)
// AC: anyTimeInfoEnquiryContext-v3 = {0.4.0.0.1.0.29.3}
//   BER OID: 04 00 00 01 00 1D 03
//
// ATI-RequestArg ::= SEQUENCE {
//   subscriberIdentity  SubscriberIdentity   -- CHOICE: [1] IMPLICIT msisdn
//   requestedInfo       RequestedInfo        -- SEQUENCE { [0] NULL } = location
// }
// MSISDN [1] IMPLICIT ISDN-AddressString: tag=0x81 + TON(0x91) + BCD
// RequestedInfo: 0x30 0x02 0x80 0x00
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_ati(const char *msisdn_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ATI");
    if (!msg) return nullptr;

    // subscriberIdentity: [1] IMPLICIT ISDN-AddressString (msisdn)
    uint8_t bcd[12]; uint8_t bcd_len = 1;
    bcd[0] = 0x91;  // TON=international, NPI=E.164
    size_t nlen = strlen(msisdn_str);
    for (size_t i = 0; i < nlen && bcd_len < 11; i += 2) {
        uint8_t lo = (uint8_t)(msisdn_str[i] - '0');
        uint8_t hi = (i + 1 < nlen) ? (uint8_t)(msisdn_str[i+1] - '0') : 0x0F;
        bcd[bcd_len++] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t sub_ie[14]; uint8_t sub_len = (uint8_t)ber_tlv(sub_ie, 0x81, bcd, bcd_len);

    // requestedInfo: SEQUENCE { locationInformation [0] NULL }
    static const uint8_t req_info[] = { 0x30, 0x02, 0x80, 0x00 };

    uint8_t seq_body[32]; uint8_t sq_len = 0;
    memcpy(seq_body, sub_ie, sub_len); sq_len += sub_len;
    memcpy(seq_body + sq_len, req_info, sizeof(req_info)); sq_len += (uint8_t)sizeof(req_info);
    uint8_t arg[40]; uint8_t arg_len = (uint8_t)ber_tlv(arg, 0x30, seq_body, sq_len);

    static const uint8_t ati_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x1D, 0x03 };
    static uint32_t ati_tid = 0x00001800;
    uint8_t pdu[300]; uint8_t pdu_len = build_tcap_begin(pdu, ati_tid++, ati_ac_oid, sizeof(ati_ac_oid),
                                                          0x01, 71 /*ATI opCode=0x47*/, arg, arg_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP AnyTimeInterrogation (ATI)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MSISDN:  " << COLOR_GREEN << msisdn_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  ReqInfo: " << COLOR_GREEN << "locationInformation" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "71 (0x47) anyTimeInterrogation" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  AC OID:  " << COLOR_GREEN << "anyTimeInfoEnquiryContext-v3 {0.4.0.0.1.0.29.3}" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ATI:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i+1)%16==0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ============================================================
// P15: MAP SMS Gateway messages (3GPP TS 29.002 §10.5.6)
//   SRI-SM          opCode=45 (0x2D)  GMSC/MSC → HLR
//   ReportSMDeliveryStatus opCode=47 (0x2F)  SMSC → HLR
//   AC OID: shortMsgGatewayContext-v3 = {0.4.0.0.1.0.20.3}
// ============================================================

// ──────────────────────────────────────────────────────────────
// MAP SendRoutingInfoForSM (SRI-SM) — 3GPP TS 29.002 §10.5.6.5
// Направление: GMSC → HLR  (C-interface)
// TCAP Begin, Invoke, opCode=45 (0x2D)
// RoutingInfoForSM-Arg ::= SEQUENCE {
//   msisdn              [0] IMPLICIT ISDN-AddressString,  — вызываемый абонент
//   sm-RP-PRI           [1] IMPLICIT BOOLEAN,             — FALSE=не срочный
//   serviceCentreAddress[2] IMPLICIT AddressString        — адрес SMSC
// }
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_sri_sm(const char *msisdn_str,
                                        const char *smsc_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP SRI-SM");
    if (!msg) return nullptr;

    // ── msisdn [0] IMPLICIT ISDNAddressString
    uint8_t msisdn_bcd[12]; uint8_t msisdn_bcd_len = 0;
    msisdn_bcd[msisdn_bcd_len++] = 0x91;  // TON=international, NPI=E.164
    {
        size_t dlen = strlen(msisdn_str);
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t lo = (uint8_t)(msisdn_str[i] - '0');
            uint8_t hi = (i + 1 < dlen) ? (uint8_t)(msisdn_str[i + 1] - '0') : 0x0F;
            msisdn_bcd[msisdn_bcd_len++] = (uint8_t)((hi << 4) | lo);
        }
    }
    uint8_t msisdn_ie[16]; uint8_t msisdn_ie_len = (uint8_t)ber_tlv(msisdn_ie, 0x80, msisdn_bcd, msisdn_bcd_len);

    // ── sm-RP-PRI [1] IMPLICIT BOOLEAN = FALSE (0x00)
    uint8_t pri_val = 0x00;
    uint8_t pri_ie[5]; uint8_t pri_ie_len = (uint8_t)ber_tlv(pri_ie, 0x81, &pri_val, 1);

    // ── serviceCentreAddress [2] IMPLICIT AddressString (SMSC E.164)
    uint8_t smsc_bcd[12]; uint8_t smsc_bcd_len = 0;
    smsc_bcd[smsc_bcd_len++] = 0x91;  // TON=international, NPI=E.164
    {
        size_t dlen = strlen(smsc_str);
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t lo = (uint8_t)(smsc_str[i] - '0');
            uint8_t hi = (i + 1 < dlen) ? (uint8_t)(smsc_str[i + 1] - '0') : 0x0F;
            smsc_bcd[smsc_bcd_len++] = (uint8_t)((hi << 4) | lo);
        }
    }
    uint8_t smsc_ie[16]; uint8_t smsc_ie_len = (uint8_t)ber_tlv(smsc_ie, 0x82, smsc_bcd, smsc_bcd_len);

    // ── RoutingInfoForSM-Arg SEQUENCE
    uint8_t seq_body[48]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, msisdn_ie, msisdn_ie_len); sq_len += msisdn_ie_len;
    memcpy(seq_body + sq_len, pri_ie,    pri_ie_len);    sq_len += pri_ie_len;
    memcpy(seq_body + sq_len, smsc_ie,   smsc_ie_len);   sq_len += smsc_ie_len;
    uint8_t seq_tlv[56]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    // shortMsgGatewayContext-v3 OID: {0.4.0.0.1.0.20.3}
    static const uint8_t sms_gw_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x14, 0x03 };
    static uint32_t sri_sm_tid = 0x00000F00;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, sri_sm_tid++, sms_gw_ac_oid, sizeof(sms_gw_ac_oid),
                                       0x01, 0x2D /* opCode=45 SRI-SM */, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP SendRoutingInfoForSM (SRI-SM)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MSISDN:  " << COLOR_GREEN << msisdn_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SMSC:    " << COLOR_GREEN << smsc_str   << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:  " << COLOR_GREEN << "45 (0x2D) SendRoutingInfoForSM" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:     " << COLOR_GREEN << "0x" << std::hex << (sri_sm_tid - 1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  AC OID:  " << COLOR_GREEN << "shortMsgGatewayContext-v3 {0.4.0.0.1.0.20.3}" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP SRI-SM:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i + 1) % 16 == 0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ──────────────────────────────────────────────────────────────
// MAP ReportSMDeliveryStatus — 3GPP TS 29.002 §10.5.6.7
// Направление: SMSC → HLR  (C-interface, после получения MT-FSM ответа)
// TCAP Begin, Invoke, opCode=47 (0x2F)
// ReportSMDeliveryStatusArg ::= SEQUENCE {
//   msisdn              [0] IMPLICIT ISDN-AddressString,
//   serviceCentreAddress       AddressString  (tag=0x04),
//   sm-DeliveryOutcome  [3] IMPLICIT SMDeliveryOutcome ENUMERATED {
//     memCapacityExceeded(0), absentSubscriber(1), successfulTransfer(2) }
// }
// ──────────────────────────────────────────────────────────────
static struct msgb *generate_map_report_sm_delivery_status(const char *msisdn_str,
                                                            const char *smsc_str,
                                                            uint8_t delivery_outcome) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "MAP ReportSMDS");
    if (!msg) return nullptr;

    // ── msisdn [0] IMPLICIT ISDNAddressString
    uint8_t msisdn_bcd[12]; uint8_t msisdn_bcd_len = 0;
    msisdn_bcd[msisdn_bcd_len++] = 0x91;
    {
        size_t dlen = strlen(msisdn_str);
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t lo = (uint8_t)(msisdn_str[i] - '0');
            uint8_t hi = (i + 1 < dlen) ? (uint8_t)(msisdn_str[i + 1] - '0') : 0x0F;
            msisdn_bcd[msisdn_bcd_len++] = (uint8_t)((hi << 4) | lo);
        }
    }
    uint8_t msisdn_ie[16]; uint8_t msisdn_ie_len = (uint8_t)ber_tlv(msisdn_ie, 0x80, msisdn_bcd, msisdn_bcd_len);

    // ── serviceCentreAddress AddressString (tag=0x04 — universal OCTET STRING)
    uint8_t smsc_bcd[12]; uint8_t smsc_bcd_len = 0;
    smsc_bcd[smsc_bcd_len++] = 0x91;
    {
        size_t dlen = strlen(smsc_str);
        for (size_t i = 0; i < dlen; i += 2) {
            uint8_t lo = (uint8_t)(smsc_str[i] - '0');
            uint8_t hi = (i + 1 < dlen) ? (uint8_t)(smsc_str[i + 1] - '0') : 0x0F;
            smsc_bcd[smsc_bcd_len++] = (uint8_t)((hi << 4) | lo);
        }
    }
    uint8_t smsc_ie[16]; uint8_t smsc_ie_len = (uint8_t)ber_tlv(smsc_ie, 0x04, smsc_bcd, smsc_bcd_len);

    // ── sm-DeliveryOutcome [3] IMPLICIT ENUMERATED
    uint8_t out_ie[5]; uint8_t out_ie_len = (uint8_t)ber_tlv(out_ie, 0x83, &delivery_outcome, 1);

    // ── ReportSMDeliveryStatusArg SEQUENCE
    uint8_t seq_body[48]; uint8_t sq_len = 0;
    memcpy(seq_body + sq_len, msisdn_ie, msisdn_ie_len); sq_len += msisdn_ie_len;
    memcpy(seq_body + sq_len, smsc_ie,   smsc_ie_len);   sq_len += smsc_ie_len;
    memcpy(seq_body + sq_len, out_ie,    out_ie_len);    sq_len += out_ie_len;
    uint8_t seq_tlv[56]; uint8_t seq_len = (uint8_t)ber_tlv(seq_tlv, 0x30, seq_body, sq_len);

    static const uint8_t sms_gw_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x14, 0x03 };
    static uint32_t rsds_tid = 0x00001000;
    uint8_t pdu[300];
    uint8_t pdu_len = build_tcap_begin(pdu, rsds_tid++, sms_gw_ac_oid, sizeof(sms_gw_ac_oid),
                                       0x01, 0x2F /* opCode=47 ReportSMDS */, seq_tlv, seq_len);
    memcpy(msgb_put(msg, pdu_len), pdu, pdu_len);

    const char *outcome_str = (delivery_outcome == 0) ? "memCapacityExceeded"
                            : (delivery_outcome == 1) ? "absentSubscriber"
                            :                           "successfulTransfer";
    std::cout << COLOR_CYAN << "✓ Сгенерировано MAP ReportSMDeliveryStatus" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MSISDN:   " << COLOR_GREEN << msisdn_str   << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SMSC:     " << COLOR_GREEN << smsc_str     << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Outcome:  " << COLOR_GREEN << (int)delivery_outcome
              << " – " << outcome_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  OpCode:   " << COLOR_GREEN << "47 (0x2F) ReportSMDeliveryStatus" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TID:      " << COLOR_GREEN << "0x" << std::hex << (rsds_tid - 1) << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  AC OID:   " << COLOR_GREEN << "shortMsgGatewayContext-v3 {0.4.0.0.1.0.20.3}" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:   " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    std::cout << COLOR_YELLOW << "Raw hex MAP ReportSMDeliveryStatus:" << COLOR_RESET << "\n    ";
    for (int i = 0; i < msg->len; ++i) { printf("%02x ", msg->data[i]); if ((i + 1) % 16 == 0) std::cout << "\n    "; }
    std::cout << "\n\n";
    return msg;
}

// ============================================================

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

// ============================================================
// P6: A-interface DTAP/BSSMAP — auth, cipher, LU accept/reject
// ============================================================

// DTAP Authentication Request (3GPP TS 24.008 §9.2.2)
// MSC → MS via BSC   DTAP MM 0x12
// rand_hex: 32 hex-символа (16 байт RAND), cksn: 0-6
static struct msgb *generate_dtap_auth_request(const char *rand_hex, uint8_t cksn) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP Auth Req");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;   // 0x05
    *(msgb_put(msg, 1)) = 0x12;              // Authentication Request
    *(msgb_put(msg, 1)) = cksn & 0x07;      // CKSN (Ciphering Key Sequence Number)
    uint8_t rand_bytes[16];
    size_t hlen = strlen(rand_hex);
    for (int i = 0; i < 16; i++) {
        if ((size_t)(i * 2 + 1) < hlen) {
            char h[3] = { rand_hex[i*2], rand_hex[i*2+1], 0 };
            rand_bytes[i] = (uint8_t)strtoul(h, nullptr, 16);
        } else {
            rand_bytes[i] = (uint8_t)(0xA0 + i);  // дефолтный RAND
        }
    }
    memcpy(msgb_put(msg, 16), rand_bytes, 16);
    std::cout << COLOR_CYAN << "✓ DTAP Authentication Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CKSN: " << COLOR_GREEN << (int)cksn << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  RAND: " << COLOR_GREEN;
    for (int i = 0; i < 16; i++) printf("%02X", rand_bytes[i]);
    std::cout << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP Authentication Response (3GPP TS 24.008 §9.2.4)
// MS → MSC via BSC   DTAP MM 0x14
// sres_hex: 8 hex-символов (4 байт SRES)
static struct msgb *generate_dtap_auth_response(const char *sres_hex) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP Auth Resp");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = 0x14;  // Authentication Response
    uint8_t sres[4];
    size_t hlen = strlen(sres_hex);
    for (int i = 0; i < 4; i++) {
        if ((size_t)(i * 2 + 1) < hlen) {
            char h[3] = { sres_hex[i*2], sres_hex[i*2+1], 0 };
            sres[i] = (uint8_t)strtoul(h, nullptr, 16);
        } else {
            sres[i] = (uint8_t)(0x01 + i);
        }
    }
    memcpy(msgb_put(msg, 4), sres, 4);
    std::cout << COLOR_CYAN << "✓ DTAP Authentication Response" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SRES: " << COLOR_GREEN;
    for (int i = 0; i < 4; i++) printf("%02X", sres[i]);
    std::cout << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP Identity Request (3GPP TS 24.008 §9.2.10)
// MSC → MS via BSC   DTAP MM 0x18
// id_type: 1=IMSI 2=IMEI 3=IMEISV 4=TMSI
static struct msgb *generate_dtap_id_request(uint8_t id_type) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP ID Req");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = 0x18;             // Identity Request
    *(msgb_put(msg, 1)) = id_type & 0x07;   // type of identity (3 bits)
    const char *type_names[] = {"?","IMSI","IMEI","IMEISV","TMSI","?","?","?"};
    std::cout << COLOR_CYAN << "✓ DTAP Identity Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Type: " << COLOR_GREEN << (int)id_type
              << " (" << type_names[id_type & 7] << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP MM Authentication Reject (3GPP TS 24.008 §9.2.1)
// MSC → MS   MM 0x11  — отклонение аутентификации (AUTH_REJ)
// Нет IE — сообщение состоит только из заголовка PD+MT
static struct msgb *generate_dtap_mm_auth_reject(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP MM AuthRej");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_MM | 0x80);
    *(msgb_put(msg, 1)) = GSM48_MT_MM_AUTH_REJ;   // 0x11
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP MM Authentication Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Direction: " << COLOR_GREEN << "MSC → MS" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:    " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP MM Abort (3GPP TS 24.008 §9.2.2)
// MS ↔ MSC   MM 0x29  — аварийное завершение MM-транзакции
// Reject Cause IE обязателен
static struct msgb *generate_dtap_mm_abort(uint8_t rej_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP MM Abort");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_MM | 0x80);
    *(msgb_put(msg, 1)) = GSM48_MT_MM_ABORT;    // 0x29
    // Reject Cause IE: tag=0x16, len=1, cause_value
    *(msgb_put(msg, 1)) = 0x16; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = rej_cause;
    const char *cs = (rej_cause==0x06)?"Illegal ME":
                     (rej_cause==0x0B)?"PLMN not allowed":
                     (rej_cause==0x0C)?"LA not allowed":"Other";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP MM Abort" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN << (int)rej_cause << " (" << cs << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP MM Status (3GPP TS 24.008 §9.2.22)
// MS ↔ MSC   MM 0x31  — сообщение о состоянии при ошибке в MM-протоколе
// Reject Cause IE обязателен
static struct msgb *generate_dtap_mm_status(uint8_t rej_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP MM Status");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_MM);
    *(msgb_put(msg, 1)) = GSM48_MT_MM_STATUS;   // 0x31
    // Reject Cause IE: tag=0x16, len=1
    *(msgb_put(msg, 1)) = 0x16; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = rej_cause;
    const char *cs = (rej_cause==0x60)?"Invalid mandatory information":
                     (rej_cause==0x61)?"Message type non-existent":
                     (rej_cause==0x62)?"Message not compatible with protocol state":"Other";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP MM Status" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << (int)rej_cause << " (" << cs << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP MM CM Re-establishment Request (3GPP TS 24.008 §9.2.6)
// MS → MSC   MM 0x28  — запрос восстановления соединения после потери радиосвязи
// MS Classmark 2 и Mobile Identity обязательны
static struct msgb *generate_dtap_mm_cm_reest_req(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP MM CM-Reest");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_MM);
    *(msgb_put(msg, 1)) = GSM48_MT_MM_CM_REEST_REQ;  // 0x28
    // CM Service Type / Ciphering Key Sequence: 0x00 = no key, re-establishment
    *(msgb_put(msg, 1)) = 0x08;  // TV: Ciphering key seq=no key (0x07), CM type bits ignored
    // MS Classmark 2 IE: tag=0x11, len=3
    *(msgb_put(msg, 1)) = 0x11; *(msgb_put(msg, 1)) = 0x03;
    *(msgb_put(msg, 1)) = 0x59; *(msgb_put(msg, 1)) = 0x59; *(msgb_put(msg, 1)) = 0x19;
    // Mobile Identity IE (IMSI): tag=0x17
    size_t imsi_slen = strlen(imsi_str);
    uint8_t bcd_mi[9]; memset(bcd_mi, 0, sizeof(bcd_mi));
    bcd_mi[0] = (uint8_t)(((imsi_str[0]-'0') & 0x0F) | 0x09);  // odd parity + type=IMSI
    size_t bcd_len = 1;
    for (size_t i = 1; i < imsi_slen && bcd_len < 9; i += 2)
        bcd_mi[bcd_len++] = (uint8_t)((((i+1<imsi_slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    *(msgb_put(msg, 1)) = 0x17; *(msgb_put(msg, 1)) = (uint8_t)bcd_len;
    memcpy(msgb_put(msg, bcd_len), bcd_mi, bcd_len);
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP MM CM Re-establishment Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP MM Authentication Failure (3GPP TS 24.008 §9.2.3a)
// MS → MSC  MM 0x1C  — отказ аутентификации (например, UMTS-AKA AUTS)
// Reject Cause обязателен; AUTS опционален для Synch Failure
static struct msgb *generate_dtap_mm_auth_failure(uint8_t rej_cause, bool with_auts) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP MM AuthFail");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = GSM48_MT_MM_AUTH_FAIL;    // 0x1C
    // Reject Cause: TLV tag=0x16, len=1
    *(msgb_put(msg, 1)) = 0x16; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = rej_cause;
    if (with_auts) {
        // Authentication Failure Parameter (AUTS): tag=0x22, len=14
        *(msgb_put(msg, 1)) = 0x22; *(msgb_put(msg, 1)) = 0x0E;
        uint8_t *auts = msgb_put(msg, 14);
        memset(auts, 0xAB, 14);  // dummy AUTS
    }
    const char *cs = (rej_cause==0x15)?"MAC failure":
                     (rej_cause==0x16)?"Synch failure":
                     (rej_cause==0x20)?"Security mode rejected, unspecified":"Other";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP MM Authentication Failure" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << (int)rej_cause << " (" << cs << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  AUTS:  " << COLOR_GREEN << (with_auts ? "включён" : "нет") << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP MM CM Service Abort (3GPP TS 24.008 §9.2.5)
// MS → MSC  MM 0x23  — прерывание запроса CM Service по инициативе MS
// Нет обязательных IE
static struct msgb *generate_dtap_mm_cm_service_abort(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP MM CmSrvAbort");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = GSM48_MT_MM_CM_SERV_ABORT; // 0x23
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP MM CM Service Abort" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP MM CM Service Prompt (3GPP TS 24.008 §9.2.5a)
// MSC → MS  MM 0x25  — запрос к MS начать CM Service (NW-initiated)
// PD Group (CC/SS) обязателен
static struct msgb *generate_dtap_mm_cm_service_prompt(uint8_t pd_and_sapi) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP MM CmSrvPrompt");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM | 0x80;   // NW→MS (bit8=1)
    *(msgb_put(msg, 1)) = GSM48_MT_MM_CM_SERV_PROMPT; // 0x25
    // PD&SAPI: half-octet TLV: tag=0x40 | (sapi&3)<<0 | pd nibble
    *(msgb_put(msg, 1)) = (uint8_t)(0x40 | (pd_and_sapi & 0x0F));
    const char *pd = (pd_and_sapi & 0x03) == 0 ? "CC (SAPI=0)" :
                     (pd_and_sapi & 0x03) == 3 ? "SMS (SAPI=3)" : "Other";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP MM CM Service Prompt" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  PD/SAPI: " << COLOR_GREEN << pd << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP Identity Response (3GPP TS 24.008 §9.2.11)
// MS → MSC via BSC   DTAP MM 0x19
static struct msgb *generate_dtap_id_response(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP ID Resp");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = 0x19;  // Identity Response
    uint8_t bcd[9];
    int imsi_len = generate_bcd_number(bcd + 1, sizeof(bcd) - 1, imsi_str);
    if (imsi_len <= 0) { msgb_free(msg); return nullptr; }
    uint8_t mi_len = imsi_len + 1;
    uint8_t *mi = msgb_put(msg, mi_len + 1);
    mi[0] = mi_len;
    mi[1] = (0x01 << 4) | 0x01;  // IMSI + odd length indicator
    memcpy(mi + 2, bcd + 1, imsi_len);
    std::cout << COLOR_CYAN << "✓ DTAP Identity Response" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Ciphering Mode Command (3GPP TS 48.008 §3.2.1.30)
// MSC → BSC   BSSMAP 0x35
// alg_mask: 0x01=no enc  0x02=A5/1  0x04=A5/2  0x08=A5/3
// kc: 8 байт ключа шифрования (Kc)
static struct msgb *generate_bssmap_cipher_mode_cmd(uint8_t alg_mask, const uint8_t *kc) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP Cipher Mode");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x35;   // Ciphering Mode Command
    // Encryption Information IE: tag=0x0F, len=9 (1 alg_mask + 8 Kc)
    *(msgb_put(msg, 1)) = 0x0F;
    *(msgb_put(msg, 1)) = 0x09;
    *(msgb_put(msg, 1)) = alg_mask;
    memcpy(msgb_put(msg, 8), kc, 8);
    *len_ptr = msg->len - 2;
    const char *alg_str = (alg_mask & 0x08) ? "A5/3" :
                          (alg_mask & 0x02) ? "A5/1" :
                          (alg_mask & 0x01) ? "no encrypt" : "?";
    std::cout << COLOR_CYAN << "✓ BSSMAP Ciphering Mode Command" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Alg: " << COLOR_GREEN << "0x" << std::hex
              << (int)alg_mask << " (" << alg_str << ")" << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Kc:  " << COLOR_GREEN;
    for (int i = 0; i < 8; i++) printf("%02X", kc[i]);
    std::cout << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Ciphering Mode Complete (3GPP TS 48.008 §3.2.1.31)
// BSC → MSC  —  BSC сообщает что шифрование активировано, передаёт Layer 3 msg
// L3 IE содержит DTAP Ciphering Mode Complete (RR 0x32) от MS
static struct msgb *generate_bssmap_cipher_mode_complete() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP CipherComplete");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x55;   // Ciphering Mode Complete
    // Layer 3 Message Contents IE: tag=0x0E, len=2 (minimal RR CipherModeComplete)
    // PD=0x06 (RR), MT=0x32 (Ciphering Mode Complete)
    *(msgb_put(msg, 1)) = 0x0E;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x06;   // PD = RR
    *(msgb_put(msg, 1)) = 0x32;   // MT = Ciphering Mode Complete
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Ciphering Mode Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x55  L3=RR-CipherModeComplete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Ciphering Mode Reject (3GPP TS 48.008 §3.2.1.32)
// BSC → MSC  —  BSC не может активировать шифрование; содержит Cause IE
static struct msgb *generate_bssmap_cipher_mode_reject(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP CipherReject");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x58;   // Ciphering Mode Reject
    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;   // e.g. 0x6E=ciphering algorithm not supported
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Ciphering Mode Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x58  Cause: " << COLOR_GREEN
              << "0x" << std::hex << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP Location Updating Accept (3GPP TS 24.008 §9.2.13)
// MSC → MS via BSC   DTAP MM 0x02
static struct msgb *generate_dtap_lu_accept(uint16_t mcc, uint16_t mnc, uint16_t lac, uint32_t tmsi) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP LU Accept");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = 0x02;  // Location Updating Accept
    // LAI (5 байт): MCC BCD + MNC BCD + LAC
    uint8_t lai[5];
    lai[0] = (mcc / 100) | ((mcc / 10 % 10) << 4);
    lai[1] = (mcc % 10) | (0xF << 4);   // digit3 + filler для 2-значного MNC
    lai[2] = (mnc / 10 % 10) | ((mnc % 10) << 4);
    lai[3] = (lac >> 8) & 0xFF;
    lai[4] = lac & 0xFF;
    memcpy(msgb_put(msg, 5), lai, 5);
    // Mobile Identity (TMSI) TLV: tag=0x17, len=5, type=0xF4, TMSI[4]
    *(msgb_put(msg, 1)) = 0x17;   // tag
    *(msgb_put(msg, 1)) = 0x05;   // len
    *(msgb_put(msg, 1)) = 0xF4;   // filler(0xF)|OE=0|type=TMSI(4)
    *(msgb_put(msg, 1)) = (tmsi >> 24) & 0xFF;
    *(msgb_put(msg, 1)) = (tmsi >> 16) & 0xFF;
    *(msgb_put(msg, 1)) = (tmsi >>  8) & 0xFF;
    *(msgb_put(msg, 1)) =  tmsi        & 0xFF;
    std::cout << COLOR_CYAN << "✓ DTAP Location Updating Accept" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  LAI: MCC=" << COLOR_GREEN << mcc << COLOR_RESET
              << COLOR_BLUE << " MNC=" << COLOR_GREEN << mnc << COLOR_RESET
              << COLOR_BLUE << " LAC=" << COLOR_GREEN << lac << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TMSI: " << COLOR_GREEN << "0x" << std::hex
              << std::uppercase << tmsi << std::dec << std::nouppercase << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP Location Updating Reject (3GPP TS 24.008 §9.2.14)
// MSC → MS via BSC   DTAP MM 0x04
static struct msgb *generate_dtap_lu_reject(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP LU Reject");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = 0x04;   // Location Updating Reject
    *(msgb_put(msg, 1)) = cause;
    // TS 24.008 Table 10.5.147
    const char *cause_str = "";
    switch (cause) {
        case 0x02: cause_str = "IMSI unknown in HLR"; break;
        case 0x03: cause_str = "illegal MS"; break;
        case 0x06: cause_str = "illegal ME"; break;
        case 0x0B: cause_str = "PLMN not allowed"; break;
        case 0x0C: cause_str = "LA not allowed"; break;
        case 0x0D: cause_str = "roaming not allowed in LA"; break;
        case 0x11: cause_str = "network failure"; break;
        case 0x16: cause_str = "congestion"; break;
        default:   cause_str = "unknown"; break;
    }
    std::cout << COLOR_CYAN << "✓ DTAP Location Updating Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << "0x" << std::hex
              << (int)cause << " (" << cause_str << ")" << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Reset (3GPP TS 48.008 §3.2.1.19)
// MSC → BSC   BSSMAP 0x30
static struct msgb *generate_bssmap_reset(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP Reset");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x30;   // Reset
    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;   // e.g. 0x00=radio interface message failure
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Reset" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << "0x" << std::hex
              << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Reset Acknowledge (3GPP TS 48.008 §3.2.1.20)
// BSC → MSC  —  подтверждение Reset (MSC ← BSC после Reset)
static struct msgb *generate_bssmap_reset_ack() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP ResetAck");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x31;   // Reset Acknowledge
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Reset Acknowledge" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x31  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ============================================================
// P8: A-interface BSSMAP — Assignment, Clear, Paging
// ============================================================

// BSSMAP Assignment Request (3GPP TS 08.08 §3.2.1.1)
// MSC → BSC  —  назначить радиоканал абоненту
// speech_ver: 0x01=FR v1, 0x21=EFR, 0x41=AMR-FR
// cic: Circuit Identity Code (номер канала TDM, 1..2047)
static struct msgb *generate_bssmap_assignment_request(uint8_t speech_ver, uint16_t cic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP AssignReq");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;          // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x01;          // Assignment Request

    // Channel Type IE: tag=0x0B, len=3 (speech + channel_rate + speech_version)
    *(msgb_put(msg, 1)) = 0x0B;          // Channel Type IE tag
    *(msgb_put(msg, 1)) = 0x03;          // IE length
    *(msgb_put(msg, 1)) = 0x01;          // Speech/Data indicator = speech
    *(msgb_put(msg, 1)) = 0x08;          // Channel rate/type: TCH/F + SDCCH/8
    *(msgb_put(msg, 1)) = speech_ver;    // Speech encoding algorithm

    // Circuit Identity Code IE: tag=0x01, len=2
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = (cic >> 8) & 0xFF;
    *(msgb_put(msg, 1)) = cic & 0xFF;

    *len_ptr = msg->len - 2;
    const char *sv_name = (speech_ver == 0x01) ? "GSM FR v1"
                        : (speech_ver == 0x21) ? "EFR"
                        : (speech_ver == 0x41) ? "AMR-FR"
                        : "Unknown";
    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Assignment Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  SpeechVer: " << COLOR_GREEN
              << "0x" << std::hex << (int)speech_ver << std::dec << " (" << sv_name << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CIC:       " << COLOR_GREEN << (int)cic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:    " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Assignment Complete (3GPP TS 08.08 §3.2.1.2)
// BSC → MSC  —  радиоканал успешно назначен
static struct msgb *generate_bssmap_assignment_complete() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP AssignCompl");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;  // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x02;  // Assignment Complete

    // RR Cause IE: tag=0x64, len=1, cause=0x00 (normal event)
    *(msgb_put(msg, 1)) = 0x64;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = 0x00;  // Normal event

    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Assignment Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  RR Cause: " << COLOR_GREEN << "0x00 (Normal event)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:   " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Assignment Failure (3GPP TS 48.008 §3.2.1.3)
// BSC → MSC  —  BSC не может назначить канал; содержит Cause IE
static struct msgb *generate_bssmap_assignment_failure(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP AssignFail");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x03;   // Assignment Failure
    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;   // e.g. 0x20=radio interface not available
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Assignment Failure" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN
              << "0x" << std::hex << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Clear Request (3GPP TS 08.08 §3.2.1.21)
// BSC → MSC  —  BSC инициирует освобождение соединения
static struct msgb *generate_bssmap_clear_request(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP ClearReq");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;    // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x22;    // Clear Request

    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;

    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Clear Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN
              << "0x" << std::hex << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Clear Complete (3GPP TS 08.08 §3.2.1.23)
// BSC → MSC  —  ресурсы BSC освобождены
static struct msgb *generate_bssmap_clear_complete() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP ClearCompl");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;  // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x21;  // Clear Complete

    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Clear Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Paging (3GPP TS 08.08 §3.2.1.12)
// MSC → BSC  —  вызов абонента (входящий вызов / SMS)
// Включает: IMSI IE (0x08) + Cell Identifier List IE (0x1A, по LAC)
static struct msgb *generate_bssmap_paging(const char *imsi_str, uint16_t lac) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP Paging");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x52;   // Paging

    // Encode IMSI as mobile identity (3GPP 04.08 §10.5.1.4)
    uint8_t digits[16];
    uint8_t num_digits = 0;
    for (const char *p = imsi_str; *p && num_digits < 15; p++, num_digits++)
        digits[num_digits] = (uint8_t)(*p - '0');
    bool odd = (num_digits % 2 == 1);
    uint8_t mi_len = num_digits / 2 + 1;  // type-byte + packed BCD pairs

    // IMSI IE: tag=0x08
    *(msgb_put(msg, 1)) = 0x08;
    *(msgb_put(msg, 1)) = mi_len;
    // First byte: digit[0] (high nibble) | odd_indicator (bit3) | IMSI type=1 (bits2-0)
    *(msgb_put(msg, 1)) = (uint8_t)((digits[0] << 4) | (odd ? 0x08 : 0x00) | 0x01);
    // Remaining digit pairs: digit[2k+1] (low) | digit[2k+2] (high)
    for (uint8_t i = 1; i < num_digits; i += 2) {
        uint8_t lo = digits[i];
        uint8_t hi = (i + 1 < num_digits) ? digits[i + 1] : 0x0F;
        *(msgb_put(msg, 1)) = (uint8_t)((hi << 4) | lo);
    }

    // Cell Identifier List IE: tag=0x1A, by LAC (discriminator=0x04)
    *(msgb_put(msg, 1)) = 0x1A;   // Cell Identifier List IE tag
    *(msgb_put(msg, 1)) = 0x03;   // IE length: 1 (discrim) + 2 (LAC)
    *(msgb_put(msg, 1)) = 0x04;   // Cell identifier discriminator: LAC
    *(msgb_put(msg, 1)) = (lac >> 8) & 0xFF;
    *(msgb_put(msg, 1)) = lac & 0xFF;

    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Paging" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  LAC:    " << COLOR_GREEN << (int)lac << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}


// BSSMAP Common Id (3GPP TS 48.008 §3.2.1.68)
// MSC → BSC  —  MSC передаёт IMSI в BSC после успешной аутентификации
// Используется BSC для корреляции IMSI с радиоресурсом (логирование, LCS)
// IMSI IE: tag=0x0D (Cell Identifier in this context = IMSI value)
static struct msgb *generate_bssmap_common_id(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP CommonId");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x2F;   // Common Id
    // IMSI IE: tag=0x08, len=variable (TBCD-encoded)
    size_t slen = strlen(imsi_str);
    uint8_t bcd[9]; memset(bcd, 0, sizeof(bcd));
    bcd[0] = (uint8_t)(((imsi_str[0]-'0') << 4) | 0x09); // type=IMSI(1) + even/odd
    size_t blen = 1;
    for (size_t i = 1; i < slen && blen < 9; i += 2) {
        bcd[blen++] = (uint8_t)((((i+1<slen)?(uint8_t)(imsi_str[i+1]-'0'):0x0F)<<4)|((uint8_t)(imsi_str[i]-'0')));
    }
    *(msgb_put(msg, 1)) = 0x08;   // IMSI IE tag
    *(msgb_put(msg, 1)) = (uint8_t)blen;
    memcpy(msgb_put(msg, blen), bcd, blen);
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Common Id" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2F  IMSI: " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}


// ============================================================

// BSSMAP Classmark Request (3GPP TS 48.008 §3.2.1.19)
// MSC → BSC  —  MSC запрашивает Classmark Information от MS
static struct msgb *generate_bssmap_classmark_request() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP ClassmarkReq");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x55;   // Classmark Request
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Classmark Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x55  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Classmark Update (3GPP TS 48.008 §3.2.1.20)
// BSC → MSC  —  BSC передаёт Classmark Information MS (Mobile Station Classmark 2)
// MS Classmark 2 IE: tag=0x12, len=3 (базовые возможности MS)
static struct msgb *generate_bssmap_classmark_update() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP ClassmarkUpd");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x53;   // Classmark Update
    // MS Classmark 2 IE: tag=0x12, len=3
    *(msgb_put(msg, 1)) = 0x12;
    *(msgb_put(msg, 1)) = 0x03;
    *(msgb_put(msg, 1)) = 0x00;   // Classmark byte 1 (RF power level etc.)
    *(msgb_put(msg, 1)) = 0x00;   // Classmark byte 2
    *(msgb_put(msg, 1)) = 0x00;   // Classmark byte 3
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Classmark Update" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x53  MS Classmark 2 (3 bytes)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}
// P13: A-interface BSSMAP Handover (3GPP TS 48.008 §3.2.1.7–3.2.1.12)
// ============================================================

// BSSMAP Handover Required (3GPP TS 48.008 §3.2.1.7)
// BSC → MSC  —  BSC запрашивает хендовер к другой соте
// Cause IE + Target Cell ID IE + Current Channel IE
static struct msgb *generate_bssmap_ho_required(uint8_t cause,
                                                 uint16_t target_lac,
                                                 uint16_t target_cell_id) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO Required");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;           // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x11;           // Handover Required

    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;           // e.g. 0x00=radio interface msg failure, 0x58=better cell

    // Target Cell ID IE: tag=0x0A, len=8
    // Cell ID Discriminator=0x00 (LAC+CI), LAC[2] + CI[2]
    *(msgb_put(msg, 1)) = 0x0A;            // Target Cell ID tag
    *(msgb_put(msg, 1)) = 0x08;            // Length
    // Cell ID List: 1 entry — LAC+CI format (discriminator 0x01)
    *(msgb_put(msg, 1)) = 0x00;            // reserved
    *(msgb_put(msg, 1)) = 0x01;            // discriminator: LAC+CI
    *(msgb_put(msg, 1)) = (target_lac  >> 8) & 0xFF;
    *(msgb_put(msg, 1)) = (target_lac       ) & 0xFF;
    *(msgb_put(msg, 1)) = (target_cell_id >> 8) & 0xFF;
    *(msgb_put(msg, 1)) = (target_cell_id     ) & 0xFF;
    *(msgb_put(msg, 1)) = 0x00;            // padding
    *(msgb_put(msg, 1)) = 0x00;

    // Current Channel type 1 IE: tag=0x2E, len=1
    // 0x08 = TCH/F (Speech Channel Full rate)
    *(msgb_put(msg, 1)) = 0x2E;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = 0x08;            // TCH/F

    *len_ptr = msg->len - 2;

    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Handover Required" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:       " << COLOR_GREEN
              << "0x" << std::hex << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Target LAC:  " << COLOR_GREEN << target_lac    << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Target Cell: " << COLOR_GREEN << target_cell_id << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:      " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Command (3GPP TS 48.008 §3.2.1.8)
// MSC → BSC (Source)  —  MSC направляет команду хендовера исходному BSC
// Layer 3 Information IE содержит RR Handover Command (GSM 04.08)
static struct msgb *generate_bssmap_ho_command(uint16_t target_lac,
                                               uint16_t target_cell_id) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO Command");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;            // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x12;            // Handover Command

    // Layer 3 Information IE: tag=0x17
    // Contains minimal RR Handover Command (GSM 04.08 §9.1.15):
    //   PD=0x06 (RR), MT=0x2B (Handover Command)
    //   Cell Description IE (2 bytes: NCC+BSIC | BCCH ARFCN low)
    //   Channel Description (3 bytes)
    //   Handover Reference (1 byte)
    //   Power Command (1 byte)
    uint8_t rr_ho_cmd[] = {
        0x06, 0x2B,                    // RR header: PD=RR, MT=HandoverCommand
        (uint8_t)(((target_lac >> 8) & 0x07) << 5 | 0x10), // Cell Desc byte1
        (uint8_t)(target_lac & 0xFF),  // Cell Desc byte2 (BCCH ARFCN low)
        0x05, 0x28, 0x01,              // Channel Description (TCH/F, no hopping)
        (uint8_t)(target_cell_id & 0xFF), // Handover Reference
        0x00                           // Power Command (0 dBm)
    };
    *(msgb_put(msg, 1)) = 0x17;            // Layer 3 Information IE tag
    *(msgb_put(msg, 1)) = sizeof(rr_ho_cmd);
    memcpy(msgb_put(msg, sizeof(rr_ho_cmd)), rr_ho_cmd, sizeof(rr_ho_cmd));

    *len_ptr = msg->len - 2;

    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Handover Command" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Target LAC:  " << COLOR_GREEN << target_lac    << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Target Cell: " << COLOR_GREEN << target_cell_id << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  RR HO Cmd:   " << COLOR_GREEN << sizeof(rr_ho_cmd) << " байт (GSM 04.08 §9.1.15)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:      " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Succeeded (3GPP TS 48.008 §3.2.1.10)
// MSC → BSC (Source)  —  подтверждение успешного хендовера (ресурсы можно освободить)
static struct msgb *generate_bssmap_ho_succeeded() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO Succeeded");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;  // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x14;  // Handover Succeeded
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Handover Succeeded" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Performed (3GPP TS 48.008 §3.2.1.13)
// BSC → MSC  —  внутрисистемный хендовер завершён на уровне BSC (intra-BSC HO)
// Включает: Cause IE + Channel Type IE + Speech Version IE
static struct msgb *generate_bssmap_ho_performed(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO Performed");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;  // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x17;  // Handover Performed

    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;

    // Channel Type IE: tag=0x0B, len=3 (same as Assignment Request)
    *(msgb_put(msg, 1)) = 0x0B;
    *(msgb_put(msg, 1)) = 0x03;
    *(msgb_put(msg, 1)) = 0x01;  // Speech
    *(msgb_put(msg, 1)) = 0x08;  // TCH/F
    *(msgb_put(msg, 1)) = 0x01;  // FR v1

    // Speech Version IE: tag=0x40, len=1
    *(msgb_put(msg, 1)) = 0x40;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = 0x01;  // GSM FR v1

    *len_ptr = msg->len - 2;

    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Handover Performed" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN
              << "0x" << std::hex << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Candidate Enquiry (3GPP TS 48.008 §3.2.1.11)
// MSC → BSC  —  запрос информации о кандидатах для хендовера
// Используется при inter-BSC HO для выбора целевого BSC
static struct msgb *generate_bssmap_ho_candidate_enquiry(uint16_t lac) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO CandEnq");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;  // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x4F;  // Handover Candidate Enquiry

    // Cell Identifier List IE: tag=0x1A, discriminator=0x04 (LAC only)
    *(msgb_put(msg, 1)) = 0x1A;
    *(msgb_put(msg, 1)) = 0x03;            // len: discrim(1) + LAC(2)
    *(msgb_put(msg, 1)) = 0x04;            // discriminator: LAC
    *(msgb_put(msg, 1)) = (lac >> 8) & 0xFF;
    *(msgb_put(msg, 1)) = lac & 0xFF;

    *len_ptr = msg->len - 2;

    std::cout << COLOR_CYAN << "✓ Сгенерировано BSSMAP Handover Candidate Enquiry" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  LAC:    " << COLOR_GREEN << lac << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Request (3GPP TS 48.008 §3.2.1.8)
// MSC → BSC (Target)  —  MSC запрашивает ресурсы в целевом BSC
// Содержит: Channel Type IE + Encryption Information IE + Classmark IE + Cell ID IE
static struct msgb *generate_bssmap_ho_request(uint8_t cause,
                                                uint16_t lac,
                                                uint16_t cell_id) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO Request");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x10;   // Handover Request

    // Channel Type IE: tag=0x0B, len=3
    *(msgb_put(msg, 1)) = 0x0B;
    *(msgb_put(msg, 1)) = 0x03;
    *(msgb_put(msg, 1)) = 0x01;   // Speech
    *(msgb_put(msg, 1)) = 0x08;   // TCH/F
    *(msgb_put(msg, 1)) = 0x01;   // FR v1

    // Encryption Information IE: tag=0x0A, len=1 (no encryption)
    *(msgb_put(msg, 1)) = 0x0A;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = 0x01;   // Algorithm id A5/1, no key

    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;

    // Cell Identifier IE (LAC+CI): tag=0x05, len=5
    *(msgb_put(msg, 1)) = 0x05;
    *(msgb_put(msg, 1)) = 0x05;
    *(msgb_put(msg, 1)) = 0x01;   // discriminator: LAC+CI
    *(msgb_put(msg, 1)) = (lac >> 8) & 0xFF;
    *(msgb_put(msg, 1)) = lac & 0xFF;
    *(msgb_put(msg, 1)) = (cell_id >> 8) & 0xFF;
    *(msgb_put(msg, 1)) = cell_id & 0xFF;

    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Handover Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:     " << COLOR_GREEN
              << "0x" << std::hex << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Target LAC:  " << COLOR_GREEN << lac     << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Target Cell: " << COLOR_GREEN << cell_id << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:      " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Request Acknowledge (3GPP TS 48.008 §3.2.1.9)
// BSC (Target) → MSC  —  целевой BSC готов принять MS; содержит HO Reference
// Layer 3 Information IE содержит RR Handover Command для MS
static struct msgb *generate_bssmap_ho_request_ack(uint8_t ho_ref) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO ReqAck");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x13;   // Handover Request Acknowledge

    // Layer 3 Information IE: tag=0x17 — minimal RR HO Command
    uint8_t rr_ho_cmd[] = { 0x06, 0x2B, 0x10, 0x01, 0x05, 0x28, 0x01, ho_ref, 0x00 };
    *(msgb_put(msg, 1)) = 0x17;
    *(msgb_put(msg, 1)) = sizeof(rr_ho_cmd);
    memcpy(msgb_put(msg, sizeof(rr_ho_cmd)), rr_ho_cmd, sizeof(rr_ho_cmd));

    // Handover Reference IE: tag=0x36, len=1
    *(msgb_put(msg, 1)) = 0x36;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = ho_ref;

    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Handover Request Acknowledge" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  HO Ref: " << COLOR_GREEN << (int)ho_ref << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Failure (3GPP TS 48.008 §3.2.1.13)
// BSC (Target) → MSC  —  целевой BSC не может принять хендовер (ресурсы недоступны)
// Cause IE обязателен
static struct msgb *generate_bssmap_ho_failure(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO Failure");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x15;   // Handover Failure
    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Handover Failure" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN
              << "0x" << std::hex << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP SAPI n Reject (3GPP TS 48.008 §3.2.1.34)
// BSC → MSC  —  BSC сообщает что SABM по SAPI n (обычно SAPI=3 для SMS)
//               был отклонён MS (DM frame received)
// SAPI IE: tag=0x1F, len=1 (bits 0-1 = SAPI: 0=RR/CC/MM, 3=SMS)
// Cause IE: tag=0x04, len=1
static struct msgb *generate_bssmap_sapi_n_reject(uint8_t sapi, uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP SAPInReject");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x00;   // BSSAP discriminator (BSSMAP)
    uint8_t *len_ptr = msgb_put(msg, 1);
    *(msgb_put(msg, 1)) = 0x25;   // SAPI n Reject
    // DLCI IE: tag=0x1F, len=1 (SAPI in bits 0-1)
    *(msgb_put(msg, 1)) = 0x1F;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = (uint8_t)(sapi & 0x07);
    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;   // e.g. 0x25=radio connection with MS lost
    *len_ptr = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP SAPI n Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x25  SAPI: " << COLOR_GREEN << (int)sapi
              << (sapi==3?" (SMS)":"") << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN
              << "0x" << std::hex << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Required Reject (3GPP TS 48.008 §3.2.1.27)
// BSC → MSC  —  MSC отклоняет запрос HO от BSC (сеть не может выполнить)
// MT=0x1A (26  /* BSS_MAP_MSG_HANDOVER_REQUIRED_REJECT */=26)
static struct msgb *generate_bssmap_ho_required_reject(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO-Req-Rej");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00;  // BSSMAP discriminator
    len_ptr[1] = 0x00;  // placeholder length
    *(msgb_put(msg, 1)) = 26  /* BSS_MAP_MSG_HANDOVER_REQUIRED_REJECT */;  // 0x1A
    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Handover Required Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x1A  Cause: " << COLOR_GREEN
              << "0x" << std::hex << (int)cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Candidate Response (3GPP TS 48.008 §3.2.1.26)
// MSC → BSC  —  ответ на Handover Candidate Enquiry
// MT=0x19 (25  /* BSS_MAP_MSG_HANDOVER_CANDIDATE_RESPONSE */=25)
static struct msgb *generate_bssmap_ho_candidate_response(uint8_t num_candidates) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO-Cand-Resp");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00;
    len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 25  /* BSS_MAP_MSG_HANDOVER_CANDIDATE_RESPONSE */;  // 0x19
    // Number of MSs (count of candidates): tag=0x1D, len=1
    *(msgb_put(msg, 1)) = 0x1D; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = num_candidates;
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Handover Candidate Response" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x19  Candidates: " << COLOR_GREEN << (int)num_candidates << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Suspend (3GPP TS 48.008 §3.2.1.42)
// BSC → MSC  —  запрос приостановки вызова (GPRS suspend)
// MT=0x28 (40  /* BSS_MAP_MSG_SUSPEND */=40)
static struct msgb *generate_bssmap_suspend(uint8_t tlli0, uint8_t tlli1, uint8_t tlli2, uint8_t tlli3) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP Suspend");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 40  /* BSS_MAP_MSG_SUSPEND */;  // 0x28
    // TLLI IE: tag=0x1F, len=4
    *(msgb_put(msg, 1)) = 0x1F; *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = tlli0; *(msgb_put(msg, 1)) = tlli1;
    *(msgb_put(msg, 1)) = tlli2; *(msgb_put(msg, 1)) = tlli3;
    len_ptr[1] = msg->len - 2;
    uint32_t tlli = ((uint32_t)tlli0<<24)|((uint32_t)tlli1<<16)|((uint32_t)tlli2<<8)|tlli3;
    std::cout << COLOR_CYAN << "✓ BSSMAP Suspend" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x28  TLLI: " << COLOR_GREEN
              << "0x" << std::hex << tlli << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Resume (3GPP TS 48.008 §3.2.1.43)
// BSC → MSC  —  возобновление вызова после приостановки
// MT=0x29 (41  /* BSS_MAP_MSG_RESUME */=41)
static struct msgb *generate_bssmap_resume(uint8_t tlli0, uint8_t tlli1, uint8_t tlli2, uint8_t tlli3) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP Resume");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 41  /* BSS_MAP_MSG_RESUME */;   // 0x29
    *(msgb_put(msg, 1)) = 0x1F; *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = tlli0; *(msgb_put(msg, 1)) = tlli1;
    *(msgb_put(msg, 1)) = tlli2; *(msgb_put(msg, 1)) = tlli3;
    len_ptr[1] = msg->len - 2;
    uint32_t tlli = ((uint32_t)tlli0<<24)|((uint32_t)tlli1<<16)|((uint32_t)tlli2<<8)|tlli3;
    std::cout << COLOR_CYAN << "✓ BSSMAP Resume" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x29  TLLI: " << COLOR_GREEN
              << "0x" << std::hex << tlli << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Load Indication (3GPP TS 48.008 §3.2.1.76)
// BSC → MSC  MT=0x5A (90)  — периодическое информирование о загрузке ячейки
// Cell Identifier и Resource Situation обязательны
static struct msgb *generate_bssmap_load_indication(uint16_t cell_id, uint8_t resource_avail) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP LoadInd");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 90;   // BSS_MAP_MSG_LOAD_INDICATION
    // Cell Identifier IE: tag=0x05, len=3, CI-discriminator=0x02, cell_id BE
    *(msgb_put(msg, 1)) = 0x05; *(msgb_put(msg, 1)) = 0x03;
    *(msgb_put(msg, 1)) = 0x02;  // CI: cell identity
    *(msgb_put(msg, 1)) = (uint8_t)(cell_id >> 8);
    *(msgb_put(msg, 1)) = (uint8_t)(cell_id & 0xFF);
    // Resource Available IE: tag=0x12, len=1
    *(msgb_put(msg, 1)) = 0x12; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = resource_avail;
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Load Indication" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x5A(90)  CellID: " << COLOR_GREEN << cell_id
              << "  ResourceAvail: " << (int)resource_avail << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Queuing Indication (3GPP TS 48.008 §3.2.1.57)
// MSC → BSC  MT=0x56 (86)  — уведомление BSC о постановке запроса в очередь (HO)
// Нет обязательных IE
static struct msgb *generate_bssmap_queuing_indication(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP QueInd");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 86;   // BSS_MAP_MSG_QUEUING_INDICATION
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Queuing Indication" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x56(86)  (MSC → BSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Confusion (3GPP TS 48.008 §3.2.1.39)
// MSC ↔ BSC  MT=38 (0x26)  — сообщение-ответ на непонятный/ошибочный MT
// Mandatory: Cause IE
static struct msgb *generate_bssmap_confusion(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP Confusion");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 38;   // BSS_MAP_MSG_CONFUSION
    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Confusion" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x26(38)  Cause: " << COLOR_GREEN << (int)cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Connection Oriented Information (3GPP TS 48.008 §3.2.1.65)
// BSC ↔ MSC  MT=42 (0x2A)  — транспорт инкапсулированных данных LCLS/VGCS
// Mandatory: Layer 3 Information IE
static struct msgb *generate_bssmap_connection_oriented_info(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP COI");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 42;   // BSS_MAP_MSG_CONNECTION_ORIENTED_INFORMATION
    // Layer 3 Information IE: tag=0x0E, len=3, dummy L3 data
    static const uint8_t l3_dummy[] = {0x05, 0x04, 0x01};
    *(msgb_put(msg, 1)) = 0x0E; *(msgb_put(msg, 1)) = (uint8_t)sizeof(l3_dummy);
    memcpy(msgb_put(msg, sizeof(l3_dummy)), l3_dummy, sizeof(l3_dummy));
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Connection Oriented Information" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2A(42)  (BSC ↔ MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Perform Location Request (3GPP TS 48.008 §3.2.1.66)
// MSC → BSC  MT=43 (0x2B)  — запрос позиционирования (LCS)
// Mandatory: Location Type IE
static struct msgb *generate_bssmap_perform_location_request(uint8_t loc_type) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP PLR");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 43;   // BSS_MAP_MSG_PERFORM_LOCATION_RQST
    // Location Type IE: tag=0x44, len=1
    *(msgb_put(msg, 1)) = 0x44; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = loc_type;  // 0=current location, 1=current or last
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Perform Location Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2B(43)  LocationType: " << COLOR_GREEN << (int)loc_type << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Perform Location Response (3GPP TS 48.008 §3.2.1.67)
// BSC → MSC  MT=45 (0x2D)  — ответ с результатом позиционирования (LCS)
// No mandatory fixed IEs (result is in optional Geographic Location)
static struct msgb *generate_bssmap_perform_location_response(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP PLResp");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 45;   // BSS_MAP_MSG_PERFORM_LOCATION_RESPONSE
    // Geographic Location IE: tag=0x7C, len=7, dummy ellipsoid point
    static const uint8_t geo[] = {0x10, 0x00, 0x12, 0x34, 0x56, 0x00, 0x78};
    *(msgb_put(msg, 1)) = 0x7C; *(msgb_put(msg, 1)) = (uint8_t)sizeof(geo);
    memcpy(msgb_put(msg, sizeof(geo)), geo, sizeof(geo));
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Perform Location Response" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2D(45)  (BSC → MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Overload (3GPP TS 48.008 §3.2.1.14)
// BSC → MSC  MT=50 (0x32)  — уведомление о перегрузке (BSC не может принять новые вызовы)
// Mandatory: Cause IE
static struct msgb *generate_bssmap_overload(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP Overload");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 50;   // BSS_MAP_MSG_OVERLOAD
    // Cause IE: tag=0x04, len=1
    *(msgb_put(msg, 1)) = 0x04; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;   // e.g. 0x58=processor overload
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Overload" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x32(50)  Cause: " << COLOR_GREEN << (int)cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Perform Location Abort (3GPP TS 48.008 §3.2.1.68)
// MSC → BSC  MT=46 (0x2E)  — отмена запроса позиционирования (LCS abort)
// Mandatory: Cause IE
static struct msgb *generate_bssmap_perform_location_abort(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP PLAbort");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 46;   // BSS_MAP_MSG_PERFORM_LOCATION_ABORT
    *(msgb_put(msg, 1)) = 0x04; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cause;
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Perform Location Abort" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2E(46)  Cause: " << COLOR_GREEN << (int)cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Handover Detect (3GPP TS 48.008 §3.2.1.45)
// new BSS → MSC  MT=27 (0x1B)  — обнаружение доступа в новой ячейке при HO
// No mandatory IEs
static struct msgb *generate_bssmap_handover_detect(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP HO Detect");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 27;   // BSS_MAP_MSG_HANDOVER_DETECT
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Handover Detect" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x1B(27)  (new BSS → MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP MSC Invoke Trace (3GPP TS 48.008 §3.2.1.30)
// MSC → BSC  MT=54 (0x36)  — запуск трассировки вызова
// Mandatory: Trace Type IE (tag=0x22)
static struct msgb *generate_bssmap_msc_invoke_trace(uint8_t trace_type) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP MSCTrace");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 54;   // BSS_MAP_MSG_MSC_INVOKE_TRACE
    // Trace Type IE: tag=0x22, len=1, trace type value
    *(msgb_put(msg, 1)) = 0x22; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = trace_type;   // 0x01=full trace
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP MSC Invoke Trace" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x36(54)  TraceType: " << COLOR_GREEN << (int)trace_type << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ─────────────────────────────────────────────────────────────────────────────
// P31: BSSMAP Channel Modify Request / Internal HO Required / Command / LSA Info
// ─────────────────────────────────────────────────────────────────────────────

// BSSMAP Channel Modify Request (3GPP TS 48.008 §3.2.1.73)
// MT=0x08  BSS → MSC  — запрос на изменение канала после внутреннего HO
// Mandatory: Cause IE (tag=0x04, len=1)
static struct msgb *generate_bssmap_channel_modify_request(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP ChanModReq");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 0x08;   // BSS_MAP_MSG_CHAN_MODIFY_REQUEST
    *(msgb_put(msg, 1)) = 0x04; *(msgb_put(msg, 1)) = 0x01; *(msgb_put(msg, 1)) = cause;
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Channel Modify Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x08(8)  Cause: " << COLOR_GREEN << (int)cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Internal Handover Required (3GPP TS 48.008 §3.2.1.73)
// MT=0x21 (33)  BSS → MSC  — внутренний хэндовер требуется (внутри одного BSC)
// Mandatory: Cause IE (tag=0x04, len=1)
static struct msgb *generate_bssmap_internal_handover_required(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP IntHOReq");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 33;   // BSS_MAP_MSG_INT_HO_RQSD (0x21)
    *(msgb_put(msg, 1)) = 0x04; *(msgb_put(msg, 1)) = 0x01; *(msgb_put(msg, 1)) = cause;
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Internal Handover Required" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x21(33)  Cause: " << COLOR_GREEN << (int)cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP Internal Handover Command (3GPP TS 48.008 §3.2.1.73)
// MT=0x23 (35)  MSC → BSS  — команда на выполнение внутреннего хэндовера
// No mandatory IEs beyond message type
static struct msgb *generate_bssmap_internal_handover_command(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP IntHOCmd");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 35;   // BSS_MAP_MSG_INT_HO_CMD (0x23)
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP Internal Handover Command" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x23(35)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// BSSMAP LSA Information (3GPP TS 48.008 §3.2.1.73)
// MT=0x2C (44)  MSC → BSS  — передача информации о Localised Service Area
// Mandatory: LSA Information IE (tag=0x2F, dummy 2-byte payload)
static struct msgb *generate_bssmap_lsa_information(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "BSSMAP LSAInfo");
    if (!msg) return nullptr;
    uint8_t *len_ptr = msgb_put(msg, 2);
    len_ptr[0] = 0x00; len_ptr[1] = 0x00;
    *(msgb_put(msg, 1)) = 44;   // BSS_MAP_MSG_LSA_INFORMATION (0x2C)
    // LSA Information IE: tag=0x2F, len=2, dummy LSA ID
    *(msgb_put(msg, 1)) = 0x2F; *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x00; *(msgb_put(msg, 1)) = 0x01;  // LSA ID dummy
    len_ptr[1] = msg->len - 2;
    std::cout << COLOR_CYAN << "✓ BSSMAP LSA Information" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2C(44)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ============================================================
// P14: A-interface DTAP MM Supplementary
//      (3GPP TS 24.008 §9.2.26, §9.2.27, §9.2.18, §9.1.9)
// ============================================================

// DTAP TMSI Reallocation Command (3GPP TS 24.008 §9.2.26)
// MSC → MS  —  MSC назначает новый TMSI, передаёт LAI + Mobile Identity
// Обычно отправляется после Location Updating Accept
static struct msgb *generate_dtap_tmsi_realloc_cmd(uint16_t mcc, uint16_t mnc,
                                                    uint16_t lac, uint32_t tmsi) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP TMSI Realloc Cmd");
    if (!msg) return nullptr;

    // ── MM header ─────────────────────────────────────────────────────────
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;   // PD = 0x05 (MM)
    *(msgb_put(msg, 1)) = 0x1A;              // MT = TMSI Reallocation Command

    // Location Area Identifier (5 bytes): MCC BCD + MNC BCD + LAC
    uint8_t lai[5];
    lai[0] = (uint8_t)(((mcc / 100) % 10) | (((mcc / 10) % 10) << 4));
    lai[1] = (uint8_t)((mcc % 10) | (0xF << 4));   // 2-digit MNC filler
    lai[2] = (uint8_t)(((mnc / 10) % 10) | ((mnc % 10) << 4));
    lai[3] = (lac >> 8) & 0xFF;
    lai[4] =  lac        & 0xFF;
    memcpy(msgb_put(msg, 5), lai, 5);

    // Mobile Identity IE (TMSI): tag=0x17, len=5
    //   id_type[3:0]=4(TMSI), OE=0, filler=0xF → octet3=0xF4
    *(msgb_put(msg, 1)) = 0x17;   // IEI
    *(msgb_put(msg, 1)) = 0x05;   // len
    *(msgb_put(msg, 1)) = 0xF4;   // filler(0xF)|OE=0|type=TMSI(4)
    *(msgb_put(msg, 1)) = (tmsi >> 24) & 0xFF;
    *(msgb_put(msg, 1)) = (tmsi >> 16) & 0xFF;
    *(msgb_put(msg, 1)) = (tmsi >>  8) & 0xFF;
    *(msgb_put(msg, 1)) =  tmsi        & 0xFF;

    std::cout << COLOR_CYAN << "✓ DTAP TMSI Reallocation Command" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MCC/MNC: " << COLOR_GREEN << mcc << "/" << mnc << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  LAC:     " << COLOR_GREEN << lac  << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TMSI:    " << COLOR_GREEN
              << "0x" << std::hex << std::uppercase << tmsi
              << std::dec << std::nouppercase << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP TMSI Reallocation Complete (3GPP TS 24.008 §9.2.27)
// MS → MSC  —  MS подтверждает получение нового TMSI (нет обязательных IE)
static struct msgb *generate_dtap_tmsi_realloc_compl() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP TMSI Realloc Compl");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;  // PD = 0x05 (MM)
    *(msgb_put(msg, 1)) = 0x1B;             // MT = TMSI Reallocation Complete
    std::cout << COLOR_CYAN << "✓ DTAP TMSI Reallocation Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP MM Information (3GPP TS 24.008 §9.2.18)
// MSC → MS  —  передаёт имя сети (Full Name IE) и часовой пояс
// tz_byte: UTC offset в четвертях часа, BCD с перестановкой ниббл
//   0x21=UTC+3 (Москва)  0x00=UTC+0  0x12=UTC+2
static struct msgb *generate_dtap_mm_information(uint8_t tz_byte) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP MM Information");
    if (!msg) return nullptr;

    // ── MM header ─────────────────────────────────────────────────────────
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;  // PD = 0x05 (MM)
    *(msgb_put(msg, 1)) = 0x32;             // MT = MM Information

    // Full Name for Network IE (§10.5.3.5a): tag=0x43, TLV
    // "vMSC" GSM 7-bit packed (28 bits → 4 bytes, 4 spare bits)
    // v=0x76, M=0x4D, S=0x53, C=0x43 → {0xF6, 0xE6, 0x74, 0x08}
    // coding_byte: ext=1 | scheme=000 | addCI=0 | spare=4 → 0x84
    static const uint8_t full_name[] = { 0x43, 0x05, 0x84, 0xF6, 0xE6, 0x74, 0x08 };
    memcpy(msgb_put(msg, sizeof(full_name)), full_name, sizeof(full_name));

    // Local Time Zone IE (§10.5.3.8): tag=0x46, TV (1 value byte)
    *(msgb_put(msg, 1)) = 0x46;   // IEI
    *(msgb_put(msg, 1)) = tz_byte;

    std::cout << COLOR_CYAN << "✓ DTAP MM Information" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Network name: " << COLOR_GREEN << "\"vMSC\" (GSM 7-bit)" << COLOR_RESET << "\n";
    {
        // Decode: low_nibble = tens digit, high_nibble = units digit (TS 23.040 §9.2.3.11)
        int quarters = (int)((tz_byte & 0x07) * 10 + ((tz_byte >> 4) & 0x0F));
        bool negative = (tz_byte & 0x08) != 0;
        std::cout << COLOR_BLUE << "  TZ byte:       " << COLOR_GREEN
                  << "0x" << std::hex << (int)tz_byte << std::dec
                  << "  (" << (negative ? "UTC-" : "UTC+")
                  << quarters / 4 << "h" << (quarters % 4) * 15 << "m)"
                  << COLOR_RESET << "\n";
    }
    std::cout << COLOR_BLUE << "  Размер:        " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP Ciphering Mode Complete (3GPP TS 24.008 §9.1.9  / TS 44.018 §9.1.9)
// MS → MSC  —  ответ на Ciphering Mode Command (P6), шифрование включено
// Сообщение RR-уровня: PD=0x06 (RR), MT=0x32
// Опционально содержит IMEISV; здесь генерируем без него (минимальное)
static struct msgb *generate_dtap_cipher_mode_compl() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP Cipher Mode Compl");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = 0x06;   // PD = RR (Radio Resource)
    *(msgb_put(msg, 1)) = 0x32;   // MT = Ciphering Mode Complete
    std::cout << COLOR_CYAN << "✓ DTAP Ciphering Mode Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  PD=0x06 (RR)  MT=0x32 — шифрование активировано" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Status (3GPP TS 44.018 §9.1.21)
// MS ↔ BSC/MSC  RR 0x12  — статусное сообщение при ошибке RR-протокола
// Обязателен RR Cause IE
static struct msgb *generate_dtap_rr_status(uint8_t rr_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR Status");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;          // PD=0x06
    *(msgb_put(msg, 1)) = GSM48_MT_RR_STATUS;       // 0x12
    // RR Cause IE (M, 1 octet value)
    *(msgb_put(msg, 1)) = rr_cause;
    const char *cs = (rr_cause==0x00)?"Normal event":
                     (rr_cause==0x60)?"Invalid mandatory information":
                     (rr_cause==0x61)?"Message type non-existent":
                     (rr_cause==0x62)?"Message not compatible with protocol state":"Other";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Status" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << (int)rr_cause << " (" << cs << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Channel Release (3GPP TS 44.018 §9.1.7)
// BSC → MS  RR 0x0D  — освобождение радиоканала
// RR Cause IE обязателен
static struct msgb *generate_dtap_rr_channel_release(uint8_t rr_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR ChanRel");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_CHAN_REL;     // 0x0D
    // RR Cause (M LV, 1 byte value)
    *(msgb_put(msg, 1)) = rr_cause;
    // BA Range (optional) — omitted
    const char *cs = (rr_cause==0x00)?"Normal event":
                     (rr_cause==0x01)?"Abnormal release, unspecified":
                     (rr_cause==0x04)?"Preemptive release":
                     (rr_cause==0x08)?"Handover impossible, timing advance out of range":"Other";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Channel Release" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << (int)rr_cause << " (" << cs << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Classmark Change (3GPP TS 44.018 §9.1.9)
// MS → BSC  RR 0x16  — MS сообщает BSC об изменении своих возможностей
// MS Classmark 2 обязателен
static struct msgb *generate_dtap_rr_classmark_change(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR ClsmChg");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_CLSM_CHG;    // 0x16
    // MS Classmark 2 LV: len=3
    *(msgb_put(msg, 1)) = 0x03;
    *(msgb_put(msg, 1)) = 0x59; *(msgb_put(msg, 1)) = 0x59; *(msgb_put(msg, 1)) = 0x19;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Classmark Change" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Classmark2: " << COLOR_GREEN << "0x59 0x59 0x19 (эмуляция)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Classmark Enquiry (3GPP TS 44.018 §9.1.10)
// BSC → MS  RR 0x13  — запрос BSC к MS о её возможностях
// Нет обязательных IE
static struct msgb *generate_dtap_rr_classmark_enquiry(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR ClsmEnq");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_CLSM_ENQ;    // 0x13
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Classmark Enquiry" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Assignment Command (3GPP TS 44.018 §9.1.2)
// BSC → MS  MT=0x2E  — команда смены канала (assignment)
// Mandatory: Channel Description (tag=0x64, type 3 length=3)
static struct msgb *generate_dtap_rr_assignment_command(uint16_t arfcn, uint8_t ts) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR Ass Cmd");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;          // PD=RR
    *(msgb_put(msg, 1)) = GSM48_MT_RR_ASS_CMD;      // 0x2E
    // Channel Description 2: tag=0x64, L=3: chan-type/ts/arfcn-hi, arfcn-lo, training-seq
    *(msgb_put(msg, 1)) = 0x64;
    *(msgb_put(msg, 1)) = 0x03;
    *(msgb_put(msg, 1)) = (uint8_t)((0x01 << 3) | (ts & 0x07)); // TCH/F + TS
    *(msgb_put(msg, 1)) = (uint8_t)(arfcn & 0xFF);
    *(msgb_put(msg, 1)) = 0x00; // training sequence 0
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Assignment Command" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2E  ARFCN: " << COLOR_GREEN << arfcn
              << COLOR_RESET << COLOR_BLUE << "  TS: " << COLOR_GREEN << (int)ts << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Assignment Complete (3GPP TS 44.018 §9.1.3)
// MS → BSC  MT=0x29  — подтверждение смены канала
// Mandatory: RR Cause (1 byte)
static struct msgb *generate_dtap_rr_assignment_complete(uint8_t rr_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR Ass Compl");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_ASS_COMPL;   // 0x29
    *(msgb_put(msg, 1)) = rr_cause;                 // RR Cause (M, 1 byte)
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Assignment Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x29  Cause: " << COLOR_GREEN << (int)rr_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Assignment Failure (3GPP TS 44.018 §9.1.4)
// MS → BSC  MT=0x2F  — отказ от смены канала
// Mandatory: RR Cause (1 byte)
static struct msgb *generate_dtap_rr_assignment_failure(uint8_t rr_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR Ass Fail");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_ASS_FAIL;    // 0x2F
    *(msgb_put(msg, 1)) = rr_cause;                 // RR Cause (M, 1 byte)
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Assignment Failure" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2F  Cause: " << COLOR_GREEN << (int)rr_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Handover Command (3GPP TS 44.018 §9.1.15)
// BSC → MS  MT=0x2B  — команда хэндовера
// Mandatory: Cell Description (tag=0x10, 2 bytes) + Channel Description (tag=0x64, 3 bytes)
static struct msgb *generate_dtap_rr_handover_command(uint16_t arfcn, uint8_t bsic) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR HO Cmd");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_HANDO_CMD;   // 0x2B
    // Cell Description: tag=0x10, len=2 (BSIC + ARFCN encoded)
    *(msgb_put(msg, 1)) = 0x10; *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = (uint8_t)((bsic << 2) | ((arfcn >> 8) & 0x03));
    *(msgb_put(msg, 1)) = (uint8_t)(arfcn & 0xFF);
    // Channel Description 2: tag=0x64, len=3
    *(msgb_put(msg, 1)) = 0x64; *(msgb_put(msg, 1)) = 0x03;
    *(msgb_put(msg, 1)) = 0x08; // TCH/F TS=0
    *(msgb_put(msg, 1)) = (uint8_t)(arfcn & 0xFF);
    *(msgb_put(msg, 1)) = 0x00;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Handover Command" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2B  ARFCN: " << COLOR_GREEN << arfcn
              << COLOR_RESET << COLOR_BLUE << "  BSIC: " << COLOR_GREEN << (int)bsic << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Handover Complete (3GPP TS 44.018 §9.1.16)
// MS → BSC  MT=0x2C  — успешное завершение хэндовера в новой ячейке
// No mandatory IEs beyond the fixed header (optional RR cause)
static struct msgb *generate_dtap_rr_handover_complete(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR HO Compl");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_HANDO_COMPL;   // 0x2C
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Handover Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x2C  (MS → BSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Handover Failure (3GPP TS 44.018 §9.1.17)
// MS → BSC  MT=0x28  — отказ хэндовера (MS не смогла переключиться)
// Mandatory: RR Cause (1 byte)
static struct msgb *generate_dtap_rr_handover_failure(uint8_t rr_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR HO Fail");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_HANDO_FAIL;    // 0x28
    *(msgb_put(msg, 1)) = rr_cause;                   // RR Cause (M, 1 byte)
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Handover Failure" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x28  Cause: " << COLOR_GREEN << (int)rr_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Measurement Report (3GPP TS 44.018 §9.1.21)
// MS → BSC  MT=0x15  — периодический отчёт об измерениях уровня сигнала
// Mandatory: Measurement Results (17 bytes fixed)
static struct msgb *generate_dtap_rr_measurement_report(uint8_t rxlev_full, uint8_t rxqual_full) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR MeasRep");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_MEAS_REP;      // 0x15
    // Measurement Results: 17 bytes (TS 44.018 §10.5.2.20)
    // Simplified: BA-USED=0, DTX=0, RXLEV-FULL, RXQUAL-FULL, NO-NCELL=0
    uint8_t mr[17] = {0};
    mr[0] = (uint8_t)((rxlev_full & 0x3F) << 2);     // RXLEV-FULL-SERVING-CELL
    mr[1] = (uint8_t)((rxqual_full & 0x07) << 5);     // RXQUAL-FULL-SERVING-CELL
    memcpy(msgb_put(msg, 17), mr, 17);
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Measurement Report" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x15  RXLEV: " << COLOR_GREEN << (int)rxlev_full
              << COLOR_RESET << COLOR_BLUE << "  RXQUAL: " << COLOR_GREEN << (int)rxqual_full << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Channel Mode Modify (3GPP TS 44.018 §9.1.8)
// BSC → MS  MT=0x10  — изменение режима канала (напр. FR→EFR или speech→data)
// Mandatory: Channel Description (3B) + Channel Mode (1B)
static struct msgb *generate_dtap_rr_channel_mode_modify(uint8_t ts, uint8_t chan_mode) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR ChanModeModify");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = GSM48_MT_RR_CHAN_MODE_MODIF;   // 0x10
    // Channel Description 2: type 3, IEI=0x64, len=3 implied
    *(msgb_put(msg, 1)) = 0x64;
    *(msgb_put(msg, 1)) = 0x03;
    *(msgb_put(msg, 1)) = (uint8_t)((0x01 << 3) | (ts & 0x07)); // TCH/F + TS
    *(msgb_put(msg, 1)) = 0x64;  // ARFCN=100
    *(msgb_put(msg, 1)) = 0x00;
    // Channel Mode: IEI=0x63, len=1, mode (0x01=speech FR, 0x21=speech EFR)
    *(msgb_put(msg, 1)) = 0x63; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = chan_mode;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Channel Mode Modify" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x10  TS: " << COLOR_GREEN << (int)ts
              << COLOR_RESET << COLOR_BLUE << "  ChanMode: " << COLOR_GREEN << (int)chan_mode << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ─────────────────────────────────────────────────────────────────────────────
// P31: DTAP RR Ciphering Mode Command / Complete / Reject + Immediate Assignment
// ─────────────────────────────────────────────────────────────────────────────

// DTAP RR Ciphering Mode Command (3GPP TS 44.018 §9.1.9)
// BSC → MS  MT=0x35  — команда включить шифрование
// Mandatory: Cipher Mode Setting (1 byte): bit0=start cipher, bits1-3=algo (1=A5/1)
// Mandatory: Cipher Response (1 byte): 0=no IMEISV required, 1=required
static struct msgb *generate_dtap_rr_ciphering_mode_command(uint8_t cipher_alg) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR CiphCmd");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = 0x35;   // GSM48_MT_RR_CIPH_M_CMD
    // Cipher Mode Setting: bit0=1 (start cipher), bits3-1=A5/alg (1=A5/1)
    *(msgb_put(msg, 1)) = (uint8_t)((cipher_alg & 0x07) << 1 | 0x01);
    // Cipher Response: 0 = do not include IMEISV
    *(msgb_put(msg, 1)) = 0x00;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Ciphering Mode Command" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x35  CipherAlg: A5/" << COLOR_GREEN << (int)cipher_alg << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Ciphering Mode Complete (3GPP TS 44.018 §9.1.10)
// MS → BSC  MT=0x32  — подтверждение включения шифрования
// No mandatory IEs
static struct msgb *generate_dtap_rr_ciphering_mode_complete(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR CiphCompl");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = 0x32;   // GSM48_MT_RR_CIPH_M_COMPL
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Ciphering Mode Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x32  (MS → BSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Ciphering Mode Reject (3GPP TS 44.018 §9.1.11)
// MS → BSC  MT=0x34  — отклонение команды шифрования
// Mandatory: RR Cause (1 byte)
static struct msgb *generate_dtap_rr_ciphering_mode_reject(uint8_t rr_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR CiphRej");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = 0x34;   // GSM48_MT_RR_CIPH_M_REJ
    *(msgb_put(msg, 1)) = rr_cause;   // RR Cause (M, 1 byte)
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Ciphering Mode Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x34  Cause: " << COLOR_GREEN << (int)rr_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP RR Immediate Assignment (3GPP TS 44.018 §9.1.18)
// BSC → MS  MT=0x3F  — выделение канала трафика (ответ на Channel Request)
// Fixed: Ded.Mode+ChanDesc(1+3B) + Req.Ref(3B) + TimingAdv(1B) + MobAlloc(2B)
static struct msgb *generate_dtap_rr_immediate_assignment(void) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP RR ImmAssign");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_RR;
    *(msgb_put(msg, 1)) = 0x3F;   // GSM48_MT_RR_IMM_ASS
    // Dedicated mode / info: 0x00 (not dedicated, no hopping)
    *(msgb_put(msg, 1)) = 0x00;
    // Channel Description: 3 bytes (TCH/F, TS0, ARFCN=100 dummy)
    *(msgb_put(msg, 1)) = 0x08;   // TCH/F (Channel type+TN=TS0)
    *(msgb_put(msg, 1)) = 0x64;   // ARFCN upper + BSIC
    *(msgb_put(msg, 1)) = 0x00;   // ARFCN lower
    // Request Reference: 3 bytes (RA + T1+T3 + T2)
    *(msgb_put(msg, 1)) = 0x7B;   // Random Access octet
    *(msgb_put(msg, 1)) = 0x40;   // T1' + T3
    *(msgb_put(msg, 1)) = 0x00;   // T2
    // Timing Advance: 0 (0 bit TA)
    *(msgb_put(msg, 1)) = 0x00;
    // Mobile Allocation: length=0 (no hopping)
    *(msgb_put(msg, 1)) = 0x00;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP RR Immediate Assignment" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  MT=0x3F  (BSC → MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ============================================================
// P9: A-interface DTAP CC / MM — вызов (Call Control)
// ============================================================

// Вспомогательная: закодировать BCDNumber для CC IE (05.08-формат)
// Возвращает длину заполненного буфера bcd_out
static uint8_t encode_cc_bcd_number(uint8_t *bcd_out, const char *num_str) {
    // IE содержит: type_of_number(0x81=E.164 international) + BCD-цифры
    size_t len = strlen(num_str);
    bcd_out[0] = 0x81;  // ext=1, TON=0 (unknown), NPI=1 (E.164)
    for (size_t i = 0; i < len; i += 2) {
        uint8_t lo = (uint8_t)(num_str[i] - '0');
        uint8_t hi = (i + 1 < len) ? (uint8_t)(num_str[i + 1] - '0') : 0x0F;
        bcd_out[1 + i / 2] = (uint8_t)((hi << 4) | lo);
    }
    return (uint8_t)(1 + (len + 1) / 2);
}

// MM CM Service Request (3GPP TS 24.008 §9.2.9)
// MS → MSC   MM 0x24  — запрос сервиса перед MO-вызовом
// cm_svc_type: 1=MO-call 2=emergency 4=SMS 8=supplementary
static struct msgb *generate_dtap_mm_cm_service_req(const char *imsi_str,
                                                     uint8_t cm_svc_type) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CM SrvReq");
    if (!msg) return nullptr;

    // PD=MM, TI=0
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = GSM48_MT_MM_CM_SERV_REQ;

    // CM Service Type + Ciphering Key Seq Number (half-octets)
    *(msgb_put(msg, 1)) = (uint8_t)((cm_svc_type & 0x0F) | 0x70);  // CKSN=7 (no key)

    // MS Classmark 2 IE (tag=0x33, len=3, minimal)
    *(msgb_put(msg, 1)) = 0x33;  // IE tag
    *(msgb_put(msg, 1)) = 0x03;  // length
    *(msgb_put(msg, 1)) = 0x53;  // Classmark2[0]: rev2, A5/1
    *(msgb_put(msg, 1)) = 0x59;  // Classmark2[1]: PS capability, freq capability
    *(msgb_put(msg, 1)) = 0x00;  // Classmark2[2]

    // Mobile Identity IE (IMSI): tag=0x17
    *(msgb_put(msg, 1)) = 0x17;
    uint8_t bcd[9] = {};
    int imsi_len = generate_bcd_number(bcd, sizeof(bcd), imsi_str);
    if (imsi_len <= 0) { msgb_free(msg); return nullptr; }
    *(msgb_put(msg, 1)) = (uint8_t)(imsi_len + 1);  // len: 1 (type) + packed BCD
    *(msgb_put(msg, 1)) = (uint8_t)((0x01 << 4) | 0x09);  // IMSI + odd nibble fix
    memcpy(msgb_put(msg, imsi_len), bcd, imsi_len);

    const char *svc_name = (cm_svc_type == 1) ? "MO-call"
                         : (cm_svc_type == 2) ? "Emergency call"
                         : (cm_svc_type == 4) ? "SMS"
                         : (cm_svc_type == 8) ? "Supplementary" : "Unknown";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CM Service Request" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:    " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Service: " << COLOR_GREEN << (int)cm_svc_type
              << " (" << svc_name << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// MM CM Service Accept (3GPP TS 24.008 §9.2.8)
// MSC → MS   MM 0x21  — подтверждение сервиса
static struct msgb *generate_dtap_mm_cm_service_acc() {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CM SrvAcc");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;
    *(msgb_put(msg, 1)) = GSM48_MT_MM_CM_SERV_ACC;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CM Service Accept" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// ── P21-R: DTAP MM CM Service Reject (3GPP TS 24.008 §9.2.10) ───────────────
// MSC → MS   PD=MM(0x05), MT=0x22
// Reject Cause IE: tag=0x08, len=1
//   0x04=IMSI unknown in HLR  0x06=illegal ME  0x0C=PLMN not allowed
//   0x11=network failure       0x16=congestion
static struct msgb *generate_dtap_mm_cm_service_reject(uint8_t cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CM SrvRej");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;  // PD = MM 0x05
    *(msgb_put(msg, 1)) = 0x22;            // MT = CM Service Reject
    // Reject Cause IE (mandatory, non-TLV format in 24.008 §10.5.3.11)
    *(msgb_put(msg, 1)) = cause;
    const char *cause_name =
        (cause == 0x04) ? "IMSI unknown in HLR" :
        (cause == 0x06) ? "Illegal ME" :
        (cause == 0x0C) ? "PLMN not allowed" :
        (cause == 0x11) ? "Network failure" :
        (cause == 0x16) ? "Congestion" : "unknown";
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e DTAP CM Service Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN << std::hex << std::showbase
              << (int)cause << " (" << cause_name << ")" << std::dec << std::noshowbase
              << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    return msg;
}

// ── P21-R: DTAP MM IMSI Detach Indication (3GPP TS 24.008 §9.2.6) ───────────
// MS → MSC   PD=MM(0x05), MT=0x01
// Mobile Identity IE (IMSI): tag=0x17, len, type_byte, BCD...
static struct msgb *generate_dtap_mm_imsi_detach(const char *imsi_str) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP IMSI Detach");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = GSM48_PDISC_MM;  // PD = MM 0x05
    *(msgb_put(msg, 1)) = 0x01;            // MT = IMSI Detach Indication
    // MS Classmark 1 IE (non-TLV, 1 byte mandatory before Mobile ID)
    *(msgb_put(msg, 1)) = 0x50;  // Classmark1: RF power class 2, Rev1 (typical)
    // Mobile Identity IE (IMSI): tag=0x17
    *(msgb_put(msg, 1)) = 0x17;
    uint8_t bcd[9] = {};
    int imsi_len = generate_bcd_number(bcd, sizeof(bcd), imsi_str);
    if (imsi_len <= 0) { msgb_free(msg); return nullptr; }
    // type nibble 0x9 = IMSI, odd-length indicator in MSB nibble of first BCD byte
    *(msgb_put(msg, 1)) = (uint8_t)(imsi_len + 1);  // len: type_byte + BCD bytes
    *(msgb_put(msg, 1)) = (uint8_t)((0x01 << 4) | 0x09);  // odd nibble | IMSI type
    memcpy(msgb_put(msg, imsi_len), bcd, imsi_len);
    std::cout << COLOR_CYAN << "\u2713 \u0421\u0433\u0435\u043d\u0435\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043e DTAP IMSI Detach Indication" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  IMSI:   " << COLOR_GREEN << imsi_str << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  \u0420\u0430\u0437\u043c\u0435\u0440: " << COLOR_GREEN << msg->len << " \u0431\u0430\u0439\u0442" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Setup MO (3GPP TS 24.008 §9.3.23.1)
// MS → MSC   PD=CC(0x03), TI=0, MT=0x05
// Содержит: Bearer Capability IE + Called Party BCD Number IE
static struct msgb *generate_dtap_cc_setup_mo(uint8_t ti, const char *called_num) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Setup MO");
    if (!msg) return nullptr;

    // PD=CC, TI flag=0 (MS→net), TI value=ti
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_SETUP;

    // Bearer Capability IE: tag=0x04, len=2 (speech, FR)
    *(msgb_put(msg, 1)) = 0x04;  // IE tag
    *(msgb_put(msg, 1)) = 0x02;  // IE length
    *(msgb_put(msg, 1)) = 0x80;  // ext=1, coding=CCITT, radio ch=FR, speech
    *(msgb_put(msg, 1)) = 0x21;  // ext=1, layer1=G.711 µ-law (speech V1)

    // Called Party BCD Number IE: tag=0x5C
    uint8_t bcd_num[20] = {};
    uint8_t bcd_len = encode_cc_bcd_number(bcd_num, called_num);
    *(msgb_put(msg, 1)) = 0x5C;
    *(msgb_put(msg, 1)) = bcd_len;
    memcpy(msgb_put(msg, bcd_len), bcd_num, bcd_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Setup (MO)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Called: " << COLOR_GREEN << called_num << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Setup MT (3GPP TS 24.008 §9.3.23.2)
// MSC → MS   PD=CC(0x03), TI flag=1 (net→MS), TI=ti, MT=0x05
// Содержит: Bearer Capability IE + Calling Party BCD Number IE (опц.)
static struct msgb *generate_dtap_cc_setup_mt(uint8_t ti, const char *calling_num) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Setup MT");
    if (!msg) return nullptr;

    // PD=CC, TI flag=1 (network→MS), TI value=ti
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_SETUP;

    // Bearer Capability IE
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x80;
    *(msgb_put(msg, 1)) = 0x21;

    // Calling Party BCD Number IE: tag=0x5E
    uint8_t bcd_num[20] = {};
    uint8_t bcd_len = encode_cc_bcd_number(bcd_num, calling_num);
    *(msgb_put(msg, 1)) = 0x5E;
    *(msgb_put(msg, 1)) = (uint8_t)(bcd_len + 1);  // +1 для поля presentation
    *(msgb_put(msg, 1)) = 0x00;  // presentation=allowed, screening=user provided
    memcpy(msgb_put(msg, bcd_len), bcd_num, bcd_len);

    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Setup (MT)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:      " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Calling: " << COLOR_GREEN << calling_num << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Emergency Setup (3GPP TS 24.008 §9.3.8a)
// MS → MSC   CC 0x0E  — экстренный вызов (без SETUP, без TI negotiation)
// Bearer Capability IE: tag=0x04, len=3 (speech, FR, A5)
static struct msgb *generate_dtap_cc_emergency_setup(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC EmergSetup");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_EMERG_SETUP;
    // Bearer Capability IE (mandatory): tag=0x04, len=2
    // octet3: ext=1, radio_channel_req=1(full), coding=0, transfer_mode=0, info_transfer_cap=000(speech)
    // octet4: ext=1, layer1_id=01, speech_coding=001(FR v1)
    *(msgb_put(msg, 1)) = 0x04;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x60;   // ext=1, FR required, speech
    *(msgb_put(msg, 1)) = 0x81;   // ext=1, layer1=speech, FR v1
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Emergency Setup" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (MS→MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Bearer: " << COLOR_GREEN << "Speech / FR v1" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Call Proceeding (3GPP TS 24.008 §9.3.2)
// MSC → MS   CC 0x02  — вызов принят к обработке
static struct msgb *generate_dtap_cc_call_proceeding(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Call Proc");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_CALL_PROC;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Call Proceeding" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Alerting (3GPP TS 24.008 §9.3.1)
// MSC → MS   CC 0x01  — вызываемый абонент оповещён (звонит)
static struct msgb *generate_dtap_cc_alerting(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Alerting");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_ALERTING;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Alerting" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Progress (3GPP TS 24.008 §9.3.15)
// MSC → MS   CC 0x03  — MSC передаёт информацию о ходе вызова (in-band info)
// Progress IE: tag=0x1E, len=2 (location + progress description)
// progress_desc: 0x08=inband information available, 0x01=call not end-to-end ISDN
static struct msgb *generate_dtap_cc_progress(uint8_t ti, uint8_t progress_desc) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Progress");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_PROGRESS;
    // Progress IE: tag=0x1E, len=2
    *(msgb_put(msg, 1)) = 0x1E;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x82;   // ext=1, coding=CCITT, location=public-network-local-user
    *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (progress_desc & 0x7F));
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Progress" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:       " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Progress: " << COLOR_GREEN
              << "0x" << std::hex << (int)progress_desc << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:   " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Call Confirmed (3GPP TS 24.008 §9.3.2)
// MS → MSC   CC 0x08  — MS подтверждает получение CC Setup (Mobile Terminating Call)
static struct msgb *generate_dtap_cc_call_confirmed(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC CallConf");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_CALL_CONF;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Call Confirmed" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (MS→MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Connect (3GPP TS 24.008 §9.3.5)
// MS → MSC / MSC → MS   CC 0x07  — ответ (вызов установлен)
// net_to_ms=true: MSC→MS (TI flag=1), false: MS→MSC (TI flag=0)
static struct msgb *generate_dtap_cc_connect(uint8_t ti, bool net_to_ms) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Connect");
    if (!msg) return nullptr;
    uint8_t ti_flag = net_to_ms ? 0x80 : 0x00;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ti_flag | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_CONNECT;
    const char *dir = net_to_ms ? "MSC→MS" : "MS→MSC";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Connect" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (" << dir << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Connect Acknowledge (3GPP TS 24.008 §9.3.6)
// MSC → MS   CC 0x0F  — подтверждение ответа
static struct msgb *generate_dtap_cc_connect_ack(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC ConnAck");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_CONNECT_ACK;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Connect Acknowledge" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Start DTMF (3GPP TS 24.008 §9.3.24)
// MS → MSC   CC 0x35  — MS запрашивает передачу тонального набора (DTMF)
// Keypad Facility IE: tag=0x2C, len=1, digit='0'-'9','*','#','A'-'D'
static struct msgb *generate_dtap_cc_start_dtmf(uint8_t ti, char digit) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC StartDTMF");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_START_DTMF;
    // Keypad Facility IE: tag=0x2C, len=1
    *(msgb_put(msg, 1)) = 0x2C;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = (uint8_t)digit;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Start DTMF" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (MS→MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Digit:  " << COLOR_GREEN << digit    << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Stop DTMF (3GPP TS 24.008 §9.3.25)
// MS → MSC   CC 0x31  — MS сообщает об окончании тонового набора
static struct msgb *generate_dtap_cc_stop_dtmf(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC StopDTMF");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_STOP_DTMF;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Stop DTMF" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (MS→MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Status (3GPP TS 24.008 §9.3.31)
// MS ↔ MSC   CC 0x3D  — индикация текущего состояния CC в одной стороне
// Обязательные IE: Cause + Call State
// call_state: 0x00=null 0x01=call-initiated 0x03=mobile-originating-call-proc
//             0x04=call-delivered 0x06=call-present 0x07=call-received
//             0x08=connect-request 0x09=mobile-terminating-call-conf 0x0A=active
static struct msgb *generate_dtap_cc_status(uint8_t ti, bool net_to_ms,
                                             uint8_t cc_cause, uint8_t call_state) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Status");
    if (!msg) return nullptr;
    uint8_t ti_flag = net_to_ms ? 0x80 : 0x00;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ti_flag | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_STATUS;
    // Cause IE: tag=0x08, len=2
    *(msgb_put(msg, 1)) = 0x08;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x80;   // ext=1, coding=CCITT, location=user
    *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (cc_cause & 0x7F));
    // Call State IE: tag=0xA4, len=1 (half-octet, no explicit length)
    *(msgb_put(msg, 1)) = 0xA4;
    *(msgb_put(msg, 1)) = (uint8_t)(call_state & 0x3F);
    const char *dir = net_to_ms ? "MSC→MS" : "MS→MSC";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Status" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:         " << COLOR_GREEN << (int)ti << " (" << dir << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:      " << COLOR_GREEN << (int)cc_cause    << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Call State: " << COLOR_GREEN
              << "0x" << std::hex << (int)call_state << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:     " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Disconnect (3GPP TS 24.008 §9.3.7)
// MS → MSC / MSC → MS   CC 0x25  — инициирование разъединения
// Обязательный IE: Cause (tag=0x08)
// Q.850 CC causes: 16=normalClearing 17=userBusy 18=noAnswer 19=noUserResponding
//                  20=subscriberAbsent 21=callRejected 31=normalUnspecified
static struct msgb *generate_dtap_cc_disconnect(uint8_t ti, bool net_to_ms,
                                                 uint8_t cc_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Disconnect");
    if (!msg) return nullptr;
    uint8_t ti_flag = net_to_ms ? 0x80 : 0x00;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ti_flag | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_DISCONNECT;
    // Cause IE: tag=0x08, len=2, location=user(0x80), cause=cc_cause
    *(msgb_put(msg, 1)) = 0x08;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x80;             // ext=1, coding=CCITT, location=user
    *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (cc_cause & 0x7F));  // ext=1 + Q.850 cause
    const char *dir = net_to_ms ? "MSC→MS" : "MS→MSC";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Disconnect" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (" << dir << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN << (int)cc_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Hold (3GPP TS 24.008 §9.3.10)
// MS → MSC   CC 0x18  — MS запрашивает постановку активного вызова на удержание
static struct msgb *generate_dtap_cc_hold(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Hold");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_HOLD;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Hold" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI: " << COLOR_GREEN << (int)ti << " (MS→MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Hold Acknowledge (3GPP TS 24.008 §9.3.11)
// MSC → MS   CC 0x19  — сеть подтверждает удержание вызова
static struct msgb *generate_dtap_cc_hold_ack(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC HoldAck");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_HOLD_ACK;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Hold Acknowledge" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI: " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Hold Reject (3GPP TS 24.008 §9.3.12)
// MSC → MS   CC 0x1A  — сеть отклоняет запрос на удержание; содержит Cause IE
static struct msgb *generate_dtap_cc_hold_reject(uint8_t ti, uint8_t cc_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC HoldRej");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_HOLD_REJ;
    // Cause IE: tag=0x08, len=2
    *(msgb_put(msg, 1)) = 0x08;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x80;
    *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (cc_cause & 0x7F));
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Hold Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:    " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << (int)cc_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Retrieve (3GPP TS 24.008 §9.3.20)
// MS → MSC   CC 0x1C  — MS запрашивает снятие вызова с удержания
static struct msgb *generate_dtap_cc_retrieve(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Retrieve");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_RETR;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Retrieve" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI: " << COLOR_GREEN << (int)ti << " (MS→MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Retrieve Acknowledge (3GPP TS 24.008 §9.3.21)
// MSC → MS   CC 0x1D  — сеть подтверждает снятие с удержания
static struct msgb *generate_dtap_cc_retrieve_ack(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC RetrAck");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_RETR_ACK;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Retrieve Acknowledge" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI: " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Retrieve Reject (3GPP TS 24.008 §9.3.22)
// MSC → MS   CC 0x1E  — сеть не может снять вызов с удержания; содержит Cause IE
static struct msgb *generate_dtap_cc_retrieve_reject(uint8_t ti, uint8_t cc_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC RetrRej");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_RETR_REJ;
    // Cause IE: tag=0x08, len=2
    *(msgb_put(msg, 1)) = 0x08;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x80;
    *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (cc_cause & 0x7F));
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Retrieve Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:    " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << (int)cc_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Modify (3GPP TS 24.008 §9.3.13)
// MS → MSC   CC 0x17  — MS запрашивает изменение codec/bearer в активном вызове
// Bearer Capability IE обязателен
static struct msgb *generate_dtap_cc_modify(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Modify");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_MODIFY;
    // Bearer Capability IE: tag=0x04, len=2 (speech, FR v1)
    *(msgb_put(msg, 1)) = 0x04; *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x60; *(msgb_put(msg, 1)) = 0x81;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Modify" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI: " << COLOR_GREEN << (int)ti << " (MS→MSC)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Bearer: " << COLOR_GREEN << "Speech / FR v1" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Modify Complete (3GPP TS 24.008 §9.3.14)
// MSC → MS   CC 0x1F  — сеть подтверждает изменение codec
// Bearer Capability IE обязателен
static struct msgb *generate_dtap_cc_modify_complete(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC ModifyCompl");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_MODIFY_COMPL;
    *(msgb_put(msg, 1)) = 0x04; *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x60; *(msgb_put(msg, 1)) = 0x81;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Modify Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI: " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Modify Reject (3GPP TS 24.008 §9.3.15)
// MSC → MS   CC 0x13  — сеть отклоняет изменение codec; Cause IE обязателен
static struct msgb *generate_dtap_cc_modify_reject(uint8_t ti, uint8_t cc_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC ModifyRej");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_MODIFY_REJECT;
    // Bearer Capability IE (dummy, same as modify request)
    *(msgb_put(msg, 1)) = 0x04; *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x60; *(msgb_put(msg, 1)) = 0x81;
    // Cause IE: tag=0x08, len=2
    *(msgb_put(msg, 1)) = 0x08; *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x80; *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (cc_cause & 0x7F));
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Modify Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:    " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << (int)cc_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Notify (3GPP TS 24.008 §9.3.18)
// MS ↔ MSC   CC 0x3E  — уведомление о дополнительных услугах (call forwarded, etc.)
// Notification Indicator IE обязателен
static struct msgb *generate_dtap_cc_notify(uint8_t ti, uint8_t notification) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Notify");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_NOTIFY;    // 0x3E
    // Notification Indicator: 1 byte, bit7=ext=1
    *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (notification & 0x7F));
    const char *ns = (notification==0x00)?"User suspended":
                     (notification==0x01)?"User resumed":
                     (notification==0x02)?"Bearer change":
                     (notification==0x40)?"Call is forwarded":"Other";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Notify" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:   " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Notif: " << COLOR_GREEN << ns << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Start DTMF Acknowledge (3GPP TS 24.008 §9.3.23)
// MSC → MS   CC 0x36  — подтверждение начала DTMF
// Keypad Facility IE содержит нажатую цифру
static struct msgb *generate_dtap_cc_start_dtmf_ack(uint8_t ti, char dtmf_key) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC StartDTMF-Ack");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_START_DTMF_ACK;   // 0x36
    // Keypad Facility IE: tag=0x2C, len=1, IA5 character
    *(msgb_put(msg, 1)) = 0x2C; *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = (uint8_t)(dtmf_key & 0x7F);
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Start DTMF Acknowledge" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:  " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Key: " << COLOR_GREEN << dtmf_key << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Start DTMF Reject (3GPP TS 24.008 §9.3.24)
// MSC → MS   CC 0x37  — отклонение запроса DTMF; Cause IE обязателен
static struct msgb *generate_dtap_cc_start_dtmf_rej(uint8_t ti, uint8_t cc_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC StartDTMF-Rej");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_START_DTMF_REJ;   // 0x37
    // Cause IE: tag=0x08, len=2
    *(msgb_put(msg, 1)) = 0x08; *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x80; *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (cc_cause & 0x7F));
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Start DTMF Reject" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:    " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause: " << COLOR_GREEN << (int)cc_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Stop DTMF Acknowledge (3GPP TS 24.008 §9.3.25)
// MSC → MS   CC 0x32  — подтверждение остановки DTMF; нет IE
static struct msgb *generate_dtap_cc_stop_dtmf_ack(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC StopDTMF-Ack");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_STOP_DTMF_ACK;   // 0x32
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Stop DTMF Acknowledge" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:    " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Status Enquiry (3GPP TS 24.008 §9.3.27)
// MS ↔ MSC   CC 0x34  — запрос состояния вызова (ответ: CC Status)
// Нет обязательных IE
static struct msgb *generate_dtap_cc_status_enquiry(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Status Enq");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_STATUS_ENQ;   // 0x34
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Status Enquiry" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI: " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC User Information (3GPP TS 24.008 §9.3.31)
// MS ↔ MSC   CC 0x10  — передача пользовательских данных (UUS)
// User-User IE обязателен
static struct msgb *generate_dtap_cc_user_info(uint8_t ti, uint8_t uus_proto, const char *data, uint8_t data_len) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC UserInfo");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_USER_INFO;    // 0x10
    // More Data IE (optional, 1 byte tag 0xA0 for MS→net)
    // User-User IE: tag=0x7E, len=1+data_len, proto, data
    uint8_t uu_len = (uint8_t)(1 + data_len);
    *(msgb_put(msg, 1)) = 0x7E; *(msgb_put(msg, 1)) = uu_len;
    *(msgb_put(msg, 1)) = uus_proto;
    if (data && data_len > 0)
        memcpy(msgb_put(msg, data_len), data, data_len);
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC User Information" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:    " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Proto: " << COLOR_GREEN << (int)uus_proto << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Congestion Control (3GPP TS 24.008 §9.3.4)
// MSC → MS   CC 0x39  — управление перегрузкой в вызове (congestion level)
// Congestion Level IE: half-octet, упакован в сам байт сообщения
static struct msgb *generate_dtap_cc_congestion(uint8_t ti, uint8_t cong_level) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Congestion");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_CONG_CTRL;    // 0x39
    // Congestion level: coded as 1 TV byte  tag=0xB0 | level (4 bits)
    *(msgb_put(msg, 1)) = (uint8_t)(0xB0 | (cong_level & 0x0F));
    const char *cls = (cong_level==0)?"receiver ready":(cong_level==0x0F)?"receiver not ready":"?";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Congestion Control" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:    " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Level: " << COLOR_GREEN << (int)cong_level << " (" << cls << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Facility (3GPP TS 24.008 §9.3.9)
// MS ↔ MSC  CC 0x3A  — передача Facility IE для SS-операций поверх CC
// Facility IE обязателен
static struct msgb *generate_dtap_cc_facility(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Facility");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_FACILITY;     // 0x3A
    // Facility IE: tag=0x1C, len=1, empty component (0xA1 end)
    *(msgb_put(msg, 1)) = 0x1C; *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0xA1; *(msgb_put(msg, 1)) = 0x00;  // empty RETURN RESULT
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Facility" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI: " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Recall (3GPP TS 24.008 §9.3.20a)
// MSC → MS  CC 0x0B  — сетевой вызов (CCBS/CCNR triggered recall)
// Recall type и Called BCD number обязательны
static struct msgb *generate_dtap_cc_recall(uint8_t ti, uint8_t recall_type) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Recall");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4)); // NW→MS
    *(msgb_put(msg, 1)) = GSM48_MT_CC_RECALL;       // 0x0B
    // Recall type: TV, tag=0xA0 | recall_type(3 bits)
    *(msgb_put(msg, 1)) = (uint8_t)(0xA0 | (recall_type & 0x07));
    // Called party BCD number IE: tag=0x5E, len, type+plan, digits
    *(msgb_put(msg, 1)) = 0x5E; *(msgb_put(msg, 1)) = 0x06;
    *(msgb_put(msg, 1)) = 0x91;  // ToN=international, NPI=ISDN/E.164
    // digits: 7916100000 (packed BCD)
    *(msgb_put(msg, 1)) = 0x97; *(msgb_put(msg, 1)) = 0x61; *(msgb_put(msg, 1)) = 0x00;
    *(msgb_put(msg, 1)) = 0x00; *(msgb_put(msg, 1)) = 0xF0;
    const char *rt = (recall_type==0)?"CCBS":((recall_type==2)?"CCNR":"Other");
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Recall" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Type:   " << COLOR_GREEN << rt << " (" << (int)recall_type << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Start CC (3GPP TS 24.008 §9.3.23a)
// MSC → MS  CC 0x09  — инициализация CC (CCBS trigger)
// CC Capabilities IE обязателен
static struct msgb *generate_dtap_cc_start_cc(uint8_t ti) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC StartCC");
    if (!msg) return nullptr;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | 0x80 | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_START_CC;     // 0x09
    // CC Capabilities: tag=0x15, len=2
    *(msgb_put(msg, 1)) = 0x15; *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x01;  // DTMF + PCP
    *(msgb_put(msg, 1)) = 0x00;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Start CC" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI: " << COLOR_GREEN << (int)ti << " (MSC→MS)" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Release (3GPP TS 24.008 §9.3.18)
// MS → MSC / MSC → MS   CC 0x2D  — освобождение канала
// Опц. IE: Cause (если есть причина), second Cause
static struct msgb *generate_dtap_cc_release(uint8_t ti, bool net_to_ms,
                                              uint8_t cc_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC Release");
    if (!msg) return nullptr;
    uint8_t ti_flag = net_to_ms ? 0x80 : 0x00;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ti_flag | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_RELEASE;
    // Cause IE (опц. в Release, но рекомендуется)
    *(msgb_put(msg, 1)) = 0x08;
    *(msgb_put(msg, 1)) = 0x02;
    *(msgb_put(msg, 1)) = 0x80;
    *(msgb_put(msg, 1)) = (uint8_t)(0x80 | (cc_cause & 0x7F));
    const char *dir = net_to_ms ? "MSC→MS" : "MS→MSC";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Release" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (" << dir << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Cause:  " << COLOR_GREEN << (int)cc_cause << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP CC Release Complete (3GPP TS 24.008 §9.3.19)
// MS → MSC / MSC → MS   CC 0x2A  — подтверждение Release
static struct msgb *generate_dtap_cc_release_complete(uint8_t ti, bool net_to_ms) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP CC RelCompl");
    if (!msg) return nullptr;
    uint8_t ti_flag = net_to_ms ? 0x80 : 0x00;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_CC | ti_flag | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = GSM48_MT_CC_RELEASE_COMPL;
    const char *dir = net_to_ms ? "MSC→MS" : "MS→MSC";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP CC Release Complete" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (" << dir << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}


// ============================================================
// P23: DTAP SMS CP — Control Protocol (3GPP TS 24.011 §7.3)
// PD = 0x09 (GSM48_PDISC_SMS), TI=1..7
// ============================================================

// DTAP SMS CP-DATA (3GPP TS 24.011 §7.3.1)
// MS ↔ MSC   SMS 0x01  — перенос RPDU (RP-DATA) внутри CM layer
// CP-User Data IE содержит RP-PDU (здесь — заглушка из 1 байта)
static struct msgb *generate_dtap_sms_cp_data(uint8_t ti, bool net_to_ms) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP SMS CP-DATA");
    if (!msg) return nullptr;
    uint8_t ti_flag = net_to_ms ? 0x80 : 0x00;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_SMS | ti_flag | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = 0x01;   // CP-DATA
    // CP-User Data IE: tag=0x01, len=1, data=0x00 (minimal RP-PDU placeholder)
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = 0x00;
    const char *dir = net_to_ms ? "MSC→MS" : "MS→MSC";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP SMS CP-DATA" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (" << dir << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP SMS CP-ACK (3GPP TS 24.011 §7.3.2)
// MS ↔ MSC   SMS 0x04  — подтверждение приёма CP-DATA или CP-ERROR
static struct msgb *generate_dtap_sms_cp_ack(uint8_t ti, bool net_to_ms) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP SMS CP-ACK");
    if (!msg) return nullptr;
    uint8_t ti_flag = net_to_ms ? 0x80 : 0x00;
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_SMS | ti_flag | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = 0x04;   // CP-ACK
    const char *dir = net_to_ms ? "MSC→MS" : "MS→MSC";
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP SMS CP-ACK" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:     " << COLOR_GREEN << (int)ti << " (" << dir << ")" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер: " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
    return msg;
}

// DTAP SMS CP-ERROR (3GPP TS 24.011 §7.3.3)
// MS ↔ MSC   SMS 0x10  — индикация ошибки в CP-layer
// CP-Cause: 17=network failure 22=congestion 81=invalid TI 95=semantically incorrect
static struct msgb *generate_dtap_sms_cp_error(uint8_t ti, uint8_t cp_cause) {
    struct msgb *msg = msgb_alloc_headroom(512, 128, "DTAP SMS CP-ERROR");
    if (!msg) return nullptr;
    // TI flag: in CP-ERROR the TI mirrors the received TI (same direction inversion)
    *(msgb_put(msg, 1)) = (uint8_t)(GSM48_PDISC_SMS | ((ti & 0x07) << 4));
    *(msgb_put(msg, 1)) = 0x10;   // CP-ERROR
    // CP-Cause IE: tag=0x10, len=1
    *(msgb_put(msg, 1)) = 0x10;
    *(msgb_put(msg, 1)) = 0x01;
    *(msgb_put(msg, 1)) = cp_cause;
    std::cout << COLOR_CYAN << "✓ Сгенерировано DTAP SMS CP-ERROR" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  TI:      " << COLOR_GREEN << (int)ti << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  CP Cause: " << COLOR_GREEN
              << "0x" << std::hex << (int)cp_cause << std::dec << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "  Размер:  " << COLOR_GREEN << msg->len << " байт" << COLOR_RESET << "\n\n";
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

    // ── Загрузка конфигурации ────────────────────────────────────────────────
    // Порядок автопоиска (каждый следующий ПЕРЕКРЫВАЕТ предыдущий):
    //   1. ./vmsc.conf  или  ../vmsc.conf  или  ~/.vmsc.conf
    //   2. ./vmsc_interfaces.conf  или  ../vmsc_interfaces.conf
    //   3. Явные --config FILE (повторяются столько раз, сколько нужно)
    //
    // Пример: ./vmsc --config base.conf --config lab_interfaces.conf
    //         (второй файл перекрывает только те ключи, которые в нём есть)
    // ────────────────────────────────────────────────────────────────────────

    // Вспомогательная функция: ищет файл в ./  и ../
    auto find_file = [](const std::string &name) -> std::string {
        if (std::ifstream("./" + name).good())   return "./" + name;
        if (std::ifstream("../" + name).good())  return "../" + name;
        const char *home = getenv("HOME");
        if (home) {
            std::string hp = std::string(home) + "/" + name;
            if (std::ifstream(hp).good()) return hp;
        }
        return "";
    };

    Config cfg;
    std::vector<std::string> loaded_configs;  // журнал всех загруженных файлов

    // Шаг 1: базовый конфиг vmsc.conf
    {
        std::string p = find_file("vmsc.conf");
        if (!p.empty() && load_config(p, cfg)) loaded_configs.push_back(p);
    }
    // Шаг 2: файл интерфейсов vmsc_interfaces.conf (если есть)
    {
        std::string p = find_file("vmsc_interfaces.conf");
        if (!p.empty() && load_config(p, cfg)) loaded_configs.push_back(p);
    }

    std::string config_path = loaded_configs.empty() ? "" : loaded_configs.back();
    bool config_loaded = !loaded_configs.empty();

    std::string imsi   = cfg.imsi;
    std::string msisdn = cfg.msisdn;  // MSISDN: номер абонента, привязан к IMSI
    uint16_t mcc = cfg.mcc;
    uint16_t mnc = cfg.mnc;
    uint16_t lac = cfg.lac;
    bool do_lu = true;
    bool do_paging = true;
    bool do_map_sai          = false;  // MAP SendAuthenticationInfo (C-interface, MSC→HLR)
    bool do_map_ul           = false;  // MAP UpdateLocation         (C-interface, MSC→HLR)
    bool do_map_check_imei   = false;  // MAP CheckIMEI              (F-interface, MSC→EIR)
    bool do_map_prepare_ho   = false;  // MAP PrepareHandover        (E-interface, MSC→MSC)
    bool do_map_end_signal   = false;  // MAP SendEndSignal          (E-interface, MSC→MSC)
    bool do_bssap_gs_lu      = false;  // BSSAP+ LocationUpdate      (Gs-interface, MSC→SGSN)
    // P10: Gs-interface BSSAP+ Paging / Detach / Reset
    // P12: E-interface MAP handover supplementary
    bool do_map_prep_subsequent_ho  = false;  // MAP PrepSubsequentHandover  opCode=69  (E-interface)
    bool do_map_process_access_sig  = false;  // MAP ProcessAccessSignalling opCode=33  (E-interface)
    // P25: MAP E/C-interface supplementary
    bool do_map_send_identification  = false; // MAP SendIdentification  opCode=55 (E-interface VLR→VLR)
    bool do_map_restore_data         = false; // MAP RestoreData         opCode=57 (C-interface VLR→HLR)
    bool do_map_forward_check_ss     = false; // MAP ForwardCheckSS-Ind  opCode=38 (C-interface HLR→VLR)
    // P26: MAP subscriber presence / info
    bool do_map_note_subscriber_present = false; // MAP NoteSubscriberPresent opCode=48 (C-interface VLR→HLR)
    bool do_map_ready_for_sm            = false; // MAP ReadyForSM            opCode=66 (C-interface VLR→HLR)
    bool do_map_provide_subscriber_info = false; // MAP ProvideSubscriberInfo opCode=70 (C-interface HLR→VLR)
    bool do_map_send_imsi               = false; // MAP SendIMSI              opCode=58 (C-interface VLR→HLR)
    std::string msisdn_param = "79161234567";    // --msisdn: for send-imsi
    // P27: MAP check_imei_res / any_time_modification / activate_trace_mode
    bool do_map_check_imei_res          = false; // MAP CheckIMEI Result       opCode=43 (F-interface EIR→MSC)
    bool do_map_any_time_modification   = false; // MAP AnyTimeModification    opCode=65 (C-interface MSC→HLR)
    bool do_map_activate_trace_mode     = false; // MAP ActivateTraceMode      opCode=50 (C-interface HLR→VLR)
    // P28: MAP NotifySubscriberData / ForwardAccessSignalling
    bool do_map_notify_subscriber_data  = false; // MAP NotifySubscriberData   opCode=120 (C-interface HLR→VLR)
    bool do_map_forward_access_signalling = false; // MAP ForwardAccessSignalling opCode=33 (E-interface MSC→MSC)
    // P29: MAP UpdateGPRSLocation / SendRoutingInfoForGPRS / Reset / BeginSubscriberActivity
    bool do_map_update_gprs_location      = false; // MAP UpdateGPRSLocation       opCode=68 (Gs-iface SGSN→HLR)
    bool do_map_send_routeing_info_gprs   = false; // MAP SendRoutingInfoForGPRS   opCode=24 (Gs-iface SGSN↔HLR)
    bool do_map_reset                     = false; // MAP Reset                    opCode=37 (C-iface HLR→VLR)
    bool do_map_begin_subscriber_activity = false; // MAP BeginSubscriberActivity  opCode=131 (C-iface VLR→HLR)
    // P30: MAP DeactivateTraceMode / ProcessUSSD / UnstructuredSSReq / SubDataMod
    bool do_map_deactivate_trace_mode             = false; // MAP DeactivateTraceMode           opCode=51 (C-iface)
    bool do_map_process_unstructured_ss_req       = false; // MAP ProcessUnstructuredSS-Req     opCode=59 (A-iface)
    bool do_map_unstructured_ss_request           = false; // MAP UnstructuredSS-Request        opCode=60 (C-iface)
    bool do_map_subscriber_data_modification      = false; // MAP SubscriberDataModification    opCode=100 (C-iface)
    bool do_map_any_time_interrogation   = false; // MAP AnyTimeInterrogation  opCode=71 (C-iface)
    bool do_map_note_mm_event            = false; // MAP NoteMM-Event          opCode=99 (C-iface)
    bool do_map_inform_service_centre    = false; // MAP InformServiceCentre   opCode=63 (C-iface)
    bool do_map_alert_service_centre     = false; // MAP AlertServiceCentre    opCode=64 (C-iface)
    std::string ussd_string_param                 = "*100#"; // --ussd-string
    uint8_t  equip_status_param         = 0;     // --equip-status: 0=white 1=black 2=grey
    bool do_gs_paging        = false;  // BSSAP+ MS-Paging-Request   0x09
    bool do_gs_imsi_detach   = false;  // BSSAP+ IMSI-Detach-Ind     0x05
    bool do_gs_reset         = false;  // BSSAP+ Reset               0x0B
    bool do_gs_reset_ack     = false;  // BSSAP+ Reset-Acknowledge   0x0C
    uint8_t  gs_reset_cause  = 0;      // --gs-reset-cause: 0=power-on 1=om 2=load
    uint8_t  gs_detach_type  = 0;      // --gs-detach-type: 0=power-off 1=reattach 2=gprs
    std::string gs_peer_number = "";   // --gs-peer-number: E.164 VLR/SGSN number
    // P21: Gs-interface BSSAP+ SMS-related (3GPP TS 29.018)
    bool do_gs_ready_for_sm  = false;  // BSSAP+ Ready-For-SM    0x0E  SGSN→MSC
    bool do_gs_alert_sc      = false;  // BSSAP+ Alert-Request   0x0F  MSC→SGSN
    bool do_gs_alert_ack     = false;  // BSSAP+ Alert-Ack       0x10  SGSN→MSC
    bool do_gs_alert_reject  = false;  // BSSAP+ Alert-Reject    0x11  SGSN→MSC
    uint8_t  gs_alert_reason = 0;      // --gs-alert-reason: 0=MS-Present 1=MemCapExceeded
    uint8_t  gs_alert_cause  = 0x01;   // --gs-alert-cause: 0x01=IMSI-detached 0x03=unknown-sub
    // TCAP диалог: завершение и промежуточные шаги
    bool do_tcap_end         = false;  // TCAP End (ack, нет компонентов)     (C-interface)
    bool do_tcap_continue    = false;  // TCAP Continue (keepalive)            (C-interface)
    bool do_map_sai_end      = false;  // MAP SAI ReturnResultLast             (C-interface)
    bool do_map_ul_end       = false;  // MAP UL  ReturnResultLast             (C-interface)
    // P2: ключевые операции C-interface MAP
    bool do_map_sri          = false;  // MAP SendRoutingInfo      opCode=22   (C-interface, GMSC→HLR)
    bool do_map_prn          = false;  // MAP ProvideRoamingNumber opCode=4    (C-interface, GMSC→HLR)
    bool do_map_cl           = false;  // MAP CancelLocation       opCode=3    (C-interface, HLR→VLR)
    bool do_map_isd          = false;  // MAP InsertSubscriberData opCode=7    (C-interface, HLR→VLR)
    bool do_map_isd_res      = false;  // MAP ISD Result          opCode=7   (C-interface, VLR→HLR)
    uint32_t isd_res_dtid_param = 0x00000700;  // --dtid for --send-map-isd-res
    bool do_map_cl_res       = false;  // MAP CancelLocation Res  opCode=3   (C-interface, VLR→HLR)
    uint32_t cl_res_dtid_param  = 0x00000600;  // --dtid for --send-map-cl-res
    bool do_map_delete_sd    = false;  // MAP DeleteSubscriberData opCode=8  (C-interface, HLR→VLR)
    bool do_map_delete_sd_res= false;  // MAP DeleteSubscriberData Res       (C-interface, VLR→HLR)
    uint32_t dsd_res_dtid_param = 0x00000D00;  // --dtid for --send-map-delete-sd-res
    // P7: MAP admin — операции управления подписчиком после аутентификации
    bool do_map_purge_ms          = false;  // MAP PurgeMS               opCode=67  (C-interface, VLR→HLR)
    bool do_map_auth_failure_rpt  = false;  // MAP AuthFailureReport     opCode=15  (C-interface, VLR→HLR)
    uint8_t failure_cause_param   = 0;      // --failure-cause 0=wrongUserResponse 1=wrongNetworkSignature
    // TCAP ошибки: Abort и ReturnError
    bool do_tcap_abort       = false;  // TCAP Abort (P-Abort)                 (C-interface)
    bool do_map_return_error = false;  // MAP ReturnError в TCAP End           (C-interface)
    uint32_t dtid_param      = 0x00000001;  // --dtid: Destination TID для End/Continue/Abort
    uint32_t otid_param      = 0x00000001;  // --otid: Originating TID для Continue
    uint8_t  abort_cause_param  = 4;   // --abort-cause: P-AbortCause (4=resourceLimitation)
    uint8_t  invoke_id_param    = 1;   // --invoke-id:   Invoke ID для ReturnError
    uint8_t  error_code_param   = 6;   // --error-code:  MAP error code (6=unknownSubscriber)
    // ISUP (P3)
    bool     do_isup_iam  = false;     // ISUP Initial Address Message         (ISUP-interface)
    bool     do_isup_rel  = false;     // ISUP Release Message                 (ISUP-interface)
    uint16_t cic_param    = 1;         // --cic:   Circuit Identification Code
    uint8_t  cause_param  = 16;        // --cause: Q.850 cause (16=normalClearing)
    // ISUP call progress (P5)
    bool     do_isup_acm  = false;     // ISUP ACM Address Complete               (ISUP-interface)
    bool     do_isup_anm  = false;     // ISUP ANM Answer                          (ISUP-interface)
    bool     do_isup_rlc  = false;     // ISUP RLC Release Complete                (ISUP-interface)
    // ISUP supplementary (P11)
    bool     do_isup_con  = false;     // ISUP CON Connect                     (ISUP-interface)
    bool     do_isup_cpg  = false;     // ISUP CPG Call Progress               (ISUP-interface)
    bool     do_isup_sus  = false;     // ISUP SUS Suspend                     (ISUP-interface)
    bool     do_isup_res  = false;     // ISUP RES Resume                      (ISUP-interface)
    uint8_t  sus_cause_param = 0;      // --sus-cause: 0=network, 1=subscriber
    // P25: ISUP supplementary (overlap dialling + continuity + info)
    bool     do_isup_sam  = false;     // ISUP SAM Subsequent Address  MT=0x02  (ISUP-interface)
    bool     do_isup_ccr  = false;     // ISUP CCR Continuity Check Req MT=0x11 (ISUP-interface)
    bool     do_isup_cot  = false;     // ISUP COT Continuity          MT=0x05  (ISUP-interface)
    bool     do_isup_fot  = false;     // ISUP FOT Forward Transfer    MT=0x08  (ISUP-interface)
    bool     do_isup_inr  = false;     // ISUP INR Information Request MT=0x0A  (ISUP-interface)
    bool     do_isup_inf  = false;     // ISUP INF Information         MT=0x04  (ISUP-interface)
    bool     cot_success_param = true; // --cot-success / --cot-fail
    std::string sam_digits_param = "56789"; // --sam-digits
    // P26: ISUP error/test/overload
    bool     do_isup_cfn  = false;     // ISUP CFN Confusion           MT=0x2F  (ISUP-interface)
    bool     do_isup_ovl  = false;     // ISUP OVL Overload            MT=0x31  (ISUP-interface)
    bool     do_isup_upt  = false;     // ISUP UPT User Part Test      MT=0x34  (ISUP-interface)
    bool     do_isup_upa  = false;     // ISUP UPA User Part Available MT=0x35  (ISUP-interface)
    uint8_t  cfn_cause_param = 0x62;   // --cfn-cause (0x62=message not compatible with call state)
    // P27: ISUP FAR / IDR
    bool     do_isup_far  = false;     // ISUP FAR Facility               MT=0x1F  (ISUP-interface)
    bool     do_isup_idr  = false;     // ISUP IDR Identification Request  MT=0x3B  (ISUP-interface)
    // P28: ISUP IRS / LPA
    bool     do_isup_irs  = false;     // ISUP IRS Identification Response MT=0x3C (ISUP-interface)
    bool     do_isup_lpa  = false;     // ISUP LPA Loop Back Acknowledgement MT=0x24 (ISUP-interface)
    // P29: ISUP CQM / CQR / LOP / PAM
    bool     do_isup_cqm = false;     // ISUP CQM Circuit Group Query           MT=0x2C
    bool     do_isup_cqr = false;     // ISUP CQR Circuit Group Query Response  MT=0x2D
    bool     do_isup_lop = false;     // ISUP LOP Loop Prevention               MT=0x23
    bool     do_isup_pam = false;     // ISUP PAM Pass-Along                    MT=0x28
    // P30: ISUP NRM / OLM / EXM / APM
    bool     do_isup_nrm = false;     // ISUP NRM Network Resource Management  MT=0x32
    bool     do_isup_olm = false;     // ISUP OLM Overload                     MT=0x30
    bool     do_isup_exm = false;     // ISUP EXM Exit                         MT=0x31
    bool     do_isup_apm = false;     // ISUP APM Application Transport        MT=0x63
    bool     do_isup_faa = false;  // ISUP FAA  MT=0x20
    bool     do_isup_frj = false;  // ISUP FRJ  MT=0x21
    bool     do_isup_crm = false;  // ISUP CRM  MT=0x1C
    bool     do_isup_cra = false;  // ISUP CRA  MT=0x1D
    // P16: ISUP Circuit Management (ITU-T Q.763)
    bool     do_isup_blo  = false;     // ISUP BLO Blocking           MT=0x13  (ISUP-interface)
    bool     do_isup_ubl  = false;     // ISUP UBL Unblocking         MT=0x14  (ISUP-interface)
    bool     do_isup_rsc  = false;     // ISUP RSC Reset Circuit      MT=0x12  (ISUP-interface)
    bool     do_isup_grs  = false;     // ISUP GRS Group Reset        MT=0x17  (ISUP-interface)
    uint8_t  grs_range_param = 7;      // --grs-range: 0–127 (0=1цепь, 7=8цепей)
    // P17: M3UA ASP Management (RFC 4666 §3.5/§3.6)
    bool     do_m3ua_aspup     = false; // ASPUP     ASPSM Class=3 Type=1
    bool     do_m3ua_aspup_ack = false; // ASPUP-ACK ASPSM Class=3 Type=4
    bool     do_m3ua_aspdn     = false; // ASPDN     ASPSM Class=3 Type=2
    bool     do_m3ua_aspac     = false; // ASPAC     ASPTM Class=4 Type=1
    bool     do_m3ua_aspac_ack = false; // ASPAC-ACK ASPTM Class=4 Type=3
    bool     do_m3ua_aspia     = false; // ASPIA     ASPTM Class=4 Type=2
    uint8_t  m3ua_tmt_param    = 2;     // --m3ua-tmt: 1=Override 2=Loadshare 3=Broadcast
    uint32_t m3ua_rc_param     = 0;     // --m3ua-rc:  Routing Context (0=не добавлять)
    // P19: ISUP Circuit Management ACKs + M3UA Keepalive (RFC 4666)
    bool     do_isup_bla       = false; // ISUP BLA Blocking Ack         MT=0x15  (ISUP-interface)
    bool     do_isup_uba       = false; // ISUP UBA Unblocking Ack       MT=0x16  (ISUP-interface)
    bool     do_isup_gra       = false; // ISUP GRA Group Reset Ack      MT=0x29  (ISUP-interface)
    bool     do_m3ua_aspdn_ack = false; // ASPDN-ACK ASPSM Class=3 Type=5
    bool     do_m3ua_aspia_ack = false; // ASPIA-ACK ASPTM Class=4 Type=4
    bool     do_m3ua_beat      = false; // BEAT      ASPSM Class=3 Type=3
    bool     do_m3ua_beat_ack  = false; // BEAT-ACK  ASPSM Class=3 Type=6
    // P25: M3UA Management (RFC 4666 §3.8)
    bool     do_m3ua_err  = false;    // M3UA ERR  Mgmt Class=0 Type=0
    bool     do_m3ua_ntfy = false;    // M3UA NTFY Mgmt Class=0 Type=1
    uint32_t m3ua_err_code_param        = 0x03; // --m3ua-err-code (0x01=InvalidVer 0x03=UnsupMsgClass)
    uint16_t m3ua_ntfy_status_type_param = 1;   // --m3ua-ntfy-type (1=AS State Change 2=Other)
    uint16_t m3ua_ntfy_status_info_param = 3;   // --m3ua-ntfy-info (2=AS-INACTIVE 3=AS-ACTIVE 4=AS-PENDING)
    // P22: M3UA SSNM (RFC 4666 §3.4)
    bool     do_m3ua_duna      = false; // DUNA  SSNM Class=2 Type=1  Dest. Unavailable
    bool     do_m3ua_dava      = false; // DAVA  SSNM Class=2 Type=2  Dest. Available
    bool     do_m3ua_daud      = false; // DAUD  SSNM Class=2 Type=3  Dest. State Audit
    uint32_t m3ua_aff_pc_param = 0;    // --m3ua-aff-pc: Affected Point Code
    // SMS / USSD (P4)
    bool     do_map_mo_fsm = false;    // MAP MO-ForwardSM (opCode=46)            (C-interface)
    bool     do_map_mt_fsm = false;    // MAP MT-ForwardSM (opCode=44)            (C-interface)
    bool     do_map_ussd   = false;    // MAP processUnstructuredSS-Request (59)  (C-interface)
    std::string sm_text_param  = "Hello from vMSC";  // --sm-text
    std::string ussd_str_param = "*100#";             // --ussd-str
    // P15: MAP SMS Gateway (3GPP TS 29.002 §10.5.6)
    bool     do_map_sri_sm      = false;  // MAP SendRoutingInfoForSM     opCode=45 (GMSC→HLR)
    bool     do_map_report_smds = false;  // MAP ReportSMDeliveryStatus   opCode=47 (SMSC→HLR)
    std::string smsc_param      = "79161000099";  // --smsc: E.164 адрес SMSC
    uint8_t  smds_outcome_param = 2;              // --smds-outcome: 0=memCap 1=absent 2=success
    // P18: MAP Supplementary Services (3GPP TS 29.002 §14)
    bool     do_map_register_ss    = false;  // RegisterSS    opCode=10  (C-interface, MSC→HLR)
    bool     do_map_erase_ss       = false;  // EraseSS       opCode=11  (C-interface, MSC→HLR)
    bool     do_map_activate_ss    = false;  // ActivateSS    opCode=12  (C-interface, MSC→HLR)
    bool     do_map_deactivate_ss  = false;  // DeactivateSS  opCode=13  (C-interface, MSC→HLR)
    bool     do_map_interrogate_ss = false;  // InterrogateSS opCode=14  (C-interface, MSC→HLR)
    uint8_t  ss_code_param         = 0x21;   // --ss-code: default 0x21=CFU
    std::string ss_fwd_num_param   = "";     // --ss-fwd-num: ForwardedToNumber (RegisterSS)
    // P20: ISUP CGB/CGU/CGBA/CGUA + MAP RegisterPassword/GetPassword/ATI
    bool     do_isup_cgb            = false; // ISUP CGB  Circuit Group Blocking   MT=0x18
    bool     do_isup_cgu            = false; // ISUP CGU  Circuit Group Unblocking MT=0x19
    bool     do_isup_cgba           = false; // ISUP CGBA CGB Acknowledgement      MT=0x1A
    bool     do_isup_cgua           = false; // ISUP CGUA CGU Acknowledgement      MT=0x1B
    uint8_t  cg_cause_param         = 0;     // --cg-cause: 0=maintenance 1=hardware failure
    bool     do_map_register_pw     = false; // MAP RegisterPassword  opCode=17
    bool     do_map_get_pw          = false; // MAP GetPassword       opCode=18
    bool     do_map_ati             = false; // MAP AnyTimeInterrogation opCode=71
    uint8_t  pw_guidance_param      = 0;     // --pw-guidance: 0=enterPW 1=enterNewPW 2=enterNewPW-Again
    std::string ati_msisdn_param    = "";    // --ati-msisdn (empty = use msisdn from config)
    // P6: A-interface DTAP/BSSMAP — аутентификация, шифрование, LU accept/reject
    bool     do_dtap_auth_req   = false;  // DTAP Authentication Request  MM 0x12
    bool     do_dtap_auth_resp  = false;  // DTAP Authentication Response MM 0x14
    bool     do_dtap_id_req     = false;  // DTAP Identity Request        MM 0x18
    bool     do_dtap_id_resp    = false;  // DTAP Identity Response       MM 0x19
    bool     do_bssmap_cipher   = false;  // BSSMAP Ciphering Mode Cmd    0x35
    bool     do_bssmap_cipher_compl  = false;  // BSSMAP Ciphering Mode Complete  0x55  (BSC→MSC)
    bool     do_bssmap_cipher_reject = false;  // BSSMAP Ciphering Mode Reject    0x58  (BSC→MSC)
    uint8_t  cipher_reject_cause_param = 0x6E; // --cipher-reject-cause (0x6E=alg not supported)
    bool     do_bssmap_common_id     = false;  // BSSMAP Common Id                0x2F  (MSC→BSC)
    bool     do_bssmap_sapi_n_reject = false;  // BSSMAP SAPI n Reject            0x25  (BSC→MSC)
    uint8_t  sapi_n_reject_sapi_param  = 3;    // --sapi (0=RR/CC/MM 3=SMS)
    uint8_t  sapi_n_reject_cause_param = 0x25; // --sapi-reject-cause
    // P27: BSSMAP HO-Required-Reject / HO-Candidate-Response / Suspend / Resume
    bool     do_bssmap_ho_req_reject     = false; // BSSMAP HO Required Reject   MT=0x1A
    bool     do_bssmap_ho_candidate_resp = false; // BSSMAP HO Candidate Resp    MT=0x19
    bool     do_bssmap_suspend           = false; // BSSMAP Suspend              MT=0x28
    bool     do_bssmap_resume            = false; // BSSMAP Resume               MT=0x29
    // P28: BSSMAP Load Indication / Queuing Indication
    bool     do_bssmap_load_indication   = false; // BSSMAP Load Indication    MT=0x5A(90)
    bool     do_bssmap_queuing_indication= false; // BSSMAP Queuing Indication MT=0x56(86)
    // P29: BSSMAP Confusion / COI / PLR / PLResp
    bool     do_bssmap_confusion                    = false; // BSSMAP Confusion                  MT=0x26(38)
    bool     do_bssmap_connection_oriented_info     = false; // BSSMAP Connection Oriented Info   MT=0x2A(42)
    bool     do_bssmap_perform_location_request     = false; // BSSMAP Perform Location Request   MT=0x2B(43)
    bool     do_bssmap_perform_location_response    = false; // BSSMAP Perform Location Response  MT=0x2D(45)
    // P30: BSSMAP Overload / PLAbort / HO Detect / MSC Invoke Trace
    bool     do_bssmap_overload                   = false; // BSSMAP Overload               MT=0x32(50)
    bool     do_bssmap_perform_location_abort     = false; // BSSMAP Perform Location Abort MT=0x2E(46)
    bool     do_bssmap_handover_detect            = false; // BSSMAP Handover Detect        MT=0x1B(27)
    bool     do_bssmap_msc_invoke_trace           = false; // BSSMAP MSC Invoke Trace       MT=0x36(54)
    bool     do_bssmap_channel_modify_request         = false; // BSSMAP Channel Modify Request     MT=0x08
    bool     do_bssmap_internal_handover_required     = false; // BSSMAP Internal HO Required       MT=0x21
    bool     do_bssmap_internal_handover_command      = false; // BSSMAP Internal HO Command        MT=0x23
    bool     do_bssmap_lsa_information                = false; // BSSMAP LSA Information            MT=0x2C
    uint8_t  bssmap_overload_cause_param          = 0x58;  // --bssmap-overload-cause (0x58=processor overload)
    uint8_t  bssmap_trace_type_param              = 0x01;  // --bssmap-trace-type
    uint8_t  bssmap_confusion_cause_param           = 0x62;  // --bssmap-confusion-cause
    uint8_t  bssmap_loc_type_param                  = 0x00;  // --bssmap-loc-type (0=current)
    uint16_t bssmap_cell_id_param        = 0x0001; // --bssmap-cell-id
    uint8_t  bssmap_resource_avail_param = 0x50;   // --bssmap-resource-avail (0x50=80%)
    uint8_t  ho_req_rej_cause_param      = 0x57;  // --ho-req-rej-cause (0x57=No radio resource available)
    uint8_t  ho_cand_count_param         = 1;     // --ho-cand-count
    uint32_t bssmap_tlli_param           = 0xDEADBEEFu; // --tlli
    bool     do_dtap_lu_accept  = false;  // DTAP Location Updating Accept MM 0x02
    bool     do_dtap_lu_reject  = false;  // DTAP Location Updating Reject MM 0x04
    bool     do_bssmap_reset    = false;  // BSSMAP Reset                  0x30
    // P8: A-interface BSSMAP — Assignment, Clear, Paging
    bool     do_bssmap_assign_req   = false;  // BSSMAP Assignment Request  0x01
    bool     do_bssmap_assign_compl = false;  // BSSMAP Assignment Complete 0x02
    bool     do_bssmap_clear_req    = false;  // BSSMAP Clear Request       0x22
    bool     do_bssmap_clear_compl  = false;  // BSSMAP Clear Complete      0x21
    bool     do_bssmap_paging       = false;  // BSSMAP Paging              0x52
    uint8_t  speech_ver_param       = 0x01;   // --speech-ver: 0x01=FR 0x21=EFR 0x41=AMR
    // P13: BSSMAP Handover (3GPP TS 48.008 §3.2.1)
    bool     do_bssmap_ho_required  = false;  // BSSMAP Handover Required        0x11
    bool     do_bssmap_ho_command   = false;  // BSSMAP Handover Command         0x12
    bool     do_bssmap_ho_succeeded = false;  // BSSMAP Handover Succeeded       0x14
    bool     do_bssmap_ho_performed = false;  // BSSMAP Handover Performed       0x17
    bool     do_bssmap_ho_candidate = false;  // BSSMAP HO Candidate Enquiry     0x4F
    uint8_t  ho_cause_param         = 0x58;   // --ho-cause: 0x58=better-cell (TS 48.008 Table 3.2.2.5)
    uint16_t ho_target_lac          = 0x0001; // --ho-lac:  целевой LAC
    uint16_t ho_target_cell         = 0x0001; // --ho-cell: целевой Cell ID
    // P23: BSSMAP additional
    bool     do_bssmap_reset_ack     = false;  // BSSMAP Reset Acknowledge      0x01
    bool     do_bssmap_assign_fail   = false;  // BSSMAP Assignment Failure     0x03
    uint8_t  assign_fail_cause_param = 0x20;   // --assign-fail-cause (TS 48.008 §3.2.2.5)
    bool     do_bssmap_classmark_req = false;  // BSSMAP Classmark Request      0x55
    bool     do_bssmap_classmark_upd = false;  // BSSMAP Classmark Update       0x53
    bool     do_bssmap_ho_request    = false;  // BSSMAP Handover Request       0x10
    bool     do_bssmap_ho_req_ack    = false;  // BSSMAP Handover Request Ack   0x13
    uint8_t  ho_ref_param            = 0x01;   // --ho-ref: handover reference byte
    bool     do_bssmap_ho_failure    = false;  // BSSMAP Handover Failure       0x15
    // P23: DTAP CC additional + SMS CP
    bool     do_dtap_cc_progress       = false;  // CC Progress                 0x03
    uint8_t  progress_desc_param       = 0x08;   // --progress-desc (8=inband)
    bool     do_dtap_cc_call_confirmed = false;  // CC Call Confirmed           0x08
    bool     do_dtap_cc_start_dtmf     = false;  // CC Start DTMF              0x35
    char     dtmf_digit_param          = '1';    // --dtmf-digit: DTMF digit char
    bool     do_dtap_cc_stop_dtmf      = false;  // CC Stop DTMF              0x31
    bool     do_dtap_cc_status         = false;  // CC Status                  0x3D
    bool     do_dtap_cc_emerg_setup    = false;  // CC Emergency Setup   0x0E  (MS→MSC)
    bool     do_dtap_cc_hold           = false;  // CC Hold              0x18  (MS→MSC)
    bool     do_dtap_cc_hold_ack       = false;  // CC Hold Ack          0x19  (MSC→MS)
    bool     do_dtap_cc_hold_reject    = false;  // CC Hold Reject       0x1A  (MSC→MS)
    bool     do_dtap_cc_retrieve       = false;  // CC Retrieve          0x1C  (MS→MSC)
    bool     do_dtap_cc_retrieve_ack   = false;  // CC Retrieve Ack      0x1D  (MSC→MS)
    bool     do_dtap_cc_retrieve_reject= false;  // CC Retrieve Reject   0x1E  (MSC→MS)
    // P25: DTAP CC Modify
    bool     do_dtap_cc_modify         = false;  // CC Modify            0x17  (MS→MSC)
    bool     do_dtap_cc_modify_complete= false;  // CC Modify Complete   0x1F  (MSC→MS)
    bool     do_dtap_cc_modify_reject  = false;  // CC Modify Reject     0x13  (MSC→MS)
    uint8_t  cc_modify_rej_cause_param = 0x4F;   // --cc-modify-rej-cause (0x4F=incompatible destination)
    // P26: DTAP CC Notify + DTMF ack/rej
    bool     do_dtap_cc_notify         = false;  // CC Notify            0x3E  (MS↔MSC)
    bool     do_dtap_cc_start_dtmf_ack = false;  // CC Start DTMF Ack    0x36  (MSC→MS)
    bool     do_dtap_cc_start_dtmf_rej = false;  // CC Start DTMF Rej    0x37  (MSC→MS)
    bool     do_dtap_cc_stop_dtmf_ack  = false;  // CC Stop DTMF Ack     0x32  (MSC→MS)
    uint8_t  cc_notify_param           = 0x00;   // --cc-notify (0x00=User suspended, 0x40=Forwarded)
    // P27: DTAP CC Status Enquiry / User Info / Congestion
    bool     do_dtap_cc_status_enquiry  = false; // CC Status Enquiry       0x34  (MS→MSC)
    bool     do_dtap_cc_user_info       = false; // CC User Information     0x10  (MS↔MSC)
    bool     do_dtap_cc_congestion      = false; // CC Congestion Control   0x39  (MSC→MS)
    // P28: DTAP CC Facility / Recall / Start CC
    bool     do_dtap_cc_facility         = false; // CC Facility   MT=0x3A
    bool     do_dtap_cc_recall           = false; // CC Recall     MT=0x0B
    bool     do_dtap_cc_start_cc         = false; // CC Start CC   MT=0x09
    uint8_t  cc_recall_type_param        = 0x00;  // --recall-type (0=CCBS 2=CCNR)
    uint8_t  uus_proto_param            = 0x00;  // --uus-proto (0=UUS1, 1=UUS2, 2=UUS3)
    uint8_t  cong_level_param           = 0x00;  // --cong-level (0=ready, 0x0F=not ready)
    uint8_t  dtmf_start_rej_cause_param = 0x3F;  // --dtmf-rej-cause
    char     dtmf_key_param            = '5';    // --dtmf-key
    uint8_t  hold_rej_cause_param      = 0x66;   // --hold-rej-cause (0x66=resources unavailable)
    uint8_t  retr_rej_cause_param      = 0x66;   // --retr-rej-cause
    uint8_t  call_state_param          = 0x00;   // --call-state (0=null/idle)
    bool     do_dtap_sms_cp_data       = false;  // SMS CP-DATA               0x01
    bool     do_dtap_sms_cp_ack        = false;  // SMS CP-ACK                0x04
    bool     do_dtap_sms_cp_error      = false;  // SMS CP-ERROR              0x10
    uint8_t  cp_cause_param            = 0x11;   // --cp-cause (0x11=network failure)
    // P14: DTAP MM Supplementary (3GPP TS 24.008)
    bool     do_dtap_tmsi_realloc_cmd   = false;  // TMSI Reallocation Command   MM 0x1A
    bool     do_dtap_tmsi_realloc_compl = false;  // TMSI Reallocation Complete  MM 0x1B
    bool     do_dtap_mm_info            = false;  // MM Information              MM 0x32
    bool     do_dtap_cipher_mode_compl  = false;  // Ciphering Mode Complete    RR 0x32
    // P28: DTAP RR Status / Channel Release / Classmark Change / Classmark Enquiry
    bool     do_dtap_rr_status           = false; // RR Status             MT=0x12
    bool     do_dtap_rr_channel_release  = false; // RR Channel Release    MT=0x0D
    bool     do_dtap_rr_classmark_change = false; // RR Classmark Change   MT=0x16
    bool     do_dtap_rr_classmark_enquiry= false; // RR Classmark Enquiry  MT=0x13
    // P29: DTAP RR Assignment Command / Complete / Failure / Handover Command
    bool     do_dtap_rr_assignment_command  = false; // RR Assignment Command   MT=0x2E
    bool     do_dtap_rr_assignment_complete = false; // RR Assignment Complete  MT=0x29
    bool     do_dtap_rr_assignment_failure  = false; // RR Assignment Failure   MT=0x2F
    bool     do_dtap_rr_handover_command    = false; // RR Handover Command     MT=0x2B
    // P30: DTAP RR Handover Complete/Failure / Measurement Report / Channel Mode Modify
    bool     do_dtap_rr_handover_complete     = false; // RR Handover Complete      MT=0x2C
    bool     do_dtap_rr_handover_failure      = false; // RR Handover Failure       MT=0x28
    bool     do_dtap_rr_measurement_report    = false; // RR Measurement Report     MT=0x15
    bool     do_dtap_rr_channel_mode_modify   = false; // RR Channel Mode Modify    MT=0x10
    bool     do_dtap_rr_ciphering_mode_command    = false; // RR Ciphering Mode Command   MT=0x35
    bool     do_dtap_rr_ciphering_mode_complete   = false; // RR Ciphering Mode Complete  MT=0x32
    bool     do_dtap_rr_ciphering_mode_reject     = false; // RR Ciphering Mode Reject    MT=0x34
    bool     do_dtap_rr_immediate_assignment      = false; // RR Immediate Assignment     MT=0x3F
    uint8_t  rr_cipher_alg_param                  = 1;     // --cipher-alg (1=A5/1, 2=A5/2)
    uint8_t  rr_rxlev_param                   = 30;    // --rxlev (0-63)
    uint8_t  rr_rxqual_param                  = 0;     // --rxqual (0-7)
    uint8_t  rr_chan_mode_param               = 0x01;  // --chan-mode (0x01=FR, 0x21=EFR)
    uint16_t rr_arfcn_param                 = 100;   // --arfcn
    uint8_t  rr_bsic_param                  = 0x3F;  // --bsic (6-bit, default 63)
    uint8_t  rr_ts_param                    = 0;     // --ts (timeslot 0-7)
    uint8_t  rr_cause_param              = 0x00;  // --rr-cause (0x00=normal)
    // P26: DTAP MM auth reject + abort
    bool     do_dtap_mm_auth_reject     = false;  // MM Auth Reject   0x11  (MSC→MS)
    bool     do_dtap_mm_abort           = false;  // MM Abort         0x29  (MS↔MSC)
    uint8_t  mm_abort_cause_param       = 0x06;   // --mm-abort-cause (0x06=Illegal ME)  // Ciphering Mode Complete     RR 0x32
    // P27: DTAP MM Status / CM Re-establishment
    bool     do_dtap_mm_status          = false; // MM Status              0x31  (MS↔MSC)
    bool     do_dtap_mm_cm_reest_req    = false; // MM CM Re-establishment 0x28  (MS→MSC)
    // P28: DTAP MM Auth Failure / CM Service Abort / CM Service Prompt
    bool     do_dtap_mm_auth_failure     = false; // MM Auth Failure       MT=0x1C
    bool     do_dtap_mm_cm_service_abort = false; // MM CM Service Abort   MT=0x23
    bool     do_dtap_mm_cm_service_prompt= false; // MM CM Service Prompt  MT=0x25
    uint8_t  mm_auth_fail_cause_param    = 0x15;  // --auth-fail-cause (0x15=MAC failure)
    bool     with_auts_param             = false; // --with-auts
    uint8_t  pd_sapi_param               = 0x00;  // --pd-sapi (0=CC 3=SMS)
    uint8_t  mm_status_cause_param      = 0x60;  // --mm-status-cause (0x60=invalid mandatory IE)
    uint8_t  mm_tz_param                = 0x21;   // --mm-tz: 0x21=UTC+3(MSK) 0x00=UTC+0
    // P9: A-interface DTAP CC / MM — управление вызовом
    bool     do_dtap_cm_srv_req     = false;  // MM CM Service Request   0x24
    bool     do_dtap_cm_srv_acc     = false;  // MM CM Service Accept    0x21
    // P21-R: DTAP MM CM Service Reject + IMSI Detach (3GPP TS 24.008)
    bool     do_dtap_cm_srv_rej     = false;  // MM CM Service Reject    0x22
    bool     do_dtap_imsi_detach_a  = false;  // MM IMSI Detach Indication 0x01
    uint8_t  cm_reject_cause_param  = 0x06;   // --cm-reject-cause: 0x04/0x06/0x0C/0x11
    bool     do_dtap_cc_setup_mo    = false;  // CC Setup (MO)           0x05
    bool     do_dtap_cc_setup_mt    = false;  // CC Setup (MT)           0x05
    bool     do_dtap_cc_call_proc   = false;  // CC Call Proceeding      0x02
    bool     do_dtap_cc_alerting    = false;  // CC Alerting             0x01
    bool     do_dtap_cc_connect     = false;  // CC Connect              0x07
    bool     do_dtap_cc_connect_ack = false;  // CC Connect Acknowledge  0x0F
    bool     do_dtap_cc_disconnect  = false;  // CC Disconnect           0x25
    bool     do_dtap_cc_release     = false;  // CC Release              0x2D
    bool     do_dtap_cc_rel_compl   = false;  // CC Release Complete     0x2A
    uint8_t  ti_param               = 0;      // --ti: Transaction Identifier 0-7
    uint8_t  cc_cause_param         = 16;     // --cc-cause: Q.850 16=normalClearing
    bool     cc_net_to_ms           = true;   // --ms-to-net: изменить направление
    std::string rand_param      = "A0A1A2A3A4A5A6A7A8A9AAABACADAEAF";  // --rand 32 hex
    std::string sres_param      = "01020304";   // --sres 8 hex (4 байта)
    uint8_t  cksn_param         = 0;            // --cksn 0-6
    uint32_t tmsi_param         = 0x01020304;   // --tmsi (hex/dec)
    uint8_t  id_type_param      = 1;            // --id-type 1=IMSI 2=IMEI 4=TMSI
    uint8_t  lu_cause_param     = 0x03;         // --lu-cause (причина отказа LU)
    uint8_t  cipher_alg_param   = 0x02;         // --cipher-alg 0x01=noEnc 0x02=A5/1 0x08=A5/3
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
    uint8_t    c_si          = cfg.c_si;
    uint8_t    c_ssn_local  = cfg.c_ssn_local;
    uint8_t    c_ssn_remote = cfg.c_ssn_remote;
    uint8_t    a_ssn        = cfg.a_ssn;

    // F-interface: MSC ↔ EIR
    std::string f_local_ip    = cfg.f_local_ip;
    uint16_t   f_local_port  = cfg.f_local_port;
    std::string f_remote_ip   = cfg.f_remote_ip;
    uint16_t   f_remote_port = cfg.f_remote_port;
    uint32_t   f_opc         = cfg.f_opc;
    uint32_t   f_dpc         = cfg.f_dpc;
    uint8_t    f_m3ua_ni     = cfg.f_m3ua_ni;
    uint8_t    f_si          = cfg.f_si;
    uint8_t    f_ssn_local   = cfg.f_ssn_local;
    uint8_t    f_ssn_remote  = cfg.f_ssn_remote;

    // E-interface: MSC ↔ MSC (три NI варианта)
    std::string e_local_ip    = cfg.e_local_ip;
    uint16_t   e_local_port  = cfg.e_local_port;
    std::string e_remote_ip   = cfg.e_remote_ip;
    uint16_t   e_remote_port = cfg.e_remote_port;
    uint32_t   e_opc_ni0 = cfg.e_opc_ni0;  uint32_t e_dpc_ni0 = cfg.e_dpc_ni0;
    uint32_t   e_opc_ni2 = cfg.e_opc_ni2;  uint32_t e_dpc_ni2 = cfg.e_dpc_ni2;
    uint32_t   e_opc_ni3 = cfg.e_opc_ni3;  uint32_t e_dpc_ni3 = cfg.e_dpc_ni3;
    uint8_t    e_m3ua_ni    = cfg.e_m3ua_ni;
    uint8_t    e_si         = cfg.e_si;
    uint8_t    e_ssn_local  = cfg.e_ssn_local;
    uint8_t    e_ssn_remote = cfg.e_ssn_remote;

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
    uint8_t    isup_si      = cfg.isup_si;

    // Gs-interface: MSC ↔ SGSN
    std::string gs_local_ip    = cfg.gs_local_ip;
    uint16_t   gs_local_port   = cfg.gs_local_port;
    std::string gs_remote_ip   = cfg.gs_remote_ip;
    uint16_t   gs_remote_port  = cfg.gs_remote_port;
    uint32_t   gs_opc_ni2 = cfg.gs_opc_ni2;  uint32_t gs_dpc_ni2 = cfg.gs_dpc_ni2;
    uint32_t   gs_opc_ni3 = cfg.gs_opc_ni3;  uint32_t gs_dpc_ni3 = cfg.gs_dpc_ni3;
    uint8_t    gs_m3ua_ni    = cfg.gs_m3ua_ni;
    uint8_t    gs_si         = cfg.gs_si;
    uint8_t    gs_ssn_local  = cfg.gs_ssn_local;
    uint8_t    gs_ssn_remote = cfg.gs_ssn_remote;

    // A-interface M3UA: один NI для BSC (Reserved = 3)
    uint32_t m3ua_opc = cfg.m3ua_opc;
    uint32_t m3ua_dpc = cfg.m3ua_dpc;
    uint8_t  m3ua_ni  = cfg.m3ua_ni;
    uint8_t  a_si     = cfg.a_si;
    // Nc-interface M3UA: три NI варианта
    uint32_t nc_opc_ni0 = cfg.nc_opc_ni0;  uint32_t nc_dpc_ni0 = cfg.nc_dpc_ni0;
    uint32_t nc_opc_ni2 = cfg.nc_opc_ni2;  uint32_t nc_dpc_ni2 = cfg.nc_dpc_ni2;
    uint32_t nc_opc_ni3 = cfg.nc_opc_ni3;  uint32_t nc_dpc_ni3 = cfg.nc_dpc_ni3;
    uint8_t  nc_m3ua_ni = cfg.nc_m3ua_ni;  // Активный NI для Nc
    uint8_t  nc_si      = cfg.nc_si;
    
    bool use_sctp = false;  // Использовать SCTP вместо UDP
    bool use_bssmap_complete_l3 = false;  // Использовать BSSMAP Complete Layer 3 вместо DTAP
    bool send_clear_command = false;  // Отправить BSSMAP Clear Command
    uint16_t cell_id = cfg.cell_id;  // Cell Identity
    uint8_t clear_cause = 0x00;  // Cause для Clear Command
    bool save_config_flag = false;  // Сохранить конфигурацию
    uint8_t sls = cfg.sls;  // Signalling Link Selection
    uint8_t mp  = cfg.mp;   // Message Priority
    // Global Title
    std::string msc_gt       = cfg.msc_gt;
    uint8_t     gt_tt        = cfg.gt_tt;
    uint8_t     gt_np        = cfg.gt_np;
    uint8_t     gt_nai       = cfg.gt_nai;
    uint8_t     c_gt_ind     = cfg.c_gt_ind;   std::string c_gt_called  = cfg.c_gt_called;
    uint8_t     f_gt_ind     = cfg.f_gt_ind;   std::string f_gt_called  = cfg.f_gt_called;
    uint8_t     e_gt_ind     = cfg.e_gt_ind;   std::string e_gt_called  = cfg.e_gt_called;
    uint8_t     gs_gt_ind    = cfg.gs_gt_ind;  std::string gs_gt_called = cfg.gs_gt_called;
    // Сетевые параметры узла
    std::string local_netmask  = cfg.local_netmask;
    std::string remote_netmask = cfg.remote_netmask;
    std::string gateway        = cfg.gateway;
    std::string ntp_primary    = cfg.ntp_primary;
    std::string ntp_secondary  = cfg.ntp_secondary;
    // SPID — текстовые метки сигнальных точек
    std::string a_local_spid    = cfg.a_local_spid;     std::string a_remote_spid    = cfg.a_remote_spid;
    std::string c_local_spid    = cfg.c_local_spid;     std::string c_remote_spid    = cfg.c_remote_spid;
    std::string f_local_spid    = cfg.f_local_spid;     std::string f_remote_spid    = cfg.f_remote_spid;
    std::string e_local_spid    = cfg.e_local_spid;     std::string e_remote_spid    = cfg.e_remote_spid;
    std::string nc_local_spid   = cfg.nc_local_spid;    std::string nc_remote_spid   = cfg.nc_remote_spid;
    std::string isup_local_spid = cfg.isup_local_spid;  std::string isup_remote_spid = cfg.isup_remote_spid;
    std::string gs_local_spid   = cfg.gs_local_spid;    std::string gs_remote_spid   = cfg.gs_remote_spid;
    // GT-таблица маршрутизации
    std::vector<GtRoute> gt_routes = cfg.gt_routes;
    
    // Флаги для выбора секций вывода
    bool show_all = true;
    bool show_identity = false;
    bool show_network = false;
    bool show_m3ua = false;
    bool show_bssmap = false;
    bool show_transport = false;
    bool show_encapsulation = false;
    // Флаги по интерфейсам
    bool show_interfaces     = false;  // --show-interfaces: все 7 интерфейсов
    bool show_subscriber    = false;
    bool show_a_interface    = false;
    bool show_c_interface    = false;
    bool show_f_interface    = false;
    bool show_e_interface    = false;
    bool show_nc_interface   = false;
    bool show_isup_interface = false;
    bool show_gs_interface   = false;
    bool show_gt_route       = false;  // --show-gt-route

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
            std::string path = argv[++i];
            if (load_config(path, cfg)) {
                // Deduplicate: remove existing entry for this path, re-add at end
                loaded_configs.erase(
                    std::remove(loaded_configs.begin(), loaded_configs.end(), path),
                    loaded_configs.end());
                loaded_configs.push_back(path);
                config_loaded = true;
                config_path   = path;
                // Применяем все поля из только что загруженного слоя
                imsi        = cfg.imsi;
                msisdn      = cfg.msisdn;
                mcc         = cfg.mcc;
                mnc         = cfg.mnc;
                lac         = cfg.lac;
                cell_id     = cfg.cell_id;
                local_ip    = cfg.local_ip;    local_port    = cfg.local_port;
                remote_ip   = cfg.remote_ip;   remote_port   = cfg.remote_port;
                m3ua_opc    = cfg.m3ua_opc;    m3ua_dpc      = cfg.m3ua_dpc;    m3ua_ni     = cfg.m3ua_ni;
                a_ssn       = cfg.a_ssn;        a_si          = cfg.a_si;
                c_local_ip  = cfg.c_local_ip;  c_local_port  = cfg.c_local_port;
                c_remote_ip = cfg.c_remote_ip; c_remote_port = cfg.c_remote_port;
                c_opc       = cfg.c_opc;       c_dpc         = cfg.c_dpc;       c_m3ua_ni   = cfg.c_m3ua_ni;
                c_ssn_local = cfg.c_ssn_local; c_ssn_remote  = cfg.c_ssn_remote;
                f_local_ip  = cfg.f_local_ip;  f_local_port  = cfg.f_local_port;
                f_remote_ip = cfg.f_remote_ip; f_remote_port = cfg.f_remote_port;
                f_opc       = cfg.f_opc;       f_dpc         = cfg.f_dpc;       f_m3ua_ni   = cfg.f_m3ua_ni;
                f_ssn_local = cfg.f_ssn_local; f_ssn_remote  = cfg.f_ssn_remote;
                e_local_ip  = cfg.e_local_ip;  e_local_port  = cfg.e_local_port;
                e_remote_ip = cfg.e_remote_ip; e_remote_port = cfg.e_remote_port;
                e_opc_ni0   = cfg.e_opc_ni0;   e_dpc_ni0     = cfg.e_dpc_ni0;
                e_opc_ni2   = cfg.e_opc_ni2;   e_dpc_ni2     = cfg.e_dpc_ni2;
                e_opc_ni3   = cfg.e_opc_ni3;   e_dpc_ni3     = cfg.e_dpc_ni3;
                e_m3ua_ni   = cfg.e_m3ua_ni;   e_ssn_local   = cfg.e_ssn_local; e_ssn_remote = cfg.e_ssn_remote;
                nc_local_ip = cfg.nc_local_ip; nc_local_port = cfg.nc_local_port;
                nc_remote_ip= cfg.nc_remote_ip;nc_remote_port= cfg.nc_remote_port;
                nc_opc_ni0  = cfg.nc_opc_ni0;  nc_dpc_ni0    = cfg.nc_dpc_ni0;
                nc_opc_ni2  = cfg.nc_opc_ni2;  nc_dpc_ni2    = cfg.nc_dpc_ni2;
                nc_opc_ni3  = cfg.nc_opc_ni3;  nc_dpc_ni3    = cfg.nc_dpc_ni3;  nc_m3ua_ni  = cfg.nc_m3ua_ni;
                isup_local_ip = cfg.isup_local_ip; isup_local_port = cfg.isup_local_port;
                isup_remote_ip= cfg.isup_remote_ip;isup_remote_port= cfg.isup_remote_port;
                isup_opc_ni0= cfg.isup_opc_ni0; isup_dpc_ni0 = cfg.isup_dpc_ni0;
                isup_opc_ni2= cfg.isup_opc_ni2; isup_dpc_ni2 = cfg.isup_dpc_ni2; isup_m3ua_ni = cfg.isup_m3ua_ni;
                gs_local_ip = cfg.gs_local_ip; gs_local_port = cfg.gs_local_port;
                gs_remote_ip= cfg.gs_remote_ip;gs_remote_port= cfg.gs_remote_port;
                gs_opc_ni2  = cfg.gs_opc_ni2;  gs_dpc_ni2    = cfg.gs_dpc_ni2;
                gs_opc_ni3  = cfg.gs_opc_ni3;  gs_dpc_ni3    = cfg.gs_dpc_ni3;  gs_m3ua_ni  = cfg.gs_m3ua_ni;
                gs_ssn_local= cfg.gs_ssn_local;gs_ssn_remote = cfg.gs_ssn_remote;
                c_si    = cfg.c_si;     f_si    = cfg.f_si;     e_si    = cfg.e_si;
                nc_si   = cfg.nc_si;    isup_si = cfg.isup_si;  gs_si   = cfg.gs_si;
                sls     = cfg.sls;      mp      = cfg.mp;
                msc_gt  = cfg.msc_gt;   gt_tt   = cfg.gt_tt;  gt_np  = cfg.gt_np;  gt_nai = cfg.gt_nai;
                c_gt_ind= cfg.c_gt_ind; c_gt_called = cfg.c_gt_called;
                f_gt_ind= cfg.f_gt_ind; f_gt_called = cfg.f_gt_called;
                e_gt_ind= cfg.e_gt_ind; e_gt_called = cfg.e_gt_called;
                gs_gt_ind=cfg.gs_gt_ind;gs_gt_called= cfg.gs_gt_called;
                local_netmask  = cfg.local_netmask;
                remote_netmask = cfg.remote_netmask;
                gateway        = cfg.gateway;
                ntp_primary    = cfg.ntp_primary;
                ntp_secondary  = cfg.ntp_secondary;
                a_local_spid  = cfg.a_local_spid;   a_remote_spid  = cfg.a_remote_spid;
                c_local_spid  = cfg.c_local_spid;   c_remote_spid  = cfg.c_remote_spid;
                f_local_spid  = cfg.f_local_spid;   f_remote_spid  = cfg.f_remote_spid;
                e_local_spid  = cfg.e_local_spid;   e_remote_spid  = cfg.e_remote_spid;
                nc_local_spid = cfg.nc_local_spid;  nc_remote_spid = cfg.nc_remote_spid;
                isup_local_spid=cfg.isup_local_spid;isup_remote_spid=cfg.isup_remote_spid;
                gs_local_spid = cfg.gs_local_spid;  gs_remote_spid = cfg.gs_remote_spid;
                // GT-маршруты: добавляем к существующим (накопительно)
                for (const auto &r : cfg.gt_routes)
                    gt_routes.push_back(r);
            } else {
                std::cerr << COLOR_YELLOW << "⚠ Не удалось загрузить конфигурацию из " << path << COLOR_RESET << "\n";
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
        else if (arg == "--show-interfaces") {
            if (show_all) { show_all = false; }
            show_interfaces     = true;
            show_a_interface    = true;
            show_c_interface    = true;
            show_f_interface    = true;
            show_e_interface    = true;
            show_nc_interface   = true;
            show_isup_interface = true;
            show_gs_interface   = true;
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
        else if (arg == "--show-f-interface") {
            if (show_all) { show_all = false; }
            show_f_interface = true;
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
        else if (arg == "--show-gt-route") {
            if (show_all) { show_all = false; }
            show_gt_route = true;
            do_lu = false;
            do_paging = false;
        }
        // ── C-интерфейс: MAP over SCCP UDT ──────────────────────────
        else if (arg == "--send-map-sai") {
            do_map_sai = true;
            do_lu      = false;
            do_paging  = false;
        }
        else if (arg == "--send-map-ul") {
            do_map_ul = true;
            do_lu     = false;
            do_paging = false;
        }
        // ── F-интерфейс: MAP CheckIMEI over SCCP UDT ────────────────
        else if (arg == "--send-map-check-imei") {
            do_map_check_imei = true;
            do_lu             = false;
            do_paging         = false;
        }
        // ── E-интерфейс: MAP Handover over SCCP UDT ─────────────────
        else if (arg == "--send-map-prepare-ho") {
            do_map_prepare_ho = true;
            do_lu             = false;
            do_paging         = false;
        }
        else if (arg == "--send-map-end-signal") {
            do_map_end_signal = true;
            do_lu             = false;
            do_paging         = false;
        }
        // P12: E-interface MAP handover supplementary
        else if (arg == "--send-map-prep-subseq-ho") {
            do_map_prep_subsequent_ho = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-process-access-sig") {
            do_map_process_access_sig = true;
            do_lu = false;  do_paging = false;
        }
        // ── P25: MAP E/C-interface supplementary ─────────────────────────────
        else if (arg == "--send-map-send-identification") { do_map_send_identification = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-restore-data")        { do_map_restore_data        = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-forward-check-ss")    { do_map_forward_check_ss    = true; do_lu = false; do_paging = false; }
        // P26: MAP subscriber presence / info
        else if (arg == "--send-map-note-subscriber-present") { do_map_note_subscriber_present = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-ready-for-sm")            { do_map_ready_for_sm            = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-provide-subscriber-info") { do_map_provide_subscriber_info = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-send-imsi")               { do_map_send_imsi               = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-check-imei-res")        { do_map_check_imei_res        = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-any-time-modification") { do_map_any_time_modification = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-activate-trace-mode")   { do_map_activate_trace_mode   = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-notify-subscriber-data")    { do_map_notify_subscriber_data   = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-forward-access-signalling")   { do_map_forward_access_signalling= true; do_lu = false; do_paging = false; }
        // P29: MAP UpdateGPRSLocation / SriGPRS / Reset / BeginSubscriberActivity
        else if (arg == "--send-map-update-gprs-location")      { do_map_update_gprs_location      = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-send-routeing-info-gprs")   { do_map_send_routeing_info_gprs   = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-reset")                     { do_map_reset                     = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-begin-subscriber-activity") { do_map_begin_subscriber_activity = true; do_lu = false; do_paging = false; }
        // P30: MAP DeactivateTraceMode / ProcessUSSD / UnstructuredSSReq / SubDataMod
        else if (arg == "--send-map-deactivate-trace-mode")        { do_map_deactivate_trace_mode        = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-process-unstructured-ss-req")  { do_map_process_unstructured_ss_req  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-unstructured-ss-request")      { do_map_unstructured_ss_request      = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-subscriber-data-modification") { do_map_subscriber_data_modification = true; do_lu = false; do_paging = false; }
        // P31: MAP AnyTimeInterrogation / NoteMM-Event / InformSC / AlertSC
        else if (arg == "--send-map-any-time-interrogation")   { do_map_any_time_interrogation   = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-note-mm-event")            { do_map_note_mm_event            = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-inform-service-centre")    { do_map_inform_service_centre    = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-map-alert-service-centre")     { do_map_alert_service_centre     = true; do_lu = false; do_paging = false; }
        else if (arg == "--ussd-string") { if (i+1<argc) ussd_string_param = argv[++i]; }
        else if (arg == "--equip-status") { if (i+1<argc) equip_status_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--msisdn")                           { if (i+1<argc) msisdn_param = argv[++i]; }
        // ── Gs-интерфейс: BSSAP+ LocationUpdate over SCCP UDT ───────
        else if (arg == "--send-gs-lu") {
            do_bssap_gs_lu = true;
            do_lu          = false;
            do_paging      = false;
        }
        else if (arg == "--send-gs-paging") {
            do_gs_paging = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-gs-imsi-detach") {
            do_gs_imsi_detach = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-gs-reset") {
            do_gs_reset = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-gs-reset-ack") {
            do_gs_reset_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--gs-reset-cause" && i+1 < argc) gs_reset_cause = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--gs-detach-type" && i+1 < argc) gs_detach_type = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--gs-peer-number" && i+1 < argc) gs_peer_number = argv[++i];
        // ── P21: Gs BSSAP+ SMS-related ────────────────────────────────
        else if (arg == "--send-gs-ready-for-sm") {
            do_gs_ready_for_sm = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-gs-alert-sc") {
            do_gs_alert_sc = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-gs-alert-ack") {
            do_gs_alert_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-gs-alert-reject") {
            do_gs_alert_reject = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--gs-alert-reason" && i+1 < argc) gs_alert_reason = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--gs-alert-cause" && i+1 < argc)  gs_alert_cause  = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        // ── TCAP диалог: завершение и промежуточные шаги ────────────
        else if (arg == "--dtid" && i+1 < argc) {
            // Принимаем decimal или hex (0x...)
            dtid_param = (uint32_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--otid" && i+1 < argc) {
            otid_param = (uint32_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--send-tcap-end") {
            do_tcap_end = true;
            do_lu       = false;
            do_paging   = false;
        }
        else if (arg == "--send-tcap-continue") {
            do_tcap_continue = true;
            do_lu            = false;
            do_paging        = false;
        }
        else if (arg == "--send-map-sai-end") {
            do_map_sai_end = true;
            do_lu          = false;
            do_paging      = false;
        }
        else if (arg == "--send-map-ul-end") {
            do_map_ul_end = true;
            do_lu         = false;
            do_paging     = false;
        }
        // ── P2: ключевые операции C-interface MAP ────────────────────
        else if (arg == "--send-map-sri") {
            do_map_sri = true;
            do_lu      = false;
            do_paging  = false;
        }
        else if (arg == "--send-map-prn") {
            do_map_prn = true;
            do_lu      = false;
            do_paging  = false;
        }
        else if (arg == "--send-map-cl") {
            do_map_cl = true;
            do_lu     = false;
            do_paging = false;
        }
        else if (arg == "--send-map-isd") {
            do_map_isd = true;
            do_lu      = false;
            do_paging  = false;
        }
        else if (arg == "--send-map-isd-res") {
            do_map_isd_res = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-cl-res") {
            do_map_cl_res = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-delete-sd") {
            do_map_delete_sd = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-delete-sd-res") {
            do_map_delete_sd_res = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--dtid") {
            if (++i < argc) {
                uint32_t v = (uint32_t)std::stoul(argv[i], nullptr, 0);
                if (do_map_isd_res) isd_res_dtid_param = v;
                else if (do_map_cl_res) cl_res_dtid_param = v;
                else if (do_map_delete_sd_res) dsd_res_dtid_param = v;
            }
        }
        // P7: MAP admin
        else if (arg == "--send-map-purge-ms") {
            do_map_purge_ms = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-auth-failure-report") {
            do_map_auth_failure_rpt = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--failure-cause" && i + 1 < argc) {
            failure_cause_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        // ── TCAP ошибки: Abort и ReturnError ────────────────────────
        else if (arg == "--abort-cause" && i+1 < argc) {
            abort_cause_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--invoke-id" && i+1 < argc) {
            invoke_id_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--error-code" && i+1 < argc) {
            error_code_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--send-tcap-abort") {
            do_tcap_abort = true;
            do_lu         = false;
            do_paging     = false;
        }
        else if (arg == "--send-map-return-error") {
            do_map_return_error = true;
            do_lu               = false;
            do_paging           = false;
        }
        else if (arg == "--send-isup-iam") {
            do_isup_iam = true;
            do_lu       = false;
            do_paging   = false;
        }
        else if (arg == "--send-isup-rel") {
            do_isup_rel = true;
            do_lu       = false;
            do_paging   = false;
        }
        else if (arg == "--cic" && i + 1 < argc) {
            cic_param = (uint16_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--cause" && i + 1 < argc) {
            cause_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--send-map-mo-fsm") {
            do_map_mo_fsm = true;
            do_lu         = false;
            do_paging     = false;
        }
        else if (arg == "--send-map-mt-fsm") {
            do_map_mt_fsm = true;
            do_lu         = false;
            do_paging     = false;
        }
        else if (arg == "--send-map-ussd") {
            do_map_ussd = true;
            do_lu       = false;
            do_paging   = false;
        }
        else if (arg == "--sm-text" && i + 1 < argc) {
            sm_text_param = argv[++i];
        }
        else if (arg == "--ussd-str" && i + 1 < argc) {
            ussd_str_param = argv[++i];
        }
        // ── P15: MAP SMS Gateway ──────────────────────────────────────────
        else if (arg == "--send-map-sri-sm") {
            do_map_sri_sm = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-report-smds") {
            do_map_report_smds = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--smsc"         && i + 1 < argc) smsc_param         = argv[++i];
        else if (arg == "--smds-outcome" && i + 1 < argc) smds_outcome_param = (uint8_t)std::stoul(argv[++i]);
        // ── P18: MAP Supplementary Services ───────────────────────────────────────
        else if (arg == "--send-map-register-ss") {
            do_map_register_ss = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-erase-ss") {
            do_map_erase_ss = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-activate-ss") {
            do_map_activate_ss = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-deactivate-ss") {
            do_map_deactivate_ss = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-interrogate-ss") {
            do_map_interrogate_ss = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--ss-code" && i + 1 < argc) {
            ss_code_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--ss-fwd-num" && i + 1 < argc) {
            ss_fwd_num_param = argv[++i];
        }
        else if (arg == "--send-isup-acm") {
            do_isup_acm = true;
            do_lu       = false;
            do_paging   = false;
        }
        else if (arg == "--send-isup-anm") {
            do_isup_anm = true;
            do_lu       = false;
            do_paging   = false;
        }
        else if (arg == "--send-isup-rlc") {
            do_isup_rlc = true;
            do_lu       = false;
            do_paging   = false;
        }
        // P11: ISUP supplementary
        else if (arg == "--send-isup-con") {
            do_isup_con = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-cpg") {
            do_isup_cpg = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-sus") {
            do_isup_sus = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-res") {
            do_isup_res = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--sus-cause") {
            if (i + 1 < argc) sus_cause_param = (uint8_t)std::stoul(argv[++i]);
        }
        // ── P25: ISUP supplementary (SAM/CCR/COT/FOT/INR/INF) ────────────────
        else if (arg == "--send-isup-sam") { do_isup_sam = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-ccr") { do_isup_ccr = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-cot") { do_isup_cot = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-fot") { do_isup_fot = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-inr") { do_isup_inr = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-inf") { do_isup_inf = true; do_lu = false; do_paging = false; }
        // P26: ISUP error/test/overload
        else if (arg == "--send-isup-cfn") { do_isup_cfn = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-ovl") { do_isup_ovl = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-upt") { do_isup_upt = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-upa") { do_isup_upa = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-far") { do_isup_far = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-idr") { do_isup_idr = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-irs") { do_isup_irs = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-lpa") { do_isup_lpa = true; do_lu = false; do_paging = false; }
        // P29: ISUP CQM / CQR / LOP / PAM
        else if (arg == "--send-isup-cqm") { do_isup_cqm = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-cqr") { do_isup_cqr = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-lop") { do_isup_lop = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-pam") { do_isup_pam = true; do_lu = false; do_paging = false; }
        // P30: ISUP NRM / OLM / EXM / APM
        else if (arg == "--send-isup-nrm") { do_isup_nrm = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-olm") { do_isup_olm = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-exm") { do_isup_exm = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-apm") { do_isup_apm = true; do_lu = false; do_paging = false; }
        // P31: ISUP FAA / FRJ / CRM / CRA
        else if (arg == "--send-isup-faa") { do_isup_faa = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-frj") { do_isup_frj = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-crm") { do_isup_crm = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-isup-cra") { do_isup_cra = true; do_lu = false; do_paging = false; }
        else if (arg == "--cfn-cause")     { if (i+1<argc) cfn_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--cot-success")   { cot_success_param = true; }
        else if (arg == "--cot-fail")      { cot_success_param = false; }
        else if (arg == "--sam-digits")    { if (i+1<argc) sam_digits_param = argv[++i]; }
        // ── P16: ISUP Circuit Management ──────────────────────────────────────
        else if (arg == "--send-isup-blo") {
            do_isup_blo = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-ubl") {
            do_isup_ubl = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-rsc") {
            do_isup_rsc = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-grs") {
            do_isup_grs = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--grs-range" && i + 1 < argc) {
            grs_range_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        // ── P17: M3UA ASP Management ──────────────────────────────────────────
        else if (arg == "--send-m3ua-aspup") {
            do_m3ua_aspup = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-aspup-ack") {
            do_m3ua_aspup_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-aspdn") {
            do_m3ua_aspdn = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-aspac") {
            do_m3ua_aspac = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-aspac-ack") {
            do_m3ua_aspac_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-aspia") {
            do_m3ua_aspia = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--m3ua-tmt" && i + 1 < argc) {
            m3ua_tmt_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--m3ua-rc" && i + 1 < argc) {
            m3ua_rc_param = (uint32_t)std::stoul(argv[++i], nullptr, 0);
        }
        // ── P19: ISUP Circuit Management ACKs + M3UA Keepalive ───────────────
        else if (arg == "--send-isup-bla") {
            do_isup_bla = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-uba") {
            do_isup_uba = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-gra") {
            do_isup_gra = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-aspdn-ack") {
            do_m3ua_aspdn_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-aspia-ack") {
            do_m3ua_aspia_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-beat") {
            do_m3ua_beat = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-beat-ack") {
            do_m3ua_beat_ack = true;
            do_lu = false;  do_paging = false;
        }
        // ── P25: M3UA Management (ERR/NTFY) ─────────────────────────────────
        else if (arg == "--send-m3ua-err")  { do_m3ua_err  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-m3ua-ntfy") { do_m3ua_ntfy = true; do_lu = false; do_paging = false; }
        else if (arg == "--m3ua-err-code")  { if (i+1<argc) m3ua_err_code_param = (uint32_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--m3ua-ntfy-type") { if (i+1<argc) m3ua_ntfy_status_type_param = (uint16_t)std::stoul(argv[++i]); }
        else if (arg == "--m3ua-ntfy-info") { if (i+1<argc) m3ua_ntfy_status_info_param = (uint16_t)std::stoul(argv[++i]); }
        // ── P22: M3UA SSNM Destination State ────────────────────────────────
        else if (arg == "--send-m3ua-duna") {
            do_m3ua_duna = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-dava") {
            do_m3ua_dava = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-m3ua-daud") {
            do_m3ua_daud = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--m3ua-aff-pc" && i + 1 < argc)
            m3ua_aff_pc_param = (uint32_t)std::stoul(argv[++i], nullptr, 0);
        // ── P20: ISUP Circuit Group + MAP Password/ATI ────────────────────────────────
        else if (arg == "--send-isup-cgb") {
            do_isup_cgb = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-cgu") {
            do_isup_cgu = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-cgba") {
            do_isup_cgba = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-isup-cgua") {
            do_isup_cgua = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--cg-cause" && i + 1 < argc) {
            cg_cause_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--send-map-register-pw") {
            do_map_register_pw = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-map-get-pw") {
            do_map_get_pw = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--pw-guidance" && i + 1 < argc) {
            pw_guidance_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        }
        else if (arg == "--send-map-ati") {
            do_map_ati = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--ati-msisdn" && i + 1 < argc) {
            ati_msisdn_param = argv[++i];
        }
        // P6: A-interface DTAP/BSSMAP
        else if (arg == "--send-dtap-auth-req") {
            do_dtap_auth_req = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-auth-resp") {
            do_dtap_auth_resp = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-id-req") {
            do_dtap_id_req = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-id-resp") {
            do_dtap_id_resp = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-cipher") {
            do_bssmap_cipher = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-cipher-compl") {
            do_bssmap_cipher_compl = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-cipher-reject") {
            do_bssmap_cipher_reject = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--cipher-reject-cause") {
            if (++i < argc) cipher_reject_cause_param = (uint8_t)std::stoul(argv[i], nullptr, 0);
        }
        else if (arg == "--send-bssmap-common-id") {
            do_bssmap_common_id = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-sapi-n-reject") {
            do_bssmap_sapi_n_reject = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--sapi") {
            if (++i < argc) sapi_n_reject_sapi_param = (uint8_t)std::stoul(argv[i], nullptr, 0);
        }
        else if (arg == "--send-bssmap-ho-required-reject")  { do_bssmap_ho_req_reject     = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-ho-candidate-response"){ do_bssmap_ho_candidate_resp = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-suspend")              { do_bssmap_suspend           = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-resume")               { do_bssmap_resume            = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-load-indication")    { do_bssmap_load_indication    = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-queuing-indication")  { do_bssmap_queuing_indication = true; do_lu = false; do_paging = false; }
        // P29: BSSMAP Confusion / COI / PLR / PLResp
        else if (arg == "--send-bssmap-confusion")                 { do_bssmap_confusion                 = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-connection-oriented-info")  { do_bssmap_connection_oriented_info  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-perform-location-request")  { do_bssmap_perform_location_request  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-perform-location-response") { do_bssmap_perform_location_response = true; do_lu = false; do_paging = false; }
        // P30: BSSMAP Overload / PLAbort / HO Detect / MSC Invoke Trace
        else if (arg == "--send-bssmap-overload")                { do_bssmap_overload               = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-perform-location-abort")  { do_bssmap_perform_location_abort = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-handover-detect")         { do_bssmap_handover_detect        = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-msc-invoke-trace")        { do_bssmap_msc_invoke_trace       = true; do_lu = false; do_paging = false; }
        // P31: BSSMAP Channel Modify Request / Internal HO / LSA Info
        else if (arg == "--send-bssmap-channel-modify-request")         { do_bssmap_channel_modify_request        = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-internal-handover-required")     { do_bssmap_internal_handover_required    = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-internal-handover-command")      { do_bssmap_internal_handover_command     = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-bssmap-lsa-information")                { do_bssmap_lsa_information               = true; do_lu = false; do_paging = false; }
        else if (arg == "--bssmap-overload-cause") { if (i+1<argc) bssmap_overload_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--bssmap-trace-type")     { if (i+1<argc) bssmap_trace_type_param     = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--bssmap-confusion-cause") { if (i+1<argc) bssmap_confusion_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--bssmap-loc-type")        { if (i+1<argc) bssmap_loc_type_param        = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--bssmap-cell-id")       { if (i+1<argc) bssmap_cell_id_param       = (uint16_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--bssmap-resource-avail"){ if (i+1<argc) bssmap_resource_avail_param= (uint8_t) std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--ho-req-rej-cause") { if (i+1<argc) ho_req_rej_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--ho-cand-count")    { if (i+1<argc) ho_cand_count_param   = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--tlli")             { if (i+1<argc) bssmap_tlli_param     = (uint32_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--sapi-reject-cause") {
            if (++i < argc) sapi_n_reject_cause_param = (uint8_t)std::stoul(argv[i], nullptr, 0);
        }
        else if (arg == "--send-dtap-lu-accept") {
            do_dtap_lu_accept = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-lu-reject") {
            do_dtap_lu_reject = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-reset") {
            do_bssmap_reset = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-assign-req") {
            do_bssmap_assign_req = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-assign-compl") {
            do_bssmap_assign_compl = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-clear-req") {
            do_bssmap_clear_req = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-clear-compl") {
            do_bssmap_clear_compl = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-paging") {
            do_bssmap_paging = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--speech-ver" && i + 1 < argc) speech_ver_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        // ── P13: BSSMAP Handover ──────────────────────────────────────────────
        else if (arg == "--send-bssmap-ho-required") {
            do_bssmap_ho_required = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-ho-command") {
            do_bssmap_ho_command = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-ho-succeeded") {
            do_bssmap_ho_succeeded = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-ho-performed") {
            do_bssmap_ho_performed = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-ho-candidate") {
            do_bssmap_ho_candidate = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--ho-cause"  && i + 1 < argc) ho_cause_param   = (uint8_t) std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--ho-lac"    && i + 1 < argc) ho_target_lac    = (uint16_t)std::stoul(argv[++i]);
        else if (arg == "--ho-cell"   && i + 1 < argc) ho_target_cell   = (uint16_t)std::stoul(argv[++i]);
        else if (arg == "--send-bssmap-reset-ack") {
            do_bssmap_reset_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-assign-fail") {
            do_bssmap_assign_fail = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--assign-fail-cause" && i + 1 < argc) assign_fail_cause_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--send-bssmap-classmark-req") {
            do_bssmap_classmark_req = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-classmark-upd") {
            do_bssmap_classmark_upd = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-ho-request") {
            do_bssmap_ho_request = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-bssmap-ho-req-ack") {
            do_bssmap_ho_req_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--ho-ref" && i + 1 < argc) ho_ref_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--send-bssmap-ho-failure") {
            do_bssmap_ho_failure = true;
            do_lu = false;  do_paging = false;
        }
        // ── P14: DTAP MM Supplementary ───────────────────────────────────────
        else if (arg == "--send-dtap-tmsi-realloc-cmd") {
            do_dtap_tmsi_realloc_cmd = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-tmsi-realloc-compl") {
            do_dtap_tmsi_realloc_compl = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-mm-info") {
            do_dtap_mm_info = true;
            do_lu = false;  do_paging = false;
        }
        // P26: DTAP MM auth reject + abort
        else if (arg == "--send-dtap-mm-auth-reject") { do_dtap_mm_auth_reject = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-mm-abort")        { do_dtap_mm_abort = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-mm-status")        { do_dtap_mm_status       = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-mm-cm-reest-req")  { do_dtap_mm_cm_reest_req = true; do_lu = false; do_paging = false; }
        // P28: MM Auth Failure / CM Service Abort / CM Service Prompt
        else if (arg == "--send-dtap-mm-auth-failure")     { do_dtap_mm_auth_failure     = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-mm-cm-service-abort") { do_dtap_mm_cm_service_abort = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-mm-cm-service-prompt"){ do_dtap_mm_cm_service_prompt= true; do_lu = false; do_paging = false; }
        else if (arg == "--auth-fail-cause") { if (i+1<argc) mm_auth_fail_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--with-auts")       { with_auts_param = true; }
        else if (arg == "--pd-sapi")         { if (i+1<argc) pd_sapi_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--mm-status-cause") { if (i+1<argc) mm_status_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--mm-abort-cause")            { if (i+1<argc) mm_abort_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--send-dtap-cipher-compl") {
            do_dtap_cipher_mode_compl = true;
            do_lu = false;  do_paging = false;
        }
        // P28: RR Status / Channel Release / Classmark Change / Classmark Enquiry
        else if (arg == "--send-dtap-rr-status")            { do_dtap_rr_status            = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-channel-release")   { do_dtap_rr_channel_release   = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-classmark-change")  { do_dtap_rr_classmark_change  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-classmark-enquiry") { do_dtap_rr_classmark_enquiry = true; do_lu = false; do_paging = false; }
        // P29: DTAP RR Assignment / Handover Command
        else if (arg == "--send-dtap-rr-assignment-command")  { do_dtap_rr_assignment_command  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-assignment-complete") { do_dtap_rr_assignment_complete = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-assignment-failure")  { do_dtap_rr_assignment_failure  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-handover-command")    { do_dtap_rr_handover_command    = true; do_lu = false; do_paging = false; }
        // P30: DTAP RR HO Complete/Failure / Measurement Report / Channel Mode Modify
        else if (arg == "--send-dtap-rr-handover-complete")   { do_dtap_rr_handover_complete   = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-handover-failure")    { do_dtap_rr_handover_failure    = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-measurement-report")  { do_dtap_rr_measurement_report  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-channel-mode-modify") { do_dtap_rr_channel_mode_modify = true; do_lu = false; do_paging = false; }
        // P31: DTAP RR Ciphering Mode Command/Complete/Reject + Immediate Assignment
        else if (arg == "--send-dtap-rr-ciphering-mode-command")   { do_dtap_rr_ciphering_mode_command   = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-ciphering-mode-complete")  { do_dtap_rr_ciphering_mode_complete  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-ciphering-mode-reject")    { do_dtap_rr_ciphering_mode_reject    = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-rr-immediate-assignment")     { do_dtap_rr_immediate_assignment     = true; do_lu = false; do_paging = false; }
        else if (arg == "--cipher-alg") { if (i+1<argc) rr_cipher_alg_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--rxlev")     { if (i+1<argc) rr_rxlev_param     = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--rxqual")    { if (i+1<argc) rr_rxqual_param    = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--chan-mode") { if (i+1<argc) rr_chan_mode_param  = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--arfcn") { if (i+1<argc) rr_arfcn_param = (uint16_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--bsic")  { if (i+1<argc) rr_bsic_param  = (uint8_t) std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--ts")    { if (i+1<argc) rr_ts_param    = (uint8_t) std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--rr-cause") { if (i+1<argc) rr_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--mm-tz" && i + 1 < argc) mm_tz_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--send-dtap-cm-srv-req") {
            do_dtap_cm_srv_req = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cm-srv-acc") {
            do_dtap_cm_srv_acc = true;
            do_lu = false;  do_paging = false;
        }
        // P21-R: DTAP CM Service Reject + IMSI Detach Indication
        else if (arg == "--send-dtap-cm-srv-rej") {
            do_dtap_cm_srv_rej = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--cm-reject-cause" && i + 1 < argc)
            cm_reject_cause_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--send-dtap-imsi-detach-a") {
            do_dtap_imsi_detach_a = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-setup-mo") {
            do_dtap_cc_setup_mo = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-setup-mt") {
            do_dtap_cc_setup_mt = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-call-proc") {
            do_dtap_cc_call_proc = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-alerting") {
            do_dtap_cc_alerting = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-connect") {
            do_dtap_cc_connect = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-connect-ack") {
            do_dtap_cc_connect_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-disconnect") {
            do_dtap_cc_disconnect = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-release") {
            do_dtap_cc_release = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-rel-compl") {
            do_dtap_cc_rel_compl = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--ti"        && i + 1 < argc) ti_param        = (uint8_t)std::stoul(argv[++i]);
        else if (arg == "--cc-cause"  && i + 1 < argc) cc_cause_param  = (uint8_t)std::stoul(argv[++i]);
        else if (arg == "--ms-to-net") cc_net_to_ms = false;
        else if (arg == "--send-dtap-cc-progress") {
            do_dtap_cc_progress = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--progress-desc" && i + 1 < argc) progress_desc_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--send-dtap-cc-call-confirmed") {
            do_dtap_cc_call_confirmed = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-start-dtmf") {
            do_dtap_cc_start_dtmf = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--dtmf-digit" && i + 1 < argc) dtmf_digit_param = argv[++i][0];
        else if (arg == "--send-dtap-cc-stop-dtmf") {
            do_dtap_cc_stop_dtmf = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-status") {
            do_dtap_cc_status = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-emerg-setup") {
            do_dtap_cc_emerg_setup = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-hold") {
            do_dtap_cc_hold = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-hold-ack") {
            do_dtap_cc_hold_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-hold-reject") {
            do_dtap_cc_hold_reject = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--hold-rej-cause") {
            if (++i < argc) hold_rej_cause_param = (uint8_t)std::stoul(argv[i], nullptr, 0);
        }
        else if (arg == "--send-dtap-cc-retrieve") {
            do_dtap_cc_retrieve = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-retrieve-ack") {
            do_dtap_cc_retrieve_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-cc-retrieve-reject") {
            do_dtap_cc_retrieve_reject = true;
            do_lu = false;  do_paging = false;
        }
        // ── P25: DTAP CC Modify ───────────────────────────────────────────────
        else if (arg == "--send-dtap-cc-modify")          { do_dtap_cc_modify          = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-modify-complete") { do_dtap_cc_modify_complete = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-modify-reject")   { do_dtap_cc_modify_reject   = true; do_lu = false; do_paging = false; }
        else if (arg == "--cc-modify-rej-cause")          { if (i+1<argc) cc_modify_rej_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        // P26: DTAP CC Notify + DTMF ack/rej
        else if (arg == "--send-dtap-cc-notify")           { do_dtap_cc_notify         = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-start-dtmf-ack")   { do_dtap_cc_start_dtmf_ack = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-start-dtmf-rej")   { do_dtap_cc_start_dtmf_rej = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-stop-dtmf-ack")    { do_dtap_cc_stop_dtmf_ack  = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-status-enquiry") { do_dtap_cc_status_enquiry = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-user-info")      { do_dtap_cc_user_info      = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-congestion")     { do_dtap_cc_congestion     = true; do_lu = false; do_paging = false; }
        // P28: CC Facility / Recall / Start CC
        else if (arg == "--send-dtap-cc-facility") { do_dtap_cc_facility = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-recall")   { do_dtap_cc_recall   = true; do_lu = false; do_paging = false; }
        else if (arg == "--send-dtap-cc-start-cc") { do_dtap_cc_start_cc = true; do_lu = false; do_paging = false; }
        else if (arg == "--recall-type") { if (i+1<argc) cc_recall_type_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--uus-proto")   { if (i+1<argc) uus_proto_param   = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--cong-level")  { if (i+1<argc) cong_level_param  = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--cc-notify")                     { if (i+1<argc) cc_notify_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--dtmf-rej-cause")                { if (i+1<argc) dtmf_start_rej_cause_param = (uint8_t)std::stoul(argv[++i],nullptr,0); }
        else if (arg == "--dtmf-key")                      { if (i+1<argc) dtmf_key_param = argv[++i][0]; }
        else if (arg == "--retr-rej-cause") {
            if (++i < argc) retr_rej_cause_param = (uint8_t)std::stoul(argv[i], nullptr, 0);
        }
        else if (arg == "--call-state" && i + 1 < argc) call_state_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--send-dtap-sms-cp-data") {
            do_dtap_sms_cp_data = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-sms-cp-ack") {
            do_dtap_sms_cp_ack = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--send-dtap-sms-cp-error") {
            do_dtap_sms_cp_error = true;
            do_lu = false;  do_paging = false;
        }
        else if (arg == "--cp-cause" && i + 1 < argc) cp_cause_param = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--rand"       && i + 1 < argc) rand_param        = argv[++i];
        else if (arg == "--sres"       && i + 1 < argc) sres_param        = argv[++i];
        else if (arg == "--cksn"       && i + 1 < argc) cksn_param        = (uint8_t)std::stoul(argv[++i]);
        else if (arg == "--tmsi"       && i + 1 < argc) tmsi_param        = (uint32_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--id-type"    && i + 1 < argc) id_type_param     = (uint8_t)std::stoul(argv[++i]);
        else if (arg == "--lu-cause"   && i + 1 < argc) lu_cause_param    = (uint8_t)std::stoul(argv[++i], nullptr, 0);
        else if (arg == "--cipher-alg" && i + 1 < argc) cipher_alg_param  = (uint8_t)std::stoul(argv[++i], nullptr, 0);
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
        new_cfg.a_local_spid    = a_local_spid;     new_cfg.a_remote_spid    = a_remote_spid;
        new_cfg.c_local_spid    = c_local_spid;     new_cfg.c_remote_spid    = c_remote_spid;
        new_cfg.f_local_spid    = f_local_spid;     new_cfg.f_remote_spid    = f_remote_spid;
        new_cfg.e_local_spid    = e_local_spid;     new_cfg.e_remote_spid    = e_remote_spid;
        new_cfg.nc_local_spid   = nc_local_spid;    new_cfg.nc_remote_spid   = nc_remote_spid;
        new_cfg.isup_local_spid = isup_local_spid;  new_cfg.isup_remote_spid = isup_remote_spid;
        new_cfg.gs_local_spid   = gs_local_spid;    new_cfg.gs_remote_spid   = gs_remote_spid;
        
        if (save_config(config_path, new_cfg)) {
            std::cout << COLOR_GREEN << "✓ Конфигурация сохранена в " << config_path << COLOR_RESET << "\n\n";
        } else {
            std::cerr << COLOR_YELLOW << "⚠ Не удалось сохранить конфигурацию в " << config_path << COLOR_RESET << "\n\n";
        }
    }

    std::cout << "vMSC инициализирован\n";
    if (!loaded_configs.empty()) {
        std::cout << COLOR_GREEN;
        for (const auto &p : loaded_configs)
            std::cout << "✓ Конфигурация загружена из " << p << "\n";
        std::cout << COLOR_RESET;
    }
    std::cout << "\n";
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

    // Вывод MTP3-параметров SI/SLS/MP (единый формат для всех интерфейсов)
    auto print_mtp3_params = [&](uint8_t si, uint8_t sls_val, uint8_t mp_val) {
        const char *si_name = (si == 3) ? "SCCP" : (si == 5) ? "ISUP" :
                              (si == 4) ? "TUP"  : (si == 0) ? "SNM"  : "?";
        std::cout << "    SI="  << COLOR_GREEN << (int)si  << " (" << si_name << ")" << COLOR_RESET
                  << "   SLS=" << COLOR_GREEN << (int)sls_val << COLOR_RESET
                  << "   MP="  << COLOR_GREEN << (int)mp_val  << COLOR_RESET << "\n";
    };

    // Вывод сетевых параметров узла (маска + шлюз) — единый для всех Transport-блоков
    auto print_netmask_gw = [&]() {
        std::cout << "    Mask-L: " << COLOR_GREEN << local_netmask  << COLOR_RESET
                  << "   Mask-R: " << COLOR_GREEN << remote_netmask << COLOR_RESET << "\n";
        std::cout << "    GW:     " << COLOR_GREEN << gateway << COLOR_RESET << "\n";
    };

    // Вывод SPID-меток сигнальных точек — OPC-имя → DPC-имя
    auto print_spid = [&](const std::string &ls, const std::string &rs) {
        if (!ls.empty() || !rs.empty()) {
            std::cout << "    SPID: "
                      << COLOR_CYAN << (ls.empty() ? "—" : ls) << COLOR_RESET
                      << "  →  "
                      << COLOR_CYAN << (rs.empty() ? "—" : rs) << COLOR_RESET << "\n";
        }
    };

    // [subscriber] / старый [identity]
    if (show_all || show_subscriber || show_identity) {
        print_section_header("[subscriber]");
        std::cout << "  IMSI:   " << COLOR_GREEN << imsi   << COLOR_RESET << "\n";
        if (!msisdn.empty())
            std::cout << "  MSISDN: " << COLOR_GREEN << msisdn << COLOR_RESET << "\n";
        std::cout << "\n";
    }

    // [network]: маска, шлюз, NTP
    if (show_all || show_network) {
        print_section_header("[network]", "Сетевые параметры узла");
        std::cout << "  " << COLOR_CYAN << "NTP (источник синхронизации):" << COLOR_RESET << "\n";
        if (!ntp_primary.empty()) {
            std::cout << "    Основной: " << COLOR_GREEN << ntp_primary << COLOR_RESET << "\n";
            if (!ntp_secondary.empty())
                std::cout << "    Резервный: " << COLOR_GREEN << ntp_secondary << COLOR_RESET << "\n";
        } else {
            std::cout << "    " << COLOR_YELLOW << "(не настроен)" << COLOR_RESET << "\n";
        }
        std::cout << "  " << COLOR_CYAN << "Маски подсетей / Шлюз:" << COLOR_RESET << "\n";
        std::cout << "    Mask-L: " << COLOR_GREEN << local_netmask  << COLOR_RESET
                  << "   Mask-R: " << COLOR_GREEN << remote_netmask << COLOR_RESET << "\n";
        std::cout << "    GW:     " << COLOR_GREEN << gateway << COLOR_RESET << "\n";
        std::cout << "\n";
    }

    // Вспомогательная функция: SSN → строковое имя (ITU Q.713 Annex B / 3GPP TS 29.002)
    auto ssn_name = [](uint8_t ssn) -> std::string {
        switch (ssn) {
            case   5: return "MAP";
            case   6: return "HLR";
            case   7: return "VLR";
            case   8: return "MSC/VLR";
            case  11: return "EIR";
            case 149: return "RANAP";
            case 253: return "BSSOM";
            case 254: return "BSSAP";
            default:  return "SSN=" + std::to_string((int)ssn);
        }
    };

    // [a-interface]: сеть + BSSMAP + M3UA + транспорт
    if (show_all || show_a_interface || show_network || show_m3ua || show_bssmap || show_transport) {
        print_section_header("[A-interface]");

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
            print_spid(a_local_spid, a_remote_spid);
            std::cout << "    NI=" << COLOR_GREEN << (int)m3ua_ni << COLOR_RESET << " (Reserved):\n";
            std::cout << "      OPC=" << COLOR_GREEN << m3ua_opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << m3ua_dpc << COLOR_RESET << "\n";
            print_mtp3_params(a_si, sls, mp);
        }
        if (show_all || show_a_interface) {
            std::cout << "  " << COLOR_CYAN << "SCCP:" << COLOR_RESET << "\n";
            std::cout << "    SSN=" << COLOR_GREEN << (int)a_ssn
                      << COLOR_RESET << " (" << ssn_name(a_ssn) << ")\n";
        }
        if (show_all || show_a_interface || show_transport) {
            std::cout << "  " << COLOR_CYAN << "Transport (UDP):" << COLOR_RESET << "\n";
            std::cout << "    LIP: " << COLOR_GREEN << local_ip << COLOR_RESET;
            if (local_port) std::cout << "  LP: " << COLOR_GREEN << local_port << COLOR_RESET;
            std::cout << "\n";
            std::cout << "    RIP: " << COLOR_GREEN << remote_ip  << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << remote_port << COLOR_RESET << "\n";
            print_netmask_gw();
        }
        std::cout << "\n";
    }

    // [c-interface]: MSC ↔ HLR
    if (show_all || show_c_interface) {
        print_section_header("[C-interface]", "MSC ↔ HLR (MAP over SCCP/M3UA)");
        // M3UA (один NI)
        std::cout << "  " << COLOR_CYAN << "M3UA:" << COLOR_RESET << "\n";
        {
            print_spid(c_local_spid, c_remote_spid);
            const char *ni_label = (c_m3ua_ni == 0) ? "International" :
                                   (c_m3ua_ni == 2) ? "National" : "Reserved";
            std::cout << "    NI=" << COLOR_GREEN << (int)c_m3ua_ni << COLOR_RESET << " (" << ni_label << "):\n";
            std::cout << "      OPC=" << COLOR_GREEN << c_opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << c_dpc << COLOR_RESET << "\n";
            print_mtp3_params(c_si, sls, mp);
        }
        std::cout << "  " << COLOR_CYAN << "SCCP:" << COLOR_RESET << "\n";
        std::cout << "    Calling SSN=" << COLOR_GREEN << (int)c_ssn_local
                  << COLOR_RESET << " (" << ssn_name(c_ssn_local) << ")\n";
        std::cout << "    Called  SSN=" << COLOR_GREEN << (int)c_ssn_remote
                  << COLOR_RESET << " (" << ssn_name(c_ssn_remote) << ")\n";
        std::cout << "  " << COLOR_CYAN << "Global Title:" << COLOR_RESET << "\n";
        if (c_gt_ind != 0) {
            std::cout << "    Mode:       " << COLOR_GREEN << "Route on GT" << COLOR_RESET
                      << " (GTI=" << (int)c_gt_ind << ", RI=0)\n";
            std::cout << "    TT/NP/NAI:  " << COLOR_GREEN << (int)gt_tt << "/" << (int)gt_np
                      << "/" << (int)gt_nai << COLOR_RESET << "\n";
            if (!msc_gt.empty())
                std::cout << "    Calling GT: " << COLOR_GREEN << msc_gt << COLOR_RESET << " (MSC)\n";
            if (!c_gt_called.empty())
                std::cout << "    Called  GT: " << COLOR_GREEN << c_gt_called << COLOR_RESET << " (HLR)\n";
        } else {
            std::cout << "    " << COLOR_YELLOW << "(GT не настроен — маршрутизация по SSN)"
                      << COLOR_RESET << "\n";
        }
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << c_local_ip << COLOR_RESET;
        if (c_local_port) std::cout << "  LP: " << COLOR_GREEN << c_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!c_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << c_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << c_remote_port << COLOR_RESET << "\n";
        print_netmask_gw();
        std::cout << "\n";
    }

    // [f-interface]: MSC ↔ EIR
    if (show_all || show_f_interface) {
        print_section_header("[F-interface]", "MSC \u2194 EIR (MAP CheckIMEI over SCCP/M3UA)");
        std::cout << "  " << COLOR_CYAN << "M3UA:" << COLOR_RESET << "\n";
        {
            print_spid(f_local_spid, f_remote_spid);
            const char *ni_label = (f_m3ua_ni == 0) ? "International" :
                                   (f_m3ua_ni == 2) ? "National" : "Reserved";
            std::cout << "    NI=" << COLOR_GREEN << (int)f_m3ua_ni << COLOR_RESET << " (" << ni_label << "):\n";
            std::cout << "      OPC=" << COLOR_GREEN << f_opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << f_dpc << COLOR_RESET << "\n";
            print_mtp3_params(f_si, sls, mp);
        }
        std::cout << "  " << COLOR_CYAN << "SCCP:" << COLOR_RESET << "\n";
        std::cout << "    Calling SSN=" << COLOR_GREEN << (int)f_ssn_local
                  << COLOR_RESET << " (" << ssn_name(f_ssn_local) << ")\n";
        std::cout << "    Called  SSN=" << COLOR_GREEN << (int)f_ssn_remote
                  << COLOR_RESET << " (" << ssn_name(f_ssn_remote) << ")\n";
        std::cout << "  " << COLOR_CYAN << "Global Title:" << COLOR_RESET << "\n";
        if (f_gt_ind != 0) {
            std::cout << "    Mode:       " << COLOR_GREEN << "Route on GT" << COLOR_RESET
                      << " (GTI=" << (int)f_gt_ind << ", RI=0)\n";
            std::cout << "    TT/NP/NAI:  " << COLOR_GREEN << (int)gt_tt << "/" << (int)gt_np
                      << "/" << (int)gt_nai << COLOR_RESET << "\n";
            if (!msc_gt.empty())
                std::cout << "    Calling GT: " << COLOR_GREEN << msc_gt << COLOR_RESET << " (MSC)\n";
            if (!f_gt_called.empty())
                std::cout << "    Called  GT: " << COLOR_GREEN << f_gt_called << COLOR_RESET << " (EIR)\n";
        } else {
            std::cout << "    " << COLOR_YELLOW << "(GT не настроен — маршрутизация по SSN)"
                      << COLOR_RESET << "\n";
        }
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << f_local_ip << COLOR_RESET;
        if (f_local_port) std::cout << "  LP: " << COLOR_GREEN << f_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!f_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << f_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << f_remote_port << COLOR_RESET << "\n";
        print_netmask_gw();
        std::cout << "\n";
    }

    // [e-interface]: MSC ↔ MSC
    if (show_all || show_e_interface) {
        print_section_header("[E-interface]", "MSC ↔ MSC (межсистемный хендовер, MAP)");
        // M3UA (три NI варианта)
        std::cout << "  " << COLOR_CYAN << "M3UA:" << COLOR_RESET << "\n";
        auto print_e_ni_row = [&](uint8_t ni, uint32_t opc, uint32_t dpc, const char *label) {
            std::string marker = (ni == e_m3ua_ni) ? COLOR_GREEN + std::string(" ◄ active") + COLOR_RESET : "";
            std::cout << "    NI=" << COLOR_GREEN << (int)ni << COLOR_RESET << " (" << label << "):" << marker << "\n";
            std::cout << "      OPC=" << COLOR_GREEN << opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << dpc << COLOR_RESET << "\n";
        };
        print_spid(e_local_spid, e_remote_spid);
        print_e_ni_row(0, e_opc_ni0, e_dpc_ni0, "International");
        print_e_ni_row(2, e_opc_ni2, e_dpc_ni2, "National");
        print_e_ni_row(3, e_opc_ni3, e_dpc_ni3, "Reserved");
        print_mtp3_params(e_si, sls, mp);
        std::cout << "  " << COLOR_CYAN << "SCCP:" << COLOR_RESET << "\n";
        std::cout << "    Calling SSN=" << COLOR_GREEN << (int)e_ssn_local
                  << COLOR_RESET << " (" << ssn_name(e_ssn_local) << ")\n";
        std::cout << "    Called  SSN=" << COLOR_GREEN << (int)e_ssn_remote
                  << COLOR_RESET << " (" << ssn_name(e_ssn_remote) << ")\n";
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << e_local_ip << COLOR_RESET;
        if (e_local_port) std::cout << "  LP: " << COLOR_GREEN << e_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!e_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << e_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << e_remote_port << COLOR_RESET << "\n";
        print_netmask_gw();
        std::cout << "\n";
    }

    // [nc-interface]: MSC-S ↔ MGW
    if (show_all || show_nc_interface) {
        print_section_header("[Nc-interface]", "MSC-S ↔ MGW (H.248/MEGACO)");
        // M3UA (три NI варианта)
        std::cout << "  " << COLOR_CYAN << "M3UA:" << COLOR_RESET << "\n";
        auto print_nc_ni_row = [&](uint8_t ni, uint32_t opc, uint32_t dpc, const char *label) {
            std::string marker = (ni == nc_m3ua_ni) ? COLOR_GREEN + std::string(" ◄ active") + COLOR_RESET : "";
            std::cout << "    NI=" << COLOR_GREEN << (int)ni << COLOR_RESET << " (" << label << "):" << marker << "\n";
            std::cout << "      OPC=" << COLOR_GREEN << opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << dpc << COLOR_RESET << "\n";
        };
        print_spid(nc_local_spid, nc_remote_spid);
        print_nc_ni_row(0, nc_opc_ni0, nc_dpc_ni0, "International");
        print_nc_ni_row(2, nc_opc_ni2, nc_dpc_ni2, "National");
        print_nc_ni_row(3, nc_opc_ni3, nc_dpc_ni3, "Reserved");
        print_mtp3_params(nc_si, sls, mp);
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << nc_local_ip << COLOR_RESET;
        if (nc_local_port) std::cout << "  LP: " << COLOR_GREEN << nc_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!nc_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << nc_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << nc_remote_port << COLOR_RESET << "\n";
        print_netmask_gw();
        std::cout << "\n";
    }

    // [isup-interface]: MSC ↔ PSTN/GW
    if (show_all || show_isup_interface) {
        print_section_header("[ISUP-interface]", "MSC ↔ PSTN/GW (ISUP over MTP3/M3UA)");
        std::cout << "  " << COLOR_CYAN << "MTP3/M3UA:" << COLOR_RESET << "\n";
        auto print_isup_ni_row = [&](uint8_t ni, uint32_t opc, uint32_t dpc, const char *label) {
            std::string marker = (ni == isup_m3ua_ni) ? COLOR_GREEN + std::string(" ◄ active") + COLOR_RESET : "";
            std::cout << "    NI=" << COLOR_GREEN << (int)ni << COLOR_RESET << " (" << label << "):" << marker << "\n";
            std::cout << "      OPC=" << COLOR_GREEN << opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << dpc << COLOR_RESET << "\n";
        };
        print_spid(isup_local_spid, isup_remote_spid);
        print_isup_ni_row(0, isup_opc_ni0, isup_dpc_ni0, "International");
        print_isup_ni_row(2, isup_opc_ni2, isup_dpc_ni2, "National");
        print_mtp3_params(isup_si, sls, mp);
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << isup_local_ip << COLOR_RESET;
        if (isup_local_port) std::cout << "  LP: " << COLOR_GREEN << isup_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!isup_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << isup_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << isup_remote_port << COLOR_RESET << "\n";
        print_netmask_gw();
        std::cout << "\n";
    }

    // [gs-interface]: MSC ↔ SGSN
    if (show_all || show_gs_interface) {
        print_section_header("[Gs-interface]", "MSC ↔ SGSN (BSSAP+ over SCCP/M3UA)");
        std::cout << "  " << COLOR_CYAN << "SCCP/M3UA:" << COLOR_RESET << "\n";
        auto print_gs_ni_row = [&](uint8_t ni, uint32_t opc, uint32_t dpc, const char *label) {
            std::string marker = (ni == gs_m3ua_ni) ? COLOR_GREEN + std::string(" ◄ active") + COLOR_RESET : "";
            std::cout << "    NI=" << COLOR_GREEN << (int)ni << COLOR_RESET << " (" << label << "):" << marker << "\n";
            std::cout << "      OPC=" << COLOR_GREEN << opc << COLOR_RESET << "\n";
            std::cout << "      DPC=" << COLOR_GREEN << dpc << COLOR_RESET << "\n";
        };
        print_spid(gs_local_spid, gs_remote_spid);
        print_gs_ni_row(2, gs_opc_ni2, gs_dpc_ni2, "National");
        print_gs_ni_row(3, gs_opc_ni3, gs_dpc_ni3, "Reserved");
        print_mtp3_params(gs_si, sls, mp);
        std::cout << "  " << COLOR_CYAN << "SCCP:" << COLOR_RESET << "\n";
        std::cout << "    Calling SSN=" << COLOR_GREEN << (int)gs_ssn_local
                  << COLOR_RESET << " (" << ssn_name(gs_ssn_local) << ")\n";
        std::cout << "    Called  SSN=" << COLOR_GREEN << (int)gs_ssn_remote
                  << COLOR_RESET << " (" << ssn_name(gs_ssn_remote) << ")\n";
        std::cout << "  " << COLOR_CYAN << "Transport:" << COLOR_RESET << "\n";
        std::cout << "    LIP: " << COLOR_GREEN << gs_local_ip << COLOR_RESET;
        if (gs_local_port) std::cout << "  LP: " << COLOR_GREEN << gs_local_port << COLOR_RESET;
        std::cout << "\n";
        if (!gs_remote_ip.empty())
            std::cout << "    RIP: " << COLOR_GREEN << gs_remote_ip << COLOR_RESET
                      << "  RP: " << COLOR_GREEN << gs_remote_port << COLOR_RESET << "\n";
        print_netmask_gw();
        std::cout << "\n";
    }

    // [gt-route]: таблица GT-маршрутизации SCCP
    if (show_all || show_gt_route) {
        print_section_header("[gt-route]", "Таблица GT-маршрутизации SCCP");
        auto iface_label = [](const std::string &iface) -> std::string {
            if      (iface == "a")    return "A  (BSC)";
            else if (iface == "c")    return "C  (HLR)";
            else if (iface == "f")    return "F  (EIR)";
            else if (iface == "e")    return "E  (MSC)";
            else if (iface == "nc")   return "Nc (MGW)";
            else if (iface == "isup") return "ISUP(PSTN)";
            else if (iface == "gs")   return "Gs (SGSN)";
            return iface;
        };
        if (gt_routes.empty()) {
            std::cout << "  " << COLOR_YELLOW << "(таблица пуста — route= в [gt-route] в vmsc_interfaces.conf)" << COLOR_RESET << "\n";
        } else {
            // unicode-safe padding (не считает байты продолжения UTF-8)
            auto padR = [](const std::string &s, int w) -> std::string {
                int chars = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++chars;
                int pad = w - chars; if (pad < 1) pad = 1;
                return s + std::string(pad, ' ');
            };
            // Авто-корреляция DPC → remote_spid из конфигурации интерфейсов
            std::map<uint32_t, std::string> dpc_spid_map;
            auto add_ds = [&](uint32_t dpc, const std::string &spid) {
                if (dpc && !spid.empty()) dpc_spid_map[dpc] = spid;
            };
            add_ds(cfg.m3ua_dpc,      cfg.a_remote_spid);
            add_ds(cfg.c_dpc,         cfg.c_remote_spid);
            add_ds(cfg.f_dpc,         cfg.f_remote_spid);
            add_ds(cfg.e_dpc_ni0,     cfg.e_remote_spid);
            add_ds(cfg.e_dpc_ni2,     cfg.e_remote_spid);
            add_ds(cfg.e_dpc_ni3,     cfg.e_remote_spid);
            add_ds(cfg.nc_dpc_ni0,    cfg.nc_remote_spid);
            add_ds(cfg.nc_dpc_ni2,    cfg.nc_remote_spid);
            add_ds(cfg.nc_dpc_ni3,    cfg.nc_remote_spid);
            add_ds(cfg.isup_dpc_ni0,  cfg.isup_remote_spid);
            add_ds(cfg.isup_dpc_ni2,  cfg.isup_remote_spid);
            add_ds(cfg.gs_dpc_ni2,    cfg.gs_remote_spid);
            add_ds(cfg.gs_dpc_ni3,    cfg.gs_remote_spid);
            // заголовок таблицы
            std::cout << "  " << COLOR_CYAN
                      << padR("Префикс", 14) << padR("Интерфейс", 14)
                      << padR("DPC", 10)     << padR("SPID", 16) << "Описание"
                      << COLOR_RESET << "\n";
            std::cout << "  " << std::string(72, '-') << "\n";
            for (const auto &r : gt_routes) {
                std::string dpc_str = std::to_string(r.dpc);
                // Разрешаем SPID: явный → авто-корреляция → пусто
                std::string spid_label = r.spid;
                bool auto_resolved = false;
                if (spid_label.empty()) {
                    auto it = dpc_spid_map.find(r.dpc);
                    if (it != dpc_spid_map.end()) {
                        spid_label = it->second;
                        auto_resolved = true;
                    }
                }
                std::string spid_col;
                if (spid_label.empty())
                    spid_col = std::string(COLOR_YELLOW) + padR("—", 16) + COLOR_RESET;
                else
                    spid_col = std::string(auto_resolved ? COLOR_CYAN : COLOR_GREEN)
                               + padR(spid_label, 16) + COLOR_RESET;
                std::cout << "  "
                          << COLOR_GREEN << padR(r.prefix, 14) << COLOR_RESET
                          << padR(iface_label(r.iface), 14)
                          << padR(dpc_str, 10)
                          << spid_col
                          << r.description << "\n";
            }
            // longest-prefix lookup демо: показать для msc_gt
            if (!msc_gt.empty()) {
                const GtRoute *best = nullptr;
                for (const auto &r : gt_routes)
                    if (msc_gt.substr(0, r.prefix.size()) == r.prefix)
                        if (!best || r.prefix.size() > best->prefix.size())
                            best = &r;
                std::cout << "\n  Поиск для msc_gt=" << COLOR_GREEN << msc_gt << COLOR_RESET << ": ";
                if (best)
                    std::cout << COLOR_GREEN << "[" << best->prefix << "] → "
                              << best->iface << " DPC=" << best->dpc
                              << " (" << best->description << ")" << COLOR_RESET << "\n";
                else
                    std::cout << COLOR_YELLOW << "нет маршрута" << COLOR_RESET << "\n";
            }
            // легенда цветов SPID
            std::cout << "\n  "
                      << COLOR_GREEN  << "■" << COLOR_RESET << " явный SPID (5-е поле route=)   "
                      << COLOR_CYAN   << "■" << COLOR_RESET << " авто-корреляция по DPC   "
                      << COLOR_YELLOW << "■" << COLOR_RESET << " SPID неизвестен\n";
        }
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

    if (do_lu) {
        print_section_header("[Location Update Request]");
        std::cout << "\n";
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
                                sccp_msg = wrap_in_sccp_cr(bssap_msg, a_ssn);
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
                                sccp_msg = wrap_in_sccp_cr(bssap_msg, a_ssn);
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
                    sccp_msg = wrap_in_sccp_cr(clear_msg, a_ssn);
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

    // ── C-интерфейс: MAP SendAuthenticationInfo ─────────────────────────────
    if (do_map_sai) {
        print_section_header("[MAP SendAuthenticationInfo]", "MSC \xe2\x86\x92 HLR  (C-interface)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_send_auth_info(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                         c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW
                          << "\xe2\x9a\xa0 C-\xd0\xb8\xd0\xbd\xd1\x82\xd0\xb5\xd1\x80\xd1\x84\xd0\xb5\xd0\xb9\xd1\x81: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd \xd0\xb2 [c-interface]\n"
                          << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── C-интерфейс: MAP UpdateLocation ─────────────────────────────────────
    if (do_map_ul) {
        print_section_header("[MAP UpdateLocation]", "MSC/VLR \xe2\x86\x92 HLR  (C-interface)");
        std::cout << "\n";
        std::string vlr_num = msisdn.empty() ? "79990000001" : msisdn;
        struct msgb *map_msg = generate_map_update_location(imsi.c_str(), vlr_num.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                         c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW
                          << "\xe2\x9a\xa0 C-\xd0\xb8\xd0\xbd\xd1\x82\xd0\xb5\xd1\x80\xd1\x84\xd0\xb5\xd0\xb9\xd1\x81: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd \xd0\xb2 [c-interface]\n"
                          << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── F-интерфейс: MAP CheckIMEI ──────────────────────────────────────────
    if (do_map_check_imei) {
        print_section_header("[MAP CheckIMEI]", "MSC/VLR \xe2\x86\x92 EIR  (F-interface)");
        std::cout << "\n";
        const std::string imei = imsi.empty() ? "490154203237518" : imsi.substr(0, 15);
        struct msgb *map_msg = generate_map_check_imei(imei.c_str());
        if (map_msg) {
            if (send_udp && !f_remote_ip.empty()) {
                ScpAddr f_called  { f_ssn_remote, f_gt_ind, gt_tt, gt_np, gt_nai, f_gt_called };
                ScpAddr f_calling { f_ssn_local,  f_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, f_called, f_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, f_opc, f_dpc, f_m3ua_ni, f_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                         f_remote_ip.c_str(), f_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW
                          << "\xe2\x9a\xa0 F-\xd0\xb8\xd0\xbd\xd1\x82\xd0\xb5\xd1\x80\xd1\x84\xd0\xb5\xd0\xb9\xd1\x81: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd \xd0\xb2 [f-interface]\n"
                          << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── E-интерфейс: MAP PrepareHandover ────────────────────────────────────
    if (do_map_prepare_ho) {
        print_section_header("[MAP PrepareHandover]", "Anchor MSC \xe2\x86\x92 Relay MSC  (E-interface)");
        std::cout << "\n";
        uint32_t e_opc = (e_m3ua_ni == 0) ? e_opc_ni0 : (e_m3ua_ni == 2) ? e_opc_ni2 : e_opc_ni3;
        uint32_t e_dpc = (e_m3ua_ni == 0) ? e_dpc_ni0 : (e_m3ua_ni == 2) ? e_dpc_ni2 : e_dpc_ni3;
        struct msgb *map_msg = generate_map_prepare_ho(0xAAAA, 0x0001);
        if (map_msg) {
            if (send_udp && !e_remote_ip.empty()) {
                ScpAddr e_called  { e_ssn_remote, e_gt_ind, gt_tt, gt_np, gt_nai, e_gt_called };
                ScpAddr e_calling { e_ssn_local,  e_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, e_called, e_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, e_opc, e_dpc, e_m3ua_ni, e_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                         e_remote_ip.c_str(), e_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW
                          << "\xe2\x9a\xa0 E-\xd0\xb8\xd0\xbd\xd1\x82\xd0\xb5\xd1\x80\xd1\x84\xd0\xb5\xd0\xb9\xd1\x81: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd \xd0\xb2 [e-interface]\n"
                          << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── E-интерфейс: MAP SendEndSignal ──────────────────────────────────────
    if (do_map_end_signal) {
        print_section_header("[MAP SendEndSignal]", "Relay MSC \xe2\x86\x92 Anchor MSC  (E-interface)");
        std::cout << "\n";
        uint32_t e_opc = (e_m3ua_ni == 0) ? e_opc_ni0 : (e_m3ua_ni == 2) ? e_opc_ni2 : e_opc_ni3;
        uint32_t e_dpc = (e_m3ua_ni == 0) ? e_dpc_ni0 : (e_m3ua_ni == 2) ? e_dpc_ni2 : e_dpc_ni3;
        struct msgb *map_msg = generate_map_send_end_signal();
        if (map_msg) {
            if (send_udp && !e_remote_ip.empty()) {
                ScpAddr e_called  { e_ssn_remote, e_gt_ind, gt_tt, gt_np, gt_nai, e_gt_called };
                ScpAddr e_calling { e_ssn_local,  e_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, e_called, e_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, e_opc, e_dpc, e_m3ua_ni, e_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                         e_remote_ip.c_str(), e_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW
                          << "\xe2\x9a\xa0 E-\xd0\xb8\xd0\xbd\xd1\x82\xd0\xb5\xd1\x80\xd1\x84\xd0\xb5\xd0\xb9\xd1\x81: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd \xd0\xb2 [e-interface]\n"
                          << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── E-интерфейс: MAP PrepareSubsequentHandover ──────────────────────────
    if (do_map_prep_subsequent_ho) {
        print_section_header("[MAP PrepareSubsequentHandover]",
                             "Anchor MSC \xe2\x86\x92 Target MSC  (E-interface)");
        std::cout << "\n";
        uint32_t e_opc = (e_m3ua_ni == 0) ? e_opc_ni0 : (e_m3ua_ni == 2) ? e_opc_ni2 : e_opc_ni3;
        uint32_t e_dpc = (e_m3ua_ni == 0) ? e_dpc_ni0 : (e_m3ua_ni == 2) ? e_dpc_ni2 : e_dpc_ni3;
        struct msgb *map_msg = generate_map_prep_subsequent_ho();
        if (map_msg) {
            if (send_udp && !e_remote_ip.empty()) {
                ScpAddr e_called  { e_ssn_remote, e_gt_ind, gt_tt, gt_np, gt_nai, e_gt_called };
                ScpAddr e_calling { e_ssn_local,  e_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, e_called, e_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, e_opc, e_dpc,
                                                         e_m3ua_ni, e_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                         e_remote_ip.c_str(), e_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW
                          << "\xe2\x9a\xa0 E-\xd0\xb8\xd0\xbd\xd1\x82\xd0\xb5\xd1\x80\xd1\x84\xd0\xb5\xd0\xb9\xd1\x81: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd \xd0\xb2 [e-interface]\n"
                          << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── E-интерфейс: MAP ProcessAccessSignalling ─────────────────────────────
    if (do_map_process_access_sig) {
        print_section_header("[MAP ProcessAccessSignalling]",
                             "Target MSC \xe2\x86\x92 Anchor MSC  (E-interface)");
        std::cout << "\n";
        uint32_t e_opc = (e_m3ua_ni == 0) ? e_opc_ni0 : (e_m3ua_ni == 2) ? e_opc_ni2 : e_opc_ni3;
        uint32_t e_dpc = (e_m3ua_ni == 0) ? e_dpc_ni0 : (e_m3ua_ni == 2) ? e_dpc_ni2 : e_dpc_ni3;
        struct msgb *map_msg = generate_map_process_access_signalling();
        if (map_msg) {
            if (send_udp && !e_remote_ip.empty()) {
                ScpAddr e_called  { e_ssn_remote, e_gt_ind, gt_tt, gt_np, gt_nai, e_gt_called };
                ScpAddr e_calling { e_ssn_local,  e_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, e_called, e_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, e_opc, e_dpc,
                                                         e_m3ua_ni, e_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                         e_remote_ip.c_str(), e_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW
                          << "\xe2\x9a\xa0 E-\xd0\xb8\xd0\xbd\xd1\x82\xd0\xb5\xd1\x80\xd1\x84\xd0\xb5\xd0\xb9\xd1\x81: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd \xd0\xb2 [e-interface]\n"
                          << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }



    // ── P25: MAP SendIdentification ──────────────────────────────────────────
    if (do_map_send_identification) {
        print_section_header("[MAP SendIdentification]", "E-interface  новый VLR → старый VLR  (opCode=55)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_send_identification(imsi.c_str());
        if (map_msg) {
            if (send_udp && !e_remote_ip.empty()) {
                uint32_t e_opc_v = (e_m3ua_ni == 0) ? e_opc_ni0 : (e_m3ua_ni == 2) ? e_opc_ni2 : e_opc_ni3;
                uint32_t e_dpc_v = (e_m3ua_ni == 0) ? e_dpc_ni0 : (e_m3ua_ni == 2) ? e_dpc_ni2 : e_dpc_ni3;
                ScpAddr e_called  { e_ssn_remote, e_gt_ind, gt_tt, gt_np, gt_nai, e_gt_called };
                ScpAddr e_calling { e_ssn_local,  e_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, e_called, e_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, e_opc_v, e_dpc_v, e_m3ua_ni, e_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, e_remote_ip.c_str(), e_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ E-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── P25: MAP RestoreData ──────────────────────────────────────────────────
    if (do_map_restore_data) {
        print_section_header("[MAP RestoreData]", "C-interface  VLR → HLR  (opCode=57)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_restore_data(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── P25: MAP ForwardCheckSS-Indication ───────────────────────────────────
    if (do_map_forward_check_ss) {
        print_section_header("[MAP ForwardCheckSS-Indication]", "C-interface  HLR → VLR  (opCode=38)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_forward_check_ss(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }


    // ── P26: MAP NoteSubscriberPresent ───────────────────────────────────────────────────────
    if (do_map_note_subscriber_present) {
        print_section_header("[MAP NoteSubscriberPresent]", "C-interface  VLR → HLR  (opCode=48)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_note_subscriber_present(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── P26: MAP ReadyForSM ───────────────────────────────────────────────────────
    if (do_map_ready_for_sm) {
        print_section_header("[MAP ReadyForSM]", "C-interface  VLR → HLR  (opCode=66)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_ready_for_sm(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── P26: MAP ProvideSubscriberInfo ───────────────────────────────────────────────────────
    if (do_map_provide_subscriber_info) {
        print_section_header("[MAP ProvideSubscriberInfo]", "C-interface  HLR → VLR  (opCode=70)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_provide_subscriber_info(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── P26: MAP SendIMSI ───────────────────────────────────────────────────────
    if (do_map_send_imsi) {
        print_section_header("[MAP SendIMSI]", "C-interface  VLR → HLR  (opCode=58)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_send_imsi(msisdn_param.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── P27: MAP CheckIMEI Result ────────────────────────────────────────────────────────
    if (do_map_check_imei_res) {
        print_section_header("[MAP CheckIMEI Result]", "F-interface  EIR → MSC  (opCode=43)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_check_imei_res(equip_status_param);
        if (map_msg) {
            if (send_udp && !f_remote_ip.empty()) {
                ScpAddr f_called  { f_ssn_remote, f_gt_ind, gt_tt, gt_np, gt_nai, f_gt_called };
                ScpAddr f_calling { f_ssn_local,  f_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, f_called, f_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, f_opc, f_dpc, f_m3ua_ni, f_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, f_remote_ip.c_str(), f_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ F-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    // ── P27: MAP AnyTimeModification ────────────────────────────────────────────────────────
    if (do_map_any_time_modification) {
        print_section_header("[MAP AnyTimeModification]", "C-interface  MSC → HLR  (opCode=65)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_any_time_modification(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    // ── P27: MAP ActivateTraceMode ────────────────────────────────────────────────────────
    if (do_map_activate_trace_mode) {
        print_section_header("[MAP ActivateTraceMode]", "C-interface  HLR → VLR  (opCode=50)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_activate_trace_mode(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP NotifySubscriberData ─────────────────────────────────────────────
    if (do_map_notify_subscriber_data) {
        print_section_header("[MAP NotifySubscriberData]", "C-interface  HLR → VLR  (opCode=120)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_notify_subscriber_data(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP ForwardAccessSignalling ──────────────────────────────────────────
    if (do_map_forward_access_signalling) {
        print_section_header("[MAP ForwardAccessSignalling]", "E-interface  MSC → MSC  (opCode=33)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_forward_access_signalling(imsi.c_str());
        if (map_msg) {
            if (send_udp && !e_remote_ip.empty()) {
                ScpAddr e_called  { e_ssn_remote, e_gt_ind, gt_tt, gt_np, gt_nai, e_gt_called };
                ScpAddr e_calling { e_ssn_local,  e_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, e_called, e_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, e_opc_ni3, e_dpc_ni3, e_m3ua_ni, e_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, e_remote_ip.c_str(), e_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ E-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP UpdateGPRSLocation ───────────────────────────────────────────────
    if (do_map_update_gprs_location) {
        print_section_header("[MAP UpdateGPRSLocation]", "Gs-interface  SGSN → HLR  (opCode=68)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_update_gprs_location(imsi.c_str());
        if (map_msg) {
            if (send_udp && !gs_remote_ip.empty()) {
                uint32_t gs_opc = (gs_m3ua_ni == 2) ? gs_opc_ni2 : gs_opc_ni3;
                uint32_t gs_dpc = (gs_m3ua_ni == 2) ? gs_dpc_ni2 : gs_dpc_ni3;
                ScpAddr gs_called  { gs_ssn_remote, gs_gt_ind, gt_tt, gt_np, gt_nai, gs_gt_called };
                ScpAddr gs_calling { gs_ssn_local,  gs_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, gs_called, gs_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, gs_opc, gs_dpc, gs_m3ua_ni, gs_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, gs_remote_ip.c_str(), gs_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ Gs-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP SendRoutingInfoForGPRS ───────────────────────────────────────────
    if (do_map_send_routeing_info_gprs) {
        print_section_header("[MAP SendRoutingInfoForGPRS]", "Gs-interface  SGSN ↔ HLR  (opCode=24)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_send_routeing_info_gprs(imsi.c_str());
        if (map_msg) {
            if (send_udp && !gs_remote_ip.empty()) {
                uint32_t gs_opc = (gs_m3ua_ni == 2) ? gs_opc_ni2 : gs_opc_ni3;
                uint32_t gs_dpc = (gs_m3ua_ni == 2) ? gs_dpc_ni2 : gs_dpc_ni3;
                ScpAddr gs_called  { gs_ssn_remote, gs_gt_ind, gt_tt, gt_np, gt_nai, gs_gt_called };
                ScpAddr gs_calling { gs_ssn_local,  gs_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, gs_called, gs_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, gs_opc, gs_dpc, gs_m3ua_ni, gs_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, gs_remote_ip.c_str(), gs_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ Gs-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP Reset ────────────────────────────────────────────────────────────
    if (do_map_reset) {
        print_section_header("[MAP Reset]", "C-interface  HLR → VLR  (opCode=37)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_reset();
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP BeginSubscriberActivity ──────────────────────────────────────────
    if (do_map_begin_subscriber_activity) {
        print_section_header("[MAP BeginSubscriberActivity]", "C-interface  VLR → HLR  (opCode=131)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_begin_subscriber_activity(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    if (do_map_deactivate_trace_mode) {
        print_section_header("[MAP DeactivateTraceMode]", "C-interface  MSC/VLR → HLR  (opCode=51)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_deactivate_trace_mode(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    if (do_map_process_unstructured_ss_req) {
        print_section_header("[MAP ProcessUnstructuredSSReq]", "C-interface  MS → MSC/VLR  (opCode=59)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_process_unstructured_ss_req(ussd_string_param.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    if (do_map_unstructured_ss_request) {
        print_section_header("[MAP UnstructuredSSRequest]", "C-interface  MSC → MS  (opCode=60)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_unstructured_ss_request(ussd_string_param.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    if (do_map_subscriber_data_modification) {
        print_section_header("[MAP SubscriberDataModification]", "C-interface  HLR → VLR  (opCode=100)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_subscriber_data_modification(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    if (do_map_any_time_interrogation) {
        print_section_header("[MAP AnyTimeInterrogation]", "C-interface  VLR/MSC → HLR  (opCode=71)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_any_time_interrogation(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    if (do_map_note_mm_event) {
        print_section_header("[MAP NoteMM-Event]", "C-interface  VLR → HLR  (opCode=99)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_note_mm_event(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    if (do_map_inform_service_centre) {
        print_section_header("[MAP InformServiceCentre]", "C-interface  MSC/VLR → SC  (opCode=63)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_inform_service_centre(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    if (do_map_alert_service_centre) {
        print_section_header("[MAP AlertServiceCentre]", "C-interface  HLR → SC  (opCode=64)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_alert_service_centre(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }
    // ── Gs-интерфейс: BSSAP+ LocationUpdate ─────────────────────────────────
    if (do_bssap_gs_lu) {
        print_section_header("[BSSAP+ Location Update]", "MSC \xe2\x86\x92 SGSN  (Gs-interface)");
        std::cout << "\n";
        uint32_t gs_opc = (gs_m3ua_ni == 2) ? gs_opc_ni2 : gs_opc_ni3;
        uint32_t gs_dpc = (gs_m3ua_ni == 2) ? gs_dpc_ni2 : gs_dpc_ni3;
        uint16_t mcc = 250, mnc = 99;
        if (imsi.size() >= 5) {
            mcc = (uint16_t)std::stoul(imsi.substr(0, 3));
            mnc = (uint16_t)std::stoul(imsi.substr(3, 2));
        }
        struct msgb *bssap_msg = generate_bssap_plus_lu(imsi.c_str(), mcc, mnc, 0x0001);
        if (bssap_msg) {
            if (send_udp && !gs_remote_ip.empty()) {
                ScpAddr gs_called  { gs_ssn_remote, gs_gt_ind, gt_tt, gt_np, gt_nai, gs_gt_called };
                ScpAddr gs_calling { gs_ssn_local,  gs_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(bssap_msg, gs_called, gs_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, gs_opc, gs_dpc, gs_m3ua_ni, gs_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                         gs_remote_ip.c_str(), gs_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW
                          << "\xe2\x9a\xa0 Gs-\xd0\xb8\xd0\xbd\xd1\x82\xd0\xb5\xd1\x80\xd1\x84\xd0\xb5\xd0\xb9\xd1\x81: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd \xd0\xb2 [gs-interface]\n"
                          << COLOR_RESET;
            }
            msgb_free(bssap_msg);
        }
    }

    // ── P10: Gs-interface BSSAP+ Paging / Detach / Reset ─────────────────────

    // Вспомогательная лямбда для Gs send-chain (SCCP UDT → M3UA → UDP)
    auto send_gs = [&](struct msgb *gs_msg, const char *hdr, const char *sub) {
        if (!gs_msg) return;
        print_section_header(hdr, sub);
        std::cout << "\n";
        if (send_udp && !gs_remote_ip.empty()) {
            uint32_t gs_opc = (gs_m3ua_ni == 2) ? gs_opc_ni2 : gs_opc_ni3;
            uint32_t gs_dpc = (gs_m3ua_ni == 2) ? gs_dpc_ni2 : gs_dpc_ni3;
            ScpAddr gs_called  { gs_ssn_remote, gs_gt_ind, gt_tt, gt_np, gt_nai, gs_gt_called };
            ScpAddr gs_calling { gs_ssn_local,  gs_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
            struct msgb *sccp_msg = wrap_in_sccp_udt(gs_msg, gs_called, gs_calling);
            if (sccp_msg) {
                struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, gs_opc, gs_dpc, gs_m3ua_ni, gs_si, mp, sls);
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len, gs_remote_ip.c_str(), gs_remote_port);
                    msgb_free(m3ua_msg);
                }
                msgb_free(sccp_msg);
            }
        } else if (send_udp) {
            std::cerr << COLOR_YELLOW << "⚠ Gs-интерфейс: remote_ip не задан в [gs-interface]\n" << COLOR_RESET;
        }
        msgb_free(gs_msg);
    };

    if (do_gs_paging) {
        std::string vlr_num = gs_peer_number.empty() ? msc_gt : gs_peer_number;
        send_gs(generate_bssap_plus_paging(imsi.c_str(), vlr_num.c_str()),
                "[BSSAP+ MS Paging Request]", "Gs-interface  MSC → SGSN");
    }

    if (do_gs_imsi_detach) {
        std::string sgsn_num = gs_peer_number.empty() ? "79161000001" : gs_peer_number;
        send_gs(generate_bssap_plus_imsi_detach(imsi.c_str(), sgsn_num.c_str(), gs_detach_type),
                "[BSSAP+ IMSI Detach Indication]", "Gs-interface  MSC → SGSN");
    }

    if (do_gs_reset)
        send_gs(generate_bssap_plus_reset(gs_reset_cause),
                "[BSSAP+ Reset]", "Gs-interface  MSC → SGSN");

    if (do_gs_reset_ack)
        send_gs(generate_bssap_plus_reset_ack(),
                "[BSSAP+ Reset Acknowledge]", "Gs-interface  SGSN → MSC");

    // ── P21: Gs-interface BSSAP+ SMS-related ─────────────────────────────────
    if (do_gs_ready_for_sm)
        send_gs(generate_bssap_plus_ready_for_sm(imsi.c_str(), gs_alert_reason),
                "[BSSAP+ Ready-For-SM]", "Gs-interface  SGSN → MSC  (0x0E)");

    if (do_gs_alert_sc)
        send_gs(generate_bssap_plus_alert_sc(imsi.c_str()),
                "[BSSAP+ Alert-SC]", "Gs-interface  MSC → SGSN  (0x0F)");

    if (do_gs_alert_ack)
        send_gs(generate_bssap_plus_alert_ack(),
                "[BSSAP+ Alert-Acknowledge]", "Gs-interface  SGSN → MSC  (0x10)");

    if (do_gs_alert_reject)
        send_gs(generate_bssap_plus_alert_reject(gs_alert_cause),
                "[BSSAP+ Alert-Reject]", "Gs-interface  SGSN → MSC  (0x11)");

    // ── MAP SendRoutingInfo (SRI) ─────────────────────────────────────────────
    if (do_map_sri) {
        print_section_header("[MAP SendRoutingInfo]", "C-interface  GMSC → HLR  (MO-call, opCode=22)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_send_routing_info(msisdn.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP ProvideRoamingNumber (PRN) ────────────────────────────────────────
    if (do_map_prn) {
        print_section_header("[MAP ProvideRoamingNumber]", "C-interface  GMSC → HLR  (MT-call, opCode=4)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_provide_roaming_number(imsi.c_str(), msisdn.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP CancelLocation (CL) ───────────────────────────────────────────────
    if (do_map_cl) {
        print_section_header("[MAP CancelLocation]", "C-interface  HLR → VLR  (opCode=3)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_cancel_location(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP InsertSubscriberData (ISD) ────────────────────────────────────────
    if (do_map_isd) {
        print_section_header("[MAP InsertSubscriberData]", "C-interface  HLR → VLR  (opCode=7)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_insert_subscriber_data(imsi.c_str(), msisdn.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP InsertSubscriberData Result (ISD-Res) ─────────────────────────────
    if (do_map_isd_res) {
        print_section_header("[MAP ISD Result]", "C-interface  VLR → HLR  (opCode=7)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_insert_subscriber_data_res(isd_res_dtid_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP CancelLocation Result (CL-Res) ────────────────────────────────────
    if (do_map_cl_res) {
        print_section_header("[MAP CancelLocation Result]", "C-interface  VLR → HLR  (opCode=3)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_cancel_location_res(cl_res_dtid_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP DeleteSubscriberData (DSD) ────────────────────────────────────────
    if (do_map_delete_sd) {
        print_section_header("[MAP DeleteSubscriberData]", "C-interface  HLR → VLR  (opCode=8)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_delete_subscriber_data(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP DeleteSubscriberData Result ───────────────────────────────────────
    if (do_map_delete_sd_res) {
        print_section_header("[MAP DSD Result]", "C-interface  VLR → HLR  (opCode=8)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_delete_subscriber_data_res(dsd_res_dtid_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── C-интерфейс: MAP PurgeMS ─────────────────────────────────────────────
    if (do_map_purge_ms) {
        print_section_header("[MAP PurgeMS]", "C-interface  VLR \xe2\x86\x92 HLR  (opCode=67)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_purge_ms(imsi.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "\xe2\x9a\xa0 C-interface: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── C-интерфейс: MAP AuthenticationFailureReport ──────────────────────────
    if (do_map_auth_failure_rpt) {
        print_section_header("[MAP AuthenticationFailureReport]", "C-interface  VLR \xe2\x86\x92 HLR  (opCode=15)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_auth_failure_report(imsi.c_str(), failure_cause_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "\xe2\x9a\xa0 C-interface: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── TCAP Continue ────────────────────────────────────────────────────────
    if (do_tcap_continue) {
        print_section_header("[TCAP Continue]", "C-interface  (промежуточный шаг диалога)");
        std::cout << "\n";
        struct msgb *map_msg = generate_tcap_continue(otid_param, dtid_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "\xe2\x9a\xa0 C-interface: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── TCAP End (пустой ack) ─────────────────────────────────────────────────
    if (do_tcap_end) {
        print_section_header("[TCAP End]", "C-interface  (завершение диалога, AARE accepted)");
        std::cout << "\n";
        // По умолчанию используем sendAuthInfoContext-v3 OID (можно расширить флагом)
        uint8_t sai_ac_oid[] = { 0x04, 0x00, 0x00, 0x01, 0x00, 0x39, 0x03 };
        struct msgb *map_msg = generate_tcap_end_ack(dtid_param, sai_ac_oid, sizeof(sai_ac_oid));
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "\xe2\x9a\xa0 C-interface: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP SAI End (ReturnResultLast) ────────────────────────────────────────
    if (do_map_sai_end) {
        print_section_header("[MAP SAI End]", "HLR \xe2\x86\x92 MSC/VLR  (C-interface, \xd1\x81\xd0\xb8\xd0\xbc\xd1\x83\xd0\xbb\xd1\x8f\xd1\x86\xd0\xb8\xd1\x8f \xd0\xbe\xd1\x82\xd0\xb2\xd0\xb5\xd1\x82\xd0\xb0 HLR)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_sai_end(dtid_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "\xe2\x9a\xa0 C-interface: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP UL End (ReturnResultLast) ─────────────────────────────────────────
    if (do_map_ul_end) {
        print_section_header("[MAP UL End]", "HLR \xe2\x86\x92 MSC/VLR  (C-interface, \xd1\x81\xd0\xb8\xd0\xbc\xd1\x83\xd0\xbb\xd1\x8f\xd1\x86\xd0\xb8\xd1\x8f \xd0\xbe\xd1\x82\xd0\xb2\xd0\xb5\xd1\x82\xd0\xb0 HLR)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_ul_end(dtid_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "\xe2\x9a\xa0 C-interface: remote_ip \xd0\xbd\xd0\xb5 \xd0\xb7\xd0\xb0\xd0\xb4\xd0\xb0\xd0\xbd\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── TCAP Abort (P-Abort) ──────────────────────────────────────────────────
    if (do_tcap_abort) {
        print_section_header("[TCAP Abort]", "C-interface  (принудительное прерывание диалога)");
        std::cout << "\n";
        struct msgb *map_msg = generate_tcap_abort(dtid_param, abort_cause_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP ReturnError (TCAP End с компонентом ошибки) ──────────────────────
    if (do_map_return_error) {
        print_section_header("[MAP ReturnError]", "C-interface  (симуляция MAP-ошибки от партнёра)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_return_error(dtid_param, invoke_id_param, error_code_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP MO-ForwardSM ──────────────────────────────────────────────────────
    if (do_map_mo_fsm) {
        print_section_header("[MAP MO-ForwardSM]", "C-interface  (MSC → SMSC, opCode=46)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_mo_forward_sm(imsi.c_str(), c_gt_called.c_str(),
                                                          sm_text_param.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP MT-ForwardSM ──────────────────────────────────────────────────────
    if (do_map_mt_fsm) {
        print_section_header("[MAP MT-ForwardSM]", "C-interface  (SMSC → MSC, opCode=44)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_mt_forward_sm(imsi.c_str(), c_gt_called.c_str(),
                                                          sm_text_param.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── MAP USSD (processUnstructuredSS-Request) ──────────────────────────────
    if (do_map_ussd) {
        print_section_header("[MAP USSD]", "C-interface  (MSC → HLR, opCode=59)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_ussd(msisdn.c_str(), ussd_str_param.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── P15: MAP SRI-SM / ReportSMDeliveryStatus (SMS Gateway) ─────────────────

    if (do_map_sri_sm) {
        print_section_header("[MAP SRI-SM]", "C-interface  (GMSC/MSC → HLR, opCode=45)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_sri_sm(msisdn.c_str(), smsc_param.c_str());
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    if (do_map_report_smds) {
        print_section_header("[MAP ReportSMDeliveryStatus]", "C-interface  (SMSC → HLR, opCode=47)");
        std::cout << "\n";
        struct msgb *map_msg = generate_map_report_sm_delivery_status(
                                   msisdn.c_str(), smsc_param.c_str(), smds_outcome_param);
        if (map_msg) {
            if (send_udp && !c_remote_ip.empty()) {
                ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
                ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
                struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(map_msg);
        }
    }

    // ── P18: MAP Supplementary Services ──────────────────────────────────────
    auto send_ss_via_c = [&](struct msgb *map_msg) {
        if (!map_msg) return;
        if (send_udp && !c_remote_ip.empty()) {
            ScpAddr c_called  { c_ssn_remote, c_gt_ind, gt_tt, gt_np, gt_nai, c_gt_called };
            ScpAddr c_calling { c_ssn_local,  c_gt_ind, gt_tt, gt_np, gt_nai, msc_gt };
            struct msgb *sccp_msg = wrap_in_sccp_udt(map_msg, c_called, c_calling);
            if (sccp_msg) {
                struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, c_opc, c_dpc, c_m3ua_ni, c_si, mp, sls);
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len, c_remote_ip.c_str(), c_remote_port);
                    msgb_free(m3ua_msg);
                }
                msgb_free(sccp_msg);
            }
        } else if (send_udp) {
            std::cerr << COLOR_YELLOW << "⚠ C-interface: remote_ip не задан\n" << COLOR_RESET;
        }
        msgb_free(map_msg);
    };

    if (do_map_register_ss) {
        print_section_header("[MAP RegisterSS]", "C-interface  (MSC/VLR \xe2\x86\x92 HLR, opCode=10)");
        std::cout << "\n";
        send_ss_via_c(generate_map_register_ss(ss_code_param, ss_fwd_num_param.c_str()));
    }
    if (do_map_erase_ss) {
        print_section_header("[MAP EraseSS]", "C-interface  (MSC/VLR \xe2\x86\x92 HLR, opCode=11)");
        std::cout << "\n";
        send_ss_via_c(generate_map_erase_ss(ss_code_param));
    }
    if (do_map_activate_ss) {
        print_section_header("[MAP ActivateSS]", "C-interface  (MSC/VLR \xe2\x86\x92 HLR, opCode=12)");
        std::cout << "\n";
        send_ss_via_c(generate_map_activate_ss(ss_code_param));
    }
    if (do_map_deactivate_ss) {
        print_section_header("[MAP DeactivateSS]", "C-interface  (MSC/VLR \xe2\x86\x92 HLR, opCode=13)");
        std::cout << "\n";
        send_ss_via_c(generate_map_deactivate_ss(ss_code_param));
    }
    if (do_map_interrogate_ss) {
        print_section_header("[MAP InterrogateSS]", "C-interface  (MSC/VLR \xe2\x86\x92 HLR, opCode=14)");
        std::cout << "\n";
        send_ss_via_c(generate_map_interrogate_ss(ss_code_param));
    }

    // ── P20: MAP Password + AnyTimeInterrogation ────────────────────────────────
    if (do_map_register_pw) {
        print_section_header("[MAP RegisterPassword]", "C-interface  (MSC/VLR \xe2\x86\x92 HLR, opCode=17)");
        std::cout << "\n";
        send_ss_via_c(generate_map_register_password(ss_code_param));
    }
    if (do_map_get_pw) {
        print_section_header("[MAP GetPassword]", "C-interface  (MSC/VLR \xe2\x86\x92 HLR, opCode=18)");
        std::cout << "\n";
        send_ss_via_c(generate_map_get_password(ss_code_param, pw_guidance_param));
    }
    if (do_map_ati) {
        print_section_header("[MAP ATI]", "C-interface  (MSC/VLR \xe2\x86\x92 HLR, opCode=71)");
        std::cout << "\n";
        const std::string &ati_ms = ati_msisdn_param.empty() ? msisdn : ati_msisdn_param;
        send_ss_via_c(generate_map_ati(ati_ms.c_str()));
    }

    // ── ISUP IAM (Initial Address Message) ───────────────────────────────────
    if (do_isup_iam) {
        print_section_header("[ISUP IAM]", "ISUP-interface  (вызов к PSTN/GW, без SCCP)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_iam(msisdn.c_str(), msc_gt.c_str(), cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                // ISUP → M3UA directly (NO SCCP wrapper), SI=5, SLS=CIC&0xFF
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP REL (Release Message) ────────────────────────────────────────────
    if (do_isup_rel) {
        print_section_header("[ISUP REL]", "ISUP-interface  (освобождение канала, без SCCP)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_rel(cic_param, cause_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                // ISUP → M3UA directly (NO SCCP wrapper), SI=5, SLS=CIC&0xFF
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP ACM (Address Complete Message) ──────────────────────────────────────
    if (do_isup_acm) {
        print_section_header("[ISUP ACM]", "ISUP-interface  (абонент вызывается, без SCCP)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_acm(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP ANM (Answer Message) ─────────────────────────────────────────────
    if (do_isup_anm) {
        print_section_header("[ISUP ANM]", "ISUP-interface  (абонент ответил, без SCCP)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_anm(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP RLC (Release Complete) ──────────────────────────────────────────
    if (do_isup_rlc) {
        print_section_header("[ISUP RLC]", "ISUP-interface  (канал освобождён, без SCCP)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_rlc(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP CON (Connect) ───────────────────────────────────────────────────
    if (do_isup_con) {
        print_section_header("[ISUP CON]", "ISUP-interface  (Connect, без SCCP)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_con(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP CPG (Call Progress) ─────────────────────────────────────────────
    if (do_isup_cpg) {
        print_section_header("[ISUP CPG]", "ISUP-interface  (Call Progress, без SCCP)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cpg(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP SUS (Suspend) ───────────────────────────────────────────────────
    if (do_isup_sus) {
        print_section_header("[ISUP SUS]", "ISUP-interface  (Suspend, без SCCP)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_sus(cic_param, sus_cause_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP RES (Resume) ────────────────────────────────────────────────────
    if (do_isup_res) {
        print_section_header("[ISUP RES]", "ISUP-interface  (Resume, без SCCP)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_res(cic_param, sus_cause_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }


    // ── P25: ISUP SAM (Subsequent Address) ───────────────────────────────────
    if (do_isup_sam) {
        print_section_header("[ISUP SAM]", "ISUP-interface  (Subsequent Address, MT=0x02)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_sam(cic_param, sam_digits_param.c_str());
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P25: ISUP CCR (Continuity Check Request) ─────────────────────────────
    if (do_isup_ccr) {
        print_section_header("[ISUP CCR]", "ISUP-interface  (Continuity Check Request, MT=0x11)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_ccr(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P25: ISUP COT (Continuity) ───────────────────────────────────────────
    if (do_isup_cot) {
        print_section_header("[ISUP COT]", "ISUP-interface  (Continuity, MT=0x05)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cot(cic_param, cot_success_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P25: ISUP FOT (Forward Transfer) ─────────────────────────────────────
    if (do_isup_fot) {
        print_section_header("[ISUP FOT]", "ISUP-interface  (Forward Transfer, MT=0x08)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_fot(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P25: ISUP INR (Information Request) ──────────────────────────────────
    if (do_isup_inr) {
        print_section_header("[ISUP INR]", "ISUP-interface  (Information Request, MT=0x0A)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_inr(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P25: ISUP INF (Information) ──────────────────────────────────────────
    if (do_isup_inf) {
        print_section_header("[ISUP INF]", "ISUP-interface  (Information, MT=0x04)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_inf(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }


    // ── P26: ISUP CFN ────────────────────────────────────────────────────
    if (do_isup_cfn) {
        print_section_header("[ISUP CFN]", "ISUP-interface  (Confusion, MT=0x2F)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cfn(cic_param, cfn_cause_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P26: ISUP OVL ────────────────────────────────────────────────────
    if (do_isup_ovl) {
        print_section_header("[ISUP OVL]", "ISUP-interface  (Overload, MT=0x31)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_ovl(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P26: ISUP UPT ────────────────────────────────────────────────────
    if (do_isup_upt) {
        print_section_header("[ISUP UPT]", "ISUP-interface  (User Part Test, MT=0x34)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_upt(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P26: ISUP UPA ────────────────────────────────────────────────────
    if (do_isup_upa) {
        print_section_header("[ISUP UPA]", "ISUP-interface  (User Part Available, MT=0x35)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_upa(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P27: ISUP Facility (FAR) ────────────────────────────────────────────────────────
    if (do_isup_far) {
        print_section_header("[ISUP Facility (FAR)]", "ISUP-interface");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_far(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    // ── P27: ISUP Identification Request (IDR) ────────────────────────────────────────────────────────
    if (do_isup_idr) {
        print_section_header("[ISUP Identification Request (IDR)]", "ISUP-interface");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_idr(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP IRS (Identification Response) ──────────────────────────────────
    if (do_isup_irs) {
        print_section_header("[ISUP Identification Response (IRS)]", "ISUP-interface  MT=0x3C");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_irs(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP LPA (Loop Back Acknowledgement) ─────────────────────────────────
    if (do_isup_lpa) {
        print_section_header("[ISUP Loop Back Acknowledgement (LPA)]", "ISUP-interface  MT=0x24");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_lpa(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP CQM (Circuit Group Query) ──────────────────────────────────────
    if (do_isup_cqm) {
        print_section_header("[ISUP Circuit Group Query (CQM)]", "ISUP-interface  MT=0x2C");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cqm(cic_param, grs_range_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP CQR (Circuit Group Query Response) ──────────────────────────────
    if (do_isup_cqr) {
        print_section_header("[ISUP Circuit Group Query Response (CQR)]", "ISUP-interface  MT=0x2D");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cqr(cic_param, grs_range_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP LOP (Loop Prevention) ───────────────────────────────────────────
    if (do_isup_lop) {
        print_section_header("[ISUP Loop Prevention (LOP)]", "ISUP-interface  MT=0x23");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_lop(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP PAM (Pass-Along) ────────────────────────────────────────────────
    if (do_isup_pam) {
        print_section_header("[ISUP Pass-Along (PAM)]", "ISUP-interface  MT=0x28");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_pam(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    if (do_isup_nrm) {
        print_section_header("[ISUP NRM]", "ISUP-interface  MT=0x32");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_nrm(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    if (do_isup_olm) {
        print_section_header("[ISUP OLM]", "ISUP-interface  MT=0x30");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_olm(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    if (do_isup_exm) {
        print_section_header("[ISUP EXM]", "ISUP-interface  MT=0x31");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_exm(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    if (do_isup_apm) {
        print_section_header("[ISUP APM]", "ISUP-interface  MT=0x63");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_apm(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    if (do_isup_faa) {
        print_section_header("[ISUP FAA]", "ISUP-interface  MT=0x20");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_faa(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    if (do_isup_frj) {
        print_section_header("[ISUP FRJ]", "ISUP-interface  MT=0x21");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_frj(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    if (do_isup_crm) {
        print_section_header("[ISUP CRM]", "ISUP-interface  MT=0x1C");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_crm(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    if (do_isup_cra) {
        print_section_header("[ISUP CRA]", "ISUP-interface  MT=0x1D");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cra(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }
    // ── ISUP BLO (Blocking) ──────────────────────────────────────────────────
    if (do_isup_blo) {
        print_section_header("[ISUP BLO]", "ISUP-interface  (Blocking, MT=0x13)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_blo(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP UBL (Unblocking) ────────────────────────────────────────────────
    if (do_isup_ubl) {
        print_section_header("[ISUP UBL]", "ISUP-interface  (Unblocking, MT=0x14)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_ubl(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP RSC (Reset Circuit) ─────────────────────────────────────────────
    if (do_isup_rsc) {
        print_section_header("[ISUP RSC]", "ISUP-interface  (Reset Circuit, MT=0x12)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_rsc(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── ISUP GRS (Group Reset) ───────────────────────────────────────────────
    if (do_isup_grs) {
        print_section_header("[ISUP GRS]", "ISUP-interface  (Group Reset, MT=0x17)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_grs(cic_param, grs_range_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── M3UA ASPUP ────────────────────────────────────────────────────────────
    if (do_m3ua_aspup) {
        print_section_header("[M3UA ASPUP]", "M3UA ASPSM  (ASP Up, Class=3 Type=1)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_aspup();
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── M3UA ASPUP-ACK ────────────────────────────────────────────────────────
    if (do_m3ua_aspup_ack) {
        print_section_header("[M3UA ASPUP-ACK]", "M3UA ASPSM  (ASP Up Ack, Class=3 Type=4)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_aspup_ack();
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── M3UA ASPDN ────────────────────────────────────────────────────────────
    if (do_m3ua_aspdn) {
        print_section_header("[M3UA ASPDN]", "M3UA ASPSM  (ASP Down, Class=3 Type=2)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_aspdn();
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── M3UA ASPAC ────────────────────────────────────────────────────────────
    if (do_m3ua_aspac) {
        print_section_header("[M3UA ASPAC]", "M3UA ASPTM  (ASP Active, Class=4 Type=1)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_aspac(m3ua_tmt_param, m3ua_rc_param);
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── M3UA ASPAC-ACK ────────────────────────────────────────────────────────
    if (do_m3ua_aspac_ack) {
        print_section_header("[M3UA ASPAC-ACK]", "M3UA ASPTM  (ASP Active Ack, Class=4 Type=3)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_aspac_ack(m3ua_tmt_param, m3ua_rc_param);
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── M3UA ASPIA ────────────────────────────────────────────────────────────
    if (do_m3ua_aspia) {
        print_section_header("[M3UA ASPIA]", "M3UA ASPTM  (ASP Inactive, Class=4 Type=2)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_aspia();
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── P19: ISUP BLA (Blocking Acknowledgement) ─────────────────────────────
    if (do_isup_bla) {
        print_section_header("[ISUP BLA]", "ISUP-interface  (Blocking Acknowledgement, MT=0x15)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_bla(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P19: ISUP UBA (Unblocking Acknowledgement) ───────────────────────────
    if (do_isup_uba) {
        print_section_header("[ISUP UBA]", "ISUP-interface  (Unblocking Acknowledgement, MT=0x16)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_uba(cic_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P19: ISUP GRA (Group Reset Acknowledgement) ──────────────────────────
    if (do_isup_gra) {
        print_section_header("[ISUP GRA]", "ISUP-interface  (Group Reset Acknowledgement, MT=0x29)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_gra(cic_param, grs_range_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) {
                    send_message_udp(m3ua_msg->data, m3ua_msg->len,
                                     isup_remote_ip.c_str(), isup_remote_port);
                    msgb_free(m3ua_msg);
                }
            } else if (send_udp) {
                std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET;
            }
            msgb_free(isup_msg);
        }
    }

    // ── P20: ISUP CGB (Circuit Group Blocking) ───────────────────────────────
    if (do_isup_cgb) {
        print_section_header("[ISUP CGB]", "ISUP-interface  (Circuit Group Blocking, MT=0x18)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cgb(cic_param, grs_range_param, cg_cause_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, isup_remote_ip.c_str(), isup_remote_port); msgb_free(m3ua_msg); }
            } else if (send_udp) { std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET; }
            msgb_free(isup_msg);
        }
    }

    // ── P20: ISUP CGU (Circuit Group Unblocking) ─────────────────────────────
    if (do_isup_cgu) {
        print_section_header("[ISUP CGU]", "ISUP-interface  (Circuit Group Unblocking, MT=0x19)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cgu(cic_param, grs_range_param, cg_cause_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, isup_remote_ip.c_str(), isup_remote_port); msgb_free(m3ua_msg); }
            } else if (send_udp) { std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET; }
            msgb_free(isup_msg);
        }
    }

    // ── P20: ISUP CGBA (CGB Acknowledgement) ─────────────────────────────────
    if (do_isup_cgba) {
        print_section_header("[ISUP CGBA]", "ISUP-interface  (CGB Acknowledgement, MT=0x1A)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cgba(cic_param, grs_range_param, cg_cause_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, isup_remote_ip.c_str(), isup_remote_port); msgb_free(m3ua_msg); }
            } else if (send_udp) { std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET; }
            msgb_free(isup_msg);
        }
    }

    // ── P20: ISUP CGUA (CGU Acknowledgement) ─────────────────────────────────
    if (do_isup_cgua) {
        print_section_header("[ISUP CGUA]", "ISUP-interface  (CGU Acknowledgement, MT=0x1B)");
        std::cout << "\n";
        uint32_t isup_opc = (isup_m3ua_ni == 0) ? isup_opc_ni0 : isup_opc_ni2;
        uint32_t isup_dpc = (isup_m3ua_ni == 0) ? isup_dpc_ni0 : isup_dpc_ni2;
        struct msgb *isup_msg = generate_isup_cgua(cic_param, grs_range_param, cg_cause_param);
        if (isup_msg) {
            if (send_udp && !isup_remote_ip.empty()) {
                struct msgb *m3ua_msg = wrap_in_m3ua(isup_msg, isup_opc, isup_dpc,
                                                     isup_m3ua_ni, isup_si, mp,
                                                     (uint8_t)(cic_param & 0xFF));
                if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, isup_remote_ip.c_str(), isup_remote_port); msgb_free(m3ua_msg); }
            } else if (send_udp) { std::cerr << COLOR_YELLOW << "⚠ ISUP-interface: remote_ip не задан\n" << COLOR_RESET; }
            msgb_free(isup_msg);
        }
    }

    // ── P19: M3UA ASPDN-ACK ───────────────────────────────────────────────────
    if (do_m3ua_aspdn_ack) {
        print_section_header("[M3UA ASPDN-ACK]", "M3UA ASPSM  (ASP Down Ack, Class=3 Type=5)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_aspdn_ack();
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── P19: M3UA ASPIA-ACK ───────────────────────────────────────────────────
    if (do_m3ua_aspia_ack) {
        print_section_header("[M3UA ASPIA-ACK]", "M3UA ASPTM  (ASP Inactive Ack, Class=4 Type=4)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_aspia_ack();
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── P19: M3UA BEAT ────────────────────────────────────────────────────────
    if (do_m3ua_beat) {
        print_section_header("[M3UA BEAT]", "M3UA ASPSM  (Heartbeat, Class=3 Type=3)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_beat();
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── P19: M3UA BEAT-ACK ────────────────────────────────────────────────────
    if (do_m3ua_beat_ack) {
        print_section_header("[M3UA BEAT-ACK]", "M3UA ASPSM  (Heartbeat Ack, Class=3 Type=6)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_beat_ack();
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }


    // ── P25: M3UA ERR ────────────────────────────────────────────────────────
    if (do_m3ua_err) {
        print_section_header("[M3UA ERR]", "M3UA Management  (Error, Class=0 Type=0)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_err(m3ua_err_code_param);
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── P25: M3UA NTFY ───────────────────────────────────────────────────────
    if (do_m3ua_ntfy) {
        print_section_header("[M3UA NTFY]", "M3UA Management  (Notify, Class=0 Type=1)");
        std::cout << "\n";
        struct msgb *asp_msg = generate_m3ua_ntfy(m3ua_ntfy_status_type_param, m3ua_ntfy_status_info_param);
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    // ── P22: M3UA SSNM Destination State ────────────────────────────────────
    if (do_m3ua_duna) {
        print_section_header("[M3UA DUNA]", "M3UA SSNM  (Destination Unavailable, Class=2 Type=1)");
        std::cout << "\n";
        uint32_t aff_pc = m3ua_aff_pc_param ? m3ua_aff_pc_param : (uint32_t)m3ua_dpc;
        struct msgb *asp_msg = generate_m3ua_duna(aff_pc);
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    if (do_m3ua_dava) {
        print_section_header("[M3UA DAVA]", "M3UA SSNM  (Destination Available, Class=2 Type=2)");
        std::cout << "\n";
        uint32_t aff_pc = m3ua_aff_pc_param ? m3ua_aff_pc_param : (uint32_t)m3ua_dpc;
        struct msgb *asp_msg = generate_m3ua_dava(aff_pc);
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }

    if (do_m3ua_daud) {
        print_section_header("[M3UA DAUD]", "M3UA SSNM  (Destination State Audit, Class=2 Type=3)");
        std::cout << "\n";
        uint32_t aff_pc = m3ua_aff_pc_param ? m3ua_aff_pc_param : (uint32_t)m3ua_dpc;
        struct msgb *asp_msg = generate_m3ua_daud(aff_pc);
        if (asp_msg) {
            if (send_udp && !remote_ip.empty())
                send_message_udp(asp_msg->data, asp_msg->len, remote_ip.c_str(), remote_port);
            else if (send_udp)
                std::cerr << COLOR_YELLOW << "⚠ M3UA: remote_ip не задан\n" << COLOR_RESET;
            msgb_free(asp_msg);
        }
    }


    // ── A-interface: DTAP Authentication Request ─────────────────────────────
    if (do_dtap_auth_req) {
        print_section_header("[DTAP Authentication Request]", "A-interface  (MSC \xe2\x86\x92 MS via BSC)");
        std::cout << "\n";
        struct msgb *dtap_msg = generate_dtap_auth_request(rand_param.c_str(), cksn_param);
        if (dtap_msg) {
            if (send_udp) {
                struct msgb *bssap_msg = wrap_in_bssap_dtap(dtap_msg);
                if (bssap_msg) {
                    struct msgb *sccp_msg = wrap_in_sccp_cr(bssap_msg, a_ssn);
                    if (sccp_msg) {
                        struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                        if (m3ua_msg) {
                            send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                            msgb_free(m3ua_msg);
                        }
                        msgb_free(sccp_msg);
                    }
                    msgb_free(bssap_msg);
                }
            }
            msgb_free(dtap_msg);
        }
    }

    // ── A-interface: DTAP Authentication Response ────────────────────────────
    if (do_dtap_auth_resp) {
        print_section_header("[DTAP Authentication Response]", "A-interface  (MS \xe2\x86\x92 MSC via BSC)");
        std::cout << "\n";
        struct msgb *dtap_msg = generate_dtap_auth_response(sres_param.c_str());
        if (dtap_msg) {
            if (send_udp) {
                struct msgb *bssap_msg = wrap_in_bssap_dtap(dtap_msg);
                if (bssap_msg) {
                    struct msgb *sccp_msg = wrap_in_sccp_cr(bssap_msg, a_ssn);
                    if (sccp_msg) {
                        struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                        if (m3ua_msg) {
                            send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                            msgb_free(m3ua_msg);
                        }
                        msgb_free(sccp_msg);
                    }
                    msgb_free(bssap_msg);
                }
            }
            msgb_free(dtap_msg);
        }
    }

    // ── A-interface: DTAP Identity Request ──────────────────────────────────
    if (do_dtap_id_req) {
        print_section_header("[DTAP Identity Request]", "A-interface  (MSC \xe2\x86\x92 MS via BSC)");
        std::cout << "\n";
        struct msgb *dtap_msg = generate_dtap_id_request(id_type_param);
        if (dtap_msg) {
            if (send_udp) {
                struct msgb *bssap_msg = wrap_in_bssap_dtap(dtap_msg);
                if (bssap_msg) {
                    struct msgb *sccp_msg = wrap_in_sccp_cr(bssap_msg, a_ssn);
                    if (sccp_msg) {
                        struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                        if (m3ua_msg) {
                            send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                            msgb_free(m3ua_msg);
                        }
                        msgb_free(sccp_msg);
                    }
                    msgb_free(bssap_msg);
                }
            }
            msgb_free(dtap_msg);
        }
    }

    // ── A-interface: DTAP Identity Response ─────────────────────────────────
    if (do_dtap_id_resp) {
        print_section_header("[DTAP Identity Response]", "A-interface  (MS \xe2\x86\x92 MSC via BSC)");
        std::cout << "\n";
        struct msgb *dtap_msg = generate_dtap_id_response(imsi.c_str());
        if (dtap_msg) {
            if (send_udp) {
                struct msgb *bssap_msg = wrap_in_bssap_dtap(dtap_msg);
                if (bssap_msg) {
                    struct msgb *sccp_msg = wrap_in_sccp_cr(bssap_msg, a_ssn);
                    if (sccp_msg) {
                        struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                        if (m3ua_msg) {
                            send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                            msgb_free(m3ua_msg);
                        }
                        msgb_free(sccp_msg);
                    }
                    msgb_free(bssap_msg);
                }
            }
            msgb_free(dtap_msg);
        }
    }

    // ── A-interface: BSSMAP Ciphering Mode Command ───────────────────────────
    if (do_bssmap_cipher) {
        print_section_header("[BSSMAP Ciphering Mode Command]", "A-interface  (MSC \xe2\x86\x92 BSC)");
        std::cout << "\n";
        static const uint8_t kc_default[8] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77};
        struct msgb *bssmap_msg = generate_bssmap_cipher_mode_cmd(cipher_alg_param, kc_default);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP Ciphering Mode Complete ────────────────────────────────────────
    if (do_bssmap_cipher_compl) {
        print_section_header("[BSSMAP Ciphering Mode Complete]", "A-interface  (BSC → MSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_cipher_mode_complete();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP Ciphering Mode Reject ──────────────────────────────────────────
    if (do_bssmap_cipher_reject) {
        print_section_header("[BSSMAP Ciphering Mode Reject]", "A-interface  (BSC → MSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_cipher_mode_reject(cipher_reject_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP Common Id ──────────────────────────────────────────────────────
    if (do_bssmap_common_id) {
        print_section_header("[BSSMAP Common Id]", "A-interface  (MSC → BSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_common_id(imsi.c_str());
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP SAPI n Reject ──────────────────────────────────────────────────
    if (do_bssmap_sapi_n_reject) {
        print_section_header("[BSSMAP SAPI n Reject]", "A-interface  (BSC → MSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_sapi_n_reject(sapi_n_reject_sapi_param, sapi_n_reject_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── P27: BSSMAP Handover Required Reject ────────────────────────────────────────────────────────
    if (do_bssmap_ho_req_reject) {
        print_section_header("[BSSMAP Handover Required Reject]", "A-interface");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_required_reject(ho_req_rej_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    // ── P27: BSSMAP Handover Candidate Response ────────────────────────────────────────────────────────
    if (do_bssmap_ho_candidate_resp) {
        print_section_header("[BSSMAP Handover Candidate Response]", "A-interface");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_candidate_response(ho_cand_count_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    // ── P27: BSSMAP Suspend ────────────────────────────────────────────────────────
    if (do_bssmap_suspend) {
        print_section_header("[BSSMAP Suspend]", "A-interface");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_suspend((uint8_t)(bssmap_tlli_param>>24),(uint8_t)(bssmap_tlli_param>>16),(uint8_t)(bssmap_tlli_param>>8),(uint8_t)bssmap_tlli_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    // ── P27: BSSMAP Resume ────────────────────────────────────────────────────────
    if (do_bssmap_resume) {
        print_section_header("[BSSMAP Resume]", "A-interface");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_resume((uint8_t)(bssmap_tlli_param>>24),(uint8_t)(bssmap_tlli_param>>16),(uint8_t)(bssmap_tlli_param>>8),(uint8_t)bssmap_tlli_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP Load Indication ───────────────────────────────────────────────
    if (do_bssmap_load_indication) {
        print_section_header("[BSSMAP Load Indication]", "A-interface  (BSC ↔ MSC)  MT=0x5A");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_load_indication(bssmap_cell_id_param, bssmap_resource_avail_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP Queuing Indication ────────────────────────────────────────────
    if (do_bssmap_queuing_indication) {
        print_section_header("[BSSMAP Queuing Indication]", "A-interface  (MSC → BSC)  MT=0x56");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_queuing_indication();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP Confusion ─────────────────────────────────────────────────────
    if (do_bssmap_confusion) {
        print_section_header("[BSSMAP Confusion]", "A-interface  MT=0x26(38)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_confusion(bssmap_confusion_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP Connection Oriented Information ────────────────────────────────
    if (do_bssmap_connection_oriented_info) {
        print_section_header("[BSSMAP Connection Oriented Information]", "A-interface  MT=0x2A(42)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_connection_oriented_info();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP Perform Location Request ──────────────────────────────────────
    if (do_bssmap_perform_location_request) {
        print_section_header("[BSSMAP Perform Location Request]", "A-interface  MT=0x2B(43)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_perform_location_request(bssmap_loc_type_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── BSSMAP Perform Location Response ────────────────────────────────────
    if (do_bssmap_perform_location_response) {
        print_section_header("[BSSMAP Perform Location Response]", "A-interface  MT=0x2D(45)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_perform_location_response();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    if (do_bssmap_overload) {
        print_section_header("[BSSMAP Overload]", "A-interface  MT=0x32(50)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_overload(bssmap_overload_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    if (do_bssmap_perform_location_abort) {
        print_section_header("[BSSMAP Perform Location Abort]", "A-interface  MT=0x2E(46)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_perform_location_abort(bssmap_overload_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    if (do_bssmap_handover_detect) {
        print_section_header("[BSSMAP Handover Detect]", "A-interface  MT=0x1B(27)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_handover_detect();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    if (do_bssmap_msc_invoke_trace) {
        print_section_header("[BSSMAP MSC Invoke Trace]", "A-interface  MT=0x36(54)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_msc_invoke_trace(bssmap_trace_type_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    if (do_bssmap_channel_modify_request) {
        print_section_header("[BSSMAP Channel Modify Request]", "A-interface  MT=0x08(8)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_channel_modify_request(bssmap_overload_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    if (do_bssmap_internal_handover_required) {
        print_section_header("[BSSMAP Internal Handover Required]", "A-interface  MT=0x21(33)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_internal_handover_required(bssmap_overload_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    if (do_bssmap_internal_handover_command) {
        print_section_header("[BSSMAP Internal Handover Command]", "A-interface  MT=0x23(35)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_internal_handover_command();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    if (do_bssmap_lsa_information) {
        print_section_header("[BSSMAP LSA Information]", "A-interface  MT=0x2C(44)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_lsa_information();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── A-interface: DTAP Location Updating Accept ───────────────────────────
    if (do_dtap_lu_accept) {
        print_section_header("[DTAP Location Updating Accept]", "A-interface  (MSC \xe2\x86\x92 MS via BSC)");
        std::cout << "\n";
        struct msgb *dtap_msg = generate_dtap_lu_accept(mcc, mnc, lac, tmsi_param);
        if (dtap_msg) {
            if (send_udp) {
                struct msgb *bssap_msg = wrap_in_bssap_dtap(dtap_msg);
                if (bssap_msg) {
                    struct msgb *sccp_msg = wrap_in_sccp_cr(bssap_msg, a_ssn);
                    if (sccp_msg) {
                        struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                        if (m3ua_msg) {
                            send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                            msgb_free(m3ua_msg);
                        }
                        msgb_free(sccp_msg);
                    }
                    msgb_free(bssap_msg);
                }
            }
            msgb_free(dtap_msg);
        }
    }

    // ── A-interface: DTAP Location Updating Reject ───────────────────────────
    if (do_dtap_lu_reject) {
        print_section_header("[DTAP Location Updating Reject]", "A-interface  (MSC \xe2\x86\x92 MS via BSC)");
        std::cout << "\n";
        struct msgb *dtap_msg = generate_dtap_lu_reject(lu_cause_param);
        if (dtap_msg) {
            if (send_udp) {
                struct msgb *bssap_msg = wrap_in_bssap_dtap(dtap_msg);
                if (bssap_msg) {
                    struct msgb *sccp_msg = wrap_in_sccp_cr(bssap_msg, a_ssn);
                    if (sccp_msg) {
                        struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                        if (m3ua_msg) {
                            send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                            msgb_free(m3ua_msg);
                        }
                        msgb_free(sccp_msg);
                    }
                    msgb_free(bssap_msg);
                }
            }
            msgb_free(dtap_msg);
        }
    }

    // ── A-interface: BSSMAP Reset ────────────────────────────────────────────
    if (do_bssmap_reset) {
        print_section_header("[BSSMAP Reset]", "A-interface  (MSC \xe2\x86\x92 BSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_reset(clear_cause);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── P8: BSSMAP Assignment / Clear / Paging ───────────────────────────────

    if (do_bssmap_assign_req) {
        print_section_header("[BSSMAP Assignment Request]", "A-interface  (MSC → BSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_assignment_request(speech_ver_param, cic_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_assign_compl) {
        print_section_header("[BSSMAP Assignment Complete]", "A-interface  (BSC → MSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_assignment_complete();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_clear_req) {
        print_section_header("[BSSMAP Clear Request]", "A-interface  (BSC → MSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_clear_request(clear_cause);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_clear_compl) {
        print_section_header("[BSSMAP Clear Complete]", "A-interface  (BSC → MSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_clear_complete();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_paging) {
        print_section_header("[BSSMAP Paging]", "A-interface  (MSC → BSC)");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_paging(imsi.c_str(), lac);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    // ── P13: BSSMAP Handover ────────────────────────────────────────────────

    if (do_bssmap_ho_required) {
        print_section_header("[BSSMAP Handover Required]", "A-interface  (BSC → MSC)  MT=0x11");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_required(ho_cause_param, ho_target_lac, ho_target_cell);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_ho_command) {
        print_section_header("[BSSMAP Handover Command]", "A-interface  (MSC → BSC)  MT=0x12");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_command(ho_target_lac, ho_target_cell);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_ho_succeeded) {
        print_section_header("[BSSMAP Handover Succeeded]", "A-interface  (MSC → BSC)  MT=0x14");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_succeeded();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_ho_performed) {
        print_section_header("[BSSMAP Handover Performed]", "A-interface  (BSC → MSC)  MT=0x17");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_performed(ho_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_ho_candidate) {
        print_section_header("[BSSMAP Handover Candidate Enquiry]", "A-interface  (MSC → BSC)  MT=0x4F");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_candidate_enquiry(ho_target_lac);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }


    // ── P23: BSSMAP additional ──────────────────────────────────────────────────
    if (do_bssmap_reset_ack) {
        print_section_header("[BSSMAP Reset Acknowledge]", "A-interface  (MSC → BSC)  MT=0x31");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_reset_ack();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port); msgb_free(m3ua_msg); }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_assign_fail) {
        print_section_header("[BSSMAP Assignment Failure]", "A-interface  (BSC → MSC)  MT=0x03");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_assignment_failure(assign_fail_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port); msgb_free(m3ua_msg); }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_classmark_req) {
        print_section_header("[BSSMAP Classmark Request]", "A-interface  (MSC → BSC)  MT=0x54");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_classmark_request();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port); msgb_free(m3ua_msg); }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_classmark_upd) {
        print_section_header("[BSSMAP Classmark Update]", "A-interface  (BSC → MSC)  MT=0x53");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_classmark_update();
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port); msgb_free(m3ua_msg); }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_ho_request) {
        print_section_header("[BSSMAP Handover Request]", "A-interface  (MSC → BSC Target)  MT=0x10");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_request(ho_cause_param, ho_target_lac, ho_target_cell);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port); msgb_free(m3ua_msg); }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_ho_req_ack) {
        print_section_header("[BSSMAP Handover Request Acknowledge]", "A-interface  (BSC Target → MSC)  MT=0x13");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_request_ack(ho_ref_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port); msgb_free(m3ua_msg); }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }

    if (do_bssmap_ho_failure) {
        print_section_header("[BSSMAP Handover Failure]", "A-interface  (BSC Target → MSC)  MT=0x1C");
        std::cout << "\n";
        struct msgb *bssmap_msg = generate_bssmap_ho_failure(ho_cause_param);
        if (bssmap_msg) {
            if (send_udp) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssmap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) { send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port); msgb_free(m3ua_msg); }
                    msgb_free(sccp_msg);
                }
            }
            msgb_free(bssmap_msg);
        }
    }
    // ── P9: DTAP CC / MM — управление вызовом ──────────────────────────────

    // Вспомогательный макрос-лямбда: DTAP→BSSAP→SCCP CR→M3UA→UDP
    auto send_dtap_a = [&](struct msgb *dtap_msg, const char *hdr, const char *sub) {
        if (!dtap_msg) return;
        print_section_header(hdr, sub);
        std::cout << "\n";
        if (send_udp) {
            struct msgb *bssap_msg = wrap_in_bssap_dtap(dtap_msg);
            if (bssap_msg) {
                struct msgb *sccp_msg = wrap_in_sccp_cr(bssap_msg, a_ssn);
                if (sccp_msg) {
                    struct msgb *m3ua_msg = wrap_in_m3ua(sccp_msg, m3ua_opc, m3ua_dpc, m3ua_ni, a_si, mp, sls);
                    if (m3ua_msg) {
                        send_message_udp(m3ua_msg->data, m3ua_msg->len, remote_ip.c_str(), remote_port);
                        msgb_free(m3ua_msg);
                    }
                    msgb_free(sccp_msg);
                }
                msgb_free(bssap_msg);
            }
        }
        msgb_free(dtap_msg);
    };

    if (do_dtap_cm_srv_req)
        send_dtap_a(generate_dtap_mm_cm_service_req(imsi.c_str(), 1),
                    "[MM CM Service Request]", "A-interface  (MS → MSC)");

    if (do_dtap_cm_srv_acc)
        send_dtap_a(generate_dtap_mm_cm_service_acc(),
                    "[MM CM Service Accept]", "A-interface  (MSC → MS)");

    // P21-R: DTAP CM Service Reject + IMSI Detach Indication (A-interface)
    if (do_dtap_cm_srv_rej)
        send_dtap_a(generate_dtap_mm_cm_service_reject(cm_reject_cause_param),
                    "[MM CM Service Reject]", "A-interface  (MSC → MS via BSC)");

    if (do_dtap_imsi_detach_a)
        send_dtap_a(generate_dtap_mm_imsi_detach(imsi.c_str()),
                    "[MM IMSI Detach Indication]", "A-interface  (MS → MSC via BSC)");


    if (do_dtap_cc_setup_mo)
        send_dtap_a(generate_dtap_cc_setup_mo(ti_param, msisdn.c_str()),
                    "[DTAP CC Setup MO]", "A-interface  (MS → MSC)");

    if (do_dtap_cc_setup_mt)
        send_dtap_a(generate_dtap_cc_setup_mt(ti_param, msisdn.c_str()),
                    "[DTAP CC Setup MT]", "A-interface  (MSC → MS)");

    if (do_dtap_cc_call_proc)
        send_dtap_a(generate_dtap_cc_call_proceeding(ti_param),
                    "[DTAP CC Call Proceeding]", "A-interface  (MSC → MS)");

    if (do_dtap_cc_alerting)
        send_dtap_a(generate_dtap_cc_alerting(ti_param),
                    "[DTAP CC Alerting]", "A-interface  (MSC → MS)");

    if (do_dtap_cc_connect)
        send_dtap_a(generate_dtap_cc_connect(ti_param, cc_net_to_ms),
                    "[DTAP CC Connect]", cc_net_to_ms
                        ? "A-interface  (MSC → MS)" : "A-interface  (MS → MSC)");

    if (do_dtap_cc_connect_ack)
        send_dtap_a(generate_dtap_cc_connect_ack(ti_param),
                    "[DTAP CC Connect Ack]", "A-interface  (MSC → MS)");

    if (do_dtap_cc_disconnect)
        send_dtap_a(generate_dtap_cc_disconnect(ti_param, cc_net_to_ms, cc_cause_param),
                    "[DTAP CC Disconnect]", cc_net_to_ms
                        ? "A-interface  (MSC → MS)" : "A-interface  (MS → MSC)");

    if (do_dtap_cc_release)
        send_dtap_a(generate_dtap_cc_release(ti_param, cc_net_to_ms, cc_cause_param),
                    "[DTAP CC Release]", cc_net_to_ms
                        ? "A-interface  (MSC → MS)" : "A-interface  (MS → MSC)");

    if (do_dtap_cc_rel_compl)
        send_dtap_a(generate_dtap_cc_release_complete(ti_param, cc_net_to_ms),
                    "[DTAP CC Release Complete]", cc_net_to_ms
                        ? "A-interface  (MSC → MS)" : "A-interface  (MS → MSC)");


    // P23: DTAP CC additional + SMS CP layer (A-interface)
    if (do_dtap_cc_progress)
        send_dtap_a(generate_dtap_cc_progress(ti_param, progress_desc_param),
                    "[DTAP CC Progress]", "A-interface  (MSC → MS)");

    if (do_dtap_cc_call_confirmed)
        send_dtap_a(generate_dtap_cc_call_confirmed(ti_param),
                    "[DTAP CC Call Confirmed]", "A-interface  (MS → MSC)");

    if (do_dtap_cc_start_dtmf)
        send_dtap_a(generate_dtap_cc_start_dtmf(ti_param, dtmf_digit_param),
                    "[DTAP CC Start DTMF]", "A-interface  (MS → MSC)");

    if (do_dtap_cc_stop_dtmf)
        send_dtap_a(generate_dtap_cc_stop_dtmf(ti_param),
                    "[DTAP CC Stop DTMF]", "A-interface  (MS → MSC)");

    if (do_dtap_cc_status)
        send_dtap_a(generate_dtap_cc_status(ti_param, cc_net_to_ms, cc_cause_param, call_state_param),
                    "[DTAP CC Status]", cc_net_to_ms
                        ? "A-interface  (MSC → MS)" : "A-interface  (MS → MSC)");

    if (do_dtap_cc_emerg_setup)
        send_dtap_a(generate_dtap_cc_emergency_setup(ti_param),
                    "[DTAP CC Emergency Setup]", "A-interface  (MS → MSC)");

    if (do_dtap_cc_hold)
        send_dtap_a(generate_dtap_cc_hold(ti_param),
                    "[DTAP CC Hold]", "A-interface  (MS → MSC)");

    if (do_dtap_cc_hold_ack)
        send_dtap_a(generate_dtap_cc_hold_ack(ti_param),
                    "[DTAP CC Hold Acknowledge]", "A-interface  (MSC → MS)");

    if (do_dtap_cc_hold_reject)
        send_dtap_a(generate_dtap_cc_hold_reject(ti_param, hold_rej_cause_param),
                    "[DTAP CC Hold Reject]", "A-interface  (MSC → MS)");

    if (do_dtap_cc_retrieve)
        send_dtap_a(generate_dtap_cc_retrieve(ti_param),
                    "[DTAP CC Retrieve]", "A-interface  (MS → MSC)");

    if (do_dtap_cc_retrieve_ack)
        send_dtap_a(generate_dtap_cc_retrieve_ack(ti_param),
                    "[DTAP CC Retrieve Acknowledge]", "A-interface  (MSC → MS)");

    if (do_dtap_cc_retrieve_reject)
        send_dtap_a(generate_dtap_cc_retrieve_reject(ti_param, retr_rej_cause_param),
                    "[DTAP CC Retrieve Reject]", "A-interface  (MSC → MS)");

    // P25: DTAP CC Modify
    if (do_dtap_cc_modify)
        send_dtap_a(generate_dtap_cc_modify(ti_param),
                    "[DTAP CC Modify]", "A-interface  (MS → MSC)");
    if (do_dtap_cc_modify_complete)
        send_dtap_a(generate_dtap_cc_modify_complete(ti_param),
                    "[DTAP CC Modify Complete]", "A-interface  (MSC → MS)");
    if (do_dtap_cc_modify_reject)
        send_dtap_a(generate_dtap_cc_modify_reject(ti_param, cc_modify_rej_cause_param),
                    "[DTAP CC Modify Reject]", "A-interface  (MSC → MS)");

    if (do_dtap_sms_cp_data)
        send_dtap_a(generate_dtap_sms_cp_data(ti_param, cc_net_to_ms),
                    "[DTAP SMS CP-DATA]", cc_net_to_ms
                        ? "A-interface  (MSC → MS)" : "A-interface  (MS → MSC)");

    if (do_dtap_sms_cp_ack)
        send_dtap_a(generate_dtap_sms_cp_ack(ti_param, cc_net_to_ms),
                    "[DTAP SMS CP-ACK]", cc_net_to_ms
                        ? "A-interface  (MSC → MS)" : "A-interface  (MS → MSC)");

    if (do_dtap_sms_cp_error)
        send_dtap_a(generate_dtap_sms_cp_error(ti_param, cp_cause_param),
                    "[DTAP SMS CP-ERROR]", "A-interface");
    // ── P14: DTAP MM Supplementary ──────────────────────────────────────────

    if (do_dtap_tmsi_realloc_cmd)
        send_dtap_a(generate_dtap_tmsi_realloc_cmd(mcc, mnc, lac, tmsi_param),
                    "[DTAP TMSI Reallocation Command]", "A-interface  (MSC → MS)");

    if (do_dtap_tmsi_realloc_compl)
        send_dtap_a(generate_dtap_tmsi_realloc_compl(),
                    "[DTAP TMSI Reallocation Complete]", "A-interface  (MS → MSC)");

    if (do_dtap_mm_info)
        send_dtap_a(generate_dtap_mm_information(mm_tz_param),
                    "[DTAP MM Information]", "A-interface  (MSC → MS)");

    // P26: DTAP MM auth reject + abort
    if (do_dtap_mm_auth_reject)
        send_dtap_a(generate_dtap_mm_auth_reject(),
                    "[DTAP MM Authentication Reject]", "A-interface  (MSC → MS)");
    if (do_dtap_mm_abort)
        send_dtap_a(generate_dtap_mm_abort(mm_abort_cause_param),
                    "[DTAP MM Abort]", "A-interface  (MSC ↔ MS)");

    // P26: DTAP CC Notify

    // P27: DTAP MM Status / CM Re-establishment
    if (do_dtap_mm_status)
        send_dtap_a(generate_dtap_mm_status(mm_status_cause_param),
                    "[DTAP MM Status]", "A-interface  (MS ↔ MSC)");
    if (do_dtap_mm_cm_reest_req)
        send_dtap_a(generate_dtap_mm_cm_reest_req(imsi.c_str()),
                    "[DTAP MM CM Re-establishment Request]", "A-interface  (MS → MSC)");

    // P28: DTAP MM Auth Failure / CM Service Abort / CM Service Prompt
    if (do_dtap_mm_auth_failure)
        send_dtap_a(generate_dtap_mm_auth_failure(mm_auth_fail_cause_param, with_auts_param),
                    "[DTAP MM Authentication Failure]", "A-interface  (MS → MSC)");
    if (do_dtap_mm_cm_service_abort)
        send_dtap_a(generate_dtap_mm_cm_service_abort(),
                    "[DTAP MM CM Service Abort]", "A-interface  (MS → MSC)");
    if (do_dtap_mm_cm_service_prompt)
        send_dtap_a(generate_dtap_mm_cm_service_prompt(pd_sapi_param),
                    "[DTAP MM CM Service Prompt]", "A-interface  (MSC → MS)");
    if (do_dtap_cc_notify)
        send_dtap_a(generate_dtap_cc_notify(ti_param, cc_notify_param),
                    "[DTAP CC Notify]", "A-interface  (MS ↔ MSC)");
    if (do_dtap_cc_start_dtmf_ack)
        send_dtap_a(generate_dtap_cc_start_dtmf_ack(ti_param, dtmf_key_param),
                    "[DTAP CC Start DTMF Acknowledge]", "A-interface  (MSC → MS)");
    if (do_dtap_cc_start_dtmf_rej)
        send_dtap_a(generate_dtap_cc_start_dtmf_rej(ti_param, dtmf_start_rej_cause_param),
                    "[DTAP CC Start DTMF Reject]", "A-interface  (MSC → MS)");
    if (do_dtap_cc_stop_dtmf_ack)
        send_dtap_a(generate_dtap_cc_stop_dtmf_ack(ti_param),
                    "[DTAP CC Stop DTMF Acknowledge]", "A-interface  (MSC → MS)");

    // P27: DTAP CC Status Enquiry / User Info / Congestion
    if (do_dtap_cc_status_enquiry)
        send_dtap_a(generate_dtap_cc_status_enquiry(ti_param),
                    "[DTAP CC Status Enquiry]", "A-interface  (MS → MSC)");
    if (do_dtap_cc_user_info)
        send_dtap_a(generate_dtap_cc_user_info(ti_param, uus_proto_param, "\x01\x02", 2),
                    "[DTAP CC User Information]", "A-interface  (MS ↔ MSC)");
    if (do_dtap_cc_congestion)
        send_dtap_a(generate_dtap_cc_congestion(ti_param, cong_level_param),
                    "[DTAP CC Congestion Control]", "A-interface  (MSC → MS)");

    // P28: DTAP CC Facility / Recall / Start CC
    if (do_dtap_cc_facility)
        send_dtap_a(generate_dtap_cc_facility(ti_param),
                    "[DTAP CC Facility]", "A-interface  (MS ↔ MSC)");
    if (do_dtap_cc_recall)
        send_dtap_a(generate_dtap_cc_recall(ti_param, cc_recall_type_param),
                    "[DTAP CC Recall]", "A-interface  (MSC → MS)");
    if (do_dtap_cc_start_cc)
        send_dtap_a(generate_dtap_cc_start_cc(ti_param),
                    "[DTAP CC Start CC]", "A-interface  (MSC → MS)");

    if (do_dtap_cipher_mode_compl)
        send_dtap_a(generate_dtap_cipher_mode_compl(),
                    "[DTAP Ciphering Mode Complete]", "A-interface  (MS → MSC)");

    // P28: DTAP RR Status / Channel Release / Classmark Change / Classmark Enquiry
    if (do_dtap_rr_status)
        send_dtap_a(generate_dtap_rr_status(rr_cause_param),
                    "[DTAP RR Status]", "A-interface  (MS ↔ MSC)");
    if (do_dtap_rr_channel_release)
        send_dtap_a(generate_dtap_rr_channel_release(rr_cause_param),
                    "[DTAP RR Channel Release]", "A-interface  (BSC → MS)");
    if (do_dtap_rr_classmark_change)
        send_dtap_a(generate_dtap_rr_classmark_change(),
                    "[DTAP RR Classmark Change]", "A-interface  (MS → BSC)");
    if (do_dtap_rr_classmark_enquiry)
        send_dtap_a(generate_dtap_rr_classmark_enquiry(),
                    "[DTAP RR Classmark Enquiry]", "A-interface  (BSC → MS)");

    // P29: DTAP RR Assignment Command / Complete / Failure / Handover Command
    if (do_dtap_rr_assignment_command)
        send_dtap_a(generate_dtap_rr_assignment_command(rr_arfcn_param, rr_ts_param),
                    "[DTAP RR Assignment Command]", "A-interface  (BSC → MS)");
    if (do_dtap_rr_assignment_complete)
        send_dtap_a(generate_dtap_rr_assignment_complete(rr_cause_param),
                    "[DTAP RR Assignment Complete]", "A-interface  (MS → BSC)");
    if (do_dtap_rr_assignment_failure)
        send_dtap_a(generate_dtap_rr_assignment_failure(rr_cause_param),
                    "[DTAP RR Assignment Failure]", "A-interface  (MS → BSC)");
    if (do_dtap_rr_handover_command)
        send_dtap_a(generate_dtap_rr_handover_command(rr_arfcn_param, rr_bsic_param),
                    "[DTAP RR Handover Command]", "A-interface  (BSC → MS)");
    if (do_dtap_rr_handover_complete)
        send_dtap_a(generate_dtap_rr_handover_complete(),
                    "[DTAP RR Handover Complete]", "A-interface  (MS → BSC)");
    if (do_dtap_rr_handover_failure)
        send_dtap_a(generate_dtap_rr_handover_failure(rr_cause_param),
                    "[DTAP RR Handover Failure]", "A-interface  (MS → BSC)");
    if (do_dtap_rr_measurement_report)
        send_dtap_a(generate_dtap_rr_measurement_report(rr_rxlev_param, rr_rxqual_param),
                    "[DTAP RR Measurement Report]", "A-interface  (MS → BSC)");
    if (do_dtap_rr_channel_mode_modify)
        send_dtap_a(generate_dtap_rr_channel_mode_modify(rr_ts_param, rr_chan_mode_param),
                    "[DTAP RR Channel Mode Modify]", "A-interface  (BSC → MS)");
    if (do_dtap_rr_ciphering_mode_command)
        send_dtap_a(generate_dtap_rr_ciphering_mode_command(rr_cipher_alg_param),
                    "[DTAP RR Ciphering Mode Command]", "A-interface  (BSC → MS)");
    if (do_dtap_rr_ciphering_mode_complete)
        send_dtap_a(generate_dtap_rr_ciphering_mode_complete(),
                    "[DTAP RR Ciphering Mode Complete]", "A-interface  (MS → BSC)");
    if (do_dtap_rr_ciphering_mode_reject)
        send_dtap_a(generate_dtap_rr_ciphering_mode_reject(rr_cause_param),
                    "[DTAP RR Ciphering Mode Reject]", "A-interface  (MS → BSC)");
    if (do_dtap_rr_immediate_assignment)
        send_dtap_a(generate_dtap_rr_immediate_assignment(),
                    "[DTAP RR Immediate Assignment]", "A-interface  (BSC → MS)");

    talloc_free(ctx);
    return 0;
}
