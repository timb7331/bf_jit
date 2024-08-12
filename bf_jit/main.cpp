#include "asmjit/asmjit.h"
#include "asmjit/core/compiler.h"
#include "asmjit/core/logger.h"
#include "asmjit/x86/x86compiler.h"
#include "asmjit/x86/x86operand.h"
#include <fstream>
#include <iostream>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace io {
std::string read_file(const std::string &file_name) {
  std::ifstream file(file_name);
  if (!file.is_open()) {
    std::cerr << "Could not open file: " << file_name << std::endl;
    return {};
  }

  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}
} // namespace io

namespace jit {

class c_bf_jit {
public:
  enum class ops_t {
    inc,   // +
    dec,   // -
    right, // >
    left,  // <
    in,    // ,
    out,   // .
    loop,  // [
    end    // ]
  };

  explicit c_bf_jit(std::string_view code)
      : m_code(code), m_optimized(optimize(code)) {
    m_code_holder.init(m_runtime.environment());
  }

  bool compile_jit() {
    asmjit::FileLogger logger(stdout);
    m_code_holder.setLogger(&logger);

    asmjit::x86::Compiler compiler(&m_code_holder);
    compiler.addFunc(asmjit::FuncSignatureT<void>());

    compiler.addDiagnosticOptions(asmjit::DiagnosticOptions::kRAAnnotate |
                                  asmjit::DiagnosticOptions::kRADebugAll);

    m_stack = compiler.newStack(1024, sizeof(void *));
    m_stack_ptr = compiler.newGpz();
    m_data_gp = compiler.newGpb();

    asmjit::x86::Mem idx(m_stack);
    idx.setIndex(m_stack_ptr);
    idx.setSize(sizeof(uint8_t));

    compiler.mov(m_stack_ptr, 0);

    initialize_memory(compiler, idx);

    for (const auto &[op, count] : m_optimized) {
      switch (op) {
      case ops_t::inc:
        compiler.add(idx, count);
        break;
      case ops_t::dec:
        compiler.sub(idx, count);
        break;
      case ops_t::right:
        compiler.add(m_stack_ptr, count);
        break;
      case ops_t::left:
        compiler.sub(m_stack_ptr, count);
        break;
      case ops_t::in: {
        compiler.invoke(&m_invoke, asmjit::imm(&getchar),
                        asmjit::FuncSignatureT<char>());
        m_invoke->setRet(0, m_data_gp);
        compiler.mov(idx, m_data_gp);
        break;
      }
      case ops_t::out: {
        compiler.mov(m_data_gp, idx);
        compiler.invoke(&m_invoke, asmjit::imm(&putchar),
                        asmjit::FuncSignatureT<void, char>());
        m_invoke->setArg(0, m_data_gp);
        break;
      }
      case ops_t::loop: {
        m_loops.emplace_back(compiler);
        compiler.bind(m_loops.back().start);
        compiler.cmp(idx, 0);
        compiler.je(m_loops.back().end);
        break;
      }
      case ops_t::end: {
        compiler.jmp(m_loops.back().start);
        compiler.bind(m_loops.back().end);
        m_loops.pop_back();
        break;
      }
      }
    }

    compiler.endFunc();
    compiler.finalize();

    void (*func)() = nullptr;
    if (m_runtime.add(&func, &m_code_holder) != asmjit::kErrorOk) {
      return false;
    }

    func();
    return true;
  }

private:
  std::vector<std::pair<ops_t, int>> optimize(std::string_view code) {
    static const std::unordered_map<char, ops_t> op_map = {
        {'+', ops_t::inc},  {'-', ops_t::dec}, {'>', ops_t::right},
        {'<', ops_t::left}, {',', ops_t::in},  {'.', ops_t::out},
        {'[', ops_t::loop}, {']', ops_t::end}};

    std::vector<std::pair<ops_t, int>> optimized;
    for (char ch : code) {
      if (auto it = op_map.find(ch); it != op_map.end()) {
        if (!optimized.empty() && it->second == optimized.back().first &&
            it->second != ops_t::out && it->second != ops_t::in) {
          optimized.back().second++;
        } else {
          optimized.emplace_back(it->second, 1);
        }
      }
    }
    return optimized;
  }

  void initialize_memory(asmjit::x86::Compiler &compiler,
                         asmjit::x86::Mem &idx) {
    asmjit::Label loop = compiler.newLabel();
    compiler.bind(loop);
    compiler.mov(idx, 0);
    compiler.inc(m_stack_ptr);
    compiler.cmp(m_stack_ptr, 1024);
    compiler.jb(loop);
    compiler.xor_(m_stack_ptr, m_stack_ptr);
  }

  struct loop_t {
    asmjit::Label start;
    asmjit::Label end;
    loop_t(asmjit::x86::Compiler &cc)
        : start(cc.newLabel()), end(cc.newLabel()) {}
  };

  std::string_view m_code;
  std::vector<std::pair<ops_t, int>> m_optimized;
  std::vector<loop_t> m_loops;

  asmjit::JitRuntime m_runtime;
  asmjit::CodeHolder m_code_holder;
  asmjit::InvokeNode *m_invoke = nullptr;

  asmjit::x86::Mem m_stack;
  asmjit::x86::Gp m_stack_ptr;
  asmjit::x86::Gp m_data_gp;
};

} // namespace jit

int main() {
  std::string code = R"(
>>,[>>,]<<[
    [<<]>>>>[
        <<[>+<<+>-]
        >>[>+<<<<[->]>[<]>>-]
        <<<[[-]>>[>+<-]>>[<<<+>>>-]]
        >>[[<+>-]>>]<
    ]<<[>>+<<-]<<
]>>>>[.>>]
)";

  jit::c_bf_jit jit(code);
  if (!jit.compile_jit()) {
    std::cerr << "JIT compilation failed." << std::endl;
    return 1;
  }

  return 0;
}
