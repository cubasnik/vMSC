# Конфигурационные файлы vMSC

## Обзор

vMSC поддерживает сохранение параметров в конфигурационном файле для удобного повторного использования.

## Расположение файла конфигурации

По умолчанию vMSC ищет конфигурацию в следующих местах (в порядке приоритета):
1. `~/.vmsc.conf` (домашняя директория пользователя)
2. `./vmsc.conf` (текущая директория)

Вы также можете указать собственный путь с помощью опции `--config <путь>`.

## Формат конфигурационного файла

Конфигурация использует простой INI-подобный формат:

```ini
# vMSC Configuration File

[network]
mcc=250
mnc=99
lac=12345

[m3ua]
# NI = 0 (International)
opc_ni0=10
dpc_ni0=20
# NI = 2 (National)
opc_ni2=100
dpc_ni2=200
# NI = 3 (Reserved)
opc_ni3=0
dpc_ni3=0
ni=2            # Текущий Network Indicator (0, 2 или 3)

[identity]
imsi=250991234567890

[bssmap]
cell_id=1

[transport]
udp_host=127.0.0.1
udp_port=4729
```

## Использование

### Автоматическая загрузка

При запуске vMSC автоматически загружает конфигурацию, если файл существует:

```bash
./vmsc --send-udp --use-m3ua
```

### Сохранение текущих параметров

Сохранить текущие параметры в конфиг можно с помощью `--save-config`:

```bash
# Сохранить в ~/.vmsc.conf
./vmsc --opc 100 --dpc 200 --lac 54321 --save-config

# Сохранить в конкретный файл
./vmsc --opc 100 --dpc 200 --save-config /path/to/my-config.conf
```

### Загрузка из конкретного файла

```bash
./vmsc --config /path/to/my-config.conf --send-udp --use-m3ua
```

### Переопределение параметров из конфига

Параметры командной строки переопределяют значения из конфига:

```bash
# Использовать конфиг, но изменить OPC
./vmsc --opc 999 --send-udp --use-m3ua
```

## Приоритет параметров

1. **Аргументы командной строки** (наивысший приоритет)
2. **Конфигурационный файл** (указанный через `--config`)
3. **~/.vmsc.conf** (домашняя директория)
4. **./vmsc.conf** (текущая директория)
5. **Значения по умолчанию** (встроенные в программу)

## Примеры использования

### Пример 1: Создание стандартной конфигурации для тестов

```bash
# Настроить параметры и сохранить
./vmsc --opc 100 --dpc 200 --mcc 250 --mnc 99 --lac 12345 \
       --imsi 250991234567890 --save-config

# Теперь можно запускать без параметров
./vmsc --send-udp --use-m3ua
```

### Пример 2: Разные конфиги для разных сетей

```bash
# Сохранить конфиг для сети A
./vmsc --opc 100 --dpc 200 --mcc 250 --save-config ~/vmsc-network-a.conf

# Сохранить конфиг для сети B
./vmsc --opc 300 --dpc 400 --mcc 251 --save-config ~/vmsc-network-b.conf

# Использовать конкретную сеть
./vmsc --config ~/vmsc-network-a.conf --send-udp --use-m3ua
./vmsc --config ~/vmsc-network-b.conf --send-udp --use-m3ua
```

### Пример 3: Разные OPC/DPC для разных Network Indicators

```bash
# В конфиге прописаны Point Codes для каждого NI
# Сохранить конфигурацию
./vmsc --opc-ni0 10 --dpc-ni0 20 \
       --opc-ni2 100 --dpc-ni2 200 \
       --opc-ni3 300 --dpc-ni3 400 \
       --save-config

# Использовать International (NI=0)
./vmsc --send-udp --use-m3ua --ni 0
# Сообщения идут с OPC=10, DPC=20

# Использовать National (NI=2)
./vmsc --send-udp --use-m3ua --ni 2
# Сообщения идут с OPC=100, DPC=200
```

## Секции конфигурационного файла

### [network]
Параметры сети GSM:
- `mcc` - Mobile Country Code (например, 250 для России)
- `mnc` - Mobile Network Code (например, 99)
- `lac` - Location Area Code

### [m3ua]
Параметры M3UA/SIGTRAN с привязкой Point Codes к Network Indicator:
- `opc_ni0` - Originating Point Code для NI=0 (International)
- `dpc_ni0` - Destination Point Code для NI=0 (International)
- `opc_ni2` - Originating Point Code для NI=2 (National)
- `dpc_ni2` - Destination Point Code для NI=2 (National)
- `opc_ni3` - Originating Point Code для NI=3 (Reserved)
- `dpc_ni3` - Destination Point Code для NI=3 (Reserved)
- `ni` - Network Indicator (0=International, 2=National, 3=Reserved)

**Логика работы:** При генерации сообщения программа использует OPC/DPC для текущего значения NI. Это позволяет иметь разные Point Codes для разных типов сетей.

### [identity]
Идентификаторы абонента:
- `imsi` - International Mobile Subscriber Identity

### [bssmap]
Параметры BSSMAP:
- `cell_id` - Cell Identity (используется в Complete Layer 3 Information)

### [transport]
Параметры транспорта:
- `udp_host` - IP адрес получателя
- `udp_port` - UDP порт получателя

## Комментарии в конфигурации

Строки, начинающиеся с `#` или `;`, игнорируются:

```ini
# Это комментарий
; Это тоже комментарий

[network]
mcc=250    # Комментарий после значения
```

## Редактирование конфигурации

Конфигурационный файл можно редактировать любым текстовым редактором:

```bash
nano ~/.vmsc.conf
vim ~/.vmsc.conf
code ~/.vmsc.conf
```

После редактирования изменения применяются при следующем запуске vMSC.
