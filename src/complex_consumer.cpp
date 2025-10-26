#include "complex_consumer.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

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