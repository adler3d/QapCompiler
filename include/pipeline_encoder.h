#pragma once

#include "pipeline_arch.h"
#include "pipeline_machine_ir.h"
#include <cstdint>
#include <vector>
#include <cassert>
#include <cstdio>

// ============================================================================
// ЭТАП 6: ENCODER - Генерация x86-64 машинного кода
// ============================================================================
// Корректная генерация REX префиксов (только когда необходимо!)
// - REX.W для 64-bit операций
// - REX.R для расширения регистра ModRM.reg (r/3)
// - REX.X для расширения SIB.index (i/3)
// - REX.B для расширения ModRM.r/m (b/3)

namespace encoder {

// ============================================================================
// CODE BUFFER
// ============================================================================

class CodeBuffer {
public:
  CodeBuffer() = default;
  
  void emit8(uint8_t byte) {
    code_.push_back(byte);
  }
  
  void emit16(uint16_t word) {
    emit8(static_cast<uint8_t>(word));
    emit8(static_cast<uint8_t>(word >> 8));
  }
  
  void emit32(uint32_t dword) {
    emit16(static_cast<uint16_t>(dword));
    emit16(static_cast<uint16_t>(dword >> 16));
  }
  
  void emit64(uint64_t qword) {
    emit32(static_cast<uint32_t>(qword));
    emit32(static_cast<uint32_t>(qword >> 32));
  }
  
  const std::vector<uint8_t>& getCode() const {
    return code_;
  }
  
  size_t getSize() const {
    return code_.size();
  }
  
  std::vector<uint8_t>& getMutableCode() {
    return code_;
  }
  
private:
  std::vector<uint8_t> code_;
};

// ============================================================================
// REX PREFIX GENERATION
// ============================================================================

class RexBuilder {
public:
  // Построить REX префикс
  // Возвращает 0 если префикс не нужен, иначе байт 0x40 + флаги
  static uint8_t build(bool w, bool r, bool x, bool b) {
    // Проверка: нужен ли REX вообще
    if (!w && !r && !x && !b) {
      return 0;  // Префикс не нужен
    }
    
    uint8_t rex = 0x40;
    if (w) rex |= 0x08;  // REX.W
    if (r) rex |= 0x04;  // REX.R
    if (x) rex |= 0x02;  // REX.X
    if (b) rex |= 0x01;  // REX.B
    
    return rex;
  }
  
  // Оптимизированная версия: только эмитирует, если нужен
  static void emitIfNeeded(CodeBuffer& buf, bool w, bool r, bool x, bool b) {
    uint8_t rex = build(w, r, x, b);
    if (rex != 0) {
      buf.emit8(rex);
    }
  }
};

// ============================================================================
// РЕГИСТРОВОЕ КОДИРОВАНИЕ
// ============================================================================

class RegisterEncoder {
public:
  // Получить 3-битный код регистра (0-7)
  // Для GPR: RAX=0, RCX=1, ..., R15=7 (с расширением через REX.B)
  // Для XMM: XMM0=0, XMM1=1, ..., XMM15=7 (с расширением через REX.B)
  static uint8_t regCode3bit(arch::RegId reg_id) {
    const auto& spec = arch::ArchRegistry::reg(reg_id);
    return spec.x86_code & 0x07;
  }
  
  // Проверить, нужен ли REX.R/B для этого регистра
  // REX.B нужен если регистр > 7 (т.е. R8+ для GPR или XMM8+ для XMM)
  static bool needsRexBit(arch::RegId reg_id) {
    const auto& spec = arch::ArchRegistry::reg(reg_id);
    return (spec.x86_code & 0x08) != 0;  // Если старший бит установлен
  }
  
  // Получить полный 4-битный код (включая расширение)
  static uint8_t regCode4bit(arch::RegId reg_id) {
    const auto& spec = arch::ArchRegistry::reg(reg_id);
    return spec.x86_code;
  }
};

// ============================================================================
// MODRM И SIB КОДИРОВАНИЕ
// ============================================================================

class ModRmBuilder {
public:
  // Построить ModRM байт для регистр-регистровой операции
  // mod=11 (3), reg=r (регистр в поле reg), r/m=m (регистр в поле r/m)
  static uint8_t buildRegReg(uint8_t reg_code, uint8_t rm_code) {
    return (3 << 6) | ((reg_code & 0x07) << 3) | (rm_code & 0x07);
  }
  
  // Построить ModRM байт для памяти с базовым регистром и смещением
  // Если disp == 0: mod=00
  // Если диапазон диsp [-128, 127]: mod=01
  // Иначе: mod=10 (32-bit диспл.)
  static uint8_t buildMemDisp(uint8_t reg_code, uint8_t base_code, int32_t disp,
                              uint8_t& mod_out) {
    uint8_t mod;
    if (disp == 0) {
      mod = 0;
    } else if (disp >= -128 && disp <= 127) {
      mod = 1;
    } else {
      mod = 2;
    }
    mod_out = mod;
    return (mod << 6) | ((reg_code & 0x07) << 3) | (base_code & 0x07);
  }
};

// ============================================================================
// X86-64 ENCODER
// ============================================================================

class X64Encoder {
public:
  X64Encoder() = default;
  
  void encode(const machine::MachineFunction& mfn, CodeBuffer& buf) {
    for (const auto& block : mfn.blocks) {
      for (const auto& inst : block.insts) {
        encodeInstruction(inst, buf);
      }
    }
  }
  
private:
  void encodeInstruction(const machine::MachineInst& inst, CodeBuffer& buf) {
    switch (inst.op) {
      case machine::MachineOp::Mov:
        encodeMov(inst, buf);
        break;
      case machine::MachineOp::Movsd:
        encodeMovsd(inst, buf);
        break;
      case machine::MachineOp::Movss:
        encodeMovss(inst, buf);
        break;
      case machine::MachineOp::Add:
        encodeAdd(inst, buf);
        break;
      case machine::MachineOp::Sub:
        encodeSub(inst, buf);
        break;
      case machine::MachineOp::Addsd:
        encodeAddsd(inst, buf);
        break;
      case machine::MachineOp::Subsd:
        encodeSubsd(inst, buf);
        break;
      case machine::MachineOp::Mulsd:
        encodeMulsd(inst, buf);
        break;
      case machine::MachineOp::Divsd:
        encodeDivsd(inst, buf);
        break;
      case machine::MachineOp::Ret:
        encodeRet(inst, buf);
        break;
      default:
        // TODO: другие инструкции
        break;
    }
  }
  
  // ========================================================================
  // MOV (целые) - 0x89 /r (mov r/m64, r64)
  // ========================================================================
  void encodeMov(const machine::MachineInst& inst, CodeBuffer& buf) {
    assert(inst.dst.kind == machine::MachineOperandKind::PhysicalReg);
    assert(inst.src1.kind == machine::MachineOperandKind::PhysicalReg ||
           inst.src1.kind == machine::MachineOperandKind::ImmediateI64);
    
    if (inst.src1.kind == machine::MachineOperandKind::ImmediateI64) {
      // MOV r64, imm64 - 0x48 0xB8 + r + imm64
      arch::RegId dst_reg = inst.dst.preg;
      uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
      bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
      
      uint8_t rex = 0x48;  // REX.W для 64-bit
      if (dst_ext) {
        rex |= 0x01;  // REX.B
      }
      buf.emit8(rex);
      buf.emit8(0xB8 + dst_code);
      buf.emit64(inst.src1.imm_i64);
    } else {
      // MOV r64, r64 - 0x48 0x89 /r
      arch::RegId dst_reg = inst.dst.preg;
      arch::RegId src_reg = inst.src1.preg;
      
      uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
      uint8_t src_code = RegisterEncoder::regCode3bit(src_reg);
      bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
      bool src_ext = RegisterEncoder::needsRexBit(src_reg);
      
      // REX.W (64-bit) + REX.R (если src > 7) + REX.B (если dst > 7)
      RexBuilder::emitIfNeeded(buf, true, src_ext, false, dst_ext);
      
      buf.emit8(0x89);
      uint8_t modrm = ModRmBuilder::buildRegReg(src_code, dst_code);
      buf.emit8(modrm);
    }
  }
  
  // ========================================================================
  // MOVSD (double) - 0xF2 0x0F 0x10 /r (movsd xmm, xmm/m64)
  // ========================================================================
  void encodeMovsd(const machine::MachineInst& inst, CodeBuffer& buf) {
    assert(inst.dst.kind == machine::MachineOperandKind::PhysicalReg);
    assert(inst.src1.kind == machine::MachineOperandKind::PhysicalReg ||
           inst.src1.kind == machine::MachineOperandKind::ImmediateF64);
    
    arch::RegId dst_reg = inst.dst.preg;
    
    // Если src — это сразу регистр XMM
    if (inst.src1.kind == machine::MachineOperandKind::PhysicalReg) {
      arch::RegId src_reg = inst.src1.preg;
      
      // Оба должны быть XMM регистрами (ID >= 24)
      assert(dst_reg >= 24 && src_reg >= 24);
      
      uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
      uint8_t src_code = RegisterEncoder::regCode3bit(src_reg);
      bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
      bool src_ext = RegisterEncoder::needsRexBit(src_reg);
      
      buf.emit8(0xF2);  // Prefix для MOVSD
      RexBuilder::emitIfNeeded(buf, false, dst_ext, false, src_ext);
      buf.emit8(0x0F);
      buf.emit8(0x10);
      uint8_t modrm = ModRmBuilder::buildRegReg(dst_code, src_code);
      buf.emit8(modrm);
    } else {
      // TODO: Загрузка из памяти
    }
  }
  
  // ========================================================================
  // MOVSS (float) - 0xF3 0x0F 0x10 /r
  // ========================================================================
  void encodeMovss(const machine::MachineInst& inst, CodeBuffer& buf) {
    arch::RegId dst_reg = inst.dst.preg;
    arch::RegId src_reg = inst.src1.preg;
    
    assert(dst_reg >= 24 && src_reg >= 24);
    
    uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
    uint8_t src_code = RegisterEncoder::regCode3bit(src_reg);
    bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
    bool src_ext = RegisterEncoder::needsRexBit(src_reg);
    
    buf.emit8(0xF3);  // Prefix для MOVSS
    RexBuilder::emitIfNeeded(buf, false, dst_ext, false, src_ext);
    buf.emit8(0x0F);
    buf.emit8(0x10);
    uint8_t modrm = ModRmBuilder::buildRegReg(dst_code, src_code);
    buf.emit8(modrm);
  }
  
  // ========================================================================
  // ADD r64, r64 - 0x48 0x01 /r (add r/m64, r64)
  // ========================================================================
  void encodeAdd(const machine::MachineInst& inst, CodeBuffer& buf) {
    // Формат: ADD dst, src1 => dst := dst + src1
    // x86: ADD r/m64, r64 (src в регистре, dst может быть в памяти или регистре)
    assert(inst.dst.kind == machine::MachineOperandKind::PhysicalReg);
    assert(inst.src1.kind == machine::MachineOperandKind::PhysicalReg);
    
    arch::RegId dst_reg = inst.dst.preg;
    arch::RegId src_reg = inst.src1.preg;
    
    uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
    uint8_t src_code = RegisterEncoder::regCode3bit(src_reg);
    bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
    bool src_ext = RegisterEncoder::needsRexBit(src_reg);
    
    RexBuilder::emitIfNeeded(buf, true, src_ext, false, dst_ext);
    buf.emit8(0x01);
    uint8_t modrm = ModRmBuilder::buildRegReg(src_code, dst_code);
    buf.emit8(modrm);
  }
  
  // ========================================================================
  // SUB r64, r64 - 0x48 0x29 /r
  // ========================================================================
  void encodeSub(const machine::MachineInst& inst, CodeBuffer& buf) {
    arch::RegId dst_reg = inst.dst.preg;
    arch::RegId src_reg = inst.src1.preg;
    
    uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
    uint8_t src_code = RegisterEncoder::regCode3bit(src_reg);
    bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
    bool src_ext = RegisterEncoder::needsRexBit(src_reg);
    
    RexBuilder::emitIfNeeded(buf, true, src_ext, false, dst_ext);
    buf.emit8(0x29);
    uint8_t modrm = ModRmBuilder::buildRegReg(src_code, dst_code);
    buf.emit8(modrm);
  }
  
  // ========================================================================
  // ADDSD xmm, xmm/m64 - 0xF2 0x0F 0x58 /r
  // ========================================================================
  void encodeAddsd(const machine::MachineInst& inst, CodeBuffer& buf) {
    arch::RegId dst_reg = inst.dst.preg;
    arch::RegId src_reg = inst.src1.preg;
    
    assert(dst_reg >= 24 && src_reg >= 24);
    
    uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
    uint8_t src_code = RegisterEncoder::regCode3bit(src_reg);
    bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
    bool src_ext = RegisterEncoder::needsRexBit(src_reg);
    
    buf.emit8(0xF2);
    RexBuilder::emitIfNeeded(buf, false, dst_ext, false, src_ext);
    buf.emit8(0x0F);
    buf.emit8(0x58);
    uint8_t modrm = ModRmBuilder::buildRegReg(dst_code, src_code);
    buf.emit8(modrm);
  }
  
  // ========================================================================
  // SUBSD xmm, xmm/m64 - 0xF2 0x0F 0x5C /r
  // ========================================================================
  void encodeSubsd(const machine::MachineInst& inst, CodeBuffer& buf) {
    arch::RegId dst_reg = inst.dst.preg;
    arch::RegId src_reg = inst.src1.preg;
    
    assert(dst_reg >= 24 && src_reg >= 24);
    
    uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
    uint8_t src_code = RegisterEncoder::regCode3bit(src_reg);
    bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
    bool src_ext = RegisterEncoder::needsRexBit(src_reg);
    
    buf.emit8(0xF2);
    RexBuilder::emitIfNeeded(buf, false, dst_ext, false, src_ext);
    buf.emit8(0x0F);
    buf.emit8(0x5C);
    uint8_t modrm = ModRmBuilder::buildRegReg(dst_code, src_code);
    buf.emit8(modrm);
  }
  
  // ========================================================================
  // MULSD xmm, xmm/m64 - 0xF2 0x0F 0x59 /r
  // ========================================================================
  void encodeMulsd(const machine::MachineInst& inst, CodeBuffer& buf) {
    arch::RegId dst_reg = inst.dst.preg;
    arch::RegId src_reg = inst.src1.preg;
    
    assert(dst_reg >= 24 && src_reg >= 24);
    
    uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
    uint8_t src_code = RegisterEncoder::regCode3bit(src_reg);
    bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
    bool src_ext = RegisterEncoder::needsRexBit(src_reg);
    
    buf.emit8(0xF2);
    RexBuilder::emitIfNeeded(buf, false, dst_ext, false, src_ext);
    buf.emit8(0x0F);
    buf.emit8(0x59);
    uint8_t modrm = ModRmBuilder::buildRegReg(dst_code, src_code);
    buf.emit8(modrm);
  }
  
  // ========================================================================
  // DIVSD xmm, xmm/m64 - 0xF2 0x0F 0x5E /r
  // ========================================================================
  void encodeDivsd(const machine::MachineInst& inst, CodeBuffer& buf) {
    arch::RegId dst_reg = inst.dst.preg;
    arch::RegId src_reg = inst.src1.preg;
    
    assert(dst_reg >= 24 && src_reg >= 24);
    
    uint8_t dst_code = RegisterEncoder::regCode3bit(dst_reg);
    uint8_t src_code = RegisterEncoder::regCode3bit(src_reg);
    bool dst_ext = RegisterEncoder::needsRexBit(dst_reg);
    bool src_ext = RegisterEncoder::needsRexBit(src_reg);
    
    buf.emit8(0xF2);
    RexBuilder::emitIfNeeded(buf, false, dst_ext, false, src_ext);
    buf.emit8(0x0F);
    buf.emit8(0x5E);
    uint8_t modrm = ModRmBuilder::buildRegReg(dst_code, src_code);
    buf.emit8(modrm);
  }
  
  // ========================================================================
  // RET - 0xC3
  // ========================================================================
  void encodeRet(const machine::MachineInst& inst, CodeBuffer& buf) {
    buf.emit8(0xC3);
  }
};

// ============================================================================
// УТИЛИТЫ
// ============================================================================

inline std::string codeBufferToHex(const CodeBuffer& buf) {
  std::string result;
  const auto& code = buf.getCode();
  
  for (size_t i = 0; i < code.size(); i++) {
    if (i > 0 && i % 16 == 0) {
      result += "\n";
    }
    char hex[4];
    snprintf(hex, sizeof(hex), "%02x ", code[i]);
    result += hex;
  }
  
  return result;
}

} // namespace encoder
