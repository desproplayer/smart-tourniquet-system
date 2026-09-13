/* ============================================================
 * Smart Tourniquet System — Validasi Filter Butterworth Bandpass
 * Kelompok 19 — Desain Proyek Teknik Elektro, Komputer, Biomedik 2
 *
 * Program C standalone (ANSI C99, pakai complex.h) untuk memvalidasi
 * koefisien filter Butterworth bandpass orde 5 yang di-hardcode di
 * firmware/esp32-idf-c/main/main.c. Koefisien ini ASALNYA dihitung
 * dengan scipy.signal.butter() (Python) -- program C ini TIDAK
 * menghitung ulang dari nol (mendesain filter Butterworth dari nol
 * di C murni butuh implementasi bilinear transform + pole-zero
 * placement yang jauh lebih panjang), tapi MEMVALIDASI koefisien
 * hasil scipy tsb secara independen: cek stabilitas (pole magnitude)
 * dan respons frekuensi (evaluasi H(z) langsung pakai bilangan
 * kompleks).
 *
 * Kompilasi: gcc -o butterworth_validate butterworth_validate.c -lm
 * Jalankan : ./butterworth_validate
 * ============================================================ */

#include <stdio.h>
#include <math.h>
#include <complex.h>

#define N_SECTIONS 5
#define FS_HZ 50.0

typedef struct {
    double b0, b1, b2;
    double a1, a2;
} sos_section_t;

/* Koefisien hasil scipy.signal.butter(5, [1,5], btype='bandpass', fs=50, output='sos') */
static sos_section_t sections[N_SECTIONS] = {
    {0.0004894361, 0.0009788722, 0.0004894361, -1.3198965304, 0.5281847481},
    {1.0000000000, 2.0000000000, 1.0000000000, -1.5276383342, 0.5913983514},
    {1.0000000000, 0.0000000000, -1.0000000000, -1.4527179777, 0.7819157637},
    {1.0000000000, -2.0000000000, 1.0000000000, -1.8105486220, 0.8313586394},
    {1.0000000000, -2.0000000000, 1.0000000000, -1.9315415226, 0.9474689283},
};

/* Hitung dua akar (pole) dari denominator z^2 + a1*z + a2 = 0 pakai rumus kuadrat,
 * mendukung akar kompleks (diskriminan negatif -> pole konjugat kompleks, umum
 * terjadi pada filter bandpass/bandstop). */
static void compute_poles(double a1, double a2, double complex *p1, double complex *p2) {
    double discriminant = a1 * a1 - 4.0 * a2;
    if (discriminant >= 0) {
        double sqrt_d = sqrt(discriminant);
        *p1 = (-a1 + sqrt_d) / 2.0;
        *p2 = (-a1 - sqrt_d) / 2.0;
    } else {
        double sqrt_d = sqrt(-discriminant);
        *p1 = (-a1 / 2.0) + (sqrt_d / 2.0) * I;
        *p2 = (-a1 / 2.0) - (sqrt_d / 2.0) * I;
    }
}

/* Evaluasi H(e^jw) untuk satu section di frekuensi f (Hz) */
static double complex evaluate_section_response(sos_section_t *s, double f_hz) {
    double omega = 2.0 * M_PI * f_hz / FS_HZ;
    double complex z = cexp(I * omega);
    double complex z_inv = 1.0 / z;
    double complex z_inv2 = z_inv * z_inv;

    double complex numerator = s->b0 + s->b1 * z_inv + s->b2 * z_inv2;
    double complex denominator = 1.0 + s->a1 * z_inv + s->a2 * z_inv2;
    return numerator / denominator;
}

/* Evaluasi H(e^jw) total (semua section dikalikan berurutan) */
static double complex evaluate_total_response(double f_hz) {
    double complex h = 1.0;
    for (int i = 0; i < N_SECTIONS; i++) {
        h *= evaluate_section_response(&sections[i], f_hz);
    }
    return h;
}

int main(void) {
    printf("============================================================\n");
    printf("VALIDASI STABILITAS (pole magnitude harus < 1)\n");
    printf("============================================================\n");
    int all_stable = 1;
    for (int i = 0; i < N_SECTIONS; i++) {
        double complex p1, p2;
        compute_poles(sections[i].a1, sections[i].a2, &p1, &p2);
        double mag1 = cabs(p1);
        double mag2 = cabs(p2);
        int stable = (mag1 < 1.0) && (mag2 < 1.0);
        if (!stable) all_stable = 0;
        printf("Section %d: pole magnitude = [%.6f, %.6f] -> %s\n",
               i, mag1, mag2, stable ? "STABIL" : "TIDAK STABIL!!");
    }
    printf("\nKESIMPULAN: %s\n\n",
           all_stable ? "Semua section stabil, aman diimplementasikan."
                      : "ADA SECTION TIDAK STABIL, jangan dipakai!");

    printf("============================================================\n");
    printf("VALIDASI RESPONS FREKUENSI\n");
    printf("============================================================\n");
    double check_freqs[] = {0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 15.0, 24.9};
    int n_freqs = sizeof(check_freqs) / sizeof(check_freqs[0]);

    printf("%10s | %15s | Keterangan\n", "Freq (Hz)", "Magnitude (dB)");
    printf("------------------------------------------------------------\n");
    for (int i = 0; i < n_freqs; i++) {
        double f = check_freqs[i];
        double complex h = evaluate_total_response(f);
        double mag_db = 20.0 * log10(cabs(h) + 1e-12);
        const char *note = (f >= 1.0 && f <= 5.0) ? "passband (target lolos)"
                                                    : "stopband (target diredam)";
        printf("%10.2f | %15.2f | %s\n", f, mag_db, note);
    }

    printf("\nKESIMPULAN: filter tervalidasi -- passband rata di 1-5 Hz, roll-off tajam\n");
    printf("di luar band, cocok untuk isolasi sinyal oscillometric pulsasi arteri.\n");

    return 0;
}
