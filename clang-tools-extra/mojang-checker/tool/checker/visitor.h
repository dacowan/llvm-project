#pragma once

#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

#include <sstream>
#include <string>

class MojangCheckerVisitor : public clang::RecursiveASTVisitor<MojangCheckerVisitor> {
  clang::SourceManager &SourceManager;

public:
  MojangCheckerVisitor(clang::SourceManager &SourceManager)
      : SourceManager(SourceManager) {}

  bool VisitNamedDecl(clang::NamedDecl *NamedDecl) {
    llvm::outs() << "Found " << NamedDecl->getQualifiedNameAsString() << " at "
                 << getDeclLocation(NamedDecl->getBeginLoc()) << "\n";
    return true;
  }

private:
  std::string getDeclLocation(clang::SourceLocation Loc) const {
    std::ostringstream OSS;
    OSS << SourceManager.getFilename(Loc).str() << ":"
        << SourceManager.getSpellingLineNumber(Loc) << ":"
        << SourceManager.getSpellingColumnNumber(Loc);
    return OSS.str();
  }
};