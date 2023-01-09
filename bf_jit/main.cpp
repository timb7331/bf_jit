#include "asmjit/asmjit.h"
#include "asmjit/core/compiler.h"
#include "asmjit/core/logger.h"
#include "asmjit/x86/x86compiler.h"
#include "asmjit/x86/x86operand.h"
#include <algorithm>
#include <iostream>
#include <stdint.h>
#include <unordered_map>
#include <vector>
#include <ranges>
#include <fstream>
namespace io {
std::string read_file(const std::string &file_name) {
  std::ifstream file(file_name);
  if (!file.is_open()) {
    std::cout << "could not open file: " << file_name << std::endl;
    return std::string();
  }

  std::string file_content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  return file_content;
}
} // namespace io

class c_bf_jit {
  // constructor
public:

    enum class ops_t {
    inc,   // +
    dec,   // -
    right, // >
    left,  // <
    in,    // ,
    out,   // .
    loop,  // [
    end,   // ]
  };

  c_bf_jit(std::string_view code) : m_code(code) {
    // optimize code
    m_optimized = optimize(code);

    // init asmjit
    m_code_holder.init(m_runtime.environment());
  }

  std::vector<std::pair<ops_t, int>> optimize(const std::string_view code) {
    std::vector<std::pair<ops_t, int>> optimized;
    std::ranges::for_each(code, [&](char ch) {
      ops_t op;
      switch (ch) {
      case '+':
        op = ops_t::inc;
        break;
      case '-':
        op = ops_t::dec;
        break;
      case '>':
        op = ops_t::right;
        break;
      case '<':
        op = ops_t::left;
        break;
      case ',':
        op = ops_t::in;
        break;
      case '.':
        op = ops_t::out;
        break;
      case '[':
        op = ops_t::loop;
        break;
      case ']':
        op = ops_t::end;
        break;
      }
      if (!optimized.empty() && op == optimized.back().first) {
        optimized.back().second++;
      } else {
        optimized.emplace_back(op, 1);
      }
    });
    return optimized;
  }

  struct loop_t {
    asmjit::Label start;
    asmjit::Label end;
    loop_t(asmjit::x86::Compiler &cc)
        : start(cc.newLabel()), end(cc.newLabel()) {}
  };

  bool compile_jit() {

    asmjit::FileLogger logger(stdout);
    m_code_holder.setLogger(&logger);
        // init compiler
    asmjit::x86::Compiler compiler(&m_code_holder);
    compiler.addFunc(asmjit::FuncSignatureT<void>());

    compiler.addDiagnosticOptions(asmjit::DiagnosticOptions::kRAAnnotate | asmjit::DiagnosticOptions::kRADebugAll);

    // init stack
    m_stack = compiler.newStack(1024, sizeof(void *));

    // init stack pointer
    m_stack_ptr = compiler.newGpz();
    m_data_gp = compiler.newGpb();

    // init stack index
    asmjit::x86::Mem idx(m_stack);
    idx.setIndex(m_stack_ptr);
    idx.setSize(sizeof(uint8_t));

    compiler.mov(m_stack_ptr, 0);
    asmjit::Label loop = compiler.newLabel();
    compiler.bind(loop);

    compiler.mov(idx, 0);
    compiler.inc(m_stack_ptr);
    compiler.cmp(m_stack_ptr, 1024);
    compiler.jb(loop);

    compiler.xor_(m_stack_ptr, m_stack_ptr);


    for (const auto &[op, count] : m_optimized) {
      switch (op) {
      case ops_t::inc: {
        compiler.add(idx, count);
        break;
      }
      case ops_t::dec: {
        compiler.sub(idx, count);
        break;
      }
      case ops_t::right: {
        compiler.add(m_stack_ptr, count);
        break;
      }
      case ops_t::left: {
        compiler.sub(m_stack_ptr, count);
        break;
      }
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
      }
      case ops_t::loop: {

        m_loops.push_back(loop_t(compiler));

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

public:
private:
  std::string_view m_code;
  std::vector<std::pair<ops_t, int>> m_optimized;
  std::vector<loop_t> m_loops;
  asmjit::JitRuntime m_runtime;
  asmjit::x86::Compiler m_compiler;
  asmjit::CodeHolder m_code_holder;
  asmjit::InvokeNode *m_invoke;
  //
  asmjit::x86::Mem m_stack;
  asmjit::x86::Mem m_stack_index;

  asmjit::x86::Gp m_stack_ptr;
  asmjit::x86::Gp m_data_gp;
};


int main() {
    //Hello world    
    std::string code = ">++++++++[<+++++++++>-]<.>++++[<+++++++>-]<+.+++++++..+++.>>++++++[<+++++++>-]<++.------------.>++++++[<+++++++++>-]<+.<.+++.------.--------.>>>++++[<++++++++>-]<+.";

    c_bf_jit jit(code);
    
    jit.compile_jit();

  return 0;
}