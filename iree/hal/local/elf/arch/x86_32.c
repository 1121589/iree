// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "iree/base/target_platform.h"
#include "iree/hal/local/elf/arch.h"

#if defined(IREE_ARCH_X86_32)

// Documentation:
// https://uclibc.org/docs/psABI-i386.pdf

//==============================================================================
// ELF machine type/ABI
//==============================================================================

bool iree_elf_arch_is_valid(const iree_elf_ehdr_t* ehdr) {
  return ehdr->e_machine == 0x03;  // EM_386 / 3
}

//==============================================================================
// ELF relocations
//==============================================================================

enum {
  IREE_ELF_R_386_NONE = 0,
  IREE_ELF_R_386_32 = 1,
  IREE_ELF_R_386_PC32 = 2,
  IREE_ELF_R_386_GLOB_DAT = 6,
  IREE_ELF_R_386_JMP_SLOT = 7,
  IREE_ELF_R_386_RELATIVE = 8,
};

static iree_status_t iree_elf_arch_x86_32_apply_rel(
    iree_elf_relocation_state_t* state, iree_host_size_t rel_count,
    const iree_elf_rel_t* rel_table) {
  for (iree_host_size_t i = 0; i < rel_count; ++i) {
    const iree_elf_rel_t* rel = &rel_table[i];
    uint32_t type = IREE_ELF_R_TYPE(rel->r_info);
    if (type == IREE_ELF_R_386_NONE) continue;

    // TODO(benvanik): support imports by resolving from the import table.
    iree_elf_addr_t sym_addr = 0;
    if (IREE_ELF_R_SYM(rel->r_info) != 0) {
      return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                              "symbol-relative relocations not implemented");
    }

    iree_elf_addr_t instr_ptr =
        (iree_elf_addr_t)state->vaddr_bias + rel->r_offset;
    switch (type) {
        // case IREE_ELF_R_386_NONE: early-exit above
      case IREE_ELF_R_386_JMP_SLOT:
        *(uint32_t*)instr_ptr = (uint32_t)sym_addr;
        break;
      case IREE_ELF_R_386_GLOB_DAT:
        *(uint32_t*)instr_ptr = (uint32_t)sym_addr;
        break;
      case IREE_ELF_R_386_RELATIVE:
        *(uint32_t*)instr_ptr += (uint32_t)state->vaddr_bias;
        break;
      case IREE_ELF_R_386_32:
        *(uint32_t*)instr_ptr += (uint32_t)sym_addr;
        break;
      case IREE_ELF_R_386_PC32:
        *(uint32_t*)instr_ptr += (uint32_t)(sym_addr - instr_ptr);
        break;
      default:
        return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                                "unimplemented x86 relocation type %08X", type);
    }
  }
  return iree_ok_status();
}

iree_status_t iree_elf_arch_apply_relocations(
    iree_elf_relocation_state_t* state) {
  // Gather the relevant relocation tables.
  iree_host_size_t rel_count = 0;
  const iree_elf_rel_t* rel_table = NULL;
  for (iree_host_size_t i = 0; i < state->dyn_table_count; ++i) {
    const iree_elf_dyn_t* dyn = &state->dyn_table[i];
    switch (dyn->d_tag) {
      case IREE_ELF_DT_REL:
        rel_table =
            (const iree_elf_rel_t*)(state->vaddr_bias + dyn->d_un.d_ptr);
        break;
      case IREE_ELF_DT_RELSZ:
        rel_count = dyn->d_un.d_val / sizeof(iree_elf_rel_t);
        break;

      case IREE_ELF_DT_RELA:
      case IREE_ELF_DT_RELASZ:
        return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                                "unsupported DT_RELA relocations");
      default:
        // Ignored.
        break;
    }
  }
  if (!rel_table) rel_count = 0;

  if (rel_count > 0) {
    IREE_RETURN_IF_ERROR(
        iree_elf_arch_x86_32_apply_rel(state, rel_count, rel_table));
  }

  return iree_ok_status();
}

//==============================================================================
// Cross-ABI function calls
//==============================================================================

// System V 386 ABI (used in IREE):
// https://uclibc.org/docs/psABI-i386.pdf
// Arguments:
//
//
// Results:
//   EAX
//
// Non-volatile:
//   EBX, ESP, EBP, ESI, EDI
//
// Everything but Windows uses this convention (linux/bsd/mac/etc) and as such
// we can just use nice little C thunks.

#if defined(IREE_PLATFORM_WINDOWS)

#error "TODO"

#else

void iree_elf_call_v_v(const void* symbol_ptr) {
  typedef void (*ptr_t)(void);
  ((ptr_t)symbol_ptr)();
}

void* iree_elf_call_p_i(const void* symbol_ptr, int a0) {
  typedef void* (*ptr_t)(int);
  return ((ptr_t)symbol_ptr)(a0);
}

void* iree_elf_call_p_ip(const void* symbol_ptr, int a0, void* a1) {
  typedef void* (*ptr_t)(int, void*);
  return ((ptr_t)symbol_ptr)(a0, a1);
}

int iree_elf_call_i_pp(const void* symbol_ptr, void* a0, void* a1) {
  typedef int (*ptr_t)(void*, void*);
  return ((ptr_t)symbol_ptr)(a0, a1);
}

#endif  // IREE_PLATFORM_WINDOWS

#endif  // IREE_ARCH_X86_32
