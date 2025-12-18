#!/bin/bash

echo "=========================================="
echo "           System Configuration"
echo "=========================================="
echo ""

# CPU型号
echo "CPU Model:"
lscpu | grep "Model name" | sed 's/Model name://g' | xargs
echo ""

# CPU基频
echo "CPU Base Frequency:"
CPU_MHZ=$(lscpu | grep "CPU MHz" | sed 's/CPU MHz://g' | xargs)
if [ -z "$CPU_MHZ" ]; then
    CPU_MHZ=$(grep "cpu MHz" /proc/cpuinfo | head -1 | awk '{print $4}')
fi
if [ -z "$CPU_MHZ" ]; then
    CPU_MHZ=$(lscpu | grep "CPU max MHz" | awk '{print $4}')
fi
if [ -z "$CPU_MHZ" ]; then
    echo "Not available (may be running in WSL2 or VM)"
else
    echo "${CPU_MHZ} MHz"
fi
echo ""

# 核心和线程数
echo "Cores and Threads:"
CORES=$(lscpu | grep "Core(s) per socket" | awk '{print $4}')
SOCKETS=$(lscpu | grep "Socket(s)" | awk '{print $2}')
THREADS=$(lscpu | grep "^CPU(s):" | awk '{print $2}')
TOTAL_CORES=$((CORES * SOCKETS))
echo "${TOTAL_CORES} cores, ${THREADS} threads"
echo ""

# 内存信息
echo "RAM:"
free -h | grep "Mem:" | awk '{print $2}'
echo ""

# 缓存信息
echo "Cache:"
echo -n "L1d: "
lscpu | grep "L1d cache" | awk '{print $3}'
echo -n "L1i: "
lscpu | grep "L1i cache" | awk '{print $3}'
echo -n "L2: "
lscpu | grep "L2 cache" | awk '{print $3}'
echo -n "L3: "
lscpu | grep "L3 cache" | awk '{print $3}'
echo ""

# 系统信息
echo "Operating System:"
if grep -q Microsoft /proc/version; then
    echo "Ubuntu in Windows 11 (WSL2)"
else
    lsb_release -d | sed 's/Description://g' | xargs
fi
echo ""

echo "=========================================="