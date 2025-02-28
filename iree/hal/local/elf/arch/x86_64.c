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

#if defined(IREE_ARCH_X86_64)

// Documentation:
// https://github.com/hjl-tools/x86-psABI/wiki/x86-64-psABI-1.0.pdf

//==============================================================================
// ELF machine type/ABI
//==============================================================================

bool iree_elf_arch_is_valid(const iree_elf_ehdr_t* ehdr) {
  return ehdr->e_machine == 0x3E;  // EM_X86_64 / 62
}

//==============================================================================
// ELF relocations
//==============================================================================

enum {
  IREE_ELF_R_X86_64_NONE = 0,       // No reloc
  IREE_ELF_R_X86_64_64 = 1,         // Direct 64 bit
  IREE_ELF_R_X86_64_PC32 = 2,       // PC relative 32 bit signed
  IREE_ELF_R_X86_64_GOT32 = 3,      // 32 bit GOT entry
  IREE_ELF_R_X86_64_PLT32 = 4,      // 32 bit PLT address
  IREE_ELF_R_X86_64_COPY = 5,       // Copy symbol at runtime
  IREE_ELF_R_X86_64_GLOB_DAT = 6,   // Create GOT entry
  IREE_ELF_R_X86_64_JUMP_SLOT = 7,  // Create PLT entry
  IREE_ELF_R_X86_64_RELATIVE = 8,   // Adjust by program base
  IREE_ELF_R_X86_64_GOTPCREL = 9,   // 32 bit signed pc relative offset to GOT
  IREE_ELF_R_X86_64_32 = 10,        // Direct 32 bit zero extended
  IREE_ELF_R_X86_64_32S = 11,       // Direct 32 bit sign extended
  IREE_ELF_R_X86_64_16 = 12,        // Direct 16 bit zero extended
  IREE_ELF_R_X86_64_PC16 = 13,      // 16 bit sign extended pc relative
  IREE_ELF_R_X86_64_8 = 14,         // Direct 8 bit sign extended
  IREE_ELF_R_X86_64_PC8 = 15,       // 8 bit sign extended pc relative
  IREE_ELF_R_X86_64_PC64 = 24,      // Place relative 64-bit signed
};

static iree_status_t iree_elf_arch_x86_64_apply_rela(
    iree_elf_relocation_state_t* state, iree_host_size_t rela_count,
    const iree_elf_rela_t* rela_table) {
  for (iree_host_size_t i = 0; i < rela_count; ++i) {
    const iree_elf_rela_t* rela = &rela_table[i];
    uint32_t type = IREE_ELF_R_TYPE(rela->r_info);
    if (type == IREE_ELF_R_X86_64_NONE) continue;

    // TODO(benvanik): support imports by resolving from the import table.
    iree_elf_addr_t sym_addr = 0;
    if (IREE_ELF_R_SYM(rela->r_info) != 0) {
      return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                              "symbol-relative relocations not implemented");
    }

    iree_elf_addr_t instr_ptr =
        (iree_elf_addr_t)state->vaddr_bias + rela->r_offset;
    switch (type) {
      // case IREE_ELF_R_X86_64_NONE: early-exit above
      case IREE_ELF_R_X86_64_RELATIVE:
        *(uint64_t*)instr_ptr = (uint64_t)(state->vaddr_bias + rela->r_addend);
        break;
      case IREE_ELF_R_X86_64_JUMP_SLOT:
        *(uint64_t*)instr_ptr = (uint64_t)sym_addr;
        break;
      case IREE_ELF_R_X86_64_GLOB_DAT:
        *(uint64_t*)instr_ptr = (uint64_t)sym_addr;
        break;
      case IREE_ELF_R_X86_64_COPY:
        *(uint64_t*)instr_ptr = (uint64_t)sym_addr;
        break;
      case IREE_ELF_R_X86_64_64:
        *(uint64_t*)instr_ptr = (uint64_t)(sym_addr + rela->r_addend);
        break;
      case IREE_ELF_R_X86_64_32:
        *(uint32_t*)instr_ptr = (uint32_t)(sym_addr + rela->r_addend);
        break;
      case IREE_ELF_R_X86_64_32S:
        *(int32_t*)instr_ptr = (int32_t)(sym_addr + rela->r_addend);
        break;
      case IREE_ELF_R_X86_64_PC32:
        *(uint32_t*)instr_ptr =
            (uint32_t)(sym_addr + rela->r_addend - instr_ptr);
        break;
      default:
        return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                                "unimplemented x86_64 relocation type %08X",
                                type);
    }
  }
  return iree_ok_status();
}

iree_status_t iree_elf_arch_apply_relocations(
    iree_elf_relocation_state_t* state) {
  // Gather the relevant relocation tables.
  iree_host_size_t rela_count = 0;
  const iree_elf_rela_t* rela_table = NULL;
  iree_host_size_t plt_rela_count = 0;
  const iree_elf_rela_t* plt_rela_table = NULL;
  for (iree_host_size_t i = 0; i < state->dyn_table_count; ++i) {
    const iree_elf_dyn_t* dyn = &state->dyn_table[i];
    switch (dyn->d_tag) {
      case IREE_ELF_DT_RELA:
        rela_table =
            (const iree_elf_rela_t*)(state->vaddr_bias + dyn->d_un.d_ptr);
        break;
      case IREE_ELF_DT_RELASZ:
        rela_count = dyn->d_un.d_val / sizeof(iree_elf_rela_t);
        break;

      case IREE_ELF_DT_PLTREL:
        // Type of reloc in PLT; we expect DT_RELA right now.
        if (dyn->d_un.d_val != IREE_ELF_DT_RELA) {
          return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                                  "unsupported DT_PLTREL != DT_RELA");
        }
        break;
      case IREE_ELF_DT_JMPREL:
        plt_rela_table =
            (const iree_elf_rela_t*)(state->vaddr_bias + dyn->d_un.d_ptr);
        break;
      case IREE_ELF_DT_PLTRELSZ:
        plt_rela_count = dyn->d_un.d_val / sizeof(iree_elf_rela_t);
        break;

      case IREE_ELF_DT_REL:
      case IREE_ELF_DT_RELSZ:
        return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                                "unsupported DT_REL relocations");

      default:
        // Ignored.
        break;
    }
  }
  if (!rela_table) rela_count = 0;
  if (!plt_rela_table) plt_rela_count = 0;

  if (rela_count > 0) {
    IREE_RETURN_IF_ERROR(
        iree_elf_arch_x86_64_apply_rela(state, rela_count, rela_table));
  }
  if (plt_rela_count > 0) {
    IREE_RETURN_IF_ERROR(
        iree_elf_arch_x86_64_apply_rela(state, plt_rela_count, plt_rela_table));
  }

  return iree_ok_status();
}

//==============================================================================
// Cross-ABI function calls
//==============================================================================

// System V AMD64 ABI (used in IREE):
// https://github.com/hjl-tools/x86-psABI/wiki/x86-64-psABI-1.0.pdf
// Arguments:
//   RDI, RSI, RDX, RCX, R8, R9, [stack]...
// Results:
//   RAX, RDX
//
// Everything but Windows uses this convention (linux/bsd/mac/etc) and as such
// we can just use nice little C thunks.

#if defined(IREE_PLATFORM_WINDOWS)
// Host is using the Microsoft x64 calling convention and we need to translate
// to the System V AMD64 ABI conventions. Unfortunately MSVC does not support
// inline assembly and we have to outline the calls in x86_64_msvc.asm.
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

#endif  // IREE_ARCH_X86_64
