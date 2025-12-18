#!/bin/bash

IMAGES="300px-Unequalized_Hawkes_Bay_NZ.ppm earth.ppm flood.ppm moon-small.ppm moon-large.ppm phobos.ppm university.ppm"
THREADS="1 2 4 8"
ITERATIONS=100

calculate_stats() {
    local sum=0
    local sum_sq=0
    local count=0
    local -a values=("$@")
    
    # 过滤空值并计数
    for val in "${values[@]}"; do
        if [ -n "$val" ] && [ "$val" != "" ]; then
            sum=$((sum + val))
            sum_sq=$((sum_sq + val * val))
            count=$((count + 1))
        fi
    done
    
    # 防止除零错误
    if [ $count -eq 0 ]; then
        echo "0 0"
        return
    fi
    
    local mean=$((sum / count))
    local variance=$(( (sum_sq - sum * sum / count) / count ))
    local std_dev=$(echo "scale=2; sqrt($variance)" | bc)
    
    echo "$mean $std_dev"
}

# ==============================================
# Valgrind Memory Leak Detection
# ==============================================
echo "=== Running Valgrind Memory Leak Detection ===" > valgrind_report.txt
echo "Testing with moon-small.ppm and 4 threads" >> valgrind_report.txt
echo "" >> valgrind_report.txt

# Test histogram (baseline)
echo "--- histogram (baseline, single-threaded) ---" >> valgrind_report.txt
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    --error-exitcode=1 \
    ./histogram ../images/moon-small.ppm valgrind_test.hist 1 2>&1 | \
    grep -E "(ERROR SUMMARY|definitely lost|indirectly lost|possibly lost|still reachable)" >> valgrind_report.txt
echo "" >> valgrind_report.txt

# Test histo_private
echo "--- histo_private ---" >> valgrind_report.txt
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    --error-exitcode=1 \
    ./histo_private ../images/moon-small.ppm valgrind_test.hist 4 2>&1 | \
    grep -E "(ERROR SUMMARY|definitely lost|indirectly lost|possibly lost|still reachable)" >> valgrind_report.txt
echo "" >> valgrind_report.txt

# Test histo_lockfree
echo "--- histo_lockfree ---" >> valgrind_report.txt
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    --error-exitcode=1 \
    ./histo_lockfree ../images/moon-small.ppm valgrind_test.hist 4 2>&1 | \
    grep -E "(ERROR SUMMARY|definitely lost|indirectly lost|possibly lost|still reachable)" >> valgrind_report.txt
echo "" >> valgrind_report.txt

# Test histo_lock1
echo "--- histo_lock1 (Test-and-Set) ---" >> valgrind_report.txt
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    --error-exitcode=1 \
    ./histo_lock1 ../images/moon-small.ppm valgrind_test.hist 4 2>&1 | \
    grep -E "(ERROR SUMMARY|definitely lost|indirectly lost|possibly lost|still reachable)" >> valgrind_report.txt
echo "" >> valgrind_report.txt

# Test histo_lock2
echo "--- histo_lock2 (Ticket Lock) ---" >> valgrind_report.txt
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    --error-exitcode=1 \
    ./histo_lock2 ../images/moon-small.ppm valgrind_test.hist 4 2>&1 | \
    grep -E "(ERROR SUMMARY|definitely lost|indirectly lost|possibly lost|still reachable)" >> valgrind_report.txt
echo "" >> valgrind_report.txt

echo "Valgrind detection complete.  Results saved to valgrind_report.txt"
echo ""

# ==============================================
# Correctness Verification
# ==============================================
echo "=== Verifying correctness with diff ===" > verification.txt
./histogram ../images/moon-small.ppm reference.hist 1
./histo_private ../images/moon-small.ppm test_private.hist 4
./histo_lockfree ../images/moon-small.ppm test_lockfree.hist 4
./histo_lock1 ../images/moon-small.ppm test_lock1.hist 4
./histo_lock2 ../images/moon-small.ppm test_lock2.hist 4

diff reference.hist test_private.hist && echo "histo_private:  PASS" >> verification.txt || echo "histo_private:  FAIL" >> verification.txt
diff reference.hist test_lockfree.hist && echo "histo_lockfree: PASS" >> verification.txt || echo "histo_lockfree: FAIL" >> verification.txt
diff reference.hist test_lock1.hist && echo "histo_lock1:    PASS" >> verification.txt || echo "histo_lock1:    FAIL" >> verification.txt
diff reference.hist test_lock2.hist && echo "histo_lock2:    PASS" >> verification.txt || echo "histo_lock2:    FAIL" >> verification.txt

cat verification.txt
echo ""

# ==============================================
# Performance Benchmarking
# ==============================================
echo "=== Testing histogram (baseline, single-threaded) ===" > histogram.txt
for img in $IMAGES; do
    echo "Image: $img" >> histogram.txt
    echo "Threads 1:" >> histogram.txt
    times=()
    for i in $(seq 1 $ITERATIONS); do
        output=$(./histogram ../images/$img output_histogram.hist 1 2>&1)
        time_ns=$(echo "$output" | grep "Time:" | awk '{print $2}')
        
        # 只添加非空的时间值
        if [ -n "$time_ns" ]; then
            times+=($time_ns)
        fi
    done
    
    # 检查是否收集到有效数据
    if [ ${#times[@]} -eq 0 ]; then
        echo "  ERROR: No valid timing data collected" >> histogram.txt
        echo "" >> histogram.txt
        continue
    fi
    
    stats=($(calculate_stats "${times[@]}"))
    mean=${stats[0]}
    std_dev=${stats[1]}
    
    echo "  Average: $mean ns" >> histogram.txt
    echo "  Std Dev: $std_dev ns" >> histogram.txt
    echo "" >> histogram.txt
done

echo "=== Testing histo_private ===" > histo_private.txt
for img in $IMAGES; do
    echo "Image:  $img" >> histo_private.txt
    for t in $THREADS; do
        echo "Threads $t:" >> histo_private.txt
        times=()
        for i in $(seq 1 $ITERATIONS); do
            output=$(./histo_private ../images/$img output_private.hist $t 2>&1)
            time_ns=$(echo "$output" | grep "Time:" | awk '{print $2}')
            
            # 只添加非空的时间值
            if [ -n "$time_ns" ]; then
                times+=($time_ns)
            fi
        done
        
        # 检查是否收集到有效数据
        if [ ${#times[@]} -eq 0 ]; then
            echo "  ERROR: No valid timing data collected" >> histo_private.txt
            echo "" >> histo_private.txt
            continue
        fi
        
        stats=($(calculate_stats "${times[@]}"))
        mean=${stats[0]}
        std_dev=${stats[1]}
        
        echo "  Average: $mean ns" >> histo_private.txt
        echo "  Std Dev: $std_dev ns" >> histo_private.txt
        echo "" >> histo_private.txt
    done
done

echo "=== Testing histo_lockfree ===" > histo_lockfree.txt
for img in $IMAGES; do
    echo "Image:  $img" >> histo_lockfree.txt
    for t in $THREADS; do
        echo "Threads $t:" >> histo_lockfree.txt
        times=()
        for i in $(seq 1 $ITERATIONS); do
            output=$(./histo_lockfree ../images/$img output_lockfree.hist $t 2>&1)
            time_ns=$(echo "$output" | grep "Time:" | awk '{print $2}')
            
            # 只添加非空的时间值
            if [ -n "$time_ns" ]; then
                times+=($time_ns)
            fi
        done
        
        # 检查是否收集到有效数据
        if [ ${#times[@]} -eq 0 ]; then
            echo "  ERROR: No valid timing data collected" >> histo_lockfree.txt
            echo "" >> histo_lockfree.txt
            continue
        fi
        
        stats=($(calculate_stats "${times[@]}"))
        mean=${stats[0]}
        std_dev=${stats[1]}
        
        echo "  Average: $mean ns" >> histo_lockfree.txt
        echo "  Std Dev:  $std_dev ns" >> histo_lockfree.txt
        echo "" >> histo_lockfree.txt
    done
done

echo "=== Testing histo_lock1 (Test-and-Set) ===" > histo_lock1.txt
for img in $IMAGES; do
    echo "Image: $img" >> histo_lock1.txt
    for t in $THREADS; do
        echo "Threads $t:" >> histo_lock1.txt
        times=()
        for i in $(seq 1 $ITERATIONS); do
            output=$(./histo_lock1 ../images/$img output_lock1.hist $t 2>&1)
            time_ns=$(echo "$output" | grep "Time:" | awk '{print $2}')
            
            # 只添加非空的时间值
            if [ -n "$time_ns" ]; then
                times+=($time_ns)
            fi
        done
        
        # 检查是否收集到有效数据
        if [ ${#times[@]} -eq 0 ]; then
            echo "  ERROR:  No valid timing data collected" >> histo_lock1.txt
            echo "" >> histo_lock1.txt
            continue
        fi
        
        stats=($(calculate_stats "${times[@]}"))
        mean=${stats[0]}
        std_dev=${stats[1]}
        
        echo "  Average: $mean ns" >> histo_lock1.txt
        echo "  Std Dev: $std_dev ns" >> histo_lock1.txt
        echo "" >> histo_lock1.txt
    done
done

echo "=== Testing histo_lock2 (Ticket Lock) ===" > histo_lock2.txt
for img in $IMAGES; do
    echo "Image:  $img" >> histo_lock2.txt
    for t in $THREADS; do
        echo "Threads $t:" >> histo_lock2.txt
        times=()
        for i in $(seq 1 $ITERATIONS); do
            output=$(./histo_lock2 ../images/$img output_lock2.hist $t 2>&1)
            time_ns=$(echo "$output" | grep "Time:" | awk '{print $2}')
            
            # 只添加非空的时间值
            if [ -n "$time_ns" ]; then
                times+=($time_ns)
            fi
        done
        
        # 检查是否收集到有效数据
        if [ ${#times[@]} -eq 0 ]; then
            echo "  ERROR: No valid timing data collected" >> histo_lock2.txt
            echo "" >> histo_lock2.txt
            continue
        fi
        
        stats=($(calculate_stats "${times[@]}"))
        mean=${stats[0]}
        std_dev=${stats[1]}
        
        echo "  Average: $mean ns" >> histo_lock2.txt
        echo "  Std Dev: $std_dev ns" >> histo_lock2.txt
        echo "" >> histo_lock2.txt
    done
done

echo ""
echo "Benchmark complete. Results saved to:"
echo "  - histogram.txt (baseline)"
echo "  - histo_private.txt"
echo "  - histo_lockfree.txt"
echo "  - histo_lock1.txt"
echo "  - histo_lock2.txt"
echo "  - verification.txt"
echo "  - valgrind_report.txt"