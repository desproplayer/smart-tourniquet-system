/* ============================================================
 * Smart Tourniquet System — Perhitungan Konversi Sensor MPX5050DP
 * Kelompok 19 — Desain Proyek Teknik Elektro, Komputer, Biomedik 2
 *
 * Program C standalone (ANSI C99), TIDAK butuh library eksternal.
 * Kompilasi: gcc -o sensor_conversion sensor_conversion.c -lm
 * Jalankan : ./sensor_conversion
 * ============================================================ */

#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#define VS 5.0f
#define KPA_TO_MMHG 7.50062f

#define SENSITIVITY_V_PER_MMHG 0.012f
#define OFFSET_V 0.2f

/* --- Rumus dasar datasheet: Vout = VS * (0.018 * P_kPa + 0.04) --- */

static float pressure_kpa_to_vout(float p_kpa, float vs) {
    return vs * (0.018f * p_kpa + 0.04f);
}

static float pressure_mmhg_to_vout_datasheet(float p_mmhg, float vs) {
    float p_kpa = p_mmhg / KPA_TO_MMHG;
    return pressure_kpa_to_vout(p_kpa, vs);
}

/* --- Rumus linear tersederhanakan (dipakai di firmware) --- */

static float pressure_mmhg_to_vout_linear(float p_mmhg) {
    return SENSITIVITY_V_PER_MMHG * p_mmhg + OFFSET_V;
}

static float vout_to_pressure_mmhg_linear(float vout) {
    return (vout - OFFSET_V) / SENSITIVITY_V_PER_MMHG;
}

int main(void) {
    printf("============================================================\n");
    printf("VALIDASI RUMUS: datasheet (kPa) vs linear-simplified (mmHg)\n");
    printf("============================================================\n");

    float test_points[] = {0, 50, 100, 150, 220, 280, 300, 375, 400};
    int n_points = sizeof(test_points) / sizeof(test_points[0]);

    printf("%10s | %15s | %13s | %8s\n", "P (mmHg)", "Vout datasheet", "Vout linear", "selisih");
    printf("------------------------------------------------------------\n");
    for (int i = 0; i < n_points; i++) {
        float p = test_points[i];
        float v_datasheet = pressure_mmhg_to_vout_datasheet(p, VS);
        float v_linear = pressure_mmhg_to_vout_linear(p);
        float diff = fabsf(v_datasheet - v_linear);
        printf("%10.0f | %15.4f | %13.4f | %8.5f\n", p, v_datasheet, v_linear, diff);
    }

    printf("\n============================================================\n");
    printf("CEK RENTANG SENSOR vs TARGET PDS (400 mmHg)\n");
    printf("============================================================\n");
    const float RATED_RANGE_MMHG = 375.0f; /* POP spec datasheet: 0-50 kPa */
    float p_electrical_saturation = vout_to_pressure_mmhg_linear(VS);

    printf("Rentang OPERASI TERKALIBRASI (rated, POP spec datasheet): 0 - %.0f mmHg\n", RATED_RANGE_MMHG);
    printf("Titik SATURASI ELEKTRIS (Vout = VS, ADC benar-benar mentok): ~%.1f mmHg\n", p_electrical_saturation);
    printf("Target hard limit PDS: 400 mmHg\n\n");
    printf("Koreksi penting:\n");
    printf("  - Sensor TIDAK mengalami clipping elektris sampai ~%.0f mmHg,\n", p_electrical_saturation);
    printf("    jadi secara sinyal MASIH bisa membaca sampai target 400 mmHg.\n");
    printf("  - TAPI akurasi & linearitas sensor cuma DIJAMIN pabrikan sampai\n");
    printf("    %.0f mmHg (rentang rated/POP di datasheet). Antara %.0f-400 mmHg,\n",
           RATED_RANGE_MMHG, RATED_RANGE_MMHG);
    printf("    pembacaan masih keluar tapi TIDAK bersertifikat akurat -> wajib\n");
    printf("    divalidasi manual dgn manometer referensi kalau target 400 mmHg dipertahankan.\n");

    printf("\n============================================================\n");
    printf("CEK AKURASI SENSOR vs TARGET PDS (spec A.4: +/- 2 mmHg)\n");
    printf("============================================================\n");
    const float VFSS = 4.5f;
    const float accuracy_percent = 0.025f;
    float error_v = accuracy_percent * VFSS;
    float error_mmhg = error_v / SENSITIVITY_V_PER_MMHG;
    printf("Akurasi sensor (datasheet): +/-%.1f%% VFSS = +/-%.4f V = +/-%.2f mmHg\n",
           accuracy_percent * 100, error_v, error_mmhg);
    printf("Target PDS spec A.4: +/-2 mmHg\n");
    if (error_mmhg > 2.0f) {
        printf("** GAP: error sensor mentah (%.2f mmHg) jauh melebihi target (2.0 mmHg).\n", error_mmhg);
        printf("   Wajib kalibrasi multi-titik terhadap manometer referensi + kompensasi suhu. **\n");
    } else {
        printf("Akurasi sensor mentah sudah memenuhi target tanpa kalibrasi tambahan.\n");
    }

    printf("\n============================================================\n");
    printf("REVISI: LEVEL TEGANGAN ADS1115 vs I2C ESP32-S3\n");
    printf("============================================================\n");
    const float DIVIDER_RATIO = 0.6f; /* R1=2.2k, R2=3.3k -- sesuai main.c */
    const float ADS1115_SUPPLY_V = 3.3f;
    float vout_max_sensor = pressure_mmhg_to_vout_linear(400.0f);
    float vadc_max_with_divider = vout_max_sensor * DIVIDER_RATIO;
    printf("Opsi A (dipakai di firmware main.c): ADS1115 disupply %.1fV,\n", ADS1115_SUPPLY_V);
    printf("Vout sensor diturunkan dulu pakai voltage divider (rasio %.1f).\n", DIVIDER_RATIO);
    printf("Vout sensor maksimum (di 400 mmHg): %.3f V\n", vout_max_sensor);
    printf("Vadc setelah divider: %.3f V -> %s (batas %.1fV, margin %.2fV)\n",
           vadc_max_with_divider,
           (vadc_max_with_divider < ADS1115_SUPPLY_V) ? "AMAN" : "MASIH MELEBIHI BATAS!",
           ADS1115_SUPPLY_V, ADS1115_SUPPLY_V - vadc_max_with_divider);
    printf("\nOpsi B (alternatif): ADS1115 tetap 5V + I2C level shifter (BSS138).\n");
    printf("Kalau Opsi B dipilih, set DIVIDER_RATIO = 1.0 di main.c.\n");

    printf("\n============================================================\n");
    printf("KONFIGURASI ADS1115 (gain, resolusi) -- Opsi A dengan divider\n");
    printf("============================================================\n");
    const float ads_range_v = 4.096f; /* GAIN_ONE */
    const int ads_resolution_bits = 16;
    float ads_lsb_v = ads_range_v / powf(2.0f, ads_resolution_bits - 1);
    float ads_lsb_mmhg = (ads_lsb_v / DIVIDER_RATIO) / SENSITIVITY_V_PER_MMHG;
    printf("Gain dipakai: GAIN_ONE (range +/-%.3f V)\n", ads_range_v);
    printf("Resolusi ADC (sisi Vadc): %.4f mV/bit\n", ads_lsb_v * 1000);
    printf("Resolusi setelah kompensasi divider (sisi tekanan): %.4f mmHg/bit\n", ads_lsb_mmhg);
    printf("Jauh lebih presisi dari kebutuhan minimum (1 mmHg) -> resolusi bukan bottleneck.\n");

    return 0;
}
