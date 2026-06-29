# Radio Analyser

Графическое приложение для анализа радиоспектра на базе **HackRF One**. Построено на **Qt 6** и **QCustomPlot**.

Поддерживаются два режима работы:

- **Анализ** — сканирование широкой полосы, отображение спектра и водопада (waterfall).
- **Детектирование** — мониторинг узкой полосы, поиск сигналов выше порога, опциональное прослушивание демодулированного аудио.

Для расчёта FFT доступны два бэкенда:

- **FFTW3** — программный FFT на CPU (работает всегда).
- **FPGA FFT** — аппаратный FFT через плату на FTDI (требует SDK D2XX).

## Аппаратные требования


| Устройство                                                  | Назначение                            | Обязательность  |
| ----------------------------------------------------------- | ------------------------------------- | --------------- |
| HackRF One                                                  | Приём IQ-сэмплов                      | **Обязательно** |
| Плата FPGA + FTDI (серийный номер по умолчанию `FT74ISENA`) | Аппаратный FFT                        | Опционально     |
| Звуковая карта / динамики                                   | Прослушивание в режиме детектирования | Опционально     |




## Зависимости



### Инструменты сборки


| Пакет                          | Минимальная версия |
| ------------------------------ | ------------------ |
| CMake                          | 3.10+              |
| Ninja (рекомендуется) или Make | —                  |
| Компилятор C++17               | GCC 9+ / Clang 10+ |




### Обязательные библиотеки


| Библиотека                       | Назначение                 | Ubuntu / Debian      | Fedora                |
| -------------------------------- | -------------------------- | -------------------- | --------------------- |
| **Qt 6** (Widgets, PrintSupport) | GUI и графики              | `qt6-base-dev`       | `qt6-qtbase-devel`    |
| **QCustomPlot**                  | Виджет построения графиков | `libqcustomplot-dev` | `qcustomplot-devel` * |
| **spdlog**                       | Логирование                | `libspdlog-dev`      | `spdlog-devel`        |
| **FFTW3**                        | Программный FFT            | `libfftw3-dev`       | `fftw-devel`          |
| **libhackrf**                    | Работа с HackRF            | `libhackrf-dev`      | `hackrf-devel`        |
| **pkg-config**                   | Поиск библиотек            | `pkg-config`         | `pkgconf-pkg-config`  |


 На Fedora пакет QCustomPlot может отсутствовать в стандартных репозиториях — в репозитории проекта есть исходники в каталоге `qcustomplot/`, их можно собрать и установить вручную.

### Опциональные библиотеки


| Библиотека                              | Назначение          | Ubuntu / Debian                                                | Fedora                   | Если не установлена                            |
| --------------------------------------- | ------------------- | -------------------------------------------------------------- | ------------------------ | ---------------------------------------------- |
| **Qt 6 Multimedia**                     | Прослушивание аудио | `qt6-multimedia-dev`                                           | `qt6-qtmultimedia-devel` | Режим прослушивания отключён                   |
| **FTDI D2XX** (`ftd2xx.h`, `libftd2xx`) | Связь с FPGA по USB | [SDK с сайта FTDI](https://ftdichip.com/drivers/d2xx-drivers/) | то же                    | Бэкенд FPGA FFT недоступен, используется FFTW3 |




### Установка зависимостей (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-multimedia-dev \
  libqcustomplot-dev libspdlog-dev libfftw3-dev libhackrf-dev \
  hackrf
```

Для работы с FPGA дополнительно установите FTDI D2XX SDK (см. раздел [Настройка FTDI / FPGA](#настройка-ftdi--fpga)).

## Сборка

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Для отладочной сборки замените `Release` на `Debug`.

После сборки исполняемый файл: `build/radio_analyser`.

### Проверка конфигурации CMake

При конфигурации CMake выводит статус опциональных компонентов:

- `Qt6Multimedia found` — аудио включено.
- `Found FTDI D2XX` — FPGA-бэкенд скомпилирован.
- Предупреждения о недостающих компонентах не блокируют сборку (кроме HackRF и обязательных библиотек).



## Запуск

```bash
./build/radio_analyser
```

Приложение открывается в полноэкранном режиме с тёмной темой Fusion.

### Переменные окружения (FPGA)


| Переменная             | Описание                                                            | По умолчанию |
| ---------------------- | ------------------------------------------------------------------- | ------------ |
| `FPGA_FTDI_INDEX`      | Индекс FTDI-устройства при нескольких подключённых                  | `0`          |
| `FPGA_FFT_DEBUG_FRAME` | Однократный вывод отладочной информации о кадре RX (`1` — включить) | выключено    |


Пример:

```bash
FPGA_FTDI_INDEX=1 ./build/radio_analyser
```



## Тесты

```bash
ctest --test-dir build --output-on-failure
```


| Тест                  | Описание                                                    |
| --------------------- | ----------------------------------------------------------- |
| `dsp_self_test`       | Самопроверка DSP-логики и парсинга кадров FPGA (без железа) |
| `fpga_fft_smoke_test` | Сквозной тест FPGA FFT (требует подключённую плату и D2XX)  |


Запуск smoke-теста вручную:

```bash
./build/fpga_fft_smoke_test [tone_bin]
```

Аргумент `tone_bin` — номер тестовой гармоники (1..1023), по умолчанию `100`.

## Настройка HackRF

### Проверка устройства

```bash
hackrf_info
lsusb | grep -i hackrf
```



## Настройка FTDI / FPGA

Для бэкенда **FPGA FFT** нужен FTDI D2XX SDK:

1. Скачайте драйвер D2XX для Linux x86_64 с [сайта FTDI](https://ftdichip.com/drivers/d2xx-drivers/).
2. Распакуйте архив и скопируйте файлы:
  - `ftd2xx.h` → `/usr/local/include/ftd2xx/`
  - `libftd2xx.so` → `/usr/local/lib/`
3. Обновите кэш линкера:

```bash
sudo ldconfig
```

1. Пересоберите проект — CMake должен вывести `Found FTDI D2XX`.



### Конфликт с драйвером ядра `ftdi_sio`

Стандартный драйвер `ftdi_sio` блокирует доступ D2XX. Перед работой с FPGA выгрузите его:

```bash
sudo rmmod ftdi_sio
```

Чтобы не выгружать вручную каждый раз, можно добавить модуль в чёрный список (осторожно — это отключит виртуальные COM-порты FTDI):

```bash
echo "blacklist ftdi_sio" | sudo tee /etc/modprobe.d/ftdi-blacklist.conf
```

Серийный номер FTDI-устройства по умолчанию задан в коде: `FT74ISENA` (см. `FpgaFftProcessor::kDefaultSerial`).

## Работа в WSL2 (Windows)

В репозитории есть вспомогательные скрипты для проброса USB-устройств из Windows в WSL через [usbipd-win](https://github.com/dorssel/usbipd-win).

### Подготовка (один раз)

1. Установите **usbipd-win** в Windows.
2. В PowerShell (от администратора) привяжите устройства:

```powershell
usbipd list
usbipd bind --busid <BUSID_HACKRF>
usbipd bind --busid <BUSID_FTDI>
```

1. В WSL установите клиент USB/IP:

```bash
sudo apt install linux-tools-generic hwdata
sudo update-alternatives --install /usr/local/bin/usbip usbip \
  /usr/lib/linux-tools/*/usbip 20
```



### Подключение устройств

Отредактируйте `configure.sh` — укажите актуальные `busid` ваших устройств (по умолчанию `3-2` и `3-3`):

```bash
./configure.sh
```

Скрипт подключает USB-устройства к WSL и выгружает `ftdi_sio`.

Отключение:

```bash
./detach.sh
```

> **Совет:** `busid` меняется при переподключении кабеля. Всегда проверяйте актуальные значения командой `usbipd list` в Windows.



## Структура проекта

```
radio-analyser/
├── main.cc                  # Точка входа
├── include/                 # Заголовочные файлы
├── src/                     # Исходный код
│   ├── MainWindow*.cc       # GUI и логика интерфейса
│   ├── radio_scanner.cc     # Работа с HackRF и FFT
│   ├── FpgaFftProcessor.cc  # Обмен с FPGA по FTDI
│   ├── SpectrumWorker.cc    # Фоновый расчёт спектра
│   └── AudioProcessorThread.cc  # Демодуляция для прослушивания
├── tests/                   # Модульные и smoke-тесты
├── qcustomplot/             # Исходники QCustomPlot (запасной вариант)
├── configure.sh             # Проброс USB в WSL
└── detach.sh                # Отключение USB от WSL
```



## Типичные проблемы


| Симптом                                 | Решение                                                                                 |
| --------------------------------------- | --------------------------------------------------------------------------------------- |
| `HackRF open failed`                    | Проверьте udev-правила, переподключите устройство, убедитесь что `hackrf_info` работает |
| Нет звука в режиме детектирования       | Установите `qt6-multimedia-dev` и пересоберите проект                                   |
| `FTDI D2XX support is not compiled in`  | Установите SDK D2XX и пересоберите                                                      |
| `FTDI reset failed` / устройство занято | Выполните `sudo rmmod ftdi_sio`, проверьте что FTDI не используется другим процессом    |
| FPGA FFT падает на FFTW3                | Нормальное поведение при отсутствии платы — в логе будет сообщение о fallback           |
| В WSL устройства не видны               | Запустите `configure.sh`, проверьте `usbipd list` и `lsusb`                             |


