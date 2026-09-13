# 🩸 Smart Tourniquet System for Rapid Hemorrhage Control in Disaster Victims

> Proyek Desain Proyek Teknik Elektro, Komputer, Biomedik 2 — Universitas Indonesia (2026/2027)
> Sistem tourniquet cerdas hibrida (pneumatik otomatis + cadangan mekanik) dengan **Focal Pressure
> Pad** dan algoritma **Limb Occlusion Pressure (LOP)** closed-loop. Klien: Basarnas / BNPB.

**CATATAN BAHASA PEMROGRAMAN:** seluruh kode di repo ini ditulis dalam **C murni** (bukan C++,
bukan Python) — termasuk firmware (ESP-IDF native, bukan Arduino framework, karena Arduino API
seperti Serial/Wire adalah objek C++) dan seluruh script perhitungan (dulunya Python, sekarang
program C standalone yang bisa dikompilasi dengan `gcc`).

## 👥 Team — Kelompok 19

| Nama | Program Studi | Peran |
|---|---|---|
| Dhafin Hamizan Setiawan | Teknik Komputer | Ketua — Firmware & Logic |
| Grace Kezia Siregar | Teknik Biomedik | Biomekanika FPP, Parameter LOP, Validasi Sensor & Phantom, FMEA |
| Michelle Winona Joyceline Simamora | Teknik Biomedik | UI/UX, Interface Klinis |
| Daffa Bagus Dhiananto | Teknik Komputer | Firmware, DSP, State Machine |
| Drina Shahada Wibowo | Teknik Elektro | Hardware, PCB, Manajemen Daya |

**Dosen Pembimbing:** Siti Fauziyah Rahman, S.T., M.Eng., Ph.D

## 📂 Struktur Repository

```
📦 smart-tourniquet-system
 ┣ 📂 Algorithms            # Program C standalone untuk validasi perhitungan
 ┃ ┣ sensor_conversion.c    # konversi MPX5050DP, cek rentang & akurasi sensor
 ┃ ┣ butterworth_validate.c # validasi stabilitas & respons frekuensi filter
 ┃ ┗ power_and_driver_calc.c# perhitungan MOSFET & daya/baterai
 ┣ 📂 Docs                  # Datasheet, proposal, laporan kemajuan, catatan perhitungan
 ┣ 📂 Hardware
 ┃ ┣ 📂 Schematics          # File KiCad (.kicad_sch, .kicad_pcb) + Gerber
 ┃ ┗ 📂 BOM                 # Bill of Materials
 ┣ 📂 Mechanical            # File CAD parametrik (OpenSCAD)
 ┣ 📂 firmware
 ┃ ┗ 📂 esp32-idf-c         # Firmware C murni (ESP-IDF native, BUKAN Arduino)
 ┃   ┣ 📂 main
 ┃   ┃ ┣ main.c             # SATU file berisi seluruh logic sistem
 ┃   ┃ ┗ CMakeLists.txt
 ┃   ┣ CMakeLists.txt
 ┃   ┗ platformio.ini
 ┣ .gitignore
 ┣ LICENSE
 ┗ README.md
```

## 🚀 Menjalankan Program Perhitungan (Algorithms/)

Semua program C di `Algorithms/` berdiri sendiri, tidak butuh library eksternal selain
`math.h` standar. Kompilasi & jalankan:

```bash
cd Algorithms
gcc -o sensor_conversion sensor_conversion.c -lm && ./sensor_conversion
gcc -o butterworth_validate butterworth_validate.c -lm && ./butterworth_validate
gcc -o power_and_driver_calc power_and_driver_calc.c -lm && ./power_and_driver_calc
```

## 🚀 Build & Flash Firmware (firmware/esp32-idf-c/)

Firmware ini ditulis pakai **ESP-IDF native C API** (bukan Arduino framework), supaya benar-benar
C murni tanpa dependensi objek C++ (Serial, Wire, dst dari Arduino adalah class C++).

### Prasyarat
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/) terinstal, ATAU
- [PlatformIO](https://platformio.org/) dengan `framework = espidf` (sudah dikonfigurasi di `platformio.ini`)

### Build & flash (pakai ESP-IDF langsung)
```bash
cd firmware/esp32-idf-c
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Build & flash (pakai PlatformIO)
```bash
cd firmware/esp32-idf-c
pio run
pio run -t upload
pio device monitor
```

## ⚠️ Status & Isu Terbuka

Lihat `Docs/perhitungan_sensor.md`, `Docs/perhitungan_driver.md`, dan
`Docs/perhitungan_butterworth.md` untuk daftar lengkap keputusan desain yang **belum final**,
di antaranya:
- Konflik rentang sensor MPX5050DP (375 mmHg rated) vs target hard limit PDS (400 mmHg)
- Pemilihan MOSFET final (RDS(on) di VGS=3.3V belum diverifikasi untuk IRLZ44N)
- Estimasi umur baterai worst-case (0.91-1.33 jam) belum memenuhi target PDS 2 jam
- Perilaku reset filter Butterworth antar window sampling belum diuji dengan data phantom arm

## ⚖️ License
Proyek akademik — Desain Proyek Teknik Biomedik/Elektro/Komputer, Fakultas Teknik Universitas
Indonesia. Lisensi MIT untuk kode (lihat `LICENSE`).

**Catatan keselamatan:** perangkat keras hasil proyek ini adalah prototipe akademik dan BELUM
melalui uji klinis, kaji etik, atau registrasi alat kesehatan Kemenkes RI. Tidak untuk digunakan
pada manusia di luar konteks pengujian phantom/simulasi yang disetujui pembimbing.
