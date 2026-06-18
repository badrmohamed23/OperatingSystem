const express = require("express");
const fs = require("fs");
const path = require("path");

const app = express();
const PORT = process.env.PORT || 3000;
const MEMORY_SIZE = 40;
const PCB_WORDS = 4;
const PROCESS_VAR_SLOTS = 3;
const PROGRAMS_DIR = path.join(__dirname, "programs");

const DEFAULT_PROGRAMS = [
  { pid: 1, file: "Program 1.txt", arrival: 0 },
  { pid: 2, file: "Program_2.txt", arrival: 1 },
  { pid: 3, file: "Program_3.txt", arrival: 4 },
];

const RESOURCES = {
  userInput: "userInput",
  userOutput: "userOutput",
  file: "file",
};

app.use(express.json({ limit: "1mb" }));
app.use(express.static(path.join(__dirname, "frontend")));

function readProgramInstructions(file) {
  const fullPath = path.join(PROGRAMS_DIR, file);
  const raw = fs.readFileSync(fullPath, "utf8");
  return raw
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0);
}

function createInitialState(config) {
  const scheduler = (config.scheduler || "HRRN").toUpperCase();
  const rrQuantum = Number.isInteger(config.rrQuantum) ? config.rrQuantum : 2;

  const processes = config.processes.map((item) => {
    const instructions = readProgramInstructions(item.file);
    return {
      pid: item.pid,
      file: item.file,
      arrival: item.arrival,
      state: "NEW",
      instructions,
      pc: 0,
      waitingTime: 0,
      burstTime: instructions.length,
      remainingTime: instructions.length,
      priorityLevel: 0,
      variables: {},
      inputMap: item.inputMap || {},
    };
  });

  return {
    scheduler,
    rrQuantum,
    clock: 0,
    step: 0,
    processes,
    readyQueue: [],
    mlfqQueues: [[], [], [], []],
    blockedQueues: {
      userInput: [],
      userOutput: [],
      file: [],
    },
    mutexes: {
      userInput: { locked: false, ownerPid: null },
      userOutput: { locked: false, ownerPid: null },
      file: { locked: false, ownerPid: null },
    },
    dispatch: {
      runningPid: null,
      quantum: 0,
      executed: 0,
      instruction: null,
      schedulerDecision: "No dispatch yet",
    },
    timeline: [],
    snapshots: [],
    outputs: [],
    errors: [],
    filesTouched: [],
  };
}

function cloneQueues(state) {
  return {
    readyQueue: [...state.readyQueue],
    mlfqQueues: state.mlfqQueues.map((q) => [...q]),
    blockedQueues: {
      userInput: [...state.blockedQueues.userInput],
      userOutput: [...state.blockedQueues.userOutput],
      file: [...state.blockedQueues.file],
    },
  };
}

function processSummary(p) {
  return {
    pid: p.pid,
    file: p.file,
    state: p.state,
    pc: p.pc,
    totalInstructions: p.instructions.length,
    waitingTime: p.waitingTime,
    remainingTime: p.remainingTime,
    priorityLevel: p.priorityLevel,
    variables: { ...p.variables },
  };
}

function cloneMutexes(state) {
  return {
    userInput: { ...state.mutexes.userInput },
    userOutput: { ...state.mutexes.userOutput },
    file: { ...state.mutexes.file },
  };
}

function mapStateCode(stateName) {
  if (stateName === "NEW") {
    return 0;
  }
  if (stateName === "READY") {
    return 1;
  }
  if (stateName === "RUNNING") {
    return 2;
  }
  if (stateName === "BLOCKED") {
    return 3;
  }
  if (stateName === "FINISHED") {
    return 4;
  }
  return -1;
}

function buildMemoryView(state) {
  const slots = Array.from({ length: MEMORY_SIZE }, (_, index) => ({
    index,
    ownerPid: null,
    kind: "FREE",
    name: "-",
    value: "-",
  }));

  const segments = [];
  let cursor = 0;

  const processes = [...state.processes].sort((a, b) => a.pid - b.pid);
  for (const proc of processes) {
    if (proc.state === "NEW" || proc.state === "FINISHED") {
      continue;
    }

    const requiredWords =
      PCB_WORDS + PROCESS_VAR_SLOTS + proc.instructions.length;
    if (cursor >= MEMORY_SIZE) {
      break;
    }

    const start = cursor;
    const end = Math.min(MEMORY_SIZE - 1, cursor + requiredWords - 1);
    let at = start;

    const pcbRows = [
      ["PID", String(proc.pid)],
      ["STATE", String(mapStateCode(proc.state))],
      ["PC", String(proc.pc)],
      ["BOUNDS", `${start}-${end}`],
    ];

    for (const [name, value] of pcbRows) {
      if (at > end) {
        break;
      }
      slots[at] = {
        index: at,
        ownerPid: proc.pid,
        kind: "PCB",
        name,
        value,
      };
      at += 1;
    }

    const vars = Object.entries(proc.variables).slice(0, PROCESS_VAR_SLOTS);
    while (vars.length < PROCESS_VAR_SLOTS) {
      vars.push(["-", "-"]);
    }

    for (const [name, value] of vars) {
      if (at > end) {
        break;
      }
      slots[at] = {
        index: at,
        ownerPid: proc.pid,
        kind: "VAR",
        name,
        value: String(value),
      };
      at += 1;
    }

    for (let i = 0; i < proc.instructions.length; i += 1) {
      if (at > end) {
        break;
      }
      slots[at] = {
        index: at,
        ownerPid: proc.pid,
        kind: "INSTR",
        name: `INSTR_${i}`,
        value: proc.instructions[i],
      };
      at += 1;
    }

    segments.push({
      pid: proc.pid,
      state: proc.state,
      start,
      end,
      requiredWords,
      allocatedWords: end - start + 1,
    });

    cursor = end + 1;
  }

  return {
    size: MEMORY_SIZE,
    usedWords: cursor,
    freeWords: Math.max(0, MEMORY_SIZE - cursor),
    segments,
    slots,
  };
}

function snapshotState(state, label) {
  state.snapshots.push({
    step: state.step,
    time: state.clock,
    label,
    queues: cloneQueues(state),
    processes: state.processes.map(processSummary),
    mutexes: cloneMutexes(state),
    dispatch: { ...state.dispatch },
    memory: buildMemoryView(state),
  });
}

function timelineEvent(state, event) {
  state.timeline.push({
    id: state.timeline.length + 1,
    step: state.step,
    time: state.clock,
    ...event,
    queues: cloneQueues(state),
  });
}

function addToReady(state, pid, levelOverride) {
  const proc = state.processes.find((p) => p.pid === pid);
  if (!proc) {
    return;
  }
  proc.state = "READY";

  if (state.scheduler === "MLFQ") {
    const level = Math.max(
      0,
      Math.min(
        3,
        Number.isInteger(levelOverride) ? levelOverride : proc.priorityLevel,
      ),
    );
    proc.priorityLevel = level;
    state.mlfqQueues[level].push(pid);
    return;
  }

  state.readyQueue.push(pid);
}

function dequeueReady(state) {
  if (state.scheduler === "MLFQ") {
    for (let i = 0; i < state.mlfqQueues.length; i += 1) {
      if (state.mlfqQueues[i].length > 0) {
        const pid = state.mlfqQueues[i].shift();
        return state.processes.find((p) => p.pid === pid);
      }
    }
    return null;
  }

  if (state.readyQueue.length === 0) {
    return null;
  }

  if (state.scheduler === "RR") {
    const pid = state.readyQueue.shift();
    return state.processes.find((p) => p.pid === pid);
  }

  let bestIndex = 0;
  let bestRatio = -Infinity;

  for (let i = 0; i < state.readyQueue.length; i += 1) {
    const pid = state.readyQueue[i];
    const proc = state.processes.find((p) => p.pid === pid);
    if (!proc) {
      continue;
    }
    const service = proc.burstTime > 0 ? proc.burstTime : 1;
    const ratio = (proc.waitingTime + service) / service;
    if (ratio > bestRatio) {
      bestRatio = ratio;
      bestIndex = i;
    }
  }

  const [selectedPid] = state.readyQueue.splice(bestIndex, 1);
  const selected = state.processes.find((p) => p.pid === selectedPid);
  if (selected) {
    selected.waitingTime = 0;
  }
  return selected || null;
}

function schedulerTick(state, runningProc) {
  if (runningProc && runningProc.remainingTime > 0) {
    runningProc.remainingTime -= 1;
  }

  if (state.scheduler === "HRRN") {
    for (const pid of state.readyQueue) {
      const proc = state.processes.find((p) => p.pid === pid);
      if (proc) {
        proc.waitingTime += 1;
      }
    }
  }
}

function semWait(state, proc, res) {
  const mutex = state.mutexes[res];
  if (!mutex) {
    state.errors.push(
      `Unknown resource '${res}' in semWait for PID ${proc.pid}`,
    );
    timelineEvent(state, {
      type: "error",
      pid: proc.pid,
      message: `Unknown semaphore resource ${res}`,
    });
    return;
  }

  if (!mutex.locked) {
    mutex.locked = true;
    mutex.ownerPid = proc.pid;
    timelineEvent(state, {
      type: "resource-acquire",
      pid: proc.pid,
      message: `PID ${proc.pid} acquired ${res}`,
    });
    return;
  }

  proc.state = "BLOCKED";
  state.blockedQueues[res].push(proc.pid);
  timelineEvent(state, {
    type: "blocked",
    pid: proc.pid,
    message: `PID ${proc.pid} blocked on ${res}`,
  });
}

function semSignal(state, proc, res) {
  const mutex = state.mutexes[res];
  if (!mutex) {
    state.errors.push(
      `Unknown resource '${res}' in semSignal for PID ${proc.pid}`,
    );
    timelineEvent(state, {
      type: "error",
      pid: proc.pid,
      message: `Unknown semaphore resource ${res}`,
    });
    return;
  }

  const queue = state.blockedQueues[res];
  if (queue.length === 0) {
    mutex.locked = false;
    mutex.ownerPid = null;
    timelineEvent(state, {
      type: "resource-release",
      pid: proc.pid,
      message: `PID ${proc.pid} released ${res}`,
    });
    return;
  }

  const unblockedPid = queue.shift();
  addToReady(state, unblockedPid);
  mutex.locked = true;
  mutex.ownerPid = unblockedPid;
  timelineEvent(state, {
    type: "unblocked",
    pid: unblockedPid,
    message: `PID ${unblockedPid} unblocked from ${res}`,
  });
}

function assignValue(state, proc, tokens) {
  const varName = tokens[1];
  const source = tokens[2];

  if (!varName || !source) {
    state.errors.push(`Malformed assign instruction for PID ${proc.pid}`);
    timelineEvent(state, {
      type: "error",
      pid: proc.pid,
      message: "Malformed assign instruction",
    });
    return;
  }

  if (source === "input") {
    const value = Object.prototype.hasOwnProperty.call(proc.inputMap, varName)
      ? String(proc.inputMap[varName])
      : "";
    proc.variables[varName] = value;
    timelineEvent(state, {
      type: "assign",
      pid: proc.pid,
      message: `PID ${proc.pid} assigned ${varName} from input (${value || "<empty>"})`,
    });
    return;
  }

  if (source === "readFile") {
    const fileVar = tokens[3];
    const fileName = proc.variables[fileVar] || "";

    if (!fileName) {
      state.errors.push(
        `readFile filename variable missing for PID ${proc.pid}`,
      );
      timelineEvent(state, {
        type: "error",
        pid: proc.pid,
        message: `PID ${proc.pid} cannot resolve filename from ${fileVar || "<missing>"}`,
      });
      return;
    }

    const fullPath = path.join(__dirname, fileName);
    if (!fs.existsSync(fullPath)) {
      state.errors.push(`File '${fileName}' not found for PID ${proc.pid}`);
      timelineEvent(state, {
        type: "error",
        pid: proc.pid,
        message: `PID ${proc.pid} readFile failed for ${fileName}`,
      });
      return;
    }

    const content = fs.readFileSync(fullPath, "utf8");
    proc.variables[varName] = content;
    timelineEvent(state, {
      type: "assign",
      pid: proc.pid,
      message: `PID ${proc.pid} assigned ${varName} from readFile ${fileName}`,
    });
    return;
  }

  proc.variables[varName] = source;
  timelineEvent(state, {
    type: "assign",
    pid: proc.pid,
    message: `PID ${proc.pid} assigned ${varName} = ${source}`,
  });
}

function executeInstruction(state, proc) {
  const instruction = proc.instructions[proc.pc] || "";
  const tokens = instruction.split(/\s+/).filter(Boolean);
  const op = tokens[0];

  timelineEvent(state, {
    type: "instruction",
    pid: proc.pid,
    instruction,
    message: `PID ${proc.pid} executing: ${instruction}`,
  });

  if (!op) {
    proc.pc += 1;
    return;
  }

  if (op === "semWait") {
    semWait(state, proc, tokens[1]);
  } else if (op === "semSignal") {
    semSignal(state, proc, tokens[1]);
  } else if (op === "assign") {
    assignValue(state, proc, tokens);
  } else if (op === "print") {
    const varName = tokens[1];
    const value =
      proc.variables[varName] !== undefined
        ? proc.variables[varName]
        : "(undefined)";
    const line = `PID ${proc.pid}: ${varName || "(null)"} = ${value}`;
    state.outputs.push({
      step: state.step,
      time: state.clock,
      pid: proc.pid,
      text: line,
    });
    timelineEvent(state, {
      type: "output",
      pid: proc.pid,
      message: line,
    });
  } else if (op === "printFromTo") {
    const fromName = tokens[1];
    const toName = tokens[2];
    const fromVal = Number(proc.variables[fromName]);
    const toVal = Number(proc.variables[toName]);

    if (Number.isNaN(fromVal) || Number.isNaN(toVal)) {
      state.errors.push(`printFromTo bounds missing for PID ${proc.pid}`);
      timelineEvent(state, {
        type: "error",
        pid: proc.pid,
        message: `PID ${proc.pid} printFromTo missing numeric bounds`,
      });
    } else if (fromVal <= toVal) {
      for (let i = fromVal + 1; i < toVal; i += 1) {
        const line = `PID ${proc.pid}: ${i}`;
        state.outputs.push({
          step: state.step,
          time: state.clock,
          pid: proc.pid,
          text: line,
        });
      }
      timelineEvent(state, {
        type: "output",
        pid: proc.pid,
        message: `PID ${proc.pid} printFromTo emitted range ${fromVal + 1}..${toVal - 1}`,
      });
    } else {
      for (let i = fromVal - 1; i > toVal; i -= 1) {
        const line = `PID ${proc.pid}: ${i}`;
        state.outputs.push({
          step: state.step,
          time: state.clock,
          pid: proc.pid,
          text: line,
        });
      }
      timelineEvent(state, {
        type: "output",
        pid: proc.pid,
        message: `PID ${proc.pid} printFromTo emitted range ${fromVal - 1}..${toVal + 1}`,
      });
    }
  } else if (op === "writeFile") {
    const fileVar = tokens[1];
    const dataVar = tokens[2];
    const fileName = proc.variables[fileVar] || "";
    const data = proc.variables[dataVar] || "";

    if (!fileName) {
      state.errors.push(`writeFile filename missing for PID ${proc.pid}`);
      timelineEvent(state, {
        type: "error",
        pid: proc.pid,
        message: `PID ${proc.pid} writeFile failed: missing filename`,
      });
    } else {
      const fullPath = path.join(__dirname, fileName);
      fs.writeFileSync(fullPath, data, "utf8");
      state.filesTouched.push(fileName);
      timelineEvent(state, {
        type: "file-write",
        pid: proc.pid,
        message: `PID ${proc.pid} wrote ${data.length} chars to ${fileName}`,
      });
    }
  } else {
    state.errors.push(`Unknown instruction '${op}' for PID ${proc.pid}`);
    timelineEvent(state, {
      type: "error",
      pid: proc.pid,
      message: `Unknown instruction ${op}`,
    });
  }

  proc.pc += 1;

  if (proc.pc >= proc.instructions.length) {
    proc.state = "FINISHED";
    timelineEvent(state, {
      type: "finished",
      pid: proc.pid,
      message: `PID ${proc.pid} finished`,
    });
  }
}

function getQuantum(state, proc) {
  if (state.scheduler === "RR") {
    return Math.max(1, state.rrQuantum);
  }

  if (state.scheduler === "MLFQ") {
    const level = Math.max(0, Math.min(3, proc.priorityLevel));
    return 2 ** level;
  }

  return 1;
}

function runSimulation(config) {
  const state = createInitialState(config);
  const guardLimit = 2000;
  let guard = 0;

  snapshotState(state, "Simulation initialized");

  while (guard < guardLimit) {
    guard += 1;
    const unfinished = state.processes.filter((p) => p.state !== "FINISHED");

    if (unfinished.length === 0) {
      timelineEvent(state, {
        type: "complete",
        message: "All processes finished",
      });
      snapshotState(state, "All processes finished");
      break;
    }

    let hasFutureArrival = false;
    for (const proc of unfinished) {
      if (proc.state === "NEW") {
        if (proc.arrival <= state.clock) {
          addToReady(state, proc.pid, 0);
          timelineEvent(state, {
            type: "arrival",
            pid: proc.pid,
            message: `PID ${proc.pid} arrived and entered ready queue`,
          });
        } else {
          hasFutureArrival = true;
        }
      }
    }

    const running = dequeueReady(state);

    if (!running) {
      if (hasFutureArrival) {
        timelineEvent(state, {
          type: "idle",
          message: `CPU idle at time ${state.clock}; waiting for arrivals`,
        });
        state.clock += 1;
        state.step += 1;
        snapshotState(state, "Idle tick");
        continue;
      }

      timelineEvent(state, {
        type: "deadlock",
        message: "No ready process and no future arrivals; possible deadlock",
      });
      snapshotState(state, "Execution halted");
      break;
    }

    running.state = "RUNNING";
    const quantum = getQuantum(state, running);
    let executed = 0;
    state.dispatch = {
      runningPid: running.pid,
      quantum,
      executed,
      instruction: running.instructions[running.pc] || null,
      schedulerDecision: `Selected PID ${running.pid} with quantum ${quantum}`,
    };
    timelineEvent(state, {
      type: "dispatch",
      pid: running.pid,
      message: `Scheduler dispatched PID ${running.pid} (quantum=${quantum})`,
    });
    snapshotState(state, `Dispatching PID ${running.pid}`);

    while (
      executed < quantum &&
      running.state === "RUNNING" &&
      running.pc < running.instructions.length
    ) {
      state.dispatch.instruction = running.instructions[running.pc] || null;
      state.dispatch.executed = executed;
      snapshotState(
        state,
        `PID ${running.pid} RUNNING instruction ${running.pc}`,
      );
      executeInstruction(state, running);
      schedulerTick(state, running);
      executed += 1;
      state.dispatch.executed = executed;

      if (running.state === "BLOCKED" || running.state === "FINISHED") {
        snapshotState(
          state,
          `PID ${running.pid} transitioned to ${running.state}`,
        );
        break;
      }
    }

    if (
      state.scheduler === "MLFQ" &&
      running.state === "RUNNING" &&
      running.pc < running.instructions.length &&
      executed >= quantum
    ) {
      running.priorityLevel = Math.min(3, running.priorityLevel + 1);
      addToReady(state, running.pid, running.priorityLevel);
      timelineEvent(state, {
        type: "preempt",
        pid: running.pid,
        message: `PID ${running.pid} preempted and demoted to Q${running.priorityLevel}`,
      });
    } else if (
      running.state === "RUNNING" &&
      running.pc < running.instructions.length
    ) {
      addToReady(state, running.pid);
      timelineEvent(state, {
        type: "preempt",
        pid: running.pid,
        message: `PID ${running.pid} preempted and returned to ready queue`,
      });
    }

    if (running.state !== "RUNNING") {
      state.dispatch.schedulerDecision = `PID ${running.pid} ${running.state.toLowerCase()}`;
    } else {
      state.dispatch.schedulerDecision = `PID ${running.pid} completed quantum`;
    }
    state.dispatch.runningPid = null;
    state.dispatch.instruction = null;

    state.clock += 1;
    state.step += 1;
    snapshotState(state, `Completed dispatch step ${state.step}`);
  }

  if (guard >= guardLimit) {
    state.errors.push("Simulation guard limit reached");
    timelineEvent(state, {
      type: "error",
      message: "Simulation guard limit reached",
    });
  }

  return {
    scheduler: state.scheduler,
    rrQuantum: state.rrQuantum,
    summary: {
      totalSteps: state.step,
      finalTime: state.clock,
      errors: state.errors.length,
      outputs: state.outputs.length,
      filesTouched: [...new Set(state.filesTouched)],
    },
    processes: state.processes.map(processSummary),
    timeline: state.timeline,
    snapshots: state.snapshots,
    outputs: state.outputs,
    errors: state.errors,
  };
}

app.get("/api/health", (req, res) => {
  res.json({ ok: true, service: "os-simulator-dashboard" });
});

app.get("/api/programs", (req, res) => {
  const programs = DEFAULT_PROGRAMS.map((item) => {
    const fullPath = path.join(PROGRAMS_DIR, item.file);
    const exists = fs.existsSync(fullPath);
    const instructions = exists ? readProgramInstructions(item.file) : [];

    return {
      ...item,
      exists,
      instructions,
      instructionCount: instructions.length,
    };
  });

  res.json({
    schedulers: ["HRRN", "RR", "MLFQ"],
    defaultRRQuantum: 2,
    programs,
  });
});

app.post("/api/simulate", (req, res) => {
  try {
    const body = req.body || {};
    const scheduler = String(body.scheduler || "HRRN").toUpperCase();
    if (!["HRRN", "RR", "MLFQ"].includes(scheduler)) {
      return res.status(400).json({ error: "Invalid scheduler" });
    }

    const requested =
      Array.isArray(body.processes) && body.processes.length > 0
        ? body.processes
        : DEFAULT_PROGRAMS;

    const processes = requested.map((item, idx) => ({
      pid: Number(item.pid) || idx + 1,
      file: String(item.file || ""),
      arrival: Number.isInteger(item.arrival)
        ? item.arrival
        : Number(item.arrival) || 0,
      inputMap:
        item.inputMap && typeof item.inputMap === "object" ? item.inputMap : {},
    }));

    for (const proc of processes) {
      if (!proc.file) {
        return res
          .status(400)
          .json({ error: `Process ${proc.pid} missing file` });
      }
      const fullPath = path.join(PROGRAMS_DIR, proc.file);
      if (!fs.existsSync(fullPath)) {
        return res
          .status(400)
          .json({ error: `Program file not found: ${proc.file}` });
      }
    }

    const result = runSimulation({
      scheduler,
      rrQuantum: Number.isInteger(body.rrQuantum)
        ? body.rrQuantum
        : Number(body.rrQuantum) || 2,
      processes,
    });

    res.json(result);
  } catch (error) {
    res
      .status(500)
      .json({ error: "Simulation failed", details: error.message });
  }
});

app.get("*", (req, res) => {
  res.sendFile(path.join(__dirname, "frontend", "index.html"));
});

app.listen(PORT, () => {
  console.log(`OS simulator dashboard running on http://localhost:${PORT}`);
});
