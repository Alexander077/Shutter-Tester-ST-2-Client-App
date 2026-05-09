# Shutter Tester ST-2 — Desktop Client App

A **Qt6 / C++ desktop application** for Windows that provides a graphical interface to the **Shutter Tester ST-2** hardware device. It communicates with the device over a USB CDC (COM port) connection using the [Serial API](../serial%20API/docs/serial%20API%20docs.md). The application is currently in alpha state (under development) but you can try it, installer is in [dist](./dist/) folder.

---

## Features

- **Device Connection** — auto-detect available COM ports, connect/disconnect, and monitor device status.
- **Echo / Ping** — verify connectivity and read firmware/hardware version information.
- **Light Setup** — real-time dual-sensor light level monitoring with visual status indicators. Helps calibrate the light source before taking measurements.
- **Shutter Measurements** — run single or series of shutter speed measurements with support for:
  - **Frame sizes:** 35mm, 6×4.5, 6×6, 6×7
  - **Shutter types:** Horizontal curtain, Vertical curtain, Leaf shutter
  - **Measurement runs:** configurable number of runs per row, multiple rows
- **Results Table** — view measurement results in a spreadsheet-like table with automatic averaging.
- **Records Management (CRUD, API support, to be added in desktop app)** — save measurements to the device's non-volatile memory, load them back, view a list of stored records, and delete records.
- **Firmware Update (OTA)** — update the device firmware over the USB serial connection with a dedicated dialog and progress tracking.
- **PDF Report Export** — export measured data as printable PDF report.

---

## Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| **Qt** | 6.8+ | Application framework |
| **Qt Widgets** | 6.8+ | UI components |
| **Qt SerialPort** | 6.8+ | COM port communication |
| **Qt PrintSupport** | 6.8+ | Printing measurement tables |
| **Qt Charts** | 6.8+ | Data visualization (charts) |
| **CMake** | 3.16+ | Build system |
| **C++ compiler** | C++17 or later | MinGW |

---

## Build Instructions

### Prerequisites

1. Install **Qt 6.8+** (with Widgets, SerialPort, PrintSupport, and Charts modules).
2. Install **CMake 3.16+**.
<!-- 3. Install a C++ compiler (**MinGW-w64**). -->

#### Example of how to build with CMake 4.3.1, Qt 6.11.0, and MinGW located in [build_scripts/build_debug_qtenv.bat](./build_scripts/build_debug_qtenv.bat)

<!-- ```bash

call "<path-to-qt>\6.11.0\mingw_64\bin\qtenv2.bat"
cd /d "%~dp0\.."
cmake --build build --config Release --target all
```

> Replace `<path-to-qt>` with the actual path to your Qt installation (e.g., `C:\Qt`).
 -->

<!-- ### Build with Qt Creator

1. Open `CMakeLists.txt` as a project in **Qt Creator**.
2. Select the appropriate Qt kit.
3. Build and run from the IDE. -->

The compiled binary will be named **Shutter Tester ST-2 App.exe**.

---

## Project Structure

```
Shutter-Tester-ST-2-App/
├── CMakeLists.txt              # CMake project file
├── main.cpp                    # Application entry point
├── mainwindow.h / .cpp / .ui   # Main window — measurement table, menus, toolbar
├── SerialController.h / .cpp   # Serial communication with the device (JSON protocol)
├── ConnectionDialog.h / .cpp   # COM port selection and connection dialog
├── LightSetupDialog.h / .cpp   # Light calibration / light setup dialog
├── FirmwareUpdateDialog.h / .cpp # Firmware update (OTA) dialog
├── resources.rc                # Windows resource file (icon, metadata)
├── version.rc.in               # Version info template
├── assets/                     # Icons and other bundled assets
├── .gitignore
├── LICENSE                     # GNU GPL v3 license
└── install/                    # NSIS installer scripts
    ├── create_payload.bat
    └── NSIS/
        └── Shutter Tester ST-2 App.nsi
```

---

## Communication Protocol

The application uses the JSON-based **Serial API** at **115200 baud** over a COM port. Every command sent and every structured response received is a single-line JSON object terminated by `\n`.

Full protocol documentation: [`serial API/docs/serial API docs.md`](../serial%20API/docs/serial%20API%20docs.md)

---

## License

Copyright &copy; 2026 Alexander Litvinov  
This program is licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE) for the full text.