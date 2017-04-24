// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef __PLUGIN_HXX__
#define __PLUGIN_HXX__

#include <ostream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

#include "info.hxx"
#include "DB.hxx"

namespace mocoda
{

    class DataCollector : public clang::RecursiveASTVisitor<DataCollector>
    {
        typedef clang::RecursiveASTVisitor<DataCollector> Super;
        typedef std::vector<const clang::FunctionDecl *> Declarations;
        typedef std::tuple<const clang::FunctionDecl *, const clang::FunctionDecl *, const clang::Expr *> Edge;

        clang::CompilerInstance & CI;
        const clang::SourceManager & sm;
        const clang::PrintingPolicy & policy;
        const std::string root;
        const std::string lock;
        const std::string cg;
        std::vector<Edge> callgraph_resolved;
        std::vector<Edge> callgraph_unresolved;
        std::unordered_map<const clang::FunctionDecl *, Declarations> defToDecl;
        std::unordered_set<const clang::FunctionDecl *> callDecl;
        std::unordered_map<const clang::FunctionDecl *, Info> cacheInfo;
        std::stack<const clang::FunctionDecl *> stack;
        
    public:

        DataCollector(clang::CompilerInstance & __CI);

        std::tuple<std::string, std::size_t, std::size_t> getFileRange(const clang::FunctionDecl * decl, const bool checkSrc) const;
        Info getInfo(const clang::FunctionDecl * decl, const bool checkSrc);
        Info getVirtualInfo(const clang::FunctionDecl * decl, const bool checkSrc);
        void pushVirtualInfo(DB & db, const clang::FunctionDecl * decl);
        std::pair<std::size_t, std::size_t> getLineColumn(const clang::Expr * expr);
        bool isVirtual(const clang::FunctionDecl * decl);
        void handleFunctionTemplateDecl(clang::FunctionTemplateDecl * decl);
        void getPureDeclaration(const clang::FunctionDecl * decl, Declarations & declarations);
        const clang::FunctionDecl * getFirstVirtualDecl(const clang::FunctionDecl * decl);
        clang::FunctionDecl * getBody(const clang::FunctionDecl * decl, Declarations & declarations);
        clang::FunctionDecl * getBody(const clang::FunctionDecl * decl);
        void handleFunctionDecl(const clang::FunctionDecl * decl);
        void handleDecl(clang::Decl * decl);
        bool VisitClassTemplateDecl(clang::ClassTemplateDecl * decl);
        bool TraverseFunctionDecl(clang::FunctionDecl * decl);
        bool TraverseCXXMethodDecl(clang::CXXMethodDecl * decl);
        bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl * decl);
        bool TraverseCXXConversionDecl(clang::CXXConversionDecl * decl);
        bool TraverseCXXDestructorDecl(clang::CXXDestructorDecl * decl);
        bool VisitLambdaExpr(clang::LambdaExpr * expr);
        bool VisitCallExpr(clang::CallExpr * expr);
        bool VisitCXXConstructExpr(clang::CXXConstructExpr * expr);
        bool handleCall(clang::Expr *, clang::Decl * d);
        bool isContainedInAClassTemplate(clang::FunctionDecl * decl);
        bool isContainedInAClassTemplate(clang::FunctionTemplateDecl * decl);
        void push();

    };

    class DataCollectorConsumer : public clang::ASTConsumer
    {
        clang::CompilerInstance & CI;
        DataCollector visitor;

    public:

        DataCollectorConsumer(clang::CompilerInstance & __CI);
        virtual ~DataCollectorConsumer();

        void HandleTranslationUnit(clang::ASTContext & ctxt) override;
    };


    class DataCollectorAction : public clang::PluginASTAction
    {
        
    protected:

        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI, llvm::StringRef) override;
        bool ParseArgs(const clang::CompilerInstance & CI, const std::vector<std::string> & args) override;

        // Automatically run the plugin after the main AST action
        PluginASTAction::ActionType getActionType() override;
    };

}

#endif // __PLUGIN_HXX__
