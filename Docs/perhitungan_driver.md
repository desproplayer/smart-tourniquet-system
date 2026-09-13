# Perhitungan Driver MOSFET & Daya/Baterai

**Status:** sudah direview. Validasi numerik lihat `Algorithms/power_and_driver_calc.c`
(kompilasi: `gcc -o power_and_driver_calc power_and_driver_calc.c -lm`).

## 1. Pemilihan MOSFET — BELUM DIPUTUSKAN

IRF540N (disebut di proposal awal) butuh VGS~10V untuk RDS(on) optimal — tidak cocok didrive
langsung dari GPIO ESP32-S3 (3.3V).

**Koreksi:** rekomendasi "ganti ke IRLZ44N karena logic-level" tidak cukup tanpa syarat. RDS(on)
0.022 ohm yang dipakai di perhitungan daya adalah nilai **@VGS=5V** dari datasheet — bukan
kondisi yang dijamin pabrikan di VGS=3.3V.

Pilih salah satu:
- **Opsi A (direkomendasikan):** MOSFET dengan RDS(on) **terjamin datasheet** di VGS=2.5-3.3V
  (mis. IRLML2502, AO3400, atau logic-level modern lain yang eksplisit mencantumkan RDS(on)
  di VGS rendah)
- **Opsi B:** tetap IRLZ44N untuk prototype, **wajib** validasi empiris (ukur suhu & arus real
  saat VGS=3.3V)
- **Opsi C:** tambah pre-driver (IC gate driver IR2104 / transistor NPN) untuk naikkan gate ke
  5-10V

## 2. Perhitungan rangkaian gate

- Gate resistor RG = 100-220 ohm
- Pull-down gate-source RPD = 10k ohm (WAJIB, cegah MOSFET nyala sendiri saat boot)
- Flyback diode 1N5819 (Schottky) paralel terbalik dengan tiap beban induktif — WAJIB

## 3. Disipasi daya MOSFET — PERLU DIHITUNG ULANG SETELAH MOSFET FINAL DIPILIH

Nilai referensi (@VGS=5V, BUKAN kondisi final): 0.88-3.52 mW untuk arus 0.2-0.4A — tidak
signifikan, tapi harus dihitung ulang dengan RDS(on) yang benar-benar berlaku pada VGS aktual
setelah MOSFET final dipilih (bagian 1).

## 4. Perhitungan daya total sistem

### Breakdown arus per komponen
| Komponen | Arus | Rail |
|---|---|---|
| ESP32-S3 aktif | 80 mA | 3.3V |
| Pompa BLDC (peak) | 400 mA | 12V |
| Solenoid (x1 aktif) | 200 mA | 12V |
| OLED | 20 mA | 3.3V |
| ADS1115 + sensor | ~5 mA | 5V |

Nilai dari datasheet nominal, **belum diverifikasi dengan pengukuran arus real**.

### Konversi ke sisi baterai (3.7V) via boost converter (eff. 93%) + LDO (asumsi eff. 85%)

Mode INFLATING (asumsi solenoid ikut aktif): **total arus baterai ~2205 mA**

**CATATAN TERBUKA:** apakah solenoid "lock" perlu energized untuk membuka jalur pompa->bladder
saat inflasi, atau jalurnya terbuka lewat check valve tanpa solenoid? Ini **belum diputuskan
tim Elektro/Hardware** — dampaknya signifikan: tanpa solenoid ~1.5A, dengan solenoid ~2.2A.

## 5. Estimasi umur baterai (LiPo 2000mAh) — ESTIMASI TEORITIS, BELUM DIVERIFIKASI

| Skenario | Hasil | Target PDS | Status |
|---|---|---|---|
| Worst-case (INFLATING terus, dgn solenoid) | 0.91 jam | D.1: min 2 jam | **TIDAK TERPENUHI** |
| Worst-case (tanpa solenoid) | 1.33 jam | D.1: min 2 jam | **TIDAK TERPENUHI** |
| Best-case (HOLDING terus) | 381 jam | D.2: min 8 jam | Terpenuhi |
| Realistis (30s inflasi + 1jam holding) | ~85 siklus | - | Estimasi teoritis |

**Kedua skenario worst-case TIDAK memenuhi target 2 jam — ini GAP REQUIREMENT yang harus
ditulis eksplisit di laporan**, bukan "mepet tapi aman". Asumsi arus HOLDING (~5mA) juga belum
terbukti — ESP32, OLED, sensor, monitoring berkala semua tetap menarik arus.

## 6. Rekomendasi tindak lanjut
- **Prioritas tinggi:** klarifikasi desain pneumatik solenoid (bagian 4)
- Validasi arus real pakai multimeter/current clamp (pompa, solenoid, DAN mode holding)
- Hitung ulang disipasi MOSFET dengan RDS(on) yang benar setelah part final dipilih
- Kalau hasil ukur mendekati/melebihi worst-case, pertimbangkan baterai lebih besar
  (3000-4000 mAh) atau optimasi duty cycle pompa
