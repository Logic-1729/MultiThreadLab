#!/bin/bash

IMAGES="300px-Unequalized_Hawkes_Bay_NZ.ppm earth.ppm flood.ppm moon-small.ppm moon-large.ppm phobos.ppm university.ppm"
THREADS="1 2 4 8"
ITERATIONS=100

calculate_stats() {
    local sum=0
    local sum_sq=0
    local count=0
    local -a values=("$@")
    for val in "${values[@]}"; do
        sum=$((sum + val))
        sum_sq=$((sum_sq + val * val))
        count=$((count + 1))
    done
    local mean=$((sum / count))
    local variance=$(( (sum_sq - sum * sum / count) / count ))
    local std_dev=$(echo "scale=2; sqrt($variance)" | bc)
    
    echo "$mean $std_dev"
}

echo "=== Testing histo_private ==="
for img in $IMAGES; do
    echo "Image: $img"
    for t in $THREADS; do
        echo "Threads $t:"
        times=()
        for i in $(seq 1 $ITERATIONS); do
            output=$(./histo_private ../images/$img output_private.hist $t 2>&1)
            time_ns=$(echo "$output" | grep "Time:" | awk '{print $2}')
            times+=($time_ns)
        done
        
        stats=($(calculate_stats "${times[@]}"))
        mean=${stats[0]}
        std_dev=${stats[1]}
        
        echo "  Average: $mean ns"
        echo "  Std Dev: $std_dev ns"
        echo ""
    done
done

echo ""
echo "=== Testing histo_lockfree ==="
for img in $IMAGES; do
    echo "Image: $img"
    for t in $THREADS; do
        echo "Threads $t:"
        times=()
        for i in $(seq 1 $ITERATIONS); do
            output=$(./histo_lockfree ../images/$img output_lockfree.hist $t 2>&1)
            time_ns=$(echo "$output" | grep "Time:" | awk '{print $2}')
            times+=($time_ns)
        done
        
        stats=($(calculate_stats "${times[@]}"))
        mean=${stats[0]}
        std_dev=${stats[1]}
        
        echo "  Average: $mean ns"
        echo "  Std Dev: $std_dev ns"
        echo ""
    done
done

echo ""
echo "=== Testing histo_lock1 (Test-and-Set) ==="
for img in $IMAGES; do
    echo "Image:  $img"
    for t in $THREADS; do
        echo "Threads $t:"
        times=()
        for i in $(seq 1 $ITERATIONS); do
            output=$(./histo_lock1 ../images/$img output_lock1.hist $t 2>&1)
            time_ns=$(echo "$output" | grep "Time:" | awk '{print $2}')
            times+=($time_ns)
        done
        
        stats=($(calculate_stats "${times[@]}"))
        mean=${stats[0]}
        std_dev=${stats[1]}
        
        echo "  Average: $mean ns"
        echo "  Std Dev: $std_dev ns"
        echo ""
    done
done

echo ""
echo "=== Testing histo_lock2 (Ticket Lock) ==="
for img in $IMAGES; do
    echo "Image: $img"
    for t in $THREADS; do
        echo "Threads $t:"
        times=()
        for i in $(seq 1 $ITERATIONS); do
            output=$(./histo_lock2 ../images/$img output_lock2.hist $t 2>&1)
            time_ns=$(echo "$output" | grep "Time:" | awk '{print $2}')
            times+=($time_ns)
        done
        
        stats=($(calculate_stats "${times[@]}"))
        mean=${stats[0]}
        std_dev=${stats[1]}
        
        echo "  Average: $mean ns"
        echo "  Std Dev: $std_dev ns"
        echo ""
    done
done

echo ""
echo "=== Verifying correctness with diff ==="
./histogram ../images/moon-small. ppm reference. hist 1
./histo_private ../images/moon-small.ppm test_private.hist 4
./histo_lockfree ../images/moon-small.ppm test_lockfree.hist 4
./histo_lock1 ../images/moon-small. ppm test_lock1.hist 4
./histo_lock2 ../images/moon-small.ppm test_lock2.hist 4

diff reference.hist test_private.hist && echo "histo_private:    PASS" || echo "histo_private: FAIL"
diff reference.hist test_lockfree.hist && echo "histo_lockfree: PASS" || echo "histo_lockfree: FAIL"
diff reference.hist test_lock1.hist && echo "histo_lock1: PASS" || echo "histo_lock1: FAIL"
diff reference.hist test_lock2.hist && echo "histo_lock2: PASS" || echo "histo_lock2: FAIL"