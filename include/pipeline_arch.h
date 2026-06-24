#pragma once

#include <cstdint>
#include <array>
#include <string_view>
#include <stdexcept>

// ============================================================================
// ЭТАП 1: УНИФИКАЦИЯ АРХИТЕКТУРНОГО ОПИСАНИЯ
// ============================================================================
// Единый источник истины для всей информации о целевой архитектуре.
// Всё остальное — производные вычисления и навигация.

namespace arch {

// --- Базовые типы данных ---
using RegId = uint32_t;
using TypeId = uint32_t;

// --- Флаги регистров (битовое поле) ---
enum RegFlags : uint32_t {
  None         = 0,
  Allocatable  = 1u << 0,
  CallerSaved  = 1u << 1,
  CalleeSaved  = 1u << 2,
  StackPtr     = 1u << 3,
  FramePtr     = 1u << 4,
  InstrPtr     = 1u << 5,
  PushPop      = 1u << 6,
  MovsSrc      = 1u << 7,
  MovsDst      = 1u << 8,
  MovsCnt      = 1u << 9,
  IntRet       = 1u << 10,
  FloatRet     = 1u << 11
};

// --- Классификация регистров ---
enum class RegKind : uint8_t {
  Gpr,
  Xmm,
  Segment,
  Flags,
  Ip
};

enum class RegClass : uint8_t {
  Gpr,
  Xmm,
  _Count
};

// --- Классификация типов ---
enum class TypeKind : uint8_t {
  Void,
  Bool,
  I32, I64,
  F32, F64,
  Ptr,
  _Count
};

// ============================================================================
// ДЕКЛАРАТИВНОЕ ОПИСАНИЕ АРХИТЕКТУРЫ
// ============================================================================

// Полная информация об одном регистре
struct RegisterSpec {
  std::string_view name;      // "rax", "xmm0", ...
  RegId id;                   // 0, 1, 2, ...
  RegKind kind;               // GPR, XMM, Segment, ...
  RegClass cls;               // Gpr или Xmm
  uint16_t bits;              // 64, 128, ...
  uint32_t flags;             // Allocatable | CallerSaved | ...
  uint8_t x86_code;           // 0..7 для REX кодирования (или расширенный индекс)
};

// Полная информация об одном типе
struct TypeSpec {
  TypeKind kind;
  std::string_view name;
  uint8_t size;               // в байтах
  RegClass preferred_class;   // где он должен храниться
};

// Параметр функции: какие регистры используются для передачи аргументов
struct CallConvArgPassage {
  RegClass reg_class;
  std::string_view name;      // "int_args", "float_args"
  const RegId* regs;          // массив регистров в порядке
  uint32_t count;
};

// Полная информация о соглашении вызовов
struct CallingConvention {
  std::string_view name;      // "win64", "sysv64"
  
  uint8_t ptr_size;
  uint8_t stack_align;
  uint16_t shadow_space;      // для Win64
  bool red_zone;              // для System V
  
  const CallConvArgPassage* arg_passages;
  uint32_t arg_passage_count;
  
  RegId int_ret_reg;
  RegId float_ret_reg;
  
  const RegId* caller_saved;
  uint32_t caller_saved_count;
  
  const RegId* callee_saved;
  uint32_t callee_saved_count;
};

// ============================================================================
// ОПИСАНИЕ X86-64
// ============================================================================

// Регистры
constexpr RegisterSpec X64_REGISTERS[] = {
  // GPR
  {"rax",   0,  RegKind::Gpr, RegClass::Gpr, 64, 
   RegFlags::Allocatable | RegFlags::CallerSaved | RegFlags::PushPop | RegFlags::IntRet, 0},
  {"rcx",   1,  RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CallerSaved | RegFlags::PushPop, 1},
  {"rdx",   2,  RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CallerSaved | RegFlags::PushPop, 2},
  {"rbx",   3,  RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CalleeSaved | RegFlags::PushPop, 3},
  
  {"rsp",   4,  RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::StackPtr, 4},
  {"rbp",   5,  RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::CalleeSaved | RegFlags::FramePtr | RegFlags::PushPop, 5},
  
  {"rsi",   6,  RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::PushPop | RegFlags::MovsSrc, 6},
  {"rdi",   7,  RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::PushPop | RegFlags::MovsDst, 7},
  
  {"r8",    8,  RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CallerSaved, 0},
  {"r9",    9,  RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CallerSaved, 1},
  {"r10",   10, RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CallerSaved, 2},
  {"r11",   11, RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CallerSaved, 3},
  
  {"r12",   12, RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CalleeSaved, 4},
  {"r13",   13, RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CalleeSaved, 5},
  {"r14",   14, RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CalleeSaved, 6},
  {"r15",   15, RegKind::Gpr, RegClass::Gpr, 64,
   RegFlags::Allocatable | RegFlags::CalleeSaved, 7},
  
  // Специальные
  {"rip",   16, RegKind::Ip,  RegClass::Gpr, 64,
   RegFlags::InstrPtr, 0},
  {"rflags",17, RegKind::Flags, RegClass::Gpr, 64,
   RegFlags::None, 0},
  
  // Сегментные
  {"cs",    18, RegKind::Segment, RegClass::Gpr, 16, RegFlags::None, 0},
  {"ds",    19, RegKind::Segment, RegClass::Gpr, 16, RegFlags::None, 0},
  {"es",    20, RegKind::Segment, RegClass::Gpr, 16, RegFlags::None, 0},
  {"fs",    21, RegKind::Segment, RegClass::Gpr, 16, RegFlags::None, 0},
  {"gs",    22, RegKind::Segment, RegClass::Gpr, 16, RegFlags::None, 0},
  {"ss",    23, RegKind::Segment, RegClass::Gpr, 16, RegFlags::None, 0},
  
  // XMM
  {"xmm0",  24, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable | RegFlags::CallerSaved | RegFlags::FloatRet, 0},
  {"xmm1",  25, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable | RegFlags::CallerSaved, 1},
  {"xmm2",  26, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable | RegFlags::CallerSaved, 2},
  {"xmm3",  27, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable | RegFlags::CallerSaved, 3},
  {"xmm4",  28, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable | RegFlags::CallerSaved, 4},
  {"xmm5",  29, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable | RegFlags::CallerSaved, 5},
  
  {"xmm6",  30, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 6},
  {"xmm7",  31, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 7},
  {"xmm8",  32, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 0},
  {"xmm9",  33, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 1},
  {"xmm10", 34, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 2},
  {"xmm11", 35, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 3},
  {"xmm12", 36, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 4},
  {"xmm13", 37, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 5},
  {"xmm14", 38, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 6},
  {"xmm15", 39, RegKind::Xmm, RegClass::Xmm, 128,
   RegFlags::Allocatable, 7},
};

constexpr uint32_t X64_REGISTER_COUNT = std::size(X64_REGISTERS);

// Типы
constexpr TypeSpec X64_TYPES[] = {
  {TypeKind::Void, "void", 0, RegClass::Gpr},
  {TypeKind::Bool, "bool", 1, RegClass::Gpr},
  {TypeKind::I32,  "i32",  4, RegClass::Gpr},
  {TypeKind::I64,  "i64",  8, RegClass::Gpr},
  {TypeKind::F32,  "f32",  4, RegClass::Xmm},
  {TypeKind::F64,  "f64",  8, RegClass::Xmm},
  {TypeKind::Ptr,  "ptr",  8, RegClass::Gpr},
};

constexpr uint32_t X64_TYPE_COUNT = std::size(X64_TYPES);

// Соглашения вызовов
// Win64
constexpr RegId WIN64_INT_ARGS[] = {1, 2, 8, 9};      // rcx, rdx, r8, r9
constexpr RegId WIN64_FP_ARGS[]  = {24, 25, 26, 27};  // xmm0..3

constexpr RegId WIN64_CALLER_SAVED[] = {
  0, 1, 2, 8, 9, 10, 11,     // rax, rcx, rdx, r8, r9, r10, r11
  24, 25, 26, 27, 28, 29     // xmm0..5
};

constexpr RegId WIN64_CALLEE_SAVED[] = {
  3, 5, 6, 7, 12, 13, 14, 15,  // rbx, rbp, rsi, rdi, r12..15
  30, 31, 32, 33, 34, 35, 36, 37, 38, 39  // xmm6..15
};

constexpr CallConvArgPassage WIN64_ARG_PASSAGES[] = {
  {RegClass::Gpr, "int_args",   WIN64_INT_ARGS,  std::size(WIN64_INT_ARGS)},
  {RegClass::Xmm, "float_args", WIN64_FP_ARGS,   std::size(WIN64_FP_ARGS)},
};

constexpr CallingConvention WIN64_ABI{
  "win64",
  8,    // ptr_size
  16,   // stack_align
  32,   // shadow_space
  false,// red_zone
  WIN64_ARG_PASSAGES,
  std::size(WIN64_ARG_PASSAGES),
  0,    // int_ret_reg (rax)
  24,   // float_ret_reg (xmm0)
  WIN64_CALLER_SAVED,
  std::size(WIN64_CALLER_SAVED),
  WIN64_CALLEE_SAVED,
  std::size(WIN64_CALLEE_SAVED),
};

// System V AMD64 ABI
constexpr RegId SYSV_INT_ARGS[] = {7, 6, 2, 1, 8, 9};  // rdi, rsi, rdx, rcx, r8, r9
constexpr RegId SYSV_FP_ARGS[]  = {24, 25, 26, 27, 28, 29, 30, 31};  // xmm0..7

constexpr RegId SYSV_CALLER_SAVED[] = {
  0, 1, 2, 6, 7, 8, 9, 10, 11,  // rax, rcx, rdx, rsi, rdi, r8..11
  24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39  // xmm0..15
};

constexpr RegId SYSV_CALLEE_SAVED[] = {
  3, 5, 12, 13, 14, 15  // rbx, rbp, r12..15
};

constexpr CallConvArgPassage SYSV_ARG_PASSAGES[] = {
  {RegClass::Gpr, "int_args",   SYSV_INT_ARGS,  std::size(SYSV_INT_ARGS)},
  {RegClass::Xmm, "float_args", SYSV_FP_ARGS,   std::size(SYSV_FP_ARGS)},
};

constexpr CallingConvention SYSV_ABI{
  "sysv64",
  8,    // ptr_size
  16,   // stack_align
  0,    // shadow_space
  true, // red_zone
  SYSV_ARG_PASSAGES,
  std::size(SYSV_ARG_PASSAGES),
  0,    // int_ret_reg (rax)
  24,   // float_ret_reg (xmm0)
  SYSV_CALLER_SAVED,
  std::size(SYSV_CALLER_SAVED),
  SYSV_CALLEE_SAVED,
  std::size(SYSV_CALLEE_SAVED),
};

// ============================================================================
// НАВИГАТОРЫ И ВАЛИДАТОРЫ
// Автоматически сгенерированные/вычисленные из декларативного описания
// ============================================================================

class ArchRegistry {
public:
  // Доступ к регистру по ID
  static const RegisterSpec& reg(RegId id) {
    if (id >= X64_REGISTER_COUNT) throw std::out_of_range("Invalid RegId");
    return X64_REGISTERS[id];
  }
  
  // Доступ к типу по ID
  static const TypeSpec& type(TypeId id) {
    if (id >= X64_TYPE_COUNT) throw std::out_of_range("Invalid TypeId");
    return X64_TYPES[id];
  }
  
  // Поиск регистра по имени
  static RegId findReg(std::string_view name) {
    for (uint32_t i = 0; i < X64_REGISTER_COUNT; i++) {
      if (X64_REGISTERS[i].name == name) return i;
    }
    throw std::runtime_error(std::string("Register not found: ") + std::string(name));
  }
  
  // Поиск типа по имени
  static TypeId findType(std::string_view name) {
    for (uint32_t i = 0; i < X64_TYPE_COUNT; i++) {
      if (X64_TYPES[i].name == name) return i;
    }
    throw std::runtime_error(std::string("Type not found: ") + std::string(name));
  }
  
  // Получить все регистры определённого класса
  template<typename Callback>
  static void forEachRegOfClass(RegClass cls, Callback cb) {
    for (uint32_t i = 0; i < X64_REGISTER_COUNT; i++) {
      if (X64_REGISTERS[i].cls == cls) cb(i, X64_REGISTERS[i]);
    }
  }
  
  // Получить все allocatable регистры
  template<typename Callback>
  static void forEachAllocatableReg(Callback cb) {
    for (uint32_t i = 0; i < X64_REGISTER_COUNT; i++) {
      if (X64_REGISTERS[i].flags & RegFlags::Allocatable) 
        cb(i, X64_REGISTERS[i]);
    }
  }
};

} // namespace arch
