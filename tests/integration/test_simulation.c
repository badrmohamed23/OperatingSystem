/* tests/integration/test_simulation.c – Integration test for complete simulations */
#include <assert.h>
#include <stdio.h>
#include "os_shared.h"

int main(void)
{
    printf("Running integration tests for Simulation Engine...\n");

    /* 1. Test RR Simulation with default programs */
    SimulationConfig config;
    memset(&config, 0, sizeof(config));
    config.algorithm = SCHED_RR;
    config.rr_quantum = 2;
    config.max_steps = 2000;
    snprintf(config.programs_dir, sizeof(config.programs_dir), "programs");

    config.process_count = 3;
    config.processes[0].pid = 1;
    snprintf(config.processes[0].file, sizeof(config.processes[0].file), "Program 1.txt");
    config.processes[0].arrival_time = 0;

    config.processes[1].pid = 2;
    snprintf(config.processes[1].file, sizeof(config.processes[1].file), "Program_2.txt");
    config.processes[1].arrival_time = 1;

    config.processes[2].pid = 3;
    snprintf(config.processes[2].file, sizeof(config.processes[2].file), "Program_3.txt");
    config.processes[2].arrival_time = 4;

    Simulation *sim_rr = simulation_create(&config);
    assert(sim_rr != NULL);
    simulation_run(sim_rr);
    assert(simulation_get_time(sim_rr) > 0);
    simulation_destroy(sim_rr);
    printf("  ✓ Round Robin integration test passed\n");

    /* 2. Test HRRN Simulation */
    config.algorithm = SCHED_HRRN;
    Simulation *sim_hrrn = simulation_create(&config);
    assert(sim_hrrn != NULL);
    simulation_run(sim_hrrn);
    assert(simulation_get_time(sim_hrrn) > 0);
    simulation_destroy(sim_hrrn);
    printf("  ✓ HRRN integration test passed\n");

    /* 3. Test MLFQ Simulation */
    config.algorithm = SCHED_MLFQ;
    Simulation *sim_mlfq = simulation_create(&config);
    assert(sim_mlfq != NULL);
    simulation_run(sim_mlfq);
    assert(simulation_get_time(sim_mlfq) > 0);
    simulation_destroy(sim_mlfq);
    printf("  ✓ MLFQ integration test passed\n");

    printf("✓ All integration tests passed successfully!\n");
    return 0;
}

