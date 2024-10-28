# ESP32 Walkie-Talkie Project Setup in Visual Studio Code

This project is a **Walkie-Talkie** system designed to securely transmit audio data between two ESP32 devices using **Wi-Fi**. The communication is encrypted using **AES-128** encryption to ensure privacy. It utilizes the **I2S** protocol for high-quality audio transmission, and the user interface is displayed on the built-in **TFT display** of the ESP32 TTGO LILYGO. The project involves the use of peripherals such as microphones and speakers to enable real-time audio exchange.

## Project Overview

The project establishes secure, encrypted communication between two ESP32 devices acting as a Walkie-Talkie system. It uses the **UDP protocol** for fast audio data transmission over Wi-Fi, ensuring minimal latency. The system also incorporates:

- **AES-128 encryption** for secure communication.
- **I2S protocol** for audio data handling.
- **Dynamic IP assignment** via **DHCP**.
- A user-friendly **TFT display interface** for ease of interaction.

## Requirements

- **ESP32 TTGO LILYGO** (or a similar ESP32 board with a TFT display)
- **Microphone INMP441**
- **MAX98357A amplifier** for audio output
- **Visual Studio Code (VSCode)** installed
- **ESP-IDF** installed and configured
- **Python 3.7+** installed
- **Git** for version control

## Setting Up Visual Studio Code

1. **Install VSCode**: Download and install [Visual Studio Code](https://code.visualstudio.com/).
2. **Install ESP-IDF extension**: Open VSCode, navigate to Extensions (`Ctrl+Shift+X`), and install the **Espressif IDF** extension.
3. **Install Python extension**: Install the **Python** extension for VSCode.
4. **Configure ESP-IDF**: 
   - Open the Command Palette (`Ctrl+Shift+P`) and search for `ESP-IDF: Configure ESP-IDF`.
   - Set the **ESP-IDF Path** to the location where ESP-IDF is installed.
5. **Install ESP-IDF Tools**

## Configuring the Project

1. **Clone the Project Repository**:

   ```bash
   git clone https://github.com/RegenerationPower/ESP32-Walkie-Talkie.git
   ```

2. **Open the Project in VSCode**

3. **Configure the Project Settings**:
   Run the following command to configure the project:

   ```bash
   idf.py menuconfig
   ```

   - Set your **Wi-Fi SSID** and **password** in the Wi-Fi configuration section.
   - Configure the **AES encryption** key for secure communication.

## Building and Flashing the Firmware

1. **Build the Project**: Open the terminal and build the project:

   ```bash
   idf.py build
   ```

2. **Flash the ESP32**: Connect your ESP32 board via USB and run:

   ```bash
   idf.py -p /dev/ttyUSB0 flash
   ```

   Replace `/dev/ttyUSB0` with the correct port on your system.

3. **Monitor Output**: After flashing, monitor the ESP32's output to see logs in real-time:

   ```bash
   idf.py monitor
   ```

## Running and Debugging

1. **Run the Project**: After flashing, the Walkie-Talkie system will initialize and connect to the configured Wi-Fi network. The device will start listening for audio packets from another paired ESP32 device.

2. **Debugging**: Set breakpoints in your code and use the ESP-IDF debug tools in VSCode:
   - Open the Command Palette (`Ctrl+Shift+P`) and select `ESP-IDF: Start Debugging`.
   - Ensure your ESP32 board supports JTAG debugging if necessary.

## Project Structure

```bash
Walkie-Talkie/
│
├── components/
│   └── st7789/           # Handles st7789 display communication
│
├── main/
│   └── main.c            # Contains all project code
│   └── CMakeLists.txt    # Include include dirs and src
│
├── partitions.csv        # Defines memory partittions for the ESP32
├── sdkconfig             # Configuration file from menuconfig
├── CMakeLists.txt        # Build system configuration
└── Makefile              # ESP-IDF Makefile
```

## Troubleshooting

### Common Issues

1. **Cannot Connect to Wi-Fi**:
   - Ensure the correct **SSID** and **password** are set in the configuration.
   - Check if the ESP32 is within range of the Wi-Fi network.

2. **Audio is Distorted**:
   - Check if the **I2S configuration** matches the microphone and amplifier specifications.
   - Verify that the sampling rate is correctly set for the audio input/output.

3. **Flashing Fails**:
   - Ensure that the correct serial port is selected.
   - Try resetting the ESP32 board before flashing.
