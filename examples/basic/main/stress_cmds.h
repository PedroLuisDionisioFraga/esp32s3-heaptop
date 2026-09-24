/**
 * @file stress_cmds.h
 * @brief `stress` console commands: create leaks, fragmentation, CPU load and
 *        allocation failures so every heaptop view has something to show.
 */

#ifndef STRESS_CMDS_H
#define STRESS_CMDS_H

#ifdef __cplusplus
extern "C"
{
#endif

/** Register the `stress` command. Call after esp_console_init(). */
void register_stress_commands(void);

#ifdef __cplusplus
}
#endif

#endif  // STRESS_CMDS_H
