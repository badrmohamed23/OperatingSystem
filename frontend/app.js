const state = {
  programCatalog: null,
  processConfig: [],
  latestRequest: {},
  latestResponse: null,
  selectedSnapshotIndex: 0,
  autoPlayInterval: null,
};

const els = {
  schedulerSelect: document.getElementById("schedulerSelect"),
  rrQuantumInput: document.getElementById("rrQuantumInput"),
  processConfigurator: document.getElementById("processConfigurator"),
  loadDefaultsBtn: document.getElementById("loadDefaultsBtn"),
  runSimulationBtn: document.getElementById("runSimulationBtn"),
  summaryCards: document.getElementById("summaryCards"),
  timelineList: document.getElementById("timelineList"),
  outputList: document.getElementById("outputList"),
  errorList: document.getElementById("errorList"),
  apiRequest: document.getElementById("apiRequest"),
  apiResponse: document.getElementById("apiResponse"),
  stepSlider: document.getElementById("stepSlider"),
  prevStepBtn: document.getElementById("prevStepBtn"),
  nextStepBtn: document.getElementById("nextStepBtn"),
  autoPlayBtn: document.getElementById("autoPlayBtn"),
  clockSpeedInput: document.getElementById("clockSpeedInput"),
  snapshotLabel: document.getElementById("snapshotLabel"),
  coreDot: document.getElementById("coreDot"),
  coreLabel: document.getElementById("coreLabel"),
  stateGrid: document.getElementById("stateGrid"),
  processTemplate: document.getElementById("processTemplate"),
};

function toJson(value) {
  return JSON.stringify(value, null, 2);
}

async function fetchJson(url, options = {}) {
  const response = await fetch(url, options);
  const body = await response.json();
  if (!response.ok) {
    throw new Error(body.error || "Request failed");
  }
  return body;
}

function renderSchedulerOptions(catalog) {
  els.schedulerSelect.innerHTML = "";
  for (const scheduler of catalog.schedulers) {
    const option = document.createElement("option");
    option.value = scheduler;
    option.textContent = scheduler;
    els.schedulerSelect.appendChild(option);
  }
}

function defaultInputMapFor(file) {
  if (file === "Program 1.txt") {
    return { x: "2", y: "7" };
  }
  if (file === "Program_2.txt") {
    return { a: "dashboard_output.txt", b: "Saved from simulator" };
  }
  if (file === "Program_3.txt") {
    return { a: "syscall_test.txt" };
  }
  return {};
}

function resetProcessConfig() {
  state.processConfig = state.programCatalog.programs.map((program) => ({
    pid: program.pid,
    file: program.file,
    arrival: program.arrival,
    inputMap: defaultInputMapFor(program.file),
  }));
  renderProcessConfigurator();
}

function renderProcessConfigurator() {
  els.processConfigurator.innerHTML = "";

  for (const processCfg of state.processConfig) {
    const fragment = els.processTemplate.content.cloneNode(true);
    const card = fragment.querySelector(".process-item");
    card.dataset.pid = processCfg.pid;

    fragment.querySelector("h3").textContent = `PID ${processCfg.pid}`;
    fragment.querySelector(".program-file").textContent = processCfg.file;

    const arrivalInput = fragment.querySelector(".arrival-input");
    arrivalInput.value = processCfg.arrival;
    arrivalInput.addEventListener("input", (event) => {
      processCfg.arrival = Number(event.target.value);
    });

    const inputMap = fragment.querySelector(".input-map");
    inputMap.value = toJson(processCfg.inputMap);
    inputMap.addEventListener("change", (event) => {
      try {
        const parsed = JSON.parse(event.target.value || "{}");
        processCfg.inputMap =
          parsed && typeof parsed === "object" ? parsed : {};
      } catch {
        event.target.value = toJson(processCfg.inputMap);
      }
    });

    els.processConfigurator.appendChild(fragment);
  }
}

function collectPayload() {
  return {
    scheduler: els.schedulerSelect.value,
    rrQuantum: Number(els.rrQuantumInput.value),
    processes: state.processConfig.map((proc) => ({
      pid: proc.pid,
      file: proc.file,
      arrival: Number(proc.arrival),
      inputMap: proc.inputMap,
    })),
  };
}

function renderSummary(result) {
  const cards = [
    { label: "Total Steps", value: result.summary.totalSteps },
    { label: "Final Time", value: result.summary.finalTime },
    { label: "Outputs", value: result.summary.outputs },
    { label: "Errors", value: result.summary.errors },
    { label: "Files Touched", value: result.summary.filesTouched.length },
  ];

  els.summaryCards.innerHTML = "";
  for (const card of cards) {
    const node = document.createElement("article");
    node.className = "metric";
    node.innerHTML = `<div class="label">${card.label}</div><div class="value">${card.value}</div>`;
    els.summaryCards.appendChild(node);
  }
}

function renderTimeline(result) {
  els.timelineList.innerHTML = "";
  result.timeline.forEach((event, index) => {
    const item = document.createElement("button");
    item.type = "button";
    item.className = `timeline-item ${event.type}`;
    item.dataset.step = event.step;
    item.innerHTML = `[t=${event.time} s=${event.step}] ${event.message}`;
    item.addEventListener("click", () => {
      const snapshotIndex = findSnapshotForEvent(result, event);
      state.selectedSnapshotIndex = Math.max(0, snapshotIndex);
      renderSnapshot(result);
      highlightSelectedTimeline(index);
    });
    els.timelineList.appendChild(item);
  });
  highlightSelectedTimeline(-1);
}

function findSnapshotForEvent(result, event) {
  if (
    !result ||
    !Array.isArray(result.snapshots) ||
    result.snapshots.length === 0
  ) {
    return 0;
  }

  let bestIndex = 0;
  for (let i = 0; i < result.snapshots.length; i += 1) {
    const snapshot = result.snapshots[i];
    if (
      snapshot.step > event.step ||
      (snapshot.step === event.step && snapshot.time > event.time)
    ) {
      break;
    }
    bestIndex = i;
  }

  return bestIndex;
}

function highlightSelectedTimeline(index) {
  const items = els.timelineList.querySelectorAll(".timeline-item");
  items.forEach((item, itemIndex) => {
    item.classList.toggle("selected", itemIndex === index);
  });
}

function renderOutputs(result) {
  els.outputList.innerHTML = "";
  if (result.outputs.length === 0) {
    const empty = document.createElement("div");
    empty.className = "console-item";
    empty.textContent = "No outputs produced.";
    els.outputList.appendChild(empty);
  } else {
    result.outputs.forEach((out) => {
      const line = document.createElement("div");
      line.className = "console-item ok";
      line.dataset.step = out.step;
      line.textContent = `[t=${out.time} s=${out.step}] ${out.text}`;
      els.outputList.appendChild(line);
    });
  }

  els.errorList.innerHTML = "";
  if (result.errors.length === 0) {
    const empty = document.createElement("div");
    empty.className = "console-item";
    empty.textContent = "No errors detected.";
    els.errorList.appendChild(empty);
  } else {
    result.errors.forEach((err) => {
      const line = document.createElement("div");
      line.className = "console-item error";
      line.textContent = err;
      els.errorList.appendChild(line);
    });
  }
}

function renderApiInspector() {
  els.apiRequest.textContent = toJson(state.latestRequest);
  els.apiResponse.textContent = state.latestResponse
    ? toJson(state.latestResponse)
    : "{}";
}

function queuePills(items) {
  if (!items || items.length === 0) {
    return '<span class="queue-empty">(empty)</span>';
  }
  return items
    .map((pid) => `<span class="queue-pill">PID ${pid}</span>`)
    .join("");
}

function memoryCellTitle(cell) {
  if (cell.kind === "FREE") {
    return `Word ${cell.index}: free`;
  }
  return `Word ${cell.index}\nPID ${cell.ownerPid}\n${cell.name}: ${cell.value}`;
}

function renderRuntimePulse(snapshot) {
  if (!snapshot || !snapshot.dispatch) {
    els.coreLabel.textContent = "IDLE";
    els.coreDot.className = "dot idle";
    return;
  }

  const running = snapshot.dispatch.runningPid;
  if (!running) {
    els.coreLabel.textContent = `IDLE (${snapshot.dispatch.schedulerDecision})`;
    els.coreDot.className = "dot idle";
    return;
  }

  const instruction = snapshot.dispatch.instruction || "(none)";
  els.coreLabel.textContent = `RUNNING PID ${running} [${instruction}]`;
  els.coreDot.className = "dot running";
}

function buildStateCards(snapshot) {
  const cards = [];

  cards.push(`
    <article class="state-box queue-box">
      <h3>Ready Queues</h3>
      <div class="queue-lane"><span class="queue-name">Ready</span><div class="queue-values">${queuePills(snapshot.queues.readyQueue)}</div></div>
      <div class="queue-lane"><span class="queue-name">Q0</span><div class="queue-values">${queuePills(snapshot.queues.mlfqQueues[0])}</div></div>
      <div class="queue-lane"><span class="queue-name">Q1</span><div class="queue-values">${queuePills(snapshot.queues.mlfqQueues[1])}</div></div>
      <div class="queue-lane"><span class="queue-name">Q2</span><div class="queue-values">${queuePills(snapshot.queues.mlfqQueues[2])}</div></div>
      <div class="queue-lane"><span class="queue-name">Q3</span><div class="queue-values">${queuePills(snapshot.queues.mlfqQueues[3])}</div></div>
    </article>
  `);

  cards.push(`
    <article class="state-box queue-box">
      <h3>Blocked Queues</h3>
      <div class="queue-lane"><span class="queue-name">userInput</span><div class="queue-values">${queuePills(snapshot.queues.blockedQueues.userInput)}</div></div>
      <div class="queue-lane"><span class="queue-name">userOutput</span><div class="queue-values">${queuePills(snapshot.queues.blockedQueues.userOutput)}</div></div>
      <div class="queue-lane"><span class="queue-name">file</span><div class="queue-values">${queuePills(snapshot.queues.blockedQueues.file)}</div></div>
    </article>
  `);

  const mutexLine = (name, mutex) => {
    const lockState = mutex.locked ? "LOCKED" : "FREE";
    const owner = mutex.ownerPid ? `PID ${mutex.ownerPid}` : "none";
    return `<div class="mutex-row ${mutex.locked ? "locked" : "free"}"><span>${name}</span><strong>${lockState}</strong><em>owner: ${owner}</em></div>`;
  };

  cards.push(`
    <article class="state-box mutex-box">
      <h3>Mutex Monitor</h3>
      ${mutexLine("userInput", snapshot.mutexes.userInput)}
      ${mutexLine("userOutput", snapshot.mutexes.userOutput)}
      ${mutexLine("file", snapshot.mutexes.file)}
    </article>
  `);

  const memory = snapshot.memory;
  const memoryCells = memory.slots
    .map((cell) => {
      const cls = `memory-cell ${cell.kind.toLowerCase()}`;
      const pid = cell.ownerPid ? `P${cell.ownerPid}` : "-";
      const style = cell.ownerPid ? `style="background-color: var(--pid-${cell.ownerPid}-bg); border: 1px solid var(--pid-${cell.ownerPid}-border); color: #fff;"` : "";
      let valDisplay = "";
      if (cell.kind !== "FREE") {
        const displayStr = cell.name ? `${cell.name}=${cell.value}` : cell.value;
        valDisplay = `<span class="val" title="${displayStr}">${displayStr}</span>`;
      }
      return `<div class="${cls}" ${style} title="${memoryCellTitle(cell)}">
        <span class="idx">${cell.index}</span>
        <span class="pid">${pid}</span>
        ${valDisplay}
      </div>`;
    })
    .join("");

  const memorySegments = memory.segments.length
    ? memory.segments
        .map(
          (seg) =>
            `<span class="queue-pill">PID ${seg.pid}: ${seg.start}-${seg.end} (${seg.state})</span>`,
        )
        .join("")
    : '<span class="queue-empty">No resident processes</span>';

  cards.push(`
    <article class="state-box memory-box full-span">
      <h3>Memory Visualizer (${memory.usedWords}/${memory.size} used)</h3>
      <div class="queue-values">${memorySegments}</div>
      <div class="memory-grid">${memoryCells}</div>
    </article>
  `);

  for (const proc of snapshot.processes) {
    const vars = Object.keys(proc.variables).length
      ? toJson(proc.variables)
      : "{}";
    cards.push(`
      <article class="state-box process-box ${proc.state === "RUNNING" ? "is-running" : ""}">
        <h3>PID ${proc.pid} <span class="state-badge ${proc.state}">${proc.state}</span></h3>
        <div>Program: ${proc.file}</div>
        <div>PC: ${proc.pc}/${proc.totalInstructions}</div>
        <div>Waiting: ${proc.waitingTime}</div>
        <div>Remaining: ${proc.remainingTime}</div>
        <div>Priority: ${proc.priorityLevel}</div>
        <div class="vars-block">Vars: ${vars}</div>
      </article>
    `);
  }

  return cards.join("");
}

function renderSnapshot(result) {
  if (!result || result.snapshots.length === 0) {
    els.snapshotLabel.textContent =
      "Run the simulator to inspect state snapshots.";
    els.coreLabel.textContent = "IDLE";
    els.coreDot.className = "dot idle";
    els.stateGrid.innerHTML = "";
    return;
  }

  const index = Math.max(
    0,
    Math.min(state.selectedSnapshotIndex, result.snapshots.length - 1),
  );
  state.selectedSnapshotIndex = index;
  const snapshot = result.snapshots[index];
  const currentStep = snapshot.step;

  els.timelineList.querySelectorAll(".timeline-item").forEach(el => {
    el.style.display = (el.dataset.step && Number(el.dataset.step) > currentStep) ? "none" : "block";
  });
  els.outputList.querySelectorAll(".console-item.ok").forEach(el => {
    el.style.display = (el.dataset.step && Number(el.dataset.step) > currentStep) ? "none" : "block";
  });

  const dispatch = snapshot.dispatch;
  if (dispatch && dispatch.instruction) {
    const tokens = dispatch.instruction.split(/\s+/).filter(Boolean);
    if (tokens[0] === "assign" && tokens[2] === "input") {
      const varName = tokens[1];
      const procConfig = state.processConfig.find(p => p.pid === dispatch.runningPid);
      if (procConfig && !Object.prototype.hasOwnProperty.call(procConfig.inputMap, varName)) {
        if (state.autoPlayInterval) {
          clearInterval(state.autoPlayInterval);
          state.autoPlayInterval = null;
          els.autoPlayBtn.textContent = "▶ Auto Run";
          els.autoPlayBtn.classList.remove("ghost");
          els.autoPlayBtn.classList.add("primary");
        }
        setTimeout(() => {
          const val = prompt(`Process PID ${dispatch.runningPid} requires input for '${varName}':`);
          if (val !== null) {
            procConfig.inputMap[varName] = val;
            renderProcessConfigurator();
            runSimulation(true);
          }
        }, 50);
      }
    }
  }

  els.stepSlider.max = String(result.snapshots.length - 1);
  els.stepSlider.value = String(index);
  els.snapshotLabel.textContent = `[Snapshot ${index + 1}/${result.snapshots.length}] ${snapshot.label} (time=${snapshot.time}, step=${snapshot.step})`;

  renderRuntimePulse(snapshot);
  els.stateGrid.innerHTML = buildStateCards(snapshot);
}

async function runSimulation(keepIndex = false) {
  if (state.autoPlayInterval) {
    clearInterval(state.autoPlayInterval);
    state.autoPlayInterval = null;
    els.autoPlayBtn.textContent = "▶ Auto Run";
    els.autoPlayBtn.classList.remove("ghost");
    els.autoPlayBtn.classList.add("primary");
  }

  const payload = collectPayload();
  state.latestRequest = payload;
  renderApiInspector();

  try {
    const result = await fetchJson("/api/simulate", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(payload),
    });

    state.latestResponse = result;
    if (keepIndex !== true) {
      state.selectedSnapshotIndex = 0;
    }

    renderSummary(result);
    renderTimeline(result);
    renderOutputs(result);
    renderSnapshot(result);
    renderApiInspector();
  } catch (error) {
    state.latestResponse = { error: error.message };
    renderApiInspector();
    els.errorList.innerHTML = `<div class="console-item error">${error.message}</div>`;
  }
}

function bindUi() {
  function toggleAutoPlay() {
    if (state.autoPlayInterval) {
      clearInterval(state.autoPlayInterval);
      state.autoPlayInterval = null;
      els.autoPlayBtn.textContent = "▶ Auto Run";
      els.autoPlayBtn.classList.remove("ghost");
      els.autoPlayBtn.classList.add("primary");
    } else {
      if (!state.latestResponse || state.latestResponse.snapshots.length === 0) return;
      if (state.selectedSnapshotIndex >= state.latestResponse.snapshots.length - 1) {
        state.selectedSnapshotIndex = 0;
      }
      els.autoPlayBtn.textContent = "⏸ Pause";
      els.autoPlayBtn.classList.remove("primary");
      els.autoPlayBtn.classList.add("ghost");
      
      const speed = Number(els.clockSpeedInput.value) || 500;
      state.autoPlayInterval = setInterval(() => {
        if (state.selectedSnapshotIndex < state.latestResponse.snapshots.length - 1) {
          state.selectedSnapshotIndex++;
          renderSnapshot(state.latestResponse);
        } else {
          toggleAutoPlay();
        }
      }, speed);
    }
  }

  els.autoPlayBtn.addEventListener("click", toggleAutoPlay);

  els.loadDefaultsBtn.addEventListener("click", () => {
    resetProcessConfig();
    state.latestRequest = {};
    state.latestResponse = null;
    state.selectedSnapshotIndex = 0;
    if (state.autoPlayInterval) {
      clearInterval(state.autoPlayInterval);
      state.autoPlayInterval = null;
      els.autoPlayBtn.textContent = "▶ Auto Run";
      els.autoPlayBtn.classList.remove("ghost");
      els.autoPlayBtn.classList.add("primary");
    }
    renderApiInspector();
    els.summaryCards.innerHTML = "";
    els.timelineList.innerHTML = "";
    els.outputList.innerHTML = "";
    els.errorList.innerHTML = "";
    renderSnapshot(null);
  });

  els.runSimulationBtn.addEventListener("click", () => {
    runSimulation();
  });

  els.stepSlider.addEventListener("input", (event) => {
    if (!state.latestResponse) {
      return;
    }
    state.selectedSnapshotIndex = Number(event.target.value);
    renderSnapshot(state.latestResponse);
  });

  els.prevStepBtn.addEventListener("click", () => {
    if (!state.latestResponse) {
      return;
    }
    state.selectedSnapshotIndex = Math.max(0, state.selectedSnapshotIndex - 1);
    renderSnapshot(state.latestResponse);
  });

  els.nextStepBtn.addEventListener("click", () => {
    if (!state.latestResponse) {
      return;
    }
    state.selectedSnapshotIndex = Math.min(
      state.latestResponse.snapshots.length - 1,
      state.selectedSnapshotIndex + 1,
    );
    renderSnapshot(state.latestResponse);
  });
}

async function init() {
  bindUi();
  const catalog = await fetchJson("/api/programs");
  state.programCatalog = catalog;
  renderSchedulerOptions(catalog);
  els.rrQuantumInput.value = String(catalog.defaultRRQuantum || 2);
  resetProcessConfig();
}

init().catch((error) => {
  els.errorList.innerHTML = `<div class="console-item error">Startup failed: ${error.message}</div>`;
});
