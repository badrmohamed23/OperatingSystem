const state = {
  programCatalog: null,
  scenariosCatalog: [],
  processConfig: [],
  latestRequest: {},
  latestResponse: null,
  selectedSnapshotIndex: 0,
  autoPlayInterval: null,
  eventFilter: "ALL", // 'ALL' | 'CPU' | 'MEM' | 'SYNC' | 'PROC'
};

const els = {
  schedulerSelect: document.getElementById("schedulerSelect"),
  rrQuantumField: document.getElementById("rrQuantumField"),
  rrQuantumInput: document.getElementById("rrQuantumInput"),
  scenarioSelect: document.getElementById("scenarioSelect"),
  addProcessBtn: document.getElementById("addProcessBtn"),
  processConfigurator: document.getElementById("processConfigurator"),
  loadDefaultsBtn: document.getElementById("loadDefaultsBtn"),
  runSimulationBtn: document.getElementById("runSimulationBtn"),
  compareBtn: document.getElementById("compareBtn"),
  comparisonModal: document.getElementById("comparisonModal"),
  closeModalBtn: document.getElementById("closeModalBtn"),
  comparisonResults: document.getElementById("comparisonResults"),
  summaryCards: document.getElementById("summaryCards"),
  timelineList: document.getElementById("timelineList"),
  eventFilterBtns: document.getElementById("eventFilterBtns"),
  outputList: document.getElementById("outputList"),
  stepSlider: document.getElementById("stepSlider"),
  prevStepBtn: document.getElementById("prevStepBtn"),
  nextStepBtn: document.getElementById("nextStepBtn"),
  autoPlayBtn: document.getElementById("autoPlayBtn"),
  clockSpeedInput: document.getElementById("clockSpeedInput"),
  snapshotLabel: document.getElementById("snapshotLabel"),
  clockLabel: document.getElementById("clockLabel"),
  coreDot: document.getElementById("coreDot"),
  coreLabel: document.getElementById("coreLabel"),
  metricsBar: document.getElementById("metricsBar"),
  ganttContainer: document.getElementById("ganttContainer"),
  ganttLegend: document.getElementById("ganttLegend"),
  processTableBody: document.getElementById("processTableBody"),
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
    throw new Error(body.error || body.details || "Request failed");
  }
  return body;
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

function renderSchedulerOptions(catalog) {
  els.schedulerSelect.innerHTML = "";
  for (const scheduler of catalog.schedulers) {
    const option = document.createElement("option");
    option.value = scheduler;
    option.textContent = scheduler;
    els.schedulerSelect.appendChild(option);
  }
  updateQuantumVisibility();
}

function updateQuantumVisibility() {
  if (els.rrQuantumField) {
    els.rrQuantumField.style.display = els.schedulerSelect.value === "RR" ? "flex" : "none";
  }
}

function renderScenariosDropdown(scenarios) {
  els.scenarioSelect.innerHTML = '<option value="">-- Custom Workload --</option>';
  for (const scenario of scenarios) {
    const opt = document.createElement("option");
    opt.value = scenario.id;
    opt.textContent = `${scenario.name} (${scenario.config.scheduler})`;
    els.scenarioSelect.appendChild(opt);
  }
}

function resetProcessConfig() {
  if (!state.programCatalog) return;
  state.processConfig = [
    { pid: 1, file: "Program 1.txt", arrival: 0, inputMap: { x: "2", y: "7" } },
    { pid: 2, file: "Program_2.txt", arrival: 1, inputMap: { a: "dashboard_output.txt", b: "Saved from simulator" } },
    { pid: 3, file: "Program_3.txt", arrival: 4, inputMap: { a: "dashboard_output.txt" } },
  ];
  renderProcessConfigurator();
}

function renderProcessConfigurator() {
  els.processConfigurator.innerHTML = "";
  const programFiles = (state.programCatalog && state.programCatalog.programs)
    ? state.programCatalog.programs.map((p) => p.file)
    : ["Program 1.txt", "Program_2.txt", "Program_3.txt"];

  state.processConfig.forEach((processCfg, idx) => {
    const fragment = els.processTemplate.content.cloneNode(true);
    const card = fragment.querySelector(".process-item");
    card.dataset.pid = processCfg.pid;

    const pidBadge = fragment.querySelector(".pid-badge");
    pidBadge.textContent = `PID ${processCfg.pid}`;
    pidBadge.style.backgroundColor = `var(--pid-${processCfg.pid}-color, var(--accent))`;

    const progSelect = fragment.querySelector(".program-file-select");
    progSelect.innerHTML = "";
    for (const file of programFiles) {
      const opt = document.createElement("option");
      opt.value = file;
      opt.textContent = file;
      if (file === processCfg.file) opt.selected = true;
      progSelect.appendChild(opt);
    }
    progSelect.addEventListener("change", (e) => {
      processCfg.file = e.target.value;
      if (Object.keys(processCfg.inputMap).length === 0) {
        processCfg.inputMap = defaultInputMapFor(e.target.value);
        renderProcessConfigurator();
      }
    });

    const removeBtn = fragment.querySelector(".remove-process-btn");
    removeBtn.addEventListener("click", () => {
      if (state.processConfig.length <= 1) {
        alert("At least one process is required.");
        return;
      }
      state.processConfig.splice(idx, 1);
      renderProcessConfigurator();
    });

    const arrivalInput = fragment.querySelector(".arrival-input");
    arrivalInput.value = processCfg.arrival;
    arrivalInput.addEventListener("input", (e) => {
      processCfg.arrival = Math.max(0, parseInt(e.target.value, 10) || 0);
    });

    const inputMap = fragment.querySelector(".input-map");
    inputMap.value = JSON.stringify(processCfg.inputMap || {});
    inputMap.addEventListener("change", (e) => {
      try {
        const parsed = JSON.parse(e.target.value || "{}");
        processCfg.inputMap = parsed && typeof parsed === "object" ? parsed : {};
      } catch {
        e.target.value = JSON.stringify(processCfg.inputMap);
      }
    });

    els.processConfigurator.appendChild(fragment);
  });
}

function addNewProcess() {
  const maxPid = state.processConfig.reduce((max, p) => Math.max(max, p.pid), 0);
  const nextPid = maxPid + 1;
  const programFiles = (state.programCatalog && state.programCatalog.programs)
    ? state.programCatalog.programs.map((p) => p.file)
    : ["Program 1.txt"];
  const file = programFiles[0] || "Program 1.txt";

  state.processConfig.push({
    pid: nextPid,
    file,
    arrival: 0,
    inputMap: defaultInputMapFor(file),
  });
  renderProcessConfigurator();
}

function collectPayload(overrideScheduler = null) {
  return {
    scheduler: overrideScheduler || els.schedulerSelect.value,
    rrQuantum: Number(els.rrQuantumInput.value) || 2,
    processes: state.processConfig.map((proc) => ({
      pid: proc.pid,
      file: proc.file,
      arrival: Number(proc.arrival) || 0,
      inputMap: proc.inputMap || {},
    })),
  };
}

function renderSummary(result) {
  const cards = [
    { label: "Total Steps", val: result.summary.totalSteps },
    { label: "Final Time", val: `${result.summary.finalTime}t` },
    { label: "Outputs", val: result.summary.outputs },
    { label: "Avg Waiting", val: `${result.metrics.avgWaitingTime.toFixed(1)}t` },
    { label: "Avg Turnaround", val: `${result.metrics.avgTurnaroundTime.toFixed(1)}t` },
    { label: "CPU Utilization", val: `${result.metrics.cpuUtilization.toFixed(0)}%` },
  ];

  els.summaryCards.innerHTML = "";
  for (const card of cards) {
    const node = document.createElement("article");
    node.className = "metric-card";
    node.innerHTML = `<div class="label">${card.label}</div><div class="val">${card.val}</div>`;
    els.summaryCards.appendChild(node);
  }

  // Top metrics bar
  els.metricsBar.innerHTML = `
    <span>Avg Wait: <strong>${result.metrics.avgWaitingTime.toFixed(2)}</strong></span>
    <span>Avg Turnaround: <strong>${result.metrics.avgTurnaroundTime.toFixed(2)}</strong></span>
    <span>CPU Util: <strong>${result.metrics.cpuUtilization.toFixed(1)}%</strong></span>
    <span>Switches: <strong>${result.metrics.contextSwitches}</strong></span>
  `;
}

function renderFilterButtons() {
  const filters = [
    { id: "ALL", label: "All" },
    { id: "CPU", label: "CPU" },
    { id: "MEM", label: "Memory" },
    { id: "SYNC", label: "Mutex" },
    { id: "PROC", label: "Process" },
  ];

  els.eventFilterBtns.innerHTML = "";
  filters.forEach((f) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = `filter-btn ${state.eventFilter === f.id ? "active" : ""}`;
    btn.textContent = f.label;
    btn.addEventListener("click", () => {
      state.eventFilter = f.id;
      renderFilterButtons();
      if (state.latestResponse) {
        filterTimelineEvents(state.latestResponse);
      }
    });
    els.eventFilterBtns.appendChild(btn);
  });
}

function eventMatchesFilter(eventType, filter) {
  if (filter === "ALL") return true;
  if (filter === "CPU") {
    return ["PROCESS_DISPATCHED", "INSTRUCTION_EXECUTED", "TIME_TICK"].includes(eventType);
  }
  if (filter === "MEM") {
    return ["MEMORY_ALLOCATED", "MEMORY_FREED", "PROCESS_SWAPPED_OUT", "PROCESS_SWAPPED_IN"].includes(eventType);
  }
  if (filter === "SYNC") {
    return ["MUTEX_ACQUIRED", "MUTEX_BLOCKED", "MUTEX_RELEASED", "MUTEX_UNBLOCKED"].includes(eventType);
  }
  if (filter === "PROC") {
    return ["PROCESS_CREATED", "PROCESS_TERMINATED", "PROCESS_BLOCKED", "PROCESS_UNBLOCKED"].includes(eventType);
  }
  return true;
}

function filterTimelineEvents(result) {
  const items = els.timelineList.querySelectorAll(".timeline-item");
  items.forEach((item) => {
    const eventType = item.dataset.type;
    const matches = eventMatchesFilter(eventType, state.eventFilter);
    item.style.display = matches ? "block" : "none";
  });
}

function renderTimeline(result) {
  els.timelineList.innerHTML = "";
  result.timeline.forEach((event, index) => {
    const item = document.createElement("div");
    item.className = `console-item timeline-item ${event.type}`;
    item.dataset.step = event.step;
    item.dataset.time = event.time;
    item.dataset.type = event.type;
    item.style.cursor = "pointer";

    let icon = "•";
    if (event.type.includes("MUTEX")) icon = "🔒";
    else if (event.type.includes("MEMORY") || event.type.includes("SWAP")) icon = "💾";
    else if (event.type.includes("DISPATCH") || event.type.includes("INSTRUCTION")) icon = "⚡";
    else if (event.type.includes("TERMINATED")) icon = "✓";

    item.innerHTML = `<span style="color:var(--text-muted);">[t=${event.time} s=${event.step}]</span> ${icon} <strong>${event.message}</strong>`;
    if (event.instruction) {
      item.innerHTML += ` <span style="color:var(--text-muted); font-size:0.7rem;">(${event.instruction})</span>`;
    }

    item.addEventListener("click", () => {
      const snapshotIndex = findSnapshotForEvent(result, event);
      state.selectedSnapshotIndex = Math.max(0, snapshotIndex);
      renderSnapshot(result);
    });

    els.timelineList.appendChild(item);
  });

  filterTimelineEvents(result);
}

function findSnapshotForEvent(result, event) {
  if (!result || !result.snapshots || result.snapshots.length === 0) return 0;
  let bestIndex = 0;
  for (let i = 0; i < result.snapshots.length; i++) {
    const s = result.snapshots[i];
    if (s.step > event.step || (s.step === event.step && s.time > event.time)) break;
    bestIndex = i;
  }
  return bestIndex;
}

function renderOutputs(result) {
  els.outputList.innerHTML = "";
  if (result.outputs.length === 0) {
    els.outputList.innerHTML = '<div class="console-item" style="color:var(--text-muted);">No console outputs recorded.</div>';
  } else {
    result.outputs.forEach((out) => {
      const line = document.createElement("div");
      line.className = "console-item ok";
      line.dataset.step = out.step;
      line.dataset.time = out.time;
      line.innerHTML = `<span style="color:var(--text-muted);">[t=${out.time}]</span> PID ${out.pid}: <strong>${out.text}</strong>`;
      els.outputList.appendChild(line);
    });
  }
}

// Gantt Chart Rendering
function renderGanttChart(result, currentSnapshot) {
  if (!result || !result.snapshots || result.snapshots.length === 0) {
    els.ganttContainer.innerHTML = '<div class="empty-state">Run simulation to view CPU execution timeline</div>';
    els.ganttLegend.innerHTML = "";
    return;
  }

  const finalTime = result.summary.finalTime || 1;
  // Map time tick -> running PID
  const timeSlices = new Array(finalTime).fill(null);

  for (const s of result.snapshots) {
    if (s.dispatch && s.dispatch.runningPid && s.time < finalTime) {
      timeSlices[s.time] = s.dispatch.runningPid;
    }
  }

  // Also verify from timeline INSTRUCTION_EXECUTED
  for (const ev of result.timeline) {
    if (ev.type === "INSTRUCTION_EXECUTED" && ev.pid > 0 && ev.time < finalTime) {
      timeSlices[ev.time] = ev.pid;
    }
  }

  // Build tracks
  let trackHtml = '<div class="gantt-track">';
  for (let t = 0; t < finalTime; t++) {
    const pid = timeSlices[t];
    const isCurrent = currentSnapshot && currentSnapshot.time === t;
    if (pid) {
      trackHtml += `
        <div class="gantt-block ${isCurrent ? "current" : ""}" 
             style="background: var(--pid-${pid}-bg, rgba(59,130,246,0.3)); border: 1px solid var(--pid-${pid}-border, rgba(59,130,246,0.7)); color: #fff;"
             title="Time ${t}: Process P${pid} running"
             onclick="seekToTime(${t})">
          P${pid}
        </div>`;
    } else {
      trackHtml += `
        <div class="gantt-block idle-block ${isCurrent ? "current" : ""}" 
             title="Time ${t}: CPU Idle"
             onclick="seekToTime(${t})">
          -
        </div>`;
    }
  }
  trackHtml += "</div>";

  // Build time axis
  let axisHtml = '<div class="gantt-axis">';
  for (let t = 0; t < finalTime; t++) {
    axisHtml += `<div class="gantt-axis-label">${t}</div>`;
  }
  axisHtml += "</div>";

  els.ganttContainer.innerHTML = trackHtml + axisHtml;

  // Build legend
  const distinctPids = Array.from(new Set(state.processConfig.map((p) => p.pid))).sort((a, b) => a - b);
  let legendHtml = "";
  for (const pid of distinctPids) {
    legendHtml += `
      <div class="legend-item">
        <span class="legend-dot" style="background: var(--pid-${pid}-color, var(--accent));"></span>
        <span>P${pid}</span>
      </div>`;
  }
  legendHtml += `
    <div class="legend-item">
      <span class="legend-dot" style="background: var(--gray);"></span>
      <span>Idle</span>
    </div>`;
  els.ganttLegend.innerHTML = legendHtml;
}

window.seekToTime = function (targetTime) {
  if (!state.latestResponse) return;
  const snapIndex = state.latestResponse.snapshots.findIndex((s) => s.time >= targetTime);
  if (snapIndex >= 0) {
    state.selectedSnapshotIndex = snapIndex;
    renderSnapshot(state.latestResponse);
  }
};

// Process Table Rendering
function renderProcessTable(result, currentSnapshot) {
  if (!currentSnapshot || !currentSnapshot.processes) {
    els.processTableBody.innerHTML = '<tr><td colspan="10" style="text-align:center; color:var(--text-muted); padding:16px;">No process state loaded</td></tr>';
    return;
  }

  const runningPid = currentSnapshot.dispatch ? currentSnapshot.dispatch.runningPid : null;
  const summaryProcesses = (result && result.processes) ? result.processes : [];

  let html = "";
  for (const p of currentSnapshot.processes) {
    const isRunning = p.pid === runningPid;
    const summary = summaryProcesses.find((sp) => sp.pid === p.pid) || {};

    const arrival = summary.arrivalTime !== undefined ? summary.arrivalTime : (p.arrival || 0);
    const respTime = summary.responseTime !== undefined && summary.responseTime >= 0 ? `${summary.responseTime}t` : "-";
    const turnTime = summary.turnaroundTime !== undefined && summary.turnaroundTime > 0 ? `${summary.turnaroundTime}t` : (p.state === "FINISHED" ? `${currentSnapshot.time - arrival}t` : "-");
    const completion = summary.completionTime !== undefined && summary.completionTime >= 0 ? `t=${summary.completionTime}` : (p.state === "FINISHED" ? `t=${currentSnapshot.time}` : "In Progress");

    html += `
      <tr class="${isRunning ? "running-row" : ""}">
        <td><strong style="color: var(--pid-${p.pid}-color, var(--accent));">PID ${p.pid}</strong></td>
        <td><span class="state-badge ${p.state}">${p.state}</span></td>
        <td style="color: var(--text-muted);">${p.file || "-"}</td>
        <td>${p.pc} / ${p.totalInstructions}</td>
        <td>Q${p.priorityLevel !== undefined ? p.priorityLevel : 0}</td>
        <td>t=${arrival}</td>
        <td>${p.waitingTime}t</td>
        <td>${respTime}</td>
        <td>${turnTime}</td>
        <td>${completion}</td>
      </tr>
    `;
  }

  els.processTableBody.innerHTML = html;
}

function queuePills(items) {
  if (!items || items.length === 0) {
    return '<span class="queue-empty">(empty)</span>';
  }
  return items
    .map((pid) => `<span class="queue-pill" style="border-color: var(--pid-${pid}-border); background: var(--pid-${pid}-bg);">PID ${pid}</span>`)
    .join("");
}

function memoryCellTitle(cell) {
  if (cell.kind === "FREE") {
    return `Word ${cell.index}: FREE`;
  }
  return `Word ${cell.index} [PID ${cell.ownerPid}]\nKind: ${cell.kind}\n${cell.name || ""}: ${cell.value}`;
}

function renderRuntimePulse(snapshot) {
  if (!snapshot || !snapshot.dispatch) {
    els.coreLabel.textContent = "IDLE";
    els.coreDot.className = "dot idle";
    els.clockLabel.textContent = "t = 0";
    return;
  }

  els.clockLabel.textContent = `t = ${snapshot.time}`;
  const running = snapshot.dispatch.runningPid;
  if (!running) {
    els.coreLabel.textContent = `IDLE (${snapshot.dispatch.schedulerDecision || "No ready tasks"})`;
    els.coreDot.className = "dot idle";
    return;
  }

  const instruction = snapshot.dispatch.instruction || "executing";
  els.coreLabel.textContent = `RUNNING PID ${running} [${instruction}]`;
  els.coreDot.className = "dot running";
}

function buildStateCards(snapshot) {
  const cards = [];

  // 1. Ready & MLFQ Queues
  cards.push(`
    <article class="state-box third-span">
      <div class="panel-header">Ready & Multi-Level Queues</div>
      <div class="queue-lane"><span class="queue-name">Ready</span><div class="queue-values">${queuePills(snapshot.queues.readyQueue)}</div></div>
      <div class="queue-lane"><span class="queue-name">MLFQ Q0</span><div class="queue-values">${queuePills(snapshot.queues.mlfqQueues[0])}</div></div>
      <div class="queue-lane"><span class="queue-name">MLFQ Q1</span><div class="queue-values">${queuePills(snapshot.queues.mlfqQueues[1])}</div></div>
      <div class="queue-lane"><span class="queue-name">MLFQ Q2</span><div class="queue-values">${queuePills(snapshot.queues.mlfqQueues[2])}</div></div>
      <div class="queue-lane"><span class="queue-name">MLFQ Q3</span><div class="queue-values">${queuePills(snapshot.queues.mlfqQueues[3])}</div></div>
    </article>
  `);

  // 2. Blocked Queues
  cards.push(`
    <article class="state-box third-span">
      <div class="panel-header">Blocked Queues</div>
      <div class="queue-lane"><span class="queue-name">userInput</span><div class="queue-values">${queuePills(snapshot.queues.blockedQueues.userInput)}</div></div>
      <div class="queue-lane"><span class="queue-name">userOutput</span><div class="queue-values">${queuePills(snapshot.queues.blockedQueues.userOutput)}</div></div>
      <div class="queue-lane"><span class="queue-name">file</span><div class="queue-values">${queuePills(snapshot.queues.blockedQueues.file)}</div></div>
    </article>
  `);

  // 3. Mutex Monitor
  const mutexLine = (name, mutex) => {
    const isLocked = mutex && mutex.locked;
    const lockState = isLocked ? "LOCKED" : "FREE";
    const owner = (isLocked && mutex.ownerPid) ? `Owner: PID ${mutex.ownerPid}` : "No Owner";
    return `
      <div class="mutex-row ${isLocked ? "locked" : "free"}">
        <span><strong>${name}</strong></span>
        <span>${lockState}</span>
        <em style="color:var(--text-muted);">${owner}</em>
      </div>`;
  };

  cards.push(`
    <article class="state-box third-span">
      <div class="panel-header">Mutex Locks & Semaphores</div>
      ${mutexLine("userInput", snapshot.mutexes.userInput)}
      ${mutexLine("userOutput", snapshot.mutexes.userOutput)}
      ${mutexLine("file", snapshot.mutexes.file)}
    </article>
  `);

  // 4. Memory Grid (40 words contiguous)
  const memory = snapshot.memory;
  const memoryCells = memory.slots
    .map((cell) => {
      const cls = `memory-cell ${cell.kind.toLowerCase()}`;
      const isAllocated = cell.ownerPid !== null && cell.ownerPid !== undefined;
      const pidText = isAllocated ? `P${cell.ownerPid}` : "-";
      const style = isAllocated
        ? `style="background-color: var(--pid-${cell.ownerPid}-bg); border: 1px solid var(--pid-${cell.ownerPid}-border);"`
        : "";

      let valDisplay = "";
      if (cell.kind !== "FREE") {
        const text = cell.name && cell.name !== "-" ? `${cell.name}=${cell.value}` : cell.value;
        valDisplay = `<span class="val" title="${text}">${text}</span>`;
      }

      return `
        <div class="${cls}" ${style} title="${memoryCellTitle(cell)}">
          <span class="idx">${cell.index}</span>
          <span class="pid">${pidText}</span>
          ${valDisplay}
        </div>
      `;
    })
    .join("");

  const memorySegments = memory.segments && memory.segments.length
    ? memory.segments
        .map(
          (seg) =>
            `<span class="queue-pill" style="border-color: var(--pid-${seg.pid}-border); background: var(--pid-${seg.pid}-bg);">PID ${seg.pid}: words [${seg.start}..${seg.end}] (${seg.state})</span>`,
        )
        .join("")
    : '<span class="queue-empty">No resident processes in RAM</span>';

  cards.push(`
    <article class="state-box full-span">
      <div class="panel-header">
        <span>RAM Memory Map (40 Words Contiguous — First-Fit) [${memory.usedWords}/40 words used]</span>
        <div class="memory-legend">
          <span><span class="mem-swatch" style="background:var(--pid-1-color);"></span> P1</span>
          <span><span class="mem-swatch" style="background:var(--pid-2-color);"></span> P2</span>
          <span><span class="mem-swatch" style="background:var(--pid-3-color);"></span> P3</span>
          <span><span class="mem-swatch" style="background:rgba(255,255,255,0.06);"></span> Free</span>
        </div>
      </div>
      <div style="margin-bottom: 8px;">${memorySegments}</div>
      <div class="memory-grid">${memoryCells}</div>
    </article>
  `);

  return cards.join("");
}

function renderSnapshot(result) {
  if (!result || !result.snapshots || result.snapshots.length === 0) {
    els.snapshotLabel.textContent = "Run the simulator to inspect state snapshots.";
    els.coreLabel.textContent = "IDLE";
    els.coreDot.className = "dot idle";
    els.stateGrid.innerHTML = "";
    els.ganttContainer.innerHTML = '<div class="empty-state">Run simulation to view CPU execution timeline</div>';
    els.processTableBody.innerHTML = '<tr><td colspan="10" style="text-align:center; color:var(--text-muted); padding:16px;">No process state loaded</td></tr>';
    return;
  }

  const index = Math.max(0, Math.min(state.selectedSnapshotIndex, result.snapshots.length - 1));
  state.selectedSnapshotIndex = index;
  const snapshot = result.snapshots[index];

  // Update slider & label
  els.stepSlider.max = String(result.snapshots.length - 1);
  els.stepSlider.value = String(index);
  els.snapshotLabel.textContent = `[${index + 1}/${result.snapshots.length}] ${snapshot.label} (t=${snapshot.time}, s=${snapshot.step})`;

  renderRuntimePulse(snapshot);
  renderGanttChart(result, snapshot);
  renderProcessTable(result, snapshot);
  els.stateGrid.innerHTML = buildStateCards(snapshot);

  // Sync timeline item highlighting
  const currentStep = snapshot.step;
  els.timelineList.querySelectorAll(".timeline-item").forEach((el) => {
    const s = Number(el.dataset.step);
    if (s <= currentStep) {
      el.style.opacity = "1";
    } else {
      el.style.opacity = "0.35";
    }
  });

  // Check if interactive input is requested
  const dispatch = snapshot.dispatch;
  if (dispatch && dispatch.instruction) {
    const tokens = dispatch.instruction.split(/\s+/).filter(Boolean);
    if (tokens[0] === "assign" && tokens[2] === "input") {
      const varName = tokens[1];
      const procConfig = state.processConfig.find((p) => p.pid === dispatch.runningPid);
      if (procConfig && !Object.prototype.hasOwnProperty.call(procConfig.inputMap, varName)) {
        if (state.autoPlayInterval) {
          clearInterval(state.autoPlayInterval);
          state.autoPlayInterval = null;
          els.autoPlayBtn.textContent = "▶ Auto Run";
          els.autoPlayBtn.classList.remove("ghost");
          els.autoPlayBtn.classList.add("primary");
        }
        setTimeout(() => {
          const val = prompt(`[I/O Prompt] PID ${dispatch.runningPid} is requesting console input for '${varName}':`);
          if (val !== null && val.trim() !== "") {
            procConfig.inputMap[varName] = val.trim();
            renderProcessConfigurator();
            runSimulation(true);
          }
        }, 60);
      }
    }
  }
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

  try {
    const result = await fetchJson("/api/simulate", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });

    state.latestResponse = result;
    if (!keepIndex) {
      state.selectedSnapshotIndex = 0;
    }

    renderSummary(result);
    renderTimeline(result);
    renderOutputs(result);
    renderSnapshot(result);
  } catch (error) {
    state.latestResponse = { error: error.message };
    alert(`Simulation Error: ${error.message}`);
  }
}

async function compareSchedulers() {
  els.comparisonModal.style.display = "flex";
  els.comparisonResults.innerHTML = `
    <div style="text-align:center; padding: 40px; color: var(--text-muted);">
      <div style="font-size: 1.2rem; margin-bottom: 8px;">⏳ Running Multi-Algorithm Benchmark...</div>
      <div>Simulating workload across HRRN, RR (q=${els.rrQuantumInput.value || 2}), and MLFQ via C engine.</div>
    </div>`;

  try {
    const schedulers = ["HRRN", "RR", "MLFQ"];
    const promises = schedulers.map((sched) => {
      const payload = collectPayload(sched);
      return fetchJson("/api/simulate", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
    });

    const results = await Promise.all(promises);
    const benchmarkData = schedulers.map((sched, i) => ({
      scheduler: sched,
      data: results[i],
    }));

    // Find best values
    const minWait = Math.min(...benchmarkData.map((b) => b.data.metrics.avgWaitingTime));
    const minTurnaround = Math.min(...benchmarkData.map((b) => b.data.metrics.avgTurnaroundTime));
    const maxUtil = Math.max(...benchmarkData.map((b) => b.data.metrics.cpuUtilization));
    const minTime = Math.min(...benchmarkData.map((b) => b.data.summary.finalTime));

    let cardsHtml = '<div class="benchmark-grid">';
    for (const b of benchmarkData) {
      const isOverallWinner = b.data.metrics.avgTurnaroundTime === minTurnaround;
      cardsHtml += `
        <div class="benchmark-card ${isOverallWinner ? "best" : ""}">
          <h4>
            <span>${b.scheduler}</span>
            ${isOverallWinner ? '<span class="badge-winner">★ Lowest Turnaround</span>' : ""}
          </h4>
          <div class="benchmark-stats">
            <div>Final Time: <span>${b.data.summary.finalTime} ticks</span></div>
            <div>Avg Wait: <span>${b.data.metrics.avgWaitingTime.toFixed(2)} ticks</span></div>
            <div>Avg Turnaround: <span>${b.data.metrics.avgTurnaroundTime.toFixed(2)} ticks</span></div>
            <div>Avg Response: <span>${b.data.metrics.avgResponseTime.toFixed(2)} ticks</span></div>
            <div>CPU Utilization: <span>${b.data.metrics.cpuUtilization.toFixed(1)}%</span></div>
            <div>Context Switches: <span>${b.data.metrics.contextSwitches}</span></div>
          </div>
        </div>
      `;
    }
    cardsHtml += "</div>";

    let tableHtml = `
      <div class="table-responsive">
        <table class="styled-table">
          <thead>
            <tr>
              <th>Algorithm</th>
              <th>Total Time</th>
              <th>Avg Waiting Time</th>
              <th>Avg Turnaround</th>
              <th>Avg Response</th>
              <th>CPU Util</th>
              <th>Context Switches</th>
            </tr>
          </thead>
          <tbody>
    `;

    for (const b of benchmarkData) {
      const m = b.data.metrics;
      tableHtml += `
        <tr>
          <td><strong>${b.scheduler}</strong></td>
          <td>${b.data.summary.finalTime}t ${b.data.summary.finalTime === minTime ? "🏆" : ""}</td>
          <td>${m.avgWaitingTime.toFixed(2)}t ${m.avgWaitingTime === minWait ? "🏆" : ""}</td>
          <td>${m.avgTurnaroundTime.toFixed(2)}t ${m.avgTurnaroundTime === minTurnaround ? "🏆" : ""}</td>
          <td>${m.avgResponseTime.toFixed(2)}t</td>
          <td>${m.cpuUtilization.toFixed(1)}% ${m.cpuUtilization === maxUtil ? "🏆" : ""}</td>
          <td>${m.contextSwitches}</td>
        </tr>
      `;
    }

    tableHtml += `
          </tbody>
        </table>
      </div>
    `;

    els.comparisonResults.innerHTML = cardsHtml + tableHtml;
  } catch (err) {
    els.comparisonResults.innerHTML = `<div class="console-item error">Benchmark error: ${err.message}</div>`;
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
      if (!state.latestResponse || !state.latestResponse.snapshots || state.latestResponse.snapshots.length === 0) return;
      if (state.selectedSnapshotIndex >= state.latestResponse.snapshots.length - 1) {
        state.selectedSnapshotIndex = 0;
      }
      els.autoPlayBtn.textContent = "⏸ Pause";
      els.autoPlayBtn.classList.remove("primary");
      els.autoPlayBtn.classList.add("ghost");

      const speed = Number(els.clockSpeedInput.value) || 400;
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

  els.schedulerSelect.addEventListener("change", updateQuantumVisibility);

  els.addProcessBtn.addEventListener("click", addNewProcess);

  els.loadDefaultsBtn.addEventListener("click", () => {
    resetProcessConfig();
    els.scenarioSelect.value = "";
    els.schedulerSelect.value = "HRRN";
    updateQuantumVisibility();
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
    els.summaryCards.innerHTML = "";
    els.timelineList.innerHTML = "";
    els.outputList.innerHTML = "";
    els.metricsBar.innerHTML = "";
    renderSnapshot(null);
  });

  els.scenarioSelect.addEventListener("change", (e) => {
    const scenarioId = e.target.value;
    if (!scenarioId) return;
    const scenario = state.scenariosCatalog.find((s) => s.id === scenarioId);
    if (!scenario || !scenario.config) return;

    if (scenario.config.scheduler) {
      els.schedulerSelect.value = scenario.config.scheduler;
      updateQuantumVisibility();
    }
    if (scenario.config.rrQuantum) {
      els.rrQuantumInput.value = String(scenario.config.rrQuantum);
    }
    if (Array.isArray(scenario.config.processes)) {
      state.processConfig = scenario.config.processes.map((p) => ({
        pid: p.pid,
        file: p.file,
        arrival: p.arrival,
        inputMap: p.inputMap || {},
      }));
      renderProcessConfigurator();
    }
    // Auto-run scenario
    runSimulation();
  });

  els.runSimulationBtn.addEventListener("click", () => {
    runSimulation();
  });

  els.compareBtn.addEventListener("click", compareSchedulers);
  els.closeModalBtn.addEventListener("click", () => {
    els.comparisonModal.style.display = "none";
  });
  window.addEventListener("click", (e) => {
    if (e.target === els.comparisonModal) {
      els.comparisonModal.style.display = "none";
    }
  });

  els.stepSlider.addEventListener("input", (event) => {
    if (!state.latestResponse) return;
    state.selectedSnapshotIndex = Number(event.target.value);
    renderSnapshot(state.latestResponse);
  });

  els.prevStepBtn.addEventListener("click", () => {
    if (!state.latestResponse) return;
    state.selectedSnapshotIndex = Math.max(0, state.selectedSnapshotIndex - 1);
    renderSnapshot(state.latestResponse);
  });

  els.nextStepBtn.addEventListener("click", () => {
    if (!state.latestResponse) return;
    state.selectedSnapshotIndex = Math.min(
      state.latestResponse.snapshots.length - 1,
      state.selectedSnapshotIndex + 1,
    );
    renderSnapshot(state.latestResponse);
  });

  renderFilterButtons();
}

async function init() {
  bindUi();
  const catalog = await fetchJson("/api/programs");
  state.programCatalog = catalog;
  renderSchedulerOptions(catalog);
  els.rrQuantumInput.value = String(catalog.defaultRRQuantum || 2);

  try {
    const scenariosData = await fetchJson("/api/scenarios");
    state.scenariosCatalog = scenariosData.scenarios || [];
    renderScenariosDropdown(state.scenariosCatalog);
  } catch (err) {
    console.warn("Failed to load scenarios:", err.message);
  }

  resetProcessConfig();
  // Auto-run default simulation on start
  await runSimulation();

  const urlParams = new URLSearchParams(window.location.search);
  const stepParam = urlParams.get("step");
  if (stepParam !== null && state.latestResponse) {
    state.selectedSnapshotIndex = parseInt(stepParam, 10);
    renderSnapshot(state.latestResponse);
  }
  if (urlParams.get("benchmark") === "1") {
    await compareSchedulers();
  }
}

init().catch((error) => {
  console.error("Initialization error:", error);
});
