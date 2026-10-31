// SPDX-FileCopyrightText: 2025 Mike Crowe
// SPDX-License-Identifier: MIT

#include "ModernizeNlohmannJsonExplicitConversionsCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace {
    void replaceAll(std::string &Str, const std::string &From, const std::string &To) {
        assert(!From.empty());

        std::string::size_type StartPos = 0;
        while ((StartPos = Str.find(From, StartPos)) != std::string::npos) {
            Str.replace(StartPos, From.length(), To);
            StartPos += To.length();
        }
    }
}

namespace clang::tidy::modernize {

void NlohmannJsonExplicitConversionsCheck::registerMatchers(
    MatchFinder *Finder) {
  auto Matcher =
      cxxMemberCallExpr(
          on(expr().bind("arg")),
          callee(cxxConversionDecl(ofClass(hasName("nlohmann::basic_json")))
                     .bind("conversionDecl")),
          optionally(expr(isInTemplateInstantiation()).bind("instantiation")))
          .bind("conversionCall");
  Finder->addMatcher(Matcher, this);
}

void NlohmannJsonExplicitConversionsCheck::check(
    const MatchFinder::MatchResult &Result) {
  const auto *Decl =
      Result.Nodes.getNodeAs<CXXConversionDecl>("conversionDecl");
  const auto *Call =
      Result.Nodes.getNodeAs<CXXMemberCallExpr>("conversionCall");
  const auto *Arg = Result.Nodes.getNodeAs<Expr>("arg");

  const QualType DestinationType = Decl->getConversionType();
  std::string DestinationTypeStr =
      DestinationType.getAsString(Result.Context->getPrintingPolicy());
  replaceAll(DestinationTypeStr, "std::basic_string<char>", "std::string");

  const SourceRange ExprRange = Call->getSourceRange();
  if (!ExprRange.isValid())
    return;

  // If we're parsing a template definition then we might be able to
  // fix the conversion if it wasn't dependent on a template
  // parameter. If we did that, then we'll have stored it in
  // AlreadyFixed.
  //
  // We'll match again later during each instantiation. If the fix was
  // already applied then we don't want to warn. If it wasn't then we
  // don't know the type that should be used so just warn so that the
  // location can be fixed by hand.
  if (Result.Nodes.getNodeAs<Expr>("instantiation")) {
    if (!AlreadyFixed.contains(Call->getExprLoc()))
      diag(Call->getExprLoc(),
           "implicit nlohmann::json conversion to templated type should be explicit");
    return;
  }

  bool Deref = false;
  if (const auto *Op = llvm::dyn_cast<UnaryOperator>(Arg);
      Op && Op->getOpcode() == UO_Deref)
    Deref = true;
  else if (const auto *Op = llvm::dyn_cast<CXXOperatorCallExpr>(Arg);
           Op && Op->getOperator() == OO_Star)
    Deref = true;

  llvm::StringRef SourceText = clang::Lexer::getSourceText(
      clang::CharSourceRange::getTokenRange(ExprRange), *Result.SourceManager,
      Result.Context->getLangOpts());

  if (Deref)
    SourceText.consume_front("*");

  const std::string ReplacementText =
      (llvm::Twine(SourceText) + (Deref ? "->" : ".") + "get<" +
       DestinationTypeStr + ">()")
          .str();
  diag(Call->getExprLoc(),
       "implicit nlohmann::json conversion to %0 should be explicit")
      << DestinationTypeStr
      << FixItHint::CreateReplacement(CharSourceRange::getTokenRange(ExprRange),
                                      ReplacementText);

  AlreadyFixed.insert(Call->getExprLoc());
}

} // namespace clang::tidy::modernize
