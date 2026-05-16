# ESP32 Framework Notes

## Konsep Awal
- Web UI satu halaman (`index.h`) dengan tab: OTA, Web Terminal, Credential, Mesh, ESP-NOW, URL.
- Komunikasi data hanya aktif pada tab yang sedang aktif supaya tidak tabrakan dan hemat resource.
- Setiap modul bisa diaktif/nonaktifkan via macro compile-time `FW_ENABLE_*`.
- Tahap saat ini fokus OTA + Web Terminal.

## Status Implementasi Saat Ini
- **OTA**: upload firmware `.bin`, reboot, rollback (jika didukung core/bootloader), status JSON.
- **Web Terminal**:
  - API mirip serial: `WebPrint`, `WebPrintln`, `WebRead`.
  - Batas TX/RX per buffer: **4092 byte**.
  - Data TX dikirim ke browser lalu buffer TX dibersihkan.
  - Data RX ditulis dari browser ke ESP32 lalu dibaca melalui `WebRead()` dan dibersihkan.
- Frontend menggunakan **HTTP fetch/XHR polling** (bukan WebSocket) untuk saat ini.

## Aturan Pengembangan
- `ESP32Framework.ino` dijaga minimal, hanya `setup()` dan `loop()`.
- Fungsi pendukung dipisah ke file lain (`.h/.cpp`) agar modular.
- `buildStatusJson()` dibiarkan fleksibel supaya mudah diubah saat sensor sudah final.
