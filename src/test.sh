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

echo "=== Testing histo_private ===" > histo_private.txt
for img in $IMAGES; do
    echo "Image: $img" >> histo_private.txt
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
        
        echo "  Average:  $mean ns" >> histo_lock2.txt
        echo "  Std Dev: $std_dev ns" >> histo_lock2.txt
        echo "" >> histo_lock2.txt
    done
done

echo "=== Verifying correctness with diff ===" > verification. txt
./histogram ../images/moon-small. ppm reference.hist 1
./histo_private ../images/moon-small.ppm test_private.hist 4
./histo_lockfree ../images/moon-small.ppm test_lockfree.hist 4
./histo_lock1 ../images/moon-small.ppm test_lock1.hist 4
./histo_lock2 ../images/moon-small.ppm test_lock2.hist 4

diff reference.hist test_private.hist && echo "histo_private:  PASS" >> verification.txt || echo "histo_private:  FAIL" >> verification.txt
diff reference.hist test_lockfree.hist && echo "histo_lockfree: PASS" >> verification.txt || echo "histo_lockfree: FAIL" >> verification.txt
diff reference. hist test_lock1.hist && echo "histo_lock1: PASS" >> verification.txt || echo "histo_lock1: FAIL" >> verification.txt
diff reference.hist test_lock2.hist && echo "histo_lock2: PASS" >> verification.txt || echo "histo_lock2: FAIL" >> verification.txt

echo "Benchmark complete.  Results saved to:"
echo "  - histo_private.txt"
echo "  - histo_lockfree.txt"
echo "  - histo_lock1.txt"
echo "  - histo_lock2.txt"
echo "  - verification.txt"