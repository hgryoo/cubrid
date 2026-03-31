# Perf Metrics Interpretation Guide

## 1. Execution / CPU

### elapsed

* Wall-clock time
* Lower is better

---

### task-clock

* Actual CPU runtime (per thread)

**Interpretation**

```
task-clock ≈ elapsed × CPUs utilized
```

---

### CPUs utilized

| Value | Meaning             |
| ----- | ------------------- |
| ~1    | Fully using 1 core  |
| <1    | Idle / stall exists |
| >1    | Multi-core usage    |

---

## 2. CPU Efficiency

### cycles

* CPU clock cycles
* Used with instructions

---

### instructions

* Number of executed instructions

---

### IPC (Instructions Per Cycle)

```
IPC = instructions / cycles
```

| IPC  | Meaning                   |
| ---- | ------------------------- |
| 3~4  | Excellent (compute-bound) |
| 1~2  | Normal                    |
| <1   | Stall exists              |
| ~0.5 | Strong memory-bound       |

---

## 3. Frontend

### frontend stalled %

| Value | Meaning      |
| ----- | ------------ |
| <2%   | Good         |
| 2~5%  | Slight issue |
| >10%  | Severe issue |

---

## 4. Branch

### branch-miss rate %

| Value | Meaning      |
| ----- | ------------ |
| <1%   | Excellent    |
| 1~3%  | Normal       |
| 3~5%  | Slightly bad |
| >5%   | Problematic  |

---

## 5. L1 Data Cache

### L1-dcache miss rate %

| Value  | Meaning   |
| ------ | --------- |
| <5%    | Excellent |
| 5~10%  | Normal    |
| 10~15% | Bad       |
| >15%   | Very bad  |

**Indicates:**

* Poor locality
* Random access
* Pointer chasing

---

## 6. Instruction Cache

### L1-icache-load-misses

Indicates:

* Large code footprint
* Poor code locality

---

## 7. TLB (Address Translation)

### dTLB miss rate %

| Value    | Meaning      |
| -------- | ------------ |
| <0.1%    | Excellent    |
| 0.1~0.5% | Normal       |
| 0.5~1%   | Slight issue |
| >1%      | Problematic  |

Indicates:

* Page-level random access
* Large working set

---

### iTLB-load-misses

* Usually negligible unless very high

---

## 8. OS / Scheduler

### context-switches

* Lower is better
* High → lock contention / thread wakeups

---

### cpu-migrations

* Lower is better
* High → cache locality loss

---

### page-faults

| Value | Meaning                     |
| ----- | --------------------------- |
| Low   | Normal                      |
| High  | Disk I/O or memory pressure |

---

## 9. Diagnosis Flow

### Step 1. Check IPC

* Low → stall exists

### Step 2. Check frontend stalled

* Low → backend problem

### Step 3. Check memory metrics

* High L1 miss → locality issue
* High dTLB miss → page-level randomness

### Step 4. Conclusion

```
Memory-bound workload
+ random access
+ pointer chasing
```

---

## 10. Quick Reference

| Metric         | Good  | Bad  |
| -------------- | ----- | ---- |
| IPC            | >2    | <1   |
| L1 miss        | <5%   | >10% |
| Branch miss    | <3%   | >5%  |
| dTLB miss      | <0.5% | >1%  |
| Frontend stall | <2%   | >10% |

---

## Final Summary

> If IPC is low and cache miss is high, the system is memory-bound, not CPU-bound.
