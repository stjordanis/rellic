/*
 * Copyright (c) 2018 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/InferenceRule.h"
#include "rellic/AST/StmtCombine.h"
#include "rellic/AST/Util.h"

namespace rellic {

namespace {

using namespace clang::ast_matchers;

class DerefAddrOfRule : public InferenceRule {
 public:
  DerefAddrOfRule() : InferenceRule(unaryOperator()) {}

  void run(const MatchFinder::MatchResult &result) { DLOG(INFO) << "SATAN"; }

  clang::Stmt *GetOrCreateSubstitution(clang::ASTContext &ctx,
                                       clang::Stmt *loop) {
    CHECK(loop == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator!";
    return nullptr;
  }
};

}  // namespace

char StmtCombine::ID = 0;

StmtCombine::StmtCombine(clang::ASTContext &ctx,
                         rellic::IRToASTVisitor &ast_gen)
    : ModulePass(StmtCombine::ID), ast_ctx(&ctx), ast_gen(&ast_gen) {}

bool StmtCombine::VisitUnaryOperator(clang::UnaryOperator *op) {
  // DLOG(INFO) << "VisitUnaryOperator";
  clang::ast_matchers::MatchFinder::MatchFinderOptions opts;
  clang::ast_matchers::MatchFinder finder(opts);

  DerefAddrOfRule deref_addrof_r;
  finder.addMatcher(deref_addrof_r.GetCondition(), &deref_addrof_r);

  finder.match(*op, *ast_ctx);

  clang::Stmt *sub = nullptr;
  if (deref_addrof_r) {
    sub = deref_addrof_r.GetOrCreateSubstitution(*ast_ctx, op);
  }

  if (sub) {
    substitutions[op] = sub;
  }

  return true;
}

bool StmtCombine::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  llvm::APSInt val;
  bool is_const = ifstmt->getCond()->isIntegerConstantExpr(val, *ast_ctx);
  auto compound = clang::dyn_cast<clang::CompoundStmt>(ifstmt->getThen());
  bool is_empty = compound ? compound->body_empty() : false;
  if ((is_const && !val.getBoolValue()) || is_empty) {
    substitutions[ifstmt] = nullptr;
  }
  return true;
}

bool StmtCombine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  std::vector<clang::Stmt *> new_body;
  for (auto stmt : compound->body()) {
    // Filter out nullptr statements
    if (!stmt) {
      continue;
    }
    // Add only necessary statements
    if (auto expr = clang::dyn_cast<clang::Expr>(stmt)) {
      if (expr->HasSideEffects(*ast_ctx)) {
        new_body.push_back(stmt);
      }
    } else {
      new_body.push_back(stmt);
    }
  }
  // Create the a new compound
  if (changed || new_body.size() < compound->size()) {
    substitutions[compound] = CreateCompoundStmt(*ast_ctx, new_body);
  }
  return true;
}

bool StmtCombine::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Rule-based statement simplification";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

llvm::ModulePass *createStmtCombinePass(clang::ASTContext &ctx,
                                        rellic::IRToASTVisitor &gen) {
  return new StmtCombine(ctx, gen);
}
}  // namespace rellic