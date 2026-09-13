/* ============================================================
 * Smart Tourniquet System — Perhitungan Driver MOSFET & Daya/Baterai
 * Kelompok 19 — Desain Proyek Teknik Elektro, Komputer, Biomedik 2
 *
 * Program C standalone (ANSI C99), TIDAK butuh library eksternal.
 * Kompilasi: gcc -o power_and_driver_calc power_and_driver_calc.c -lm
 * Jalankan : ./power_and_driver_calc
 * ============================================================ */

#include <stdio.h>
#include <math.h>
#include <string.h>

/* ============================================================
 * BAGIAN 1: Perhitungan Driver MOSFET
 * ============================================================ */

static float mosfet_gate_current_ma(float qg_nc, float t_rise_us) {
    float qg_c = qg_nc * 1e-9f;
    float t_rise_s = t_rise_us * 1e-6f;
    float ig_a = qg_c / t_rise_s;
    return ig_a * 1000.0f;
}

static float mosfet_conduction_loss_w(float current_a, float rds_on_ohm) {
    return (current_a * current_a) * rds_on_ohm;
}

/* ============================================================
 * BAGIAN 2: Perhitungan Daya Total Sistem
 * ============================================================ */

typedef struct {
    char name[40];
    float voltage;
    float current_a;
    char rail[8]; /* "3.3V", "5V", atau "12V" */
} load_spec_t;

static float load_power_w(const load_spec_t *load) {
    return load->voltage * load->current_a;
}

#define BATTERY_VOLTAGE 3.7f
#define BATTERY_CAPACITY_MAH 2000.0f
#define BOOST_CONVERTER_EFFICIENCY 0.93f
#define LDO_EFFICIENCY_ASSUMED 0.85f

static float battery_current_for_loads(const load_spec_t *loads, int n_loads) {
    float total_power_from_battery_w = 0.0f;
    for (int i = 0; i < n_loads; i++) {
        float p_load = load_power_w(&loads[i]);
        float p_from_battery;
        if (strcmp(loads[i].rail, "12V") == 0) {
            p_from_battery = p_load / BOOST_CONVERTER_EFFICIENCY;
        } else {
            p_from_battery = p_load / LDO_EFFICIENCY_ASSUMED;
        }
        total_power_from_battery_w += p_from_battery;
    }
    return total_power_from_battery_w / BATTERY_VOLTAGE;
}

static float battery_life_hours(float current_a, float capacity_mah) {
    float capacity_ah = capacity_mah / 1000.0f;
    return capacity_ah / current_a;
}

int main(void) {
    printf("============================================================\n");
    printf("BAGIAN 1: PERHITUNGAN DRIVER MOSFET\n");
    printf("============================================================\n");

    float qg_total_nc = 48.0f;
    float t_rise_target_us = 2.5f;
    float ig_ma = mosfet_gate_current_ma(qg_total_nc, t_rise_target_us);
    printf("Total gate charge (Qg): %.1f nC\n", qg_total_nc);
    printf("Target rise time: %.1f us\n", t_rise_target_us);
    printf("Arus gate dibutuhkan: %.2f mA\n", ig_ma);
    printf("-> Gate resistor disarankan: 100-220 ohm (GPIO ESP32-S3 aman sampai 40mA/pin)\n\n");

    float rds_on_at_5v = 0.022f; /* REFERENSI SAJA @VGS=5V, BUKAN kondisi aktual di VGS=3.3V */
    printf("CATATAN REVISI: RDS(on)=%.3f ohm ini nilai @VGS=5V dari datasheet IRLZ44N --\n", rds_on_at_5v);
    printf("BUKAN kondisi terjamin pabrikan pada VGS=3.3V (GPIO ESP32-S3). Pastikan MOSFET\n");
    printf("yang benar-benar dipasang punya RDS(on) terjamin di VGS 2.5-3.3V sebelum memakai\n");
    printf("hasil di bawah sebagai angka final (lihat Docs/perhitungan_driver.md bagian 1).\n\n");

    float test_currents[] = {0.2f, 0.4f};
    for (int i = 0; i < 2; i++) {
        float p_cond = mosfet_conduction_loss_w(test_currents[i], rds_on_at_5v);
        printf("Disipasi @ I=%.1fA, Rds(on)=%.3f ohm (REFERENSI @VGS=5V): %.2f mW -> %s\n",
               test_currents[i], rds_on_at_5v, p_cond * 1000,
               (p_cond > 1.0f) ? "perlu heatsink" : "TIDAK perlu heatsink (lihat catatan di atas)");
    }

    printf("\n============================================================\n");
    printf("BAGIAN 2: DAYA TOTAL SISTEM & UMUR BATERAI\n");
    printf("============================================================\n");

    /* CATATAN TERBUKA: apakah solenoid ikut aktif saat INFLATING? Lihat Docs/perhitungan_driver.md
     * bagian 4 -- ini belum diputuskan tim Elektro/Hardware, dan berdampak besar ke hasil. */
    load_spec_t loads_inflating[] = {
        {"ESP32-S3 aktif", 3.3f, 0.080f, "3.3V"},
        {"OLED", 3.3f, 0.020f, "3.3V"},
        {"ADS1115 + sensor", 5.0f, 0.005f, "5V"},
        {"Pompa BLDC (peak)", 12.0f, 0.400f, "12V"},
        {"Solenoid (1 aktif)", 12.0f, 0.200f, "12V"},
    };
    int n_loads_inflating = sizeof(loads_inflating) / sizeof(loads_inflating[0]);

    load_spec_t loads_holding[] = {
        {"MCU light sleep", 3.3f, 0.005f, "3.3V"},
    };
    int n_loads_holding = 1;

    printf("\n--- Mode INFLATING ---\n");
    printf("%-25s | %6s | %10s | %10s\n", "Komponen", "Rail", "Arus (mA)", "Daya (W)");
    printf("------------------------------------------------------------\n");
    for (int i = 0; i < n_loads_inflating; i++) {
        printf("%-25s | %6s | %10.1f | %10.3f\n",
               loads_inflating[i].name, loads_inflating[i].rail,
               loads_inflating[i].current_a * 1000, load_power_w(&loads_inflating[i]));
    }

    float i_bat_inflating = battery_current_for_loads(loads_inflating, n_loads_inflating);
    printf("\nTotal arus baterai (3.7V) saat INFLATING: %.1f mA\n", i_bat_inflating * 1000);
    printf("(memperhitungkan efisiensi boost converter 93%% utk rail 12V, asumsi LDO 85%% utk 3.3V/5V)\n");

    printf("\n--- Mode HOLDING ---\n");
    float i_bat_holding = battery_current_for_loads(loads_holding, n_loads_holding);
    printf("Total arus baterai (3.7V) saat HOLDING: %.2f mA\n", i_bat_holding * 1000);
    printf("(asumsi ~5mA MCU light sleep -- BELUM TERVALIDASI, lihat Docs/perhitungan_driver.md bagian 5)\n");

    printf("\n--- Estimasi umur baterai (ESTIMASI TEORITIS, BELUM DIVERIFIKASI) ---\n");
    float t_max_continuous = battery_life_hours(i_bat_inflating, BATTERY_CAPACITY_MAH);
    float t_max_holding = battery_life_hours(i_bat_holding, BATTERY_CAPACITY_MAH);
    printf("Worst-case (INFLATING terus-menerus): %.2f jam\n", t_max_continuous);
    printf("  -> Target PDS spec D.1 (min. 2 jam operasi aktif): %s\n",
           (t_max_continuous >= 2.0f) ? "TERPENUHI" : "TIDAK TERPENUHI (GAP REQUIREMENT)");
    printf("Best-case (HOLDING terus-menerus): %.1f jam\n", t_max_holding);
    printf("  -> Target PDS spec D.2 (min. 8 jam mode HOLDING): %s\n",
           (t_max_holding >= 8.0f) ? "TERPENUHI" : "TIDAK TERPENUHI");

    printf("\n--- Skenario realistis (siklus: 30 detik inflasi + 1 jam holding) ---\n");
    float mah_inflating = i_bat_inflating * 1000 * (30.0f / 3600.0f);
    float mah_holding = i_bat_holding * 1000 * 1.0f;
    float mah_total = mah_inflating + mah_holding;
    float n_cycles = BATTERY_CAPACITY_MAH / mah_total;
    printf("Kapasitas terpakai per siklus: %.2f mAh (inflasi: %.2f mAh + holding 1 jam: %.2f mAh)\n",
           mah_total, mah_inflating, mah_holding);
    printf("Estimasi teoritis: baterai %.0f mAh sanggup ~%.0f siklus (skenario dominan holding)\n",
           BATTERY_CAPACITY_MAH, n_cycles);
    printf("** Klaim ini BELUM FINAL -- wajib verifikasi arus HOLDING aktual dengan pengukuran nyata. **\n");

    return 0;
}
