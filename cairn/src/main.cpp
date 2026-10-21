#include <chrono>
#include <iomanip>
#include <iostream>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cairn/ast.hpp"
#include "cairn/chunk.hpp"
#include "cairn/compiler.hpp"
#include "cairn/diagnostic.hpp"
#include "cairn/lexer.hpp"
#include "cairn/parser.hpp"
#include "cairn/source.hpp"
#include "cairn/vm.hpp"
#include "cairn/token.hpp"

namespace {

void print_usage(const char* program) {
    std::cout
        << "cairn - a stack-based scripting language and virtual machine\n\n"
        << "usage: " << program << " [options] <file.cairn>\n\n"
        << "options:\n"
        << "  --run       compile and execute the program (default)\n"
        << "  --bytecode  disassemble the compiled bytecode\n"
        << "  --ast       print the abstract syntax tree\n"
        << "  --tokens    print the token stream\n"
        << "  --quiet     report errors only\n"
        << "  --time      report timings for every stage\n"
        << "  --bench N   execute the program N times and report ns/instruction\n"
        << "  -h, --help  show this message\n";
}

void dump_tokens(const std::vector<cairn::Token>& tokens) {
    std::cout << std::left << std::setw(6) << "LINE" << std::setw(6) << "COL"
              << std::setw(16) << "KIND" << "LEXEME\n";
    std::cout << std::string(46, '-') << '\n';

    for (const cairn::Token& token : tokens) {
        std::cout << std::left << std::setw(6) << token.line << std::setw(6)
                  << token.column << std::setw(16) << cairn::to_string(token.kind);
        if (token.kind == cairn::TokenKind::EndOfFile) {
            std::cout << "<eof>";
        } else {
            std::cout << token.lexeme;
        }
        if (token.kind == cairn::TokenKind::Integer) {
            std::cout << "   (value = " << token.int_value << ")";
        }
        std::cout << '\n';
    }
    std::cout << '\n' << tokens.size() << " tokens\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    bool show_tokens = false;
    bool show_ast = false;
    bool show_bytecode = false;
    bool execute = false;
    long bench_runs = 0;
    bool quiet = false;
    bool timing = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--tokens") {
            show_tokens = true;
        } else if (arg == "--ast") {
            show_ast = true;
        } else if (arg == "--bytecode") {
            show_bytecode = true;
        } else if (arg == "--run") {
            execute = true;
        } else if (arg == "--bench") {
            if (i + 1 >= argc) {
                std::cerr << "cairn: --bench needs a repetition count\n";
                return 2;
            }
            bench_runs = std::strtol(argv[++i], nullptr, 10);
            if (bench_runs <= 0) {
                std::cerr << "cairn: --bench needs a positive repetition count\n";
                return 2;
            }
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--time") {
            timing = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "cairn: unknown option '" << arg << "'\n";
            return 2;
        } else if (path.empty()) {
            path = arg;
        } else {
            std::cerr << "cairn: more than one input file given\n";
            return 2;
        }
    }

    if (path.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    if (!show_tokens && !show_ast && !show_bytecode && bench_runs == 0) {
        execute = true;  // running the program is the default
    }
    if (quiet) {
        show_tokens = false;
        show_ast = false;
        show_bytecode = false;
    }

    std::string load_error;
    auto source = cairn::SourceFile::from_path(path, load_error);
    if (!source) {
        std::cerr << "cairn: " << load_error << '\n';
        return 2;
    }

    cairn::DiagnosticBag diagnostics;

    const auto lex_started = std::chrono::steady_clock::now();
    cairn::Lexer lexer(source->text(), diagnostics);
    const std::vector<cairn::Token> tokens = lexer.tokenize();
    const auto lex_finished = std::chrono::steady_clock::now();

    if (show_tokens) {
        dump_tokens(tokens);
        if (show_ast) {
            std::cout << '\n';
        }
    }

    // Parsing proceeds even when the lexer found problems, so the user gets
    // lexical and syntactic errors from the same run instead of one round-trip
    // per layer.
    const auto parse_started = std::chrono::steady_clock::now();
    cairn::Parser parser(tokens, diagnostics);
    const std::vector<cairn::StmtPtr> program = parser.parse();
    const auto parse_finished = std::chrono::steady_clock::now();

    if (show_ast) {
        std::cout << cairn::to_sexpr(program) << '\n';
        std::cout << '\n' << program.size() << " top-level statements\n";
        if (show_bytecode) {
            std::cout << '\n';
        }
    }

    // Compiling a tree that already contains syntax errors would report
    // undefined-variable noise for declarations the parser had to skip, so the
    // pipeline stops here when earlier phases failed.
    const auto compile_started = std::chrono::steady_clock::now();
    std::optional<cairn::Chunk> chunk;
    if (!diagnostics.has_errors()) {
        cairn::Compiler compiler(diagnostics);
        chunk = compiler.compile(program);
    }
    const auto compile_finished = std::chrono::steady_clock::now();

    if (show_bytecode && chunk) {
        std::cout << chunk->disassemble(source->name());
        std::cout << '\n' << chunk->size() << " bytes of bytecode, "
                  << chunk->constants().size() << " constants\n";
    }

    if (timing) {
        const auto lex_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                lex_finished - lex_started).count();
        const auto parse_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  parse_finished - parse_started).count();
        const double per_token =
            tokens.empty() ? 0.0
                           : static_cast<double>(lex_ns) / static_cast<double>(tokens.size());
        std::cout << "lex:   " << source->text().size() << " bytes -> " << tokens.size()
                  << " tokens in " << lex_ns << " ns (" << std::fixed
                  << std::setprecision(1) << per_token << " ns/token)\n";
        std::cout << "parse: " << parse_ns << " ns\n";
        const auto compile_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    compile_finished - compile_started).count();
        std::cout << "compile: " << compile_ns << " ns";
        if (chunk) {
            std::cout << " -> " << chunk->size() << " bytes";
        }
        std::cout << '\n';
    }

    if (diagnostics.size() > 0) {
        diagnostics.render(std::cerr, source->text(), source->name());
    }
    if (diagnostics.has_errors() || !chunk) {
        return 1;
    }

    if (execute) {
        cairn::VM vm(std::cout, std::cin);
        if (vm.run(*chunk) == cairn::VM::Result::RuntimeError) {
            std::cerr << source->name() << ": runtime error: " << vm.error() << '\n';
            return 1;
        }
    }

    if (bench_runs > 0) {
        // Discard output during benchmarking: measuring the dispatch loop
        // through a terminal would measure the terminal.
        std::ostringstream sink;
        std::istringstream no_input;
        cairn::VM vm(sink, no_input);

        vm.run(*chunk);  // warm the caches and the branch predictor

        const auto started = std::chrono::steady_clock::now();
        std::uint64_t retired = 0;
        for (long i = 0; i < bench_runs; ++i) {
            if (vm.run(*chunk) == cairn::VM::Result::RuntimeError) {
                std::cerr << source->name() << ": runtime error: " << vm.error() << '\n';
                return 1;
            }
            retired += vm.instructions_executed();
        }
        const auto elapsed = std::chrono::steady_clock::now() - started;

        const auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        std::cout << "dispatch:     "
                  << (cairn::VM::uses_computed_goto() ? "computed goto" : "switch")
                  << '\n';
        std::cout << "runs:         " << bench_runs << '\n';
        std::cout << "instructions: " << retired << '\n';
        std::cout << "elapsed:      " << ns << " ns\n";
        if (retired > 0) {
            std::cout << "per instr:    " << std::fixed << std::setprecision(2)
                      << static_cast<double>(ns) / static_cast<double>(retired)
                      << " ns\n";
        }
    }

    return 0;
}
