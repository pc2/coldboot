/**\file */
#ifndef SLIC_DECLARATIONS_LocExtHWFixEVTOpt_H
#define SLIC_DECLARATIONS_LocExtHWFixEVTOpt_H
#include "MaxSLiCInterface.h"
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/*----------------------------------------------------------------------------*/
/*--------------------------- Interface pollResult ---------------------------*/
/*----------------------------------------------------------------------------*/




/**
 * \brief Basic static function for the interface 'pollResult'.
 * 
 * \param [in] param_pollID Interface Parameter "pollID".
 * \param [out] outstream_output0 The stream should be of size 192 bytes.
 */
void LocExtHWFixEVTOpt_pollResult(
	uint64_t param_pollID,
	uint8_t *outstream_output0);

/**
 * \brief Basic static non-blocking function for the interface 'pollResult'.
 * 
 * Schedule to run on an engine and return immediately.
 * The status of the run can be checked either by ::max_wait or ::max_nowait;
 * note that one of these *must* be called, so that associated memory can be released.
 * 
 * 
 * \param [in] param_pollID Interface Parameter "pollID".
 * \param [out] outstream_output0 The stream should be of size 192 bytes.
 * \return A handle on the execution status, or NULL in case of error.
 */
max_run_t *LocExtHWFixEVTOpt_pollResult_nonblock(
	uint64_t param_pollID,
	uint8_t *outstream_output0);

/**
 * \brief Advanced static interface, structure for the engine interface 'pollResult'
 * 
 */
typedef struct { 
	uint64_t param_pollID; /**<  [in] Interface Parameter "pollID". */
	uint8_t *outstream_output0; /**<  [out] The stream should be of size 192 bytes. */
} LocExtHWFixEVTOpt_pollResult_actions_t;

/**
 * \brief Advanced static function for the interface 'pollResult'.
 * 
 * \param [in] engine The engine on which the actions will be executed.
 * \param [in,out] interface_actions Actions to be executed.
 */
void LocExtHWFixEVTOpt_pollResult_run(
	max_engine_t *engine,
	LocExtHWFixEVTOpt_pollResult_actions_t *interface_actions);

/**
 * \brief Advanced static non-blocking function for the interface 'pollResult'.
 *
 * Schedule the actions to run on the engine and return immediately.
 * The status of the run can be checked either by ::max_wait or ::max_nowait;
 * note that one of these *must* be called, so that associated memory can be released.
 *
 * 
 * \param [in] engine The engine on which the actions will be executed.
 * \param [in] interface_actions Actions to be executed.
 * \return A handle on the execution status of the actions, or NULL in case of error.
 */
max_run_t *LocExtHWFixEVTOpt_pollResult_run_nonblock(
	max_engine_t *engine,
	LocExtHWFixEVTOpt_pollResult_actions_t *interface_actions);

/**
 * \brief Group run advanced static function for the interface 'pollResult'.
 * 
 * \param [in] group Group to use.
 * \param [in,out] interface_actions Actions to run.
 *
 * Run the actions on the first device available in the group.
 */
void LocExtHWFixEVTOpt_pollResult_run_group(max_group_t *group, LocExtHWFixEVTOpt_pollResult_actions_t *interface_actions);

/**
 * \brief Group run advanced static non-blocking function for the interface 'pollResult'.
 * 
 *
 * Schedule the actions to run on the first device available in the group and return immediately.
 * The status of the run must be checked with ::max_wait. 
 * Note that use of ::max_nowait is prohibited with non-blocking running on groups:
 * see the ::max_run_group_nonblock documentation for more explanation.
 *
 * \param [in] group Group to use.
 * \param [in] interface_actions Actions to run.
 * \return A handle on the execution status of the actions, or NULL in case of error.
 */
max_run_t *LocExtHWFixEVTOpt_pollResult_run_group_nonblock(max_group_t *group, LocExtHWFixEVTOpt_pollResult_actions_t *interface_actions);

/**
 * \brief Array run advanced static function for the interface 'pollResult'.
 * 
 * \param [in] engarray The array of devices to use.
 * \param [in,out] interface_actions The array of actions to run.
 *
 * Run the array of actions on the array of engines.  The length of interface_actions
 * must match the size of engarray.
 */
void LocExtHWFixEVTOpt_pollResult_run_array(max_engarray_t *engarray, LocExtHWFixEVTOpt_pollResult_actions_t *interface_actions[]);

/**
 * \brief Array run advanced static non-blocking function for the interface 'pollResult'.
 * 
 *
 * Schedule to run the array of actions on the array of engines, and return immediately.
 * The length of interface_actions must match the size of engarray.
 * The status of the run can be checked either by ::max_wait or ::max_nowait;
 * note that one of these *must* be called, so that associated memory can be released.
 *
 * \param [in] engarray The array of devices to use.
 * \param [in] interface_actions The array of actions to run.
 * \return A handle on the execution status of the actions, or NULL in case of error.
 */
max_run_t *LocExtHWFixEVTOpt_pollResult_run_array_nonblock(max_engarray_t *engarray, LocExtHWFixEVTOpt_pollResult_actions_t *interface_actions[]);

/**
 * \brief Converts a static-interface action struct into a dynamic-interface max_actions_t struct.
 *
 * Note that this is an internal utility function used by other functions in the static interface.
 *
 * \param [in] maxfile The maxfile to use.
 * \param [in] interface_actions The interface-specific actions to run.
 * \return The dynamic-interface actions to run, or NULL in case of error.
 */
max_actions_t* LocExtHWFixEVTOpt_pollResult_convert(max_file_t *maxfile, LocExtHWFixEVTOpt_pollResult_actions_t *interface_actions);



/*----------------------------------------------------------------------------*/
/*---------------------------- Interface default -----------------------------*/
/*----------------------------------------------------------------------------*/




/**
 * \brief Basic static function for the interface 'default'.
 * 
 * \param [in] param_N Interface Parameter "N".
 * \param [in] inscalar_AESFixEVTSM0_expected_n0 Input scalar parameter "AESFixEVTSM0.expected_n0".
 * \param [in] inscalar_AESFixEVTSM0_expected_n1 Input scalar parameter "AESFixEVTSM0.expected_n1".
 * \param [in] instream_input0 The stream should be of size (param_N * 1) bytes.
 * \param [out] outstream_output0 The stream should be of size 192 bytes.
 * \param [in] inmem_AESFixEVTSM0_betterGuess0 Mapped ROM inmem_AESFixEVTSM0_betterGuess0, should be of size (4096 * sizeof(uint64_t)).
 * \param [in] inmem_AESFixEVTSM0_betterGuess1 Mapped ROM inmem_AESFixEVTSM0_betterGuess1, should be of size (4096 * sizeof(uint64_t)).
 * \param [in] inmem_AESFixEVTSM0_betterGuess2 Mapped ROM inmem_AESFixEVTSM0_betterGuess2, should be of size (4096 * sizeof(uint64_t)).
 * \param [in] inmem_AESFixEVTSM0_betterGuess3 Mapped ROM inmem_AESFixEVTSM0_betterGuess3, should be of size (4096 * sizeof(uint64_t)).
 */
void LocExtHWFixEVTOpt(
	int64_t param_N,
	uint64_t inscalar_AESFixEVTSM0_expected_n0,
	uint64_t inscalar_AESFixEVTSM0_expected_n1,
	const uint8_t *instream_input0,
	uint8_t *outstream_output0,
	const uint64_t *inmem_AESFixEVTSM0_betterGuess0,
	const uint64_t *inmem_AESFixEVTSM0_betterGuess1,
	const uint64_t *inmem_AESFixEVTSM0_betterGuess2,
	const uint64_t *inmem_AESFixEVTSM0_betterGuess3);

/**
 * \brief Basic static non-blocking function for the interface 'default'.
 * 
 * Schedule to run on an engine and return immediately.
 * The status of the run can be checked either by ::max_wait or ::max_nowait;
 * note that one of these *must* be called, so that associated memory can be released.
 * 
 * 
 * \param [in] param_N Interface Parameter "N".
 * \param [in] inscalar_AESFixEVTSM0_expected_n0 Input scalar parameter "AESFixEVTSM0.expected_n0".
 * \param [in] inscalar_AESFixEVTSM0_expected_n1 Input scalar parameter "AESFixEVTSM0.expected_n1".
 * \param [in] instream_input0 The stream should be of size (param_N * 1) bytes.
 * \param [out] outstream_output0 The stream should be of size 192 bytes.
 * \param [in] inmem_AESFixEVTSM0_betterGuess0 Mapped ROM inmem_AESFixEVTSM0_betterGuess0, should be of size (4096 * sizeof(uint64_t)).
 * \param [in] inmem_AESFixEVTSM0_betterGuess1 Mapped ROM inmem_AESFixEVTSM0_betterGuess1, should be of size (4096 * sizeof(uint64_t)).
 * \param [in] inmem_AESFixEVTSM0_betterGuess2 Mapped ROM inmem_AESFixEVTSM0_betterGuess2, should be of size (4096 * sizeof(uint64_t)).
 * \param [in] inmem_AESFixEVTSM0_betterGuess3 Mapped ROM inmem_AESFixEVTSM0_betterGuess3, should be of size (4096 * sizeof(uint64_t)).
 * \return A handle on the execution status, or NULL in case of error.
 */
max_run_t *LocExtHWFixEVTOpt_nonblock(
	int64_t param_N,
	uint64_t inscalar_AESFixEVTSM0_expected_n0,
	uint64_t inscalar_AESFixEVTSM0_expected_n1,
	const uint8_t *instream_input0,
	uint8_t *outstream_output0,
	const uint64_t *inmem_AESFixEVTSM0_betterGuess0,
	const uint64_t *inmem_AESFixEVTSM0_betterGuess1,
	const uint64_t *inmem_AESFixEVTSM0_betterGuess2,
	const uint64_t *inmem_AESFixEVTSM0_betterGuess3);

/**
 * \brief Advanced static interface, structure for the engine interface 'default'
 * 
 */
typedef struct { 
	int64_t param_N; /**<  [in] Interface Parameter "N". */
	uint64_t inscalar_AESFixEVTSM0_expected_n0; /**<  [in] Input scalar parameter "AESFixEVTSM0.expected_n0". */
	uint64_t inscalar_AESFixEVTSM0_expected_n1; /**<  [in] Input scalar parameter "AESFixEVTSM0.expected_n1". */
	const uint8_t *instream_input0; /**<  [in] The stream should be of size (param_N * 1) bytes. */
	uint8_t *outstream_output0; /**<  [out] The stream should be of size 192 bytes. */
	const uint64_t *inmem_AESFixEVTSM0_betterGuess0; /**<  [in] Mapped ROM inmem_AESFixEVTSM0_betterGuess0, should be of size (4096 * sizeof(uint64_t)). */
	const uint64_t *inmem_AESFixEVTSM0_betterGuess1; /**<  [in] Mapped ROM inmem_AESFixEVTSM0_betterGuess1, should be of size (4096 * sizeof(uint64_t)). */
	const uint64_t *inmem_AESFixEVTSM0_betterGuess2; /**<  [in] Mapped ROM inmem_AESFixEVTSM0_betterGuess2, should be of size (4096 * sizeof(uint64_t)). */
	const uint64_t *inmem_AESFixEVTSM0_betterGuess3; /**<  [in] Mapped ROM inmem_AESFixEVTSM0_betterGuess3, should be of size (4096 * sizeof(uint64_t)). */
} LocExtHWFixEVTOpt_actions_t;

/**
 * \brief Advanced static function for the interface 'default'.
 * 
 * \param [in] engine The engine on which the actions will be executed.
 * \param [in,out] interface_actions Actions to be executed.
 */
void LocExtHWFixEVTOpt_run(
	max_engine_t *engine,
	LocExtHWFixEVTOpt_actions_t *interface_actions);

/**
 * \brief Advanced static non-blocking function for the interface 'default'.
 *
 * Schedule the actions to run on the engine and return immediately.
 * The status of the run can be checked either by ::max_wait or ::max_nowait;
 * note that one of these *must* be called, so that associated memory can be released.
 *
 * 
 * \param [in] engine The engine on which the actions will be executed.
 * \param [in] interface_actions Actions to be executed.
 * \return A handle on the execution status of the actions, or NULL in case of error.
 */
max_run_t *LocExtHWFixEVTOpt_run_nonblock(
	max_engine_t *engine,
	LocExtHWFixEVTOpt_actions_t *interface_actions);

/**
 * \brief Group run advanced static function for the interface 'default'.
 * 
 * \param [in] group Group to use.
 * \param [in,out] interface_actions Actions to run.
 *
 * Run the actions on the first device available in the group.
 */
void LocExtHWFixEVTOpt_run_group(max_group_t *group, LocExtHWFixEVTOpt_actions_t *interface_actions);

/**
 * \brief Group run advanced static non-blocking function for the interface 'default'.
 * 
 *
 * Schedule the actions to run on the first device available in the group and return immediately.
 * The status of the run must be checked with ::max_wait. 
 * Note that use of ::max_nowait is prohibited with non-blocking running on groups:
 * see the ::max_run_group_nonblock documentation for more explanation.
 *
 * \param [in] group Group to use.
 * \param [in] interface_actions Actions to run.
 * \return A handle on the execution status of the actions, or NULL in case of error.
 */
max_run_t *LocExtHWFixEVTOpt_run_group_nonblock(max_group_t *group, LocExtHWFixEVTOpt_actions_t *interface_actions);

/**
 * \brief Array run advanced static function for the interface 'default'.
 * 
 * \param [in] engarray The array of devices to use.
 * \param [in,out] interface_actions The array of actions to run.
 *
 * Run the array of actions on the array of engines.  The length of interface_actions
 * must match the size of engarray.
 */
void LocExtHWFixEVTOpt_run_array(max_engarray_t *engarray, LocExtHWFixEVTOpt_actions_t *interface_actions[]);

/**
 * \brief Array run advanced static non-blocking function for the interface 'default'.
 * 
 *
 * Schedule to run the array of actions on the array of engines, and return immediately.
 * The length of interface_actions must match the size of engarray.
 * The status of the run can be checked either by ::max_wait or ::max_nowait;
 * note that one of these *must* be called, so that associated memory can be released.
 *
 * \param [in] engarray The array of devices to use.
 * \param [in] interface_actions The array of actions to run.
 * \return A handle on the execution status of the actions, or NULL in case of error.
 */
max_run_t *LocExtHWFixEVTOpt_run_array_nonblock(max_engarray_t *engarray, LocExtHWFixEVTOpt_actions_t *interface_actions[]);

/**
 * \brief Converts a static-interface action struct into a dynamic-interface max_actions_t struct.
 *
 * Note that this is an internal utility function used by other functions in the static interface.
 *
 * \param [in] maxfile The maxfile to use.
 * \param [in] interface_actions The interface-specific actions to run.
 * \return The dynamic-interface actions to run, or NULL in case of error.
 */
max_actions_t* LocExtHWFixEVTOpt_convert(max_file_t *maxfile, LocExtHWFixEVTOpt_actions_t *interface_actions);

/**
 * \brief Initialise a maxfile.
 */
max_file_t* LocExtHWFixEVTOpt_init(void);

/* Error handling functions */
int LocExtHWFixEVTOpt_has_errors(void);
const char* LocExtHWFixEVTOpt_get_errors(void);
void LocExtHWFixEVTOpt_clear_errors(void);
/* Free statically allocated maxfile data */
void LocExtHWFixEVTOpt_free(void);
/* These are dummy functions for hardware builds. */
int LocExtHWFixEVTOpt_simulator_start(void);
int LocExtHWFixEVTOpt_simulator_stop(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* SLIC_DECLARATIONS_LocExtHWFixEVTOpt_H */

