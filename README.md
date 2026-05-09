# Shutter Tester ST-2 — Desktop Client & Serial API

This repository contains **Windows desktop client** application and the **Serial API documentation & tests** for the **Shutter Tester ST-2** — a hardware device designed for testing and calibrating film camera shutters.

---

## Repository Structure

### 📁 `serial API/`

The **Serial API** defines the JSON-based communication protocol between the Shutter Tester ST-2 device and any third-party software over a USB CDC (COM port) connection.

| Path | Description |
|------|-------------|
| [`serial API/docs/serial API docs.md`](serial%20API/docs/serial%20API%20docs.md) | Full API reference: commands, responses, error codes, and protocol rules for echo, light setup, measurements, records storage (CRUD), and firmware updates (OTA). |
| [`serial API/tests/`](serial%20API/tests/) | Python-based test/demo scripts for exercising the API against the device. |

> ⚡ The Serial API is designed to be used by **any application or script** — not just the official client. Use it to integrate the Shutter Tester ST-2 into your own workflows.

### 📁 `Shutter-Tester-ST-2-App/`

A **Qt-based C++ desktop application** for Windows that provides a full graphical interface to the Shutter Tester ST-2 device: connecting via COM port, configuring light setup, running measurements, managing saved records (to be added), and performing firmware updates.

> 🖥️ See [`Shutter-Tester-ST-2-App/README.md`](Shutter-Tester-ST-2-App/README.md) for build instructions, dependencies, and project structure.

---

## Key Features (Device & Client)

- **Echo / Ping** — verify device connectivity and read firmware/hardware version.
- **Light Setup** — real-time light level and quality monitoring for precise sensor calibration.
- **Shutter Measurements** — measure shutter speeds and curtain travel times with support for multiple frame sizes (35mm, 6×4.5, 6×6, 6×7) and shutter types (horizontal, vertical, leaf).
- **Records Storage (CRUD, to be added)** — list, read, save, and delete measurement records stored in the device's non-volatile memory.
- **Firmware Update (OTA)** — update device firmware over the same USB serial connection.

---

## Quick Start (API Testing)

1. Connect the Shutter Tester ST-2 via USB.
2. Note the assigned COM port (e.g., `COM40`).
3. Run the test script:

```bash
python "serial API/tests/api_test_demo.py"
```

---

## License

This project is licensed under the **GNU General Public License v3.0**. See [`Shutter-Tester-ST-2-App/LICENSE`](Shutter-Tester-ST-2-App/LICENSE) for the full text.