
#include "checker/action.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

static llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);

llvm::cl::OptionCategory MojangCheckerCategory("mojang-checker options");

static char MojangCheckerUsage[] = "mojang-checker <no idea just yet>";

int main(int argc, const char **argv) {
    auto optionsParser = clang::tooling::CommonOptionsParser::create( argc, argv, MojangCheckerCategory, 
      llvm::cl::OneOrMore, MojangCheckerUsage);
  auto files = optionsParser->getSourcePathList();
  clang::tooling::ClangTool tool(optionsParser->getCompilations(), files);

  return tool.run(
      clang::tooling::newFrontendActionFactory<MojangCheckerAction>().get());
}