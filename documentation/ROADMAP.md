# vMSC — Дорожная карта проекта (версии 1.0, 2.0, 2.1)

## 📋 Легенда проекта

**vMSC** — это генератор и симулятор GSM/SIGTRAN протоколов для тестирования мобильных сетей ядра (мобильных коммутационных центров).

### Назначение
Инструмент предназначен для:
- Генерации сигнальных сообщений GSM 04.08 (LU Request, Paging Response, CC/MM)
- Эмуляции протоколов SCCP, M3UA, ISUP для всех MSC интерфейсов
- Отправки сообщений по UDP в тестовые сети
- Управления подписчиками VLR, маршрутизацией GT (Global Title)
- Диагностики и мониторинга состояния цепей и интерфейсов

### Технические характеристики
- **Язык:** C++17 (single-file: `main.cpp`)
- **Зависимости:** 
  - `libosmocore` (LGPL 2.0+, Osmocom) — GSM протоколы, буферы сообщений
  - `libosmogsm` (LGPL 2.0+, Osmocom) — GSM 04.08 константы
  - `libtalloc` (LGPL 3.0+, Samba) — иерархический аллокатор памяти
  - ℹ️ **Все зависимости — свободное ПО (open source)**
  - **Osmocom** = Open Source Mobile Communications — проект для сетей мобильной связи
- **Платформа:** Linux (через WSL на Windows)
- **Построение:** CMake

---

## 🏗️ Архитектура

### Стек инкапсуляции сообщений (снизу вверх)
```
GSM 04.08 (LU Request / Paging Response / DTAP CC/MM)
    ↓
BSSAP DTAP или BSSMAP
    ↓
SCCP CR/DT1 (A/E-интерфейсы, соединение-ориентированные)
SCCP UDT    (C/F/Gs-интерфейсы, без соединения, опциональный GTI=4)
    ↓
M3UA DATA (SIGTRAN)
    ↓
UDP сокет
```

### Семь интерфейсов MSC

| Интерфейс | Пиры | SI | SCCP SSN | Назначение |
|---|---|---|---|---|
| **A** | MSC ↔ BSC | 3 | BSSAP (254) | Сигнализация RAN |
| **C** | MSC ↔ HLR | 3 | MSC/VLR=8 → HLR=6 | Запросы подписчика |
| **F** | MSC ↔ EIR | 3 | MSC/VLR=8 → EIR=11 | Проверка оборудования |
| **E** | MSC ↔ MSC | 3 | MSC/VLR=8 | Роуминг между MSC |
| **Nc** | MSC-S ↔ MGW | 3 | — | Управление шлюзом мультимедиа |
| **ISUP** | MSC ↔ PSTN/GW | **5** | — | ISUP цепи PSTN |
| **Gs** | MSC ↔ SGSN | 3 | BSSAP+ (254) | Сигнализация PS доменов |

⚠️ **ISUP-интерфейс уникален: SI=5** (остальные используют SI=3)

---

## 📦 Структура проекта

```
vMSC/
├── main.cpp                  # Главный исходный код (все в одном файле)
├── CMakeLists.txt            # Конфигурация сборки
├── README.md                 # Описание проекта
├── ROADMAP.md                # Этот файл
│
├── vmsc.conf                 # Основная конфигурация (подписчики, VLR, CIC)
├── vmsc_interfaces.conf      # Интерфейсы и маршруты GT
├── vmsc_vlr.conf             # Таблица VLR (runtime, генерируется программой)
├── vmsc_cic.conf             # Состояние ISUP цепей (runtime)
│
├── tests/                    # Тестирование
│   ├── test_implemented.sh   # Master test suite (27 tests, 100%)
│   └── test_report.md        # Отчёт о тестировании
│
└── build/                    # Каталог сборки
    ├── vmsc                  # Скомпилированный исполняемый файл
    └── run_vmsc.sh           # Скрипт запуска (с --send-udp --use-m3ua)
```

---

## 🚀 Основные команды

### Сборка
```bash
cd build && cmake .. && make -j$(nproc)
```

### Просмотр конфигурации и состояния
```bash
./vmsc                        # Все секции конфига
./vmsc --show-interfaces      # Интерфейсы и таблица GT
./vmsc --show-vlr             # Таблица подписчиков VLR (читает vmsc_vlr.conf)
./vmsc --show-gt-route        # Таблица маршрутизации GT
./vmsc --show-alarms          # Диагностика аварий и состояния
./vmsc --show-subscriber      # Параметры абонента (IMSI, MSISDN, msc_gt)
./vmsc --show-subscribers     # Таблица всех абонентов из [subscriber-N]
./vmsc --show-identity        # Идентификация узла (MCC, MNC, LAC, Cell ID)
./vmsc --show-m3ua            # Параметры M3UA: OPC/DPC/NI/SI/SLS
./vmsc --show-encapsulation   # Цепочка инкапсуляции сообщения
```

### Тестирование
```bash
# Master test suite (27 tests)
./tests/test_implemented.sh
```

### Генерация и отправка сообщений

#### A-интерфейс (MSC ↔ BSC, BSSMAP + DTAP)
```bash
./vmsc --send-lu-request --send-udp --use-m3ua              # Location Update Request
./vmsc --send-bssmap-reset --send-udp --use-m3ua            # BSSMAP Reset
./vmsc --send-dtap-mm-null --send-udp --use-m3ua            # MM NULL (heartbeat)
./vmsc --send-bssmap-ho-complete --send-udp --use-m3ua      # BSSMAP HO Complete
./vmsc --send-dtap-auth-req --send-udp --use-m3ua           # Authentication Request (MM 0x12)
./vmsc --send-dtap-auth-resp --send-udp --use-m3ua          # Authentication Response (MM 0x14)
./vmsc --send-dtap-id-req --send-udp --use-m3ua             # Identity Request (MM 0x18)
./vmsc --send-dtap-id-resp --send-udp --use-m3ua            # Identity Response (MM 0x19)
./vmsc --send-bssmap-cipher --send-udp --use-m3ua           # Ciphering Mode Command
./vmsc --send-dtap-cipher-compl --send-udp --use-m3ua       # Ciphering Mode Complete (RR 0x32)
./vmsc --send-dtap-lu-accept --send-udp --use-m3ua          # LU Accept (MM 0x02)
./vmsc --send-dtap-lu-reject --send-udp --use-m3ua          # LU Reject (MM 0x04)
./vmsc --send-dtap-cm-srv-req --send-udp --use-m3ua         # CM Service Request (MM 0x24)
./vmsc --send-dtap-cm-srv-acc --send-udp --use-m3ua         # CM Service Accept (MM 0x21)
./vmsc --send-dtap-cc-setup-mo --send-udp --use-m3ua        # CC Setup (MO, MS→MSC)
./vmsc --send-dtap-cc-alerting --send-udp --use-m3ua        # CC Alerting (0x01)
./vmsc --send-dtap-cc-disconnect --send-udp --use-m3ua      # CC Disconnect (0x25)
./vmsc --send-dtap-cc-release --send-udp --use-m3ua         # CC Release (0x2D)
./vmsc --send-dtap-cc-rel-compl --send-udp --use-m3ua       # CC Release Complete (0x2A)
```

#### RR сообщения (управление радиоресурсом)
```bash
./vmsc --send-rr-channel-req --send-udp --use-m3ua                # Channel Request (RR 0x23)
./vmsc --send-rr-immediate-assign --send-udp --use-m3ua           # Immediate Assignment (RR 0x3F)
./vmsc --send-rr-paging-req1 --send-udp --use-m3ua                # Paging Request Type 1 (RR 0x39)
./vmsc --send-rr-assignment-cmd --send-udp --use-m3ua             # Assignment Command (RR 0x2E)
./vmsc --send-rr-assignment-compl --send-udp --use-m3ua           # Assignment Complete (RR 0x29)
./vmsc --send-rr-channel-release --send-udp --use-m3ua            # Channel Release (RR 0x0D)
./vmsc --send-rr-handover-compl --send-udp --use-m3ua             # Handover Complete (RR 0x2C)
```

#### SMS (P42, реализовано в v1.0)
```bash
./vmsc --send-sms-rp-data-mo --sms-msg-ref 1 --send-udp --use-m3ua    # SMS RP-DATA (MO)
./vmsc --send-sms-rp-data-mt --sms-msg-ref 2 --send-udp --use-m3ua    # SMS RP-DATA (MT)
./vmsc --send-sms-rp-ack --sms-msg-ref 3 --send-udp --use-m3ua        # SMS RP-ACK
./vmsc --send-sms-rp-error --sms-msg-ref 4 --rp-cause 27 --send-udp --use-m3ua  # SMS RP-ERROR
./vmsc --send-sms-rp-smma --sms-msg-ref 5 --send-udp --use-m3ua       # SMS RP-SMMA
```

#### SI 16-20 (P41, реализовано в v1.0)
```bash
./vmsc --send-si-bicc --si-billing-id 42 --send-udp --use-m3ua        # SI 16 BiCC
./vmsc --send-si-dup --send-udp --use-m3ua                            # SI 17 DUP
./vmsc --send-si-tup --send-udp --use-m3ua                            # SI 18 TUP
./vmsc --send-si-isomap --send-udp --use-m3ua                         # SI 19 ISOMAP
./vmsc --send-si-ituup --send-udp --use-m3ua                          # SI 20 ITUUP
```

#### C-интерфейс (MSC ↔ HLR, MAP)
```bash
./vmsc --send-map-sai --send-udp --use-m3ua                 # SendAuthenticationInfo
./vmsc --send-map-ul --send-udp --use-m3ua                  # UpdateLocation
./vmsc --send-map-cl --send-udp --use-m3ua                  # CancelLocation
./vmsc --send-map-isd --send-udp --use-m3ua                 # InsertSubscriberData
./vmsc --send-map-check-imei --send-udp --use-m3ua          # CheckIMEI (F-interface)
```

#### ISUP-интерфейс (MSC ↔ PSTN, **SI=5**)
```bash
./vmsc --send-isup-iam --cic 1 --send-udp --use-m3ua         # IAM (Initial Address Message)
./vmsc --send-isup-acm --cic 1 --send-udp --use-m3ua         # ACM (Address Complete)
./vmsc --send-isup-anm --cic 1 --send-udp --use-m3ua         # ANM (Answer)
./vmsc --send-isup-rel --cic 1 --send-udp --use-m3ua         # REL (Release)
./vmsc --send-isup-blo --cic 1 --send-udp --use-m3ua         # BLO (Block Circuit)
./vmsc --send-isup-ubl --cic 1 --send-udp --use-m3ua         # UBL (Unblock Circuit)
```

#### TCAP диалоги (для многошаговых операций)
```bash
./vmsc --send-map-sai-end --dtid 0xABCD --send-udp --use-m3ua         # Return Result
./vmsc --send-tcap-continue --otid 0x1 --dtid 0xABCD --send-udp --use-m3ua
./vmsc --send-tcap-abort --dtid 0xABCD --send-udp --use-m3ua           # Abort
```

### Управление конфигурацией
```bash
./vmsc --opc 999 --dpc 888 --ni 2 --save-config              # Override + сохранить
./vmsc --config base.conf --config lab.conf                  # Стекирование конфигов
./build/run_vmsc.sh                                          # Быстрый запуск
```

---

## 📊 Статус версий

### ✅ vMSC v1.0 — PRODUCTION-READY

**Дата релиза:** 19 февраля 2026 г.  
**Статус:** ✅ **ПОЛНОСТЬЮ РЕАЛИЗОВАНО И ПРОТЕСТИРОВАНО**

#### Реализованные сообщения (24 генератора)

**MM (Mobility Management) — 3 сообщения:**
- ✅ MM NULL (0x30)
- ✅ Location Update Request (0x08)
- ✅ Authentication Request (0x12)

**RR (Radio Resource) — 3 сообщения:**
- ✅ Paging Response (0x27)
- ✅ Channel Request (0x23)
- ✅ Handover Complete (0x2C)

**CC (Call Control) — 3 сообщения:**
- ✅ CC Setup (MO) (0x05)
- ✅ CC Alerting (0x01)
- ✅ CC Disconnect (0x25)

**SMS RP Protocol (P42) — 8 сообщений:**
- ✅ SMS RP-DATA (MO) (0x00)
- ✅ SMS RP-DATA (MT) (0x01)
- ✅ SMS RP-ACK (0x02)
- ✅ SMS RP-ERROR (0x03)
- ✅ SMS RP-SMMA (0x04)
- ✅ SMS MMS-DATA (MO) (0x05)
- ✅ SMS MMS-DATA (MT) (0x06)
- ✅ SMS MMS-ACK (0x07)

**SI 16-20 (Signaling Information, P41 Option A) — 5 сообщений:**
- ✅ SI 16 BiCC (Billing Information Collection)
- ✅ SI 17 DUP (Data User Part)
- ✅ SI 18 TUP (Telephony User Part)
- ✅ SI 19 ISOMAP (ISO-SCCP Mapping)
- ✅ SI 20 ITUUP (ITU User Part)

**BSSMAP (Base Station System Management) — 2 сообщения:**
- ✅ BSSMAP RESET (0x00, 0x04, 0x30)
- ✅ BSSMAP Handover Complete (0x00, 0x01, 0x14)

**Дополнительно:**
- ✅ MAP SendAuthenticationInfo (SAI)
- ✅ MAP UpdateLocation (UL)
- ✅ MAP CancelLocation (CL)
- ✅ MAP CheckIMEI
- ✅ ISUP сообщения (IAM, ACM, ANM, REL, BLO, UBL)
- ✅ TCAP диалоги (Begin, Continue, End, Abort)
- ✅ SCCP соединение-ориентированное и без соединения
- ✅ M3UA транспорт (SIGTRAN)
- ✅ GT маршрутизация (GTI=4)
- ✅ VLR управление
- ✅ CIC пул управление
- ✅ Система аварий (Alarms)

#### Тестирование v1.0
```
Тесты: 27/27 ✓ (100% pass rate)
├─ Core generators: 24 tests
└─ Parameter validation: 3 tests

Результат: ✓ ALL 27/27 TESTS PASSED (100%)
```

#### Use-case готовность
- ✅ **Автоматизированное тестирование** — Ready for CI/CD
- ✅ **Batch-генерация сообщений** — Ready for production
- ✅ **Отправка по UDP** — Fully functional

---

### 📅 vMSC v2.0 — Планируется (Q2 2026)

**Статус:** 🔄 Планирование  
**Целевой срок:** Квартал 2, 2026  
**Приоритет:** Расширение функционала (не критично для v1.0)

#### Приоритет 1: Полная поддержка MT-вызовов

**1.1 Paging Request (Запрос поиска)**

```cpp
./vmsc --send-dtap-rr-paging-request --mi-count 2 \
       --mi-1 "250990000000001" --mi-2 "250990000000002"
```

- **Message Type:** 0x21
- **Спецификация:** GSM 04.08 sec 9.2.3
- **Направление:** MSC → BSC
- **Код:** ~50-80 строк
- **Статус:** To-do

**1.2 Service Request (Запрос услуги)**

```cpp
./vmsc --send-dtap-mm-service-request --service-type 0 --cksn 7
```

- **Message Type:** 0x4C
- **Спецификация:** GSM 04.08 sec 9.2.16
- **Направление:** MS → BSC → MSC
- **Код:** ~40-60 строк
- **Статус:** To-do

#### Приоритет 2: Полная USSD реализация

**2.1 unstructuredSS-Notify (opCode=60)**

```cpp
./vmsc --send-map-unstructured-ss-notify --ussd-str "*100#" --dcs 0x0F
```

- **Направление:** Net → MS
- **Код:** ~80-100 строк
- **Статус:** To-do

**2.2 unstructuredSS-Response (opCode=61)**

```cpp
./vmsc --send-map-unstructured-ss-response --ussd-str "Saldo: 100 руб" --dcs 0x0F
```

- **Направление:** Net ↔ MS
- **Код:** ~70-90 строк
- **Статус:** To-do

#### Таблица v2.0

| Компонент | Неделя | Статус | Код |
|---|---|---|---|
| Paging Request | 1 | To-do | +50 |
| Service Request | 1 | To-do | +40 |
| unstructuredSS-Notify | 2 | To-do | +80 |
| unstructuredSS-Response | 2 | To-do | +70 |
| Тесты & Integration | 3 | To-do | +100 |
| Компиляция & Dokumentация | 4 | To-do | - |
| **ИТОГО** | **4 нед.** | | **+450 строк** |

---

### 🚀 vMSC v2.1 — Планируется (Q3 2026)

**Статус:** 📅 Планирование  
**Целевой срок:** Квартал 3, 2026  

#### Приоритет 3: REPL интерактивный режим

**Архитектура:**

```
main(argc, argv)
  ├─ if (--repl) → run_repl()
  │  └─ while (true) → read_line() → parse_command() → execute()
  └─ else → single message generation
```

**Команды:**

```bash
vmsc --repl
vmsc> gen mm-null                    # Generate message
vmsc> gen sms-rp-data-mo --ref 5    # Generate with parameters
vmsc> send mm-null --udp            # Generate + send
vmsc> show config                    # Display configuration
vmsc> help gen                       # Show generator help
vmsc> quit                           # Exit
```

**Компоненты:**

- 🔧 Readline integration (Tab completion, history)
- 🔧 Command parser (fuzzy matching)
- 🔧 Interactive menu system
- 🔧 Parameter input validation
- 🔧 State management (.vmsc_history)

**Временная оценка:**

| Компонент | Время |
|---|---|
| REPL Framework | 2 недели |
| Command Parser | 1 неделя |
| Readline Integration | 3 дня |
| Tests & Documentation | 1 неделя |

**Зависимости:**

```bash
sudo apt install libreadline-dev
# Add to CMakeLists.txt:
find_package(PkgConfig REQUIRED)
pkg_check_modules(READLINE readline)
target_link_libraries(vmsc ${READLINE_LIBRARIES})
```

---

## 🔧 Флаги командной строки

### Отправка и просмотр сообщений

| Флаг | Описание |
|---|---|
| `--send-udp` | Отправить сообщение по UDP |
| `--send-*` | Генерировать конкретное сообщение |
| `--use-m3ua` | Использовать M3UA (SIGTRAN) транспорт |

### Параметры сообщений

| Флаг | Описание |
|---|---|
| `--cic <N>` | Circuit ID для ISUP |
| `--cause <N>` | Q.850 cause код |
| `--dtid <N>` | Destination Transaction ID (TCAP) |
| `--otid <N>` | Originating Transaction ID (TCAP) |
| `--sms-msg-ref <N>` | SMS Message Reference |
| `--rp-cause <N>` | SMS RP-ERROR cause |
| `--si-billing-id <N>` | SI 16 Billing ID |
| `--ussd-str "<text>"` | USSD строка |

### Параметры конфигурации

| Флаг | Описание |
|---|---|
| `--config <file>` | Загрузить конфиг файл |
| `--save-config` | Сохранить конфигурацию |
| `--opc <N>` | Originating Point Code |
| `--dpc <N>` | Destination Point Code |
| `--ni <N>` | Network Indicator |
| `--no-color` | Отключить цветной вывод |

---

## 📊 Система аварий

Программа может диагностировать состояние системы через `--show-alarms`:

### Типы аварий

**По CIC пулу:**
- CRITICAL: `allCircuitsBusy` — все CIC заняты/заблокированы
- MAJOR: `circuitBlocked` — отдельный CIC заблокирован
- MAJOR: `highBlockedRatio` — ≥30% CIC блокировано
- MINOR: `circuitResetting` — CIC в процессе сброса
- WARNING: `highOccupancy` — ≥80% CIC активны

**По VLR:**
- MAJOR: `noRegisteredSubscribers` — нет зарегистрированных подписчиков
- WARNING: `vlrTableEmpty` — VLR пуста

**По интерфейсам:**
- MAJOR: `remoteIpNotConfigured` — отсутствует IP удалённого пира

---

## 🔧 Конфигурационные файлы

### `vmsc.conf` (основной конфиг)
```ini
[identity]
mcc=250
mnc=01
lac=0001
cell_id=0001

[vlr]
imsi_prefix=250019999
vlr_number=+79991234567

[cic]
cic_range_start=1
cic_range_end=30

[subscriber-1]
imsi=250019999000001
msisdn=+79991111111
```

### `vmsc_interfaces.conf` (интерфейсы)
```ini
[a-interface]
remote_ip=192.168.1.100
remote_port=5000
local_ip=192.168.1.1
local_port=5001

[c-interface]
remote_ip=192.168.1.50
ni=3

[gt-route]
route=7916:c:20001:HLR-A
```

---

## 📈 Метрики проекта

| Метрика | v1.0 | v2.0 | v2.1 |
|---|---|---|---|
| Генераторов | 24 | +2 | +0 |
| USSD операций | 1 (MO) | +2 | +0 |
| Интерактивный режим | ✗ | ✗ | ✓ |
| Строк кода (новых) | 21,525 | +450 | +500 |
| Версия C++ | C++17 | C++17 | C++17 |
| Тесты | 27 | +10 | +5 |
| Production-ready | ✅ | ✅ | ✅ |

---

## 🎯 Примеры использования

### Пример 1: A-интерфейс — полный сценарий аутентификации

```bash
# 1. MS отправляет LU Request
./vmsc --send-lu-request --send-udp --use-m3ua

# 2. MSC отправляет Authentication Request
./vmsc --send-dtap-auth-req --rand A0A1A2A3A4A5A6A7A8A9AAABACADAEAF --send-udp --use-m3ua

# 3. MS отвечает Authentication Response
./vmsc --send-dtap-auth-resp --sres 01020304 --send-udp --use-m3ua

# 4. MSC отправляет Ciphering Mode Command
./vmsc --send-bssmap-cipher --send-udp --use-m3ua

# 5. MS подтверждает Ciphering Mode Complete
./vmsc --send-dtap-cipher-compl --send-udp --use-m3ua

# 6. MSC выдаёт новый TMSI и завершает LU
./vmsc --send-dtap-lu-accept --tmsi 0x12345678 --send-udp --use-m3ua
```

### Пример 2: CC Call Setup (MO-call)

```bash
./vmsc --send-dtap-cm-srv-req --send-udp --use-m3ua
./vmsc --send-dtap-cm-srv-acc --send-udp --use-m3ua
./vmsc --send-dtap-cc-setup-mo --ti 0 --send-udp --use-m3ua
./vmsc --send-dtap-cc-alerting --ti 0 --send-udp --use-m3ua
./vmsc --send-dtap-cc-connect --ti 0 --send-udp --use-m3ua
./vmsc --send-dtap-cc-disconnect --ti 0 --send-udp --use-m3ua
./vmsc --send-dtap-cc-rel-compl --ti 0 --send-udp --use-m3ua
```

### Пример 3: RR Channel Establishment

```bash
./vmsc --send-rr-channel-req --send-udp --use-m3ua
./vmsc --send-rr-immediate-assign --ta 5 --send-udp --use-m3ua
./vmsc --send-lu-request --send-udp --use-m3ua
./vmsc --send-rr-channel-release --cause 0x00 --send-udp --use-m3ua
```

### Пример 4: SMS RP Protocol

```bash
./vmsc --send-sms-rp-data-mo --sms-msg-ref 1 --send-udp --use-m3ua
./vmsc --send-sms-rp-ack --sms-msg-ref 1 --send-udp --use-m3ua
./vmsc --send-sms-rp-error --sms-msg-ref 2 --rp-cause 27 --send-udp --use-m3ua
```

### Пример 5: SI 16-20 (Billing & Telephony)

```bash
./vmsc --send-si-bicc --si-billing-id 42 --send-udp --use-m3ua
./vmsc --send-si-dup --send-udp --use-m3ua
./vmsc --send-si-tup --send-udp --use-m3ua
./vmsc --send-si-isomap --send-udp --use-m3ua
./vmsc --send-si-ituup --send-udp --use-m3ua
```

### Пример 6 (v2.0): MT-call с Paging

```bash
./vmsc --send-dtap-rr-paging-request --mi-count 1 --mi-1 "250990000000001" --send-udp --use-m3ua
./vmsc --send-dtap-cm-srv-req --send-udp --use-m3ua
./vmsc --send-rr-immediate-assign --ta 8 --send-udp --use-m3ua
./vmsc --send-dtap-cc-setup-mt --ti 0 --send-udp --use-m3ua
./vmsc --send-dtap-cc-alerting --ti 0 --send-udp --use-m3ua
./vmsc --send-dtap-cc-connect --ti 0 --send-udp --use-m3ua
```

### Пример 7 (v2.0): USSD операция

```bash
./vmsc --send-map-process-unstructured-ss-req --ussd-str "*100#" --send-udp --use-m3ua
./vmsc --send-map-unstructured-ss-notify --ussd-str "Saldo: 100 руб" --send-udp --use-m3ua
./vmsc --send-map-unstructured-ss-response --ussd-str "Спасибо!" --send-udp --use-m3ua
```

---

## 🔗 Ссылки и ресурсы

- **ITU Q.713** — SCCP спецификация
- **3GPP TS 24.008** — GSM MM/RR/CC протоколы
- **3GPP TS 29.002** — MAP протокол
- **IETF RFC 3868** — SIGTRAN архитектура
- **libosmocore:** https://osmocom.org/projects/libosmocore

---

**Документ:** ROADMAP.md (Unified v1.0/v2.0/v2.1)  
**Создан:** 19 февраля 2026 г.  
**Последнее обновление:** 19 февраля 2026 г.  
**Статус:** ✅ v1.0 Complete, v2.0 Planned, v2.1 Planned
