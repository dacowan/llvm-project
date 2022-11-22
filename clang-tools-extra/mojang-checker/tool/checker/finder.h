#pragma once

#include "checker/visitor.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"

class MojangCheckerFinder : public clang::ASTConsumer {
    MojangCheckerVisitor Visitor;

public:
    MojangCheckerFinder(clang::SourceManager &SM) : Visitor(SM) {}

  void HandleTranslationUnit(clang::ASTContext &Context) final {
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
  }
};