// test_protocol.c
//
// Automated efficiency tester for the Stop-and-Wait DLL.
//
// Launches cable + receiver + transmitter as sub-processes, times each
// file transfer, and computes efficiency S = R/C for combinations of
// BER (bit error rate) and propagation delay.
//
// Outputs:
//   efficiency_results.csv   — raw results
//   plot_efficiency.gnuplot  — gnuplot script for S(a) curves
//
// Prerequisites: cable, transmitter, receiver must be compiled.
// Run as root (or with CAP_SYS_NICE) for best timing precision.
//
// Usage:  ./test_protocol

#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// ── Binaries / ports ─────────────────────────────────────────────────────────
#define CABLE_BIN   "./cable"
#define TX_BIN      "./transmitter"
#define RX_BIN      "./receiver"
#define TX_PORT     "/dev/ttyS10"      // cable's TX-side PTY
#define RX_PORT     "/dev/ttyS11"      // cable's RX-side PTY
#define TEST_FILE   "penguin.gif"      // file to transfer
#define OUT_FILE    "/tmp/rx_test.gif" // where receiver saves the file

// ── Protocol constants ───────────────────────────────────────────────────────
// Baud rate must match what transmitter.c / receiver.c use (B9600 = 9600 bps).
// Serial framing is 8-N-1, so each byte occupies 10 bit-times on the wire.
#define BAUD_BPS    9600.0

// I-frame byte count (before stuffing), for a 128-byte AL payload:
//   FLAG(1) A(1) C(1) BCC1(1)  +  [C_al(1) L2(1) L1(1) data(128)]  +  BCC2(1)  +  FLAG(1)
//   = 4 + 131 + 1 + 1 = 137 bytes
#define FRAME_BYTES  137
// T_frame (seconds): time to put the frame on the wire
#define T_FRAME_S   (FRAME_BYTES * 10.0 / BAUD_BPS)   // ≈ 0.1427 s

// ── Sweep parameters ─────────────────────────────────────────────────────────
// BER values passed to cable (per-bit error probability).
// cable.c warns accuracy degrades above 0.01, so we stay at or below that.
static const double ber_vals[] = {0.0, 0.0001, 0.001, 0.005, 0.01};
#define N_BER  5

// Propagation delays (µs) chosen to give useful a = T_prop / T_frame values.
// T_FRAME_S ≈ 0.1427 s
//   a ≈ 0.01 → prop ≈  1 427 µs
//   a ≈ 0.10 → prop ≈ 14 271 µs
//   a ≈ 0.50 → prop ≈ 71 354 µs
//   a ≈ 1.00 → prop ≈142 708 µs
static const long prop_us_vals[] = {0, 1427, 14271, 71354, 142708};
#define N_PROP  5

// Number of repeated runs per (BER, prop) configuration (results are averaged).
#define N_REPEATS  3

// Maximum wall-clock seconds we wait for a single transmitter run.
#define TX_TIMEOUT_S  120

// ── Output files ─────────────────────────────────────────────────────────────
#define RESULTS_CSV     "efficiency_results.csv"
#define GNUPLOT_SCRIPT  "plot_efficiency.gnuplot"

// ── Globals ───────────────────────────────────────────────────────────────────
static pid_t cable_pid       = -1;
static int   cable_stdin_fd  = -1;  // write-end of pipe → cable's stdin

// ── Utility functions ─────────────────────────────────────────────────────────

// Send a formatted command line to the running cable process.
static void cable_cmd(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    write(cable_stdin_fd, buf, strlen(buf));
    usleep(200000);  // 200 ms: let cable process the command
}

// Return the size of a file in bytes, or -1 on error.
static long file_size(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? st.st_size : -1;
}

// Elapsed seconds between two CLOCK_MONOTONIC samples.
static double elapsed_s(const struct timespec *a, const struct timespec *b)
{
    return (b->tv_sec  - a->tv_sec)
         + (b->tv_nsec - a->tv_nsec) * 1e-9;
}

// waitpid with a wall-clock timeout; kills the child if it takes too long.
// Returns the child's exit status, or -1 on timeout / error.
static int wait_with_timeout(pid_t pid, int timeout_s)
{
    time_t deadline = time(NULL) + timeout_s;
    int status;
    while (time(NULL) < deadline) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (r < 0)
            return -1;
        usleep(100000);  // poll every 100 ms
    }
    fprintf(stderr, "  [timeout] killing pid %d\n", (int)pid);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    return -1;
}

// ── Cable lifecycle ───────────────────────────────────────────────────────────

// Fork cable, pipe to its stdin, then poll until socat has created the PTY
// symlinks at TX_PORT and RX_PORT — that is the true readiness signal.
// (Parsing cable's stdout is unreliable because stdio buffering holds the
// "Cable ready" line in the buffer when stdout is a pipe, not a terminal.)
// Returns 0 on success, -1 on failure.
static int start_cable(void)
{
    int in_pipe[2];
    if (pipe(in_pipe) < 0) { perror("pipe"); return -1; }

    cable_pid = fork();
    if (cable_pid < 0) { perror("fork"); return -1; }

    if (cable_pid == 0) {           // child: become cable
        close(in_pipe[1]);
        dup2(in_pipe[0], STDIN_FILENO);
        // Suppress cable's verbose output during automated testing.
        int null_fd = open("/dev/null", O_WRONLY);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        close(in_pipe[0]);
        execl(CABLE_BIN, "cable", NULL);
        perror("execl cable");
        exit(1);
    }

    close(in_pipe[0]);
    cable_stdin_fd = in_pipe[1];

    // Wait for socat to create the PTY symlinks — their existence means the
    // cable is ready to forward bytes.
    printf("Waiting for cable to initialise (socat setup ~2 s)...\n");
    struct stat st;
    time_t deadline = time(NULL) + 20;
    while (time(NULL) < deadline) {
        if (stat(TX_PORT, &st) == 0 && stat(RX_PORT, &st) == 0) {
            printf("Cable ready.\n\n");
            return 0;
        }
        usleep(200000);  // poll every 200 ms
    }

    fprintf(stderr, "Timed out waiting for %s / %s\n", TX_PORT, RX_PORT);
    kill(cable_pid, SIGKILL);
    waitpid(cable_pid, NULL, 0);
    cable_pid = -1;
    return -1;
}

// Send "quit" to cable (triggers its own cleanup: killall socat), then wait.
static void stop_cable(void)
{
    if (cable_stdin_fd >= 0) {
        cable_cmd("quit");
        usleep(500000);
        close(cable_stdin_fd);
        cable_stdin_fd = -1;
    }
    if (cable_pid > 0) {
        waitpid(cable_pid, NULL, 0);
        cable_pid = -1;
    }
}

// ── Single transfer ───────────────────────────────────────────────────────────

// Configure cable, run one full file transfer, return elapsed seconds (tx side).
// Returns -1 on failure or timeout.
static double run_transfer(double ber, long prop_us)
{
    // Apply cable settings
    cable_cmd("ber %f",  ber);
    cable_cmd("prop %ld", prop_us);
    usleep(100000);  // brief settle time

    // Start receiver in background (redirect output to /dev/null)
    pid_t rx_pid = fork();
    if (rx_pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        execl(RX_BIN, "receiver", RX_PORT, OUT_FILE, NULL);
        perror("execl receiver");
        exit(1);
    }

    // Give receiver time to open the port and call llopen (wait for SET).
    usleep(300000);  // 300 ms

    // Start transmitter and record wall-clock time around it.
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    pid_t tx_pid = fork();
    if (tx_pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        execl(TX_BIN, "transmitter", TX_PORT, TEST_FILE, NULL);
        perror("execl transmitter");
        exit(1);
    }

    int tx_ret = wait_with_timeout(tx_pid, TX_TIMEOUT_S);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    wait_with_timeout(rx_pid, 10);  // receiver finishes shortly after

    if (tx_ret != 0) {
        fprintf(stderr, "  Transfer failed (ret=%d)\n", tx_ret);
        return -1.0;
    }
    return elapsed_s(&t0, &t1);
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main(void)
{
    long fsize = file_size(TEST_FILE);
    if (fsize <= 0) {
        fprintf(stderr, "Cannot stat '%s'. Make sure it exists.\n", TEST_FILE);
        return 1;
    }
    double file_bits = fsize * 8.0;

    printf("=== Stop-and-Wait Efficiency Test ===\n");
    printf("File      : %s  (%ld bytes)\n", TEST_FILE, fsize);
    printf("Baud rate : %.0f bps\n", BAUD_BPS);
    printf("T_frame   : %.4f s  (%d bytes × 10 bits)\n", T_FRAME_S, FRAME_BYTES);
    printf("Runs/config: %d\n\n", N_REPEATS);

    if (start_cable() < 0)
        return 1;

    FILE *csv = fopen(RESULTS_CSV, "w");
    if (!csv) { perror("fopen " RESULTS_CSV); stop_cable(); return 1; }
    fprintf(csv, "BER,prop_delay_us,a,FER,S_measured,S_theoretical\n");

    for (int i = 0; i < N_BER; i++) {
        for (int j = 0; j < N_PROP; j++) {
            double ber     = ber_vals[i];
            long   prop_us = prop_us_vals[j];
            double a       = (prop_us * 1e-6) / T_FRAME_S;

            // FER: probability that at least one of the FRAME_BITS data bits
            // is flipped.  cable corrupts each bit independently with prob BER,
            // so  FER = 1 - (1-BER)^FRAME_BITS.
            double fer = 1.0 - pow(1.0 - ber, FRAME_BYTES * 8.0);

            // Theoretical S for Stop-and-Wait ARQ (REJ-based retransmission):
            //   S = (1 - FER) / (1 + 2a)
            double S_theo = (1.0 - fer) / (1.0 + 2.0 * a);

            printf("── BER=%.4f  prop=%6ld µs  a=%.3f  FER≈%.5f ──\n",
                   ber, prop_us, a, fer);

            double sum_S = 0.0;
            int    valid = 0;

            for (int r = 0; r < N_REPEATS; r++) {
                double elapsed = run_transfer(ber, prop_us);
                if (elapsed > 0.0) {
                    // S = R / C  where R = file_bits / elapsed
                    double S = file_bits / (elapsed * BAUD_BPS);
                    printf("  run %d: %.3f s → S = %.4f\n", r + 1, elapsed, S);
                    sum_S += S;
                    valid++;
                } else {
                    printf("  run %d: FAILED\n", r + 1);
                }
            }

            double S_meas = (valid > 0) ? sum_S / valid : 0.0;
            fprintf(csv, "%.4f,%ld,%.4f,%.6f,%.4f,%.4f\n",
                    ber, prop_us, a, fer, S_meas, S_theo);
            printf("  → S_avg=%.4f  S_theo=%.4f\n\n", S_meas, S_theo);
        }
    }

    fclose(csv);
    stop_cable();
    printf("Results saved to %s\n", RESULTS_CSV);

    // ── Gnuplot script ────────────────────────────────────────────────────────
    // Generates one S-vs-a curve per BER value (measured + theoretical).
    FILE *gp = fopen(GNUPLOT_SCRIPT, "w");
    if (!gp) { perror("fopen gnuplot"); return 0; }

    fprintf(gp,
        "# Run with: gnuplot -persistent %s\n"
        "set datafile separator ','\n"
        "set title 'Stop-and-Wait Efficiency S vs propagation factor a'\n"
        "set xlabel 'a = T_{prop} / T_{frame}'\n"
        "set ylabel 'Efficiency S'\n"
        "set yrange [0:1.1]\n"
        "set xrange [-0.05:1.1]\n"
        "set key top right\n"
        "set grid\n"
        "set style data linespoints\n\n"
        "# CSV columns: BER, prop_us, a, FER, S_meas, S_theo\n",
        GNUPLOT_SCRIPT);

    // One plot command per BER value, filtering rows with ternary operator.
    fprintf(gp, "plot \\\n");
    for (int i = 0; i < N_BER; i++) {
        const char *sep   = (i < N_BER - 1) ? ", \\" : "";
        // Measured (col 5) and theoretical (col 6), filtered by BER (col 1)
        fprintf(gp,
            "  '%s' using ($1==%.4f ? $3 : 1/0):5 title 'meas BER=%.4f', \\\n"
            "  '%s' using ($1==%.4f ? $3 : 1/0):6 with lines dashtype 2 title 'theo BER=%.4f'%s\n",
            RESULTS_CSV, ber_vals[i], ber_vals[i],
            RESULTS_CSV, ber_vals[i], ber_vals[i],
            sep);
    }
    fclose(gp);
    printf("Gnuplot script : %s\n", GNUPLOT_SCRIPT);
    printf("To plot results: gnuplot -persistent %s\n", GNUPLOT_SCRIPT);

    return 0;
}
