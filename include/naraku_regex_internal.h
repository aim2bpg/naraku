/**
 * @file naraku_regex_internal.h
 */

#ifndef NARAKU_REGEX_INTERNAL_H
#define NARAKU_REGEX_INTERNAL_H

#include <naraku_regex.h>

// ==========================================================================
//
// src/regex_vm.c
//
// ==========================================================================

/**
 * Whether the consuming state `state` matches `code` (handles `/i` case
 * folding for `NK_VM_OP_CODE` per `state->fold_flags`). `code` must not be
 * `VM_NO_CHAR`.
 *
 * Shared with src/regex_compile.c so that compile-time tables derived from
 * "which bytes does this state match" (e.g. the first-byte jump table) stay
 * in sync with the actual runtime matching logic instead of re-implementing
 * a parallel (and possibly diverging) fold rule.
 */
bool state_matches_code(const nk_program_t* program, const nk_vm_state_t* state, uint32_t code);

#endif  // NARAKU_REGEX_INTERNAL_H
