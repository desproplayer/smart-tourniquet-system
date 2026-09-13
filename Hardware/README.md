# 🔌 Hardware

- `Schematics/` — file KiCad (.kicad_pro, .kicad_sch, .kicad_pcb) + Gerber untuk PCB custom 2-layer
- `BOM/` — Bill of Materials

Untuk validasi logic sebelum PCB jadi: model MPX5050DP sebagai input analog buatan di Wokwi
(bukan komponen drag-and-drop bawaan), atau flash langsung firmware C (`firmware/esp32-idf-c/`)
ke ESP32-S3 devkit untuk uji breadboard.
