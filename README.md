# Build Instructions

To build the project for Yocto on Raspberry Pi (aarch64) using Docker, you can use the following command:

```bash
sudo docker build -t rpisignal-crossbuild --output type=local,dest=./build .
```

or use this command to build it for normal Ubuntu on Rapsberry Pi:
```bash
docker buildx build --platform linux/arm64 --build-arg BUILD_TYPE=Release --output type=local,dest=./build
```

This will create a build output in the `./build` directory, targeting the Raspberry Pi architecture.

# Guide to Perf
Perf can be used to count the occurences of specific events related to task scheduling and more. Below are some useful events:

```bash
perf stat -e context-switches,sched:sched_wakeup,sched:sched_migrate_task -C 1 ./RPISignal -f 100 -c 1 -o out.csv
```

- **`-e <event_list>`**  
  Specifies the events to monitor. Below are the events used:

  - **`context-switches`**  
    Counts the number of context switches that occur. A context switch happens when the CPU switches from running one task to running another. This event can be useful for identifying how frequently a process is being preempted or moved between CPUs.

  - **`sched:sched_wakeup`**  
    Counts how often a task is woken up. This event occurs when a task moves from a sleeping state to a running state, typically due to an event like an interrupt or a timer expiring.

  - **`sched:sched_migrate_task`**  
    Tracks when a task is migrated between CPUs. This can happen when the scheduler decides to move the task to another CPU for load balancing or other reasons.

- **`-C 1`**  
  Specifies that `perf` should monitor **CPU core 1**. You can change this to a different core or even monitor all cores by omitting this option. This is useful if you want to narrow down the performance analysis to a specific CPU core.

- **`./RPISignal -f 100 -c 1 -o out.csv`**  
  The test application to run. In this case, `RPISignal` is your program, and the options passed to it are:

  - `-f 100`: Signal frequency (100 Hz).
  - `-c 1`: Execute signal generation on cpu core 1.
  - `-o out.csv`: Output file.

## Output Example

This command will provide an output similar to this:

```bash
   1234 context-switches           # Total context switches
   8765 sched_wakeup               # Number of wakeup events
   123 sched_migrate_task          # Task migrations between CPUs
```

# Guide to stress-ng

# Possible causes of high litter
- System Interrupts and Kernel Tasks (see '/proc/interrupts')
