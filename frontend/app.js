// Minimal frontend for OS simulator demo
function getDemoFiles() {
  return {
    a: "dashboard_output.txt",
    b: "Saved from simulator",
    c: "syscall_test.txt",
  };
}

document.addEventListener("DOMContentLoaded", () => {
  const out = document.getElementById("output");
  if (!out) return;
  const files = getDemoFiles();
  out.innerHTML = `
        <h2>Simulator Demo Files</h2>
        <ul>
            <li>${files.a}</li>
            <li>${files.b}</li>
            <li>${files.c}</li>
        </ul>
    `;
});
