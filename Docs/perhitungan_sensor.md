# Perhitungan Transfer Function Sensor MPX5050DP

**Status:** sudah direview — lihat "Isu terbuka" untuk poin yang masih perlu keputusan desain
atau validasi empiris. Validasi numerik lihat `Algorithms/sensor_conversion.c` (program C,
kompilasi: `gcc -o sensor_conversion sensor_conversion.c -lm`).

## 1. Rumus dasar (datasheet Motorola/NXP)

    Vout = VS x (0.018 x P_kPa + 0.04)

## 2. Konversi ke mmHg (1 kPa = 7.50062 mmHg), VS = 5.0V

    Vout = 0.012 x P_mmHg + 0.2

Sensitivitas efektif: **12 mV/mmHg**, offset **0.2 V** pada P = 0. Rumus invers (dipakai di
firmware `main.c`, fungsi `pressure_read_mmhg()`):

    P_mmHg = (Vout - 0.2) / 0.012

## 3. Tabel validasi

| P (mmHg) | Vout (V) |
|---|---|
| 0   | 0.200 |
| 50  | 0.800 |
| 100 | 1.400 |
| 150 | 2.000 |
| 220 (target LOP lengan) | 2.840 |
| 280 (target LOP paha)   | 3.560 |
| 300 | 3.800 |
| 375 (batas rated/POP datasheet) | 4.700 |
| ~400 (titik saturasi elektris, Vout=VS) | ~5.000 |

## 4. Isu terbuka (WAJIB diputuskan/divalidasi sebelum implementasi final)

### 4.1 Rentang sensor 375 mmHg vs target hard limit 400 mmHg (PDS spec A.1) — BELUM DIPUTUSKAN

Sensor **tidak clipping elektris** sampai kira-kira 400 mmHg (Vout baru menyentuh VS di titik
itu). Tapi **375 mmHg adalah batas rentang operasi TERKALIBRASI (rated/POP)** menurut datasheet
— akurasi dan linearitas di atas titik itu (375-400 mmHg) TIDAK dijamin pabrikan.

Pilih salah satu:
- **Opsi A:** turunkan hard limit software sistem menjadi ≤375 mmHg (revisi PDS spec A.1)
- **Opsi B:** tetap 400 mmHg, validasi manual dengan manometer referensi di rentang 375-400 mmHg
- **Opsi C:** ganti sensor ke seri dengan rentang lebih tinggi (mis. MPX5100 series)

### 4.2 Akurasi sensor ±9.4 mmHg vs target PDS ±2 mmHg (spec A.4) — PERLU KALIBRASI

Akurasi mentah (±2.5% VFSS = ±9.4 mmHg) jauh dari target ±2 mmHg. **Wajib** kalibrasi
multi-titik terhadap manometer referensi (PIC: Grace/Biomedik, bagian protokol validasi phantom
arm) + kompensasi suhu di software.

### 4.3 Level tegangan ADS1115 vs I2C ESP32 — DIIMPLEMENTASIKAN (Opsi A)

Firmware `main.c` memakai **Opsi A**: ADS1115 disupply 3.3V, Vout sensor MPX5050DP (bisa sampai
~5V) diturunkan pakai voltage divider (R1=2.2k, R2=3.3k, rasio 0.6) sebelum masuk ADS1115.
Vadc maksimum hasil divider = 3.0V, aman di bawah batas 3.3V (margin 0.3V).

Alternatif **Opsi B** (ADS1115 tetap 5V + I2C level shifter BSS138) tersedia sebagai gantinya —
kalau dipilih, ubah `DIVIDER_RATIO` di `main.c` jadi `1.0f`.

### 4.4 Gain ADS1115

`GAIN_ONE` (range ±4.096V) — cukup untuk Vadc maks ~3.0V hasil divider, resolusi 0.0174
mmHg/bit (sudah dikompensasi rasio divider), jauh lebih presisi dari kebutuhan (1 mmHg).
