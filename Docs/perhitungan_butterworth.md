# Perhitungan Filter Butterworth Bandpass Orde 5

**Status:** sudah direview. Validasi numerik lihat `Algorithms/butterworth_validate.c`
(kompilasi: `gcc -o butterworth_validate butterworth_validate.c -lm`) — program C ini
memvalidasi ulang stabilitas & respons frekuensi koefisien secara independen (pakai
`complex.h`), tanpa bergantung pada scipy/Python.

## 1. Spesifikasi

- Orde: 5, Band-pass 1-5 Hz, Sampling rate 50 Hz
- Implementasi: Direct Form II Transposed, 5 biquad (Second-Order Sections) berurutan

## 2. Koefisien (di-hardcode di `firmware/esp32-idf-c/main/main.c`)

Koefisien awalnya dihitung dengan `scipy.signal.butter(5, [1,5], btype='bandpass', fs=50,
output='sos')` (Python), lalu divalidasi ulang secara independen dengan
`Algorithms/butterworth_validate.c` (C murni) — hasilnya identik.

| Section | b0 | b1 | b2 | a1 | a2 |
|---|---|---|---|---|---|
| 0 | 0.0004894361 | 0.0009788722 | 0.0004894361 | -1.3198965304 | 0.5281847481 |
| 1 | 1.0 | 2.0 | 1.0 | -1.5276383342 | 0.5913983514 |
| 2 | 1.0 | 0.0 | -1.0 | -1.4527179777 | 0.7819157637 |
| 3 | 1.0 | -2.0 | 1.0 | -1.8105486220 | 0.8313586394 |
| 4 | 1.0 | -2.0 | 1.0 | -1.9315415226 | 0.9474689283 |

## 3. Validasi stabilitas & respons frekuensi

Pole magnitude tiap section (harus <1): 0.727, 0.769, 0.884, 0.912, 0.973 — **semua stabil**.

Respons magnitude: passband rata di 1-5 Hz (0 dB), -3dB tepat di edge (1Hz, 5Hz), roll-off
tajam di luar band (-29dB di 8Hz, -71dB di 15Hz).

## 4. Catatan implementasi (STATUS: sebagian perlu keputusan/pengujian)

- `prominence_threshold` (default 0.05 mmHg di `main.c`) adalah nilai awal, **belum final** —
  wajib dikalibrasi dari data uji phantom arm (PIC: Grace/Biomedik) sebelum dipakai untuk
  klaim performa final.

- **Reset filter tiap window sampling — PERLU PENGUJIAN, bukan keputusan final.** Mereset
  state biquad tiap 3 detik menciptakan transien (filter belum settle) di awal tiap window,
  berisiko mengganggu akurasi peak detection.

  Dua opsi tersedia di `main.c` (lewat flag `USE_CONTINUOUS_FILTER`):
  - **Opsi A** (`USE_CONTINUOUS_FILTER = 1`): filter berjalan kontinu, tidak pernah direset
  - **Opsi B** (`USE_CONTINUOUS_FILTER = 0`, default saat ini): tetap reset, tapi buang
    `FILTER_SETTLE_SAMPLES = 27` sampel pertama (~540ms) sebelum mulai peak detection.
    Angka 27 dihitung dari group delay filter di 3Hz (~10.4 sampel) × margin 2.5x.

  Implementasi saat ini pakai Opsi B — **wajib diuji dengan data phantom arm real** untuk
  memastikan tidak menimbulkan false negative/positive pada deteksi LOP sebelum dianggap final.
