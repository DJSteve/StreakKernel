cmd_drivers/cpufreq/built-in.o :=  /root/adam/prebuilt/linux-x86/toolchain/arm-eabi-4.4.0/bin/arm-eabi-ld -EL    -r -o drivers/cpufreq/built-in.o drivers/cpufreq/cpufreq.o drivers/cpufreq/cpufreq_stats.o drivers/cpufreq/cpufreq_performance.o drivers/cpufreq/cpufreq_powersave.o drivers/cpufreq/cpufreq_userspace.o drivers/cpufreq/cpufreq_ondemand.o drivers/cpufreq/cpufreq_conservative.o drivers/cpufreq/cpufreq_smartass.o drivers/cpufreq/cpufreq_interactive.o drivers/cpufreq/freq_table.o 
