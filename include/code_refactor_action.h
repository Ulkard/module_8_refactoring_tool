#pragma once

#include "complex_consumer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"

class CodeRefactorAction : public clang::ASTFrontendAction {
public:
    // Returns our ASTConsumer per translation unit.
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                                  clang::StringRef file) override;
    virtual bool BeginSourceFileAction(clang::CompilerInstance &CI) override;
    virtual void EndSourceFileAction() override;

private:
    clang::Rewriter RewriterForCodeRefactor;
};