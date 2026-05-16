#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Bench  —  start / end style micro-benchmark
//
//  Usage:
//
//      Bench b("my_bench");          // optional label
//      b.start(1'000'000);           // number of iterations you plan to run
//
//      for (uint64_t i = 0; i < b.iterations(); ++i) {
//          b.tick();                 // snapshot time BEFORE your work
//          do_work();
//          b.tock();                 // snapshot time AFTER your work
//      }
//
//      b.end();                      // computes stats, prints + saves .txt
//
//  OR — even simpler, time a lambda:
//
//      b.start(1'000'000);
//      b.run([&]{ do_work(); });     // calls tick/tock automatically
//      b.end();
//
// ─────────────────────────────────────────────────────────────────────────────

class Bench {
public:
    // ── Construction ─────────────────────────────────────────────────────────

    explicit Bench(const std::string& label = "bench",
                   const std::string& output_file = "")
        : label_(label)
        , output_file_(output_file.empty() ? label + "_results.txt" : output_file)
    {}

    // ── Control ──────────────────────────────────────────────────────────────

    /// Prepare for `n` iterations (reserves memory, resets state).
    void start(uint64_t n) {
        iterations_ = n;
        samples_.clear();
        samples_.reserve(n);
        running_ = true;
    }

    /// Record start of one iteration.
    inline void tick() {
        t_start_ = now_ns();
    }

    /// Record end of one iteration and store the elapsed time.
    inline void tock() {
        double elapsed = now_ns() - t_start_;
        samples_.push_back(elapsed);
    }

    /// Convenience: run a zero-argument callable `iterations_` times,
    /// automatically calling tick/tock around each call.
    template<typename Fn>
    void run(Fn&& fn) {
        for (uint64_t i = 0; i < iterations_; ++i) {
            tick();
            fn();
            tock();
        }
    }

    /// Finish: compute statistics, print to stdout, save to file.
    void end() {
        if (!running_ || samples_.empty()) {
            std::cerr << "[Bench] end() called with no samples.\n";
            return;
        }
        running_ = false;
        compute_stats();
        print_stats();
        save_stats();
    }

    // ── Accessors ─────────────────────────────────────────────────────────────

    uint64_t iterations()  const { return iterations_; }
    double   min_ns()      const { return stats_.min_ns;     }
    double   max_ns()      const { return stats_.max_ns;     }
    double   mean_ns()     const { return stats_.mean_ns;    }
    double   stddev_ns()   const { return stats_.stddev_ns;  }
    double   p50_ns()      const { return stats_.p50_ns;     }
    double   p90_ns()      const { return stats_.p90_ns;     }
    double   p99_ns()      const { return stats_.p99_ns;     }
    double   p999_ns()     const { return stats_.p999_ns;    }

    /// Query any arbitrary percentile (0–100) after end() has been called.
    double percentile(double pct) const {
        return pct_from_sorted(sorted_, pct);
    }

private:
    // ── Internal types ────────────────────────────────────────────────────────

    struct Stats {
        double   min_ns    = 0;
        double   max_ns    = 0;
        double   mean_ns   = 0;
        double   stddev_ns = 0;
        double   p50_ns    = 0;
        double   p90_ns    = 0;
        double   p99_ns    = 0;
        double   p999_ns   = 0;
        uint64_t samples   = 0;
    };

    // ── Helpers ───────────────────────────────────────────────────────────────

    static inline double now_ns() {
        using namespace std::chrono;
        return static_cast<double>(
            duration_cast<nanoseconds>(
                steady_clock::now().time_since_epoch()).count());
    }

    static double pct_from_sorted(const std::vector<double>& sv, double p) {
        if (sv.empty()) return 0.0;
        p = std::clamp(p, 0.0, 100.0);
        double idx = (p / 100.0) * static_cast<double>(sv.size() - 1);
        size_t lo  = static_cast<size_t>(idx);
        size_t hi  = std::min(lo + 1, sv.size() - 1);
        double frac = idx - static_cast<double>(lo);
        return sv[lo] * (1.0 - frac) + sv[hi] * frac;
    }

    void compute_stats() {
        sorted_ = samples_;
        std::sort(sorted_.begin(), sorted_.end());

        const size_t n = sorted_.size();
        stats_.samples = n;
        stats_.min_ns  = sorted_.front();
        stats_.max_ns  = sorted_.back();

        double sum = 0.0;
        for (double v : samples_) sum += v;
        stats_.mean_ns = sum / static_cast<double>(n);

        double var = 0.0;
        for (double v : samples_) var += (v - stats_.mean_ns) * (v - stats_.mean_ns);
        stats_.stddev_ns = std::sqrt(var / static_cast<double>(n));

        stats_.p50_ns  = pct_from_sorted(sorted_, 50.0);
        stats_.p90_ns  = pct_from_sorted(sorted_, 90.0);
        stats_.p99_ns  = pct_from_sorted(sorted_, 99.0);
        stats_.p999_ns = pct_from_sorted(sorted_, 99.9);
    }

    // ── Formatting ────────────────────────────────────────────────────────────

    std::string build_report() const {
        auto fmt = [](double v) -> std::string {
            std::ostringstream o;
            o << std::fixed << std::setprecision(2) << v;
            return o.str();
        };

        std::ostringstream oss;
        oss << "=== " << label_ << " ===\n"
            << "  samples   : " << stats_.samples          << "\n"
            << "  min       : " << fmt(stats_.min_ns)    << " ns\n"
            << "  mean      : " << fmt(stats_.mean_ns)   << " ns\n"
            << "  stddev    : " << fmt(stats_.stddev_ns) << " ns\n"
            << "  p50       : " << fmt(stats_.p50_ns)    << " ns\n"
            << "  p90       : " << fmt(stats_.p90_ns)    << " ns\n"
            << "  p99       : " << fmt(stats_.p99_ns)    << " ns\n"
            << "  p99.9     : " << fmt(stats_.p999_ns)   << " ns\n"
            << "  max       : " << fmt(stats_.max_ns)    << " ns\n";
        return oss.str();
    }

    void print_stats() const {
        std::cout << build_report();
    }

    void save_stats() const {
        std::ofstream f(output_file_);
        if (!f) {
            std::cerr << "[Bench] Could not open '" << output_file_ << "' for writing.\n";
            return;
        }
        f << build_report();
        std::cout << "[Bench] Results saved to '" << output_file_ << "'\n";
    }

    // ── Members ───────────────────────────────────────────────────────────────

    std::string          label_;
    std::string          output_file_;
    uint64_t             iterations_ = 0;
    bool                 running_    = false;
    double               t_start_    = 0.0;
    std::vector<double>  samples_;
    std::vector<double>  sorted_;
    Stats                stats_;
};