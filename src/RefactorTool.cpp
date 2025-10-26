#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include <clang/AST/DeclCXX.h>
#include <clang/AST/StmtCXX.h>
#include <clang/Basic/SourceLocation.h>
#include <iostream>

#include <clang/AST/Decl.h>
#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

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

// матчеры для поиска узлов AST
auto NvDtorMatcher() {
    return cxxRecordDecl(unless(isDerivedFrom(anything())), has(cxxRecordDecl()),
                         hasDescendant(cxxDestructorDecl(unless(anyOf(isVirtual(), isImplicit())))))
        .bind("nonVirtualDtor");
}

auto NoOverrideMatcher() { return cxxMethodDecl(isOverride(), unless(isImplicit())).bind("missingOverride"); }

auto NoRefConstVarInRangeLoopMatcher() {
    return cxxForRangeStmt(hasLoopVariable(varDecl(hasType(isConstQualified())))).bind("NoRefConstVarInRangeLoop");
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    // Создаем ClangTool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}