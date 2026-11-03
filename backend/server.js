const express = require("express");
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const app = express();
const PROJECT_ROOT = path.join(__dirname, "..");
const PORT = process.env.PORT || 3000;
const PROGRAMS_DIR = path.join(PROJECT_ROOT, "programs");
const EXAMPLES_DIR = path.join(PROJECT_ROOT, "examples");
const C_BINARY = path.join(PROJECT_ROOT, ".build", "os_sim");

const DEFAULT_PROGRAMS = [
  { pid: 1, file: "Program 1.txt", arrival: 0, inputMap: { x: "2", y: "7" } },
  { pid: 2, file: "Program_2.txt", arrival: 1, inputMap: { a: "dashboard_output.txt", b: "Saved from simulator" } },
  { pid: 3, file: "Program_3.txt", arrival: 4, inputMap: { a: "dashboard_output.txt" } },
];

app.use(express.json({ limit: "2mb" }));
app.use(express.static(path.join(PROJECT_ROOT, "frontend")));

function ensureBinary() {
  if (!fs.existsSync(C_BINARY)) {
    console.log("[Server] Compiling C simulation engine...");
    execFileSync("make", ["build"], {
      cwd: PROJECT_ROOT,
      stdio: "inherit",
    });
  }
}

function readProgramInstructions(file) {
  const fullPath = path.join(PROGRAMS_DIR, file);
  if (!fs.existsSync(fullPath)) return [];
  const raw = fs.readFileSync(fullPath, "utf8");
  return raw
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0);
}

// GET /api/health
app.get("/api/health", (req, res) => {
  try {
    ensureBinary();
    res.json({
      ok: true,
      service: "os-simulator-backend",
      cEngineAvailable: fs.existsSync(C_BINARY),
    });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message });
  }
});

// GET /api/algorithms
app.get("/api/algorithms", (req, res) => {
  res.json({
    schedulers: ["HRRN", "RR", "MLFQ"],
    defaultRRQuantum: 2,
    descriptions: {
      HRRN: "Highest Response Ratio Next — Non-preemptive scheduling policy selecting the ready process with max (W + S) / S.",
      RR: "Round Robin — Preemptive scheduling with configurable time slice and FIFO rotation.",
      MLFQ: "Multi-Level Feedback Queue — 4 priority levels (Q0-Q3) with 2^i quantums and automatic priority demotion.",
    },
  });
});

// GET /api/programs
app.get("/api/programs", (req, res) => {
  try {
    const files = fs
      .readdirSync(PROGRAMS_DIR)
      .filter((file) => file.endsWith(".txt"));

    const programs = files.map((file, idx) => {
      const instructions = readProgramInstructions(file);
      const defaultMatch = DEFAULT_PROGRAMS.find((p) => p.file === file);
      return {
        pid: defaultMatch ? defaultMatch.pid : idx + 1,
        file,
        arrival: defaultMatch ? defaultMatch.arrival : 0,
        inputMap: defaultMatch ? defaultMatch.inputMap : {},
        instructions,
        instructionCount: instructions.length,
      };
    });

    res.json({
      schedulers: ["HRRN", "RR", "MLFQ"],
      defaultRRQuantum: 2,
      programs,
    });
  } catch (err) {
    res.status(500).json({ error: "Failed to list program files", details: err.message });
  }
});

// GET /api/scenarios
app.get("/api/scenarios", (req, res) => {
  try {
    if (!fs.existsSync(EXAMPLES_DIR)) {
      return res.json({ scenarios: [] });
    }
    const files = fs.readdirSync(EXAMPLES_DIR).filter((f) => f.endsWith(".json"));
    const scenarios = files.map((file) => {
      const raw = fs.readFileSync(path.join(EXAMPLES_DIR, file), "utf8");
      const data = JSON.parse(raw);
      return {
        id: file.replace(".json", ""),
        file,
        ...data,
      };
    });
    res.json({ scenarios });
  } catch (err) {
    res.status(500).json({ error: "Failed to load scenarios", details: err.message });
  }
});

// POST /api/simulate — invokes the native C engine
app.post("/api/simulate", (req, res) => {
  try {
    ensureBinary();

    const body = req.body || {};
    const scheduler = String(body.scheduler || "HRRN").toUpperCase();
    if (!["HRRN", "RR", "MLFQ"].includes(scheduler)) {
      return res.status(400).json({ error: "Invalid scheduler. Must be HRRN, RR, or MLFQ." });
    }

    const requested =
      Array.isArray(body.processes) && body.processes.length > 0
        ? body.processes
        : DEFAULT_PROGRAMS;

    const processes = requested.map((item, idx) => ({
      pid: Number(item.pid) || idx + 1,
      file: String(item.file || "").trim(),
      arrival: Number.isInteger(item.arrival)
        ? item.arrival
        : Number(item.arrival) || 0,
      inputMap:
        item.inputMap && typeof item.inputMap === "object" ? item.inputMap : {},
    }));

    for (const proc of processes) {
      if (!proc.file) {
        return res.status(400).json({ error: `Process ${proc.pid} missing program filename` });
      }
      const fullPath = path.join(PROGRAMS_DIR, proc.file);
      if (!fs.existsSync(fullPath)) {
        return res.status(400).json({ error: `Program file not found: ${proc.file}` });
      }
    }

    const cPayload = {
      scheduler,
      rrQuantum: Number(body.rrQuantum) > 0 ? Number(body.rrQuantum) : 2,
      programs: PROGRAMS_DIR,
      sandbox: path.join(PROJECT_ROOT, "sandbox"),
      processes,
    };

    const stdout = execFileSync(C_BINARY, ["--json"], {
      cwd: PROJECT_ROOT,
      input: JSON.stringify(cPayload),
      encoding: "utf8",
      maxBuffer: 32 * 1024 * 1024,
    });

    const result = JSON.parse(stdout);
    res.json(result);
  } catch (error) {
    console.error("[Simulate Error]", error.message);
    res.status(500).json({
      error: "Simulation execution failed",
      details: error.message,
    });
  }
});

app.get("*", (req, res) => {
  res.sendFile(path.join(PROJECT_ROOT, "frontend", "index.html"));
});

app.listen(PORT, () => {
  console.log(`OS Simulator Dashboard running at http://localhost:${PORT}`);
  try {
    ensureBinary();
    console.log(`Native C engine ready at ${C_BINARY}`);
  } catch (err) {
    console.warn(`Warning: failed to compile C engine on startup: ${err.message}`);
  }
});
