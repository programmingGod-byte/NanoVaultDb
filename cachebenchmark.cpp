#include "benchmark.hpp"
#include "utils/cpu_affinity.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <cstdint>

// Define buffer sizes
// L1 Data cache is typically 32-48 KB per core
const size_t L1_SIZE = 16 * 1024;         // 16 KB (Safe L1 hit)
// L2 cache is typically 256 KB - 1 MB
const size_t L2_SIZE = 512 * 1024;        // 512 KB (Exceeds L1, fits in L2)
// Main Memory (well beyond L3)
const size_t RAM_SIZE = 128 * 1024 * 1024; // 128 MB (Cold cache for RAM)

/**
 * Measures the average latency of memory access for a given buffer size.
 * Uses random access to prevent hardware prefetching from hiding true latency.
 */
void run_cache_bench(const std::string& label, size_t size, int iterations) {
    std::cout << "\n>>> Starting Benchmark: " << label << " (Test Size: " << size / 1024 << " KB) <<<\n";
    
    // 1. Allocate and initialize buffer
    size_t count = size / sizeof(uint64_t);
    std::vector<uint64_t> buffer(count);
    for (size_t i = 0; i < count; ++i) buffer[i] = i;
    
    // 2. Prepare random indices to defeat prefetcher
    std::vector<size_t> indices(count);
    for (size_t i = 0; i < count; ++i) indices[i] = i;
    std::shuffle(indices.begin(), indices.end(), std::mt19937(1337));

    Bench b(label, label + "_results.txt");
    b.start(iterations);

    // Use a volatile sink to prevent compiler optimization
    volatile uint64_t sum = 0;

    // Warm-up to ensure cache is primed (for L1/L2)
    for (size_t i = 0; i < std::min<size_t>(count, 10000); ++i) {
        sum += buffer[indices[i % count]];
    }

    // 3. Main benchmark loop
    for (int i = 0; i < iterations; ++i) {
        size_t idx = indices[i % count];
        b.tick();
        sum += buffer[idx]; // Load operation
        b.tock();
    }
    
    b.end();
}

/**
 * Measures store speed by performing random writes
 */
void run_store_bench(const std::string& label, size_t size, int iterations) {
    std::cout << "\n>>> Starting Benchmark: " << label << " (Store Speed, Test Size: " << size / 1024 << " KB) <<<\n";
    
    size_t count = size / sizeof(uint64_t);
    std::vector<uint64_t> buffer(count, 0);
    
    std::vector<size_t> indices(count);
    for (size_t i = 0; i < count; ++i) indices[i] = i;
    std::shuffle(indices.begin(), indices.end(), std::mt19937(4242));

    Bench b(label, label + "_store_results.txt");
    b.start(iterations);

    for (int i = 0; i < iterations; ++i) {
        size_t idx = indices[i % count];
        uint64_t val = (uint64_t)i;
        b.tick();
        buffer[idx] = val; // Store operation
        b.tock();
    }
    
    b.end();
}

int main() {
    // Pin to CPU to prevent context switching jitter
    try {
        pin_thread_to_cpu(1);
        std::cout << "Thread pinned to CPU 1.\n";
    } catch (...) {
        std::cout << "Warning: Could not pin thread.\n";
    }

    const int ITERS = 1000000;

    // MEASURE LOADS (Latency)
    run_cache_bench("L1_Load_Latency", L1_SIZE, ITERS);
    run_cache_bench("L2_Load_Latency", L2_SIZE, ITERS);
    run_cache_bench("RAM_Load_Latency", RAM_SIZE, ITERS);

    // MEASURE STORES
    run_store_bench("L1_Store_Latency", L1_SIZE, ITERS);
    run_store_bench("RAM_Store_Latency", RAM_SIZE, ITERS);

    return 0;
}
