#pragma once
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"

#include <unordered_set>

class RefactorHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    explicit RefactorHandler(clang::Rewriter &Rewrite) : rewriter_(Rewrite) {}
    // Метод run вызывается для каждого совпадения с матчем.
    // Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
    virtual void run(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;

private:
    // 1. Невиртуальные деструкторы
    void handle_nv_dtor(const clang::CXXRecordDecl *Dtor, clang::DiagnosticsEngine &Diag, clang::SourceManager &SM);

    // 2. Методы без override
    void handle_miss_override(const clang::CXXMethodDecl *Method, clang::DiagnosticsEngine &Diag,
                              clang::SourceManager &SM);

    // 3. range-for без &
    void handle_crange_for(const clang::CXXForRangeStmt *LoopVar, clang::DiagnosticsEngine &Diag,
                           clang::SourceManager &SM);

private:
    clang::Rewriter &rewriter_;
    std::unordered_set<std::string>
        virtual_dtor_locations_;  // Для хранения позиций деструкторов, к которым уже добавлен virtual

    bool trySaveLocation(const clang::CXXRecordDecl *Dtor, clang::SourceManager &SM);
};