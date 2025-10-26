#include <clang/AST/DeclCXX.h>
#include <clang/AST/StmtCXX.h>
#include <clang/Basic/SourceLocation.h>

#include "refactor_handler.h"
#include <clang/AST/Decl.h>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;

    if (const auto *class_decl = Result.Nodes.getNodeAs<CXXRecordDecl>("nonVirtualDtor")) {
        handle_nv_dtor(class_decl, Diag, SM);
    }

    if (const auto *method = Result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride");
        method && method->size_overridden_methods() > 0 && !method->hasAttr<OverrideAttr>()) {
        handle_miss_override(method, Diag, SM);
    }

    if (const auto *for_loop = Result.Nodes.getNodeAs<CXXForRangeStmt>("NoRefConstVarInRangeLoop")) {
        handle_crange_for(for_loop, Diag, SM);
    }
}

bool RefactorHandler::trySaveLocation(const clang::CXXRecordDecl *class_decl, clang::SourceManager &SM) {
    std::string key = class_decl->getLocation().printToString(SM);
    if (virtual_dtor_locations_.find(key) != virtual_dtor_locations_.end()) {
        return true;
    } else {
        virtual_dtor_locations_.insert(key);
        return false;
    }
}

SourceLocation findOverrideInsertLoc(const CXXMethodDecl *method) {
    SourceLocation insertLoc = method->getEndLoc();

    // If method has a body, we need the location before the body starts
    if (method->hasBody()) {
        if (Stmt *body = method->getBody()) {
            insertLoc = body->getBeginLoc().getLocWithOffset(-1);
        }
    }

    return insertLoc;
}

bool hasDirectDerivedClasses(const clang::CXXRecordDecl *base) {
    if (!base->hasDefinition()) {
        return false;
    }

    // Look through all declarations in the same context
    auto *declContext = base->getDeclContext();

    for (auto *decl : declContext->decls()) {
        if (auto *record = llvm::dyn_cast<clang::CXXRecordDecl>(decl)) {
            if (record->isThisDeclarationADefinition() && record != base) {
                // Check if this record derives from our base
                for (const auto &baseSpecifier : record->bases()) {
                    auto baseType = baseSpecifier.getType()->getAs<clang::RecordType>();
                    if (baseType && baseType->getDecl() == base) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

void RefactorHandler::handle_nv_dtor(const CXXRecordDecl *class_decl, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(class_decl->getLocation()) || trySaveLocation(class_decl, SM)) {
        return;
    }

    if (!hasDirectDerivedClasses(class_decl)) {
        return;
    }

    const CXXDestructorDecl *dtor = class_decl->getDestructor();

    rewriter_.InsertTextBefore(dtor->getBeginLoc(), "virtual ");
    const unsigned successDiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "added virtual before destructor");
    Diag.Report(dtor->getLocation(), successDiagID);
}

void RefactorHandler::handle_miss_override(const CXXMethodDecl *method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(method->getLocation())) {
        return;
    }

    rewriter_.InsertTextAfter(findOverrideInsertLoc(method), " override");
    const unsigned successDiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "added override");
    Diag.Report(method->getLocation(), successDiagID);
}

void RefactorHandler::handle_crange_for(const CXXForRangeStmt *for_stmt, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!SM.isInMainFile(for_stmt->getForLoc())) {
        return;
    }
    const VarDecl *loop_var = for_stmt->getLoopVariable();
    if (loop_var->getType()->isFundamentalType()) {
        return;
    }

    rewriter_.InsertTextBefore(loop_var->getLocation(), "&");
    const unsigned successDiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "added &");
    Diag.Report(loop_var->getLocation(), successDiagID);
}
