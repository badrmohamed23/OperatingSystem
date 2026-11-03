# REST API & CLI Interface Specification

## 1. REST Endpoints

The Node.js backend serves static frontend assets and exposes clean JSON REST endpoints. The server communicates directly with the native C binary (`.build/os_sim`) via standard I/O pipes.

---

### `GET /api/health`
Returns system status and verifies C binary compilation.

#### Response `200 OK`
```json
{
  "ok": true,
  "service": "os-simulator-backend",
  "cEngineAvailable": true
}
```

---

### `GET /api/algorithms`
Returns supported scheduling policies with descriptions.

#### Response `200 OK`
```json
{
  "schedulers": ["HRRN", "RR", "MLFQ"],
  "defaultRRQuantum": 2,
  "descriptions": {
    "HRRN": "Highest Response Ratio Next — Non-preemptive scheduling policy selecting the ready process with max (W + S) / S.",
    "RR": "Round Robin — Preemptive scheduling with configurable time slice and FIFO rotation.",
    "MLFQ": "Multi-Level Feedback Queue — 4 priority levels (Q0-Q3) with 2^i quantums and automatic priority demotion."
  }
}
```

---

### `GET /api/programs`
Scans the `programs/` directory and returns all available simulation programs with their parsed instruction lists.

#### Response `200 OK`
```json
{
  "schedulers": ["HRRN", "RR", "MLFQ"],
  "defaultRRQuantum": 2,
  "programs": [
    {
      "pid": 1,
      "file": "Program 1.txt",
      "arrival": 0,
      "inputMap": { "x": "2", "y": "7" },
      "instructions": [
        "semWait userInput",
        "assign a input",
        "semSignal userInput"
      ],
      "instructionCount": 3
    }
  ]
}
```

---

### `GET /api/scenarios`
Returns pre-configured simulation workloads from the `examples/` directory.

#### Response `200 OK`
```json
{
  "scenarios": [
    {
      "id": "scenario_rr_fairness",
      "file": "scenario_rr_fairness.json",
      "name": "Round Robin Fairness",
      "description": "Demonstrates quantum expiration and queue rotation.",
      "config": {
        "scheduler": "RR",
        "rrQuantum": 2,
        "processes": [
          { "pid": 1, "file": "test_cpu_bound.txt", "arrival": 0, "inputMap": {} },
          { "pid": 2, "file": "test_simple.txt", "arrival": 0, "inputMap": {} }
        ]
      }
    }
  ]
}
```

---

### `POST /api/simulate`
Executes the simulation engine via `.build/os_sim --json` and returns full execution metrics and step-by-step state snapshots.

#### Request Body
```json
{
  "scheduler": "HRRN",
  "rrQuantum": 2,
  "processes": [
    {
      "pid": 1,
      "file": "Program 1.txt",
      "arrival": 0,
      "inputMap": { "x": "2", "y": "7" }
    },
    {
      "pid": 2,
      "file": "Program_2.txt",
      "arrival": 1,
      "inputMap": { "a": "dashboard_output.txt", "b": "Saved from simulator" }
    }
  ]
}
```

#### Response `200 OK`
```json
{
  "scheduler": "HRRN",
  "rrQuantum": 2,
  "summary": {
    "totalSteps": 45,
    "finalTime": 32,
    "outputs": 6,
    "errors": 0,
    "completedProcesses": 2,
    "filesTouched": ["sandbox/dashboard_output.txt"]
  },
  "metrics": {
    "avgWaitingTime": 4.5,
    "avgTurnaroundTime": 16.0,
    "avgResponseTime": 0.5,
    "cpuUtilization": 96.8,
    "throughput": 0.0625,
    "contextSwitches": 3,
    "totalTime": 32,
    "completedProcesses": 2
  },
  "processes": [ ... ],
  "timeline": [ ... ],
  "snapshots": [ ... ],
  "outputs": [ ... ],
  "errors": []
}
```

---

## 2. CLI Execution Interface

The engine can also be executed directly via command-line arguments:

```bash
# Flags-based invocation
./.build/os_sim --scheduler RR --quantum 2 --programs programs \
  --process "1:Program 1.txt:0" \
  --process "2:Program_2.txt:1" \
  --process "3:Program_3.txt:4"

# JSON stdin/stdout invocation
./.build/os_sim --json < scenario.json > trace.json
```

