// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#include <cstdlib>
#include <fstream>
#include <iostream>

#include <sys/file.h>
#include <unistd.h>

#include "utils.hxx"
#include "plugin.hxx"

namespace mocoda
{

    DataCollector::DataCollector(clang::CompilerInstance & __CI) : CI(__CI), sm(CI.getSourceManager()),
                                                                   policy(CI.getASTContext().getPrintingPolicy()),
                                                                   root(utils::getEnv("MOCODA_ROOT")),
                                                                   lock(utils::getEnv("MOCODA_LOCK")),
                                                                   cg(utils::getEnv("MOCODA_CG"))
    {
        const_cast<clang::PrintingPolicy &>(policy).SuppressTagKeyword = true;
    }

    std::tuple<std::string, std::size_t, std::size_t> DataCollector::getFileRange(const clang::FunctionDecl * decl, const bool checkSrc) const
    {
        const clang::SourceRange range = decl->getSourceRange();
        const clang::SourceLocation beginLoc = sm.getExpansionLoc(range.getBegin());
        const clang::FileID id = sm.getFileID(beginLoc);
        const clang::FileEntry * entry = sm.getFileEntryForID(id);

        if (entry && range.isValid())
        {
            const std::string rpath = utils::getRealPath(entry->getName());
            std::string tpath;
            if (checkSrc)
            {
                if (utils::startswith(rpath, root))
                {
                    tpath = rpath.substr(root.length());
                }
            }
            else
            {
                tpath = rpath;
            }

            if (!tpath.empty())
            {
                const clang::SourceLocation endLoc = sm.getExpansionLoc(range.getEnd());
                return std::make_tuple(tpath,
                                       sm.getExpansionLineNumber(beginLoc),
                                       sm.getExpansionLineNumber(endLoc));
            }
        }

        return std::make_tuple("", 0, 0);
    }

    std::pair<std::size_t, std::size_t> DataCollector::getLineColumn(const clang::Expr * expr)
    {
        const clang::SourceRange range = expr->getSourceRange();
        const clang::SourceLocation beginLoc = sm.getExpansionLoc(range.getBegin());

        return std::pair<std::size_t, std::size_t>(sm.getExpansionLineNumber(beginLoc),
                                                   sm.getExpansionColumnNumber(beginLoc));
    }

    bool DataCollector::isVirtual(const clang::FunctionDecl * decl)
    {
        if (const clang::CXXMethodDecl * cmd = clang::dyn_cast<clang::CXXMethodDecl>(decl))
        {
            return cmd->isVirtual();
        }
        return false;
    }

    const clang::FunctionDecl * DataCollector::getFirstVirtualDecl(const clang::FunctionDecl * decl)
    {
        if (const clang::CXXMethodDecl * cmd = clang::dyn_cast<clang::CXXMethodDecl>(decl))
        {
            if (cmd->isVirtual())
            {
                auto i = cmd->begin_overridden_methods();
                if (i != cmd->end_overridden_methods())
                {
                    return *i;
                }
            }
        }

        return nullptr;
    }
    
    Info DataCollector::getVirtualInfo(const clang::FunctionDecl * decl, const bool checkSrc)
    {
        if (const clang::FunctionDecl * virt = getFirstVirtualDecl(decl))
        {
            return getInfo(virt, checkSrc);
        }
        return Info();
    }
    
    Info DataCollector::getInfo(const clang::FunctionDecl * decl, const bool checkSrc)
    {
        auto i = cacheInfo.find(decl);
        if (i == cacheInfo.end())
        {
            const auto fn = getFileRange(decl, checkSrc);
            if (!std::get<0>(fn).empty() && std::get<1>(fn) && std::get<2>(fn))
            {
                std::string s;
                llvm::raw_string_ostream out(s);
                decl->getNameForDiagnostic(out, policy, true);
                out << '(';
                bool first = true;
                for (auto && parameter : decl->parameters())
                {
                    if (!first)
                    {
                        out << ", ";
                    }
                    else
                    {
                        first = false;
                    }
                    clang::QualType qtype = parameter->getOriginalType();
                    const clang::Type * type = qtype.getTypePtrOrNull();
                    if (type && type->isDependentType())
                    {
                        return cacheInfo.emplace(decl, Info()).first->second;
                    }
                    out << qtype.getDesugaredType(decl->getASTContext()).getAsString(policy);
                }
                out << ')';

                if (decl->getType()->getAs<clang::FunctionType>()->isConst())
                {
                    out << " const";
                }

                return cacheInfo.emplace(decl,
                                         Info(std::get<0>(fn),
                                              out.str(),
                                              std::get<1>(fn),
                                              std::get<2>(fn))).first->second;
            }
        }
        else
        {
            return i->second;
        }

        return cacheInfo.emplace(decl, Info()).first->second;
    }

    bool DataCollector::isContainedInAClassTemplate(clang::FunctionDecl * decl)
    {
        if (clang::CXXRecordDecl * rec = clang::dyn_cast_or_null<clang::CXXRecordDecl>(decl->getParent()))
        {
            if (rec->getDescribedClassTemplate() || clang::ClassTemplateSpecializationDecl::classof(rec))
            {
                return true;
            }
        }
        return false;
    }

    bool DataCollector::isContainedInAClassTemplate(clang::FunctionTemplateDecl * decl)
    {
        return isContainedInAClassTemplate(decl->getTemplatedDecl());
    }

    void DataCollector::handleFunctionTemplateDecl(clang::FunctionTemplateDecl * decl)
    {
        for (auto && specialization : decl->specializations())
        {
            handleFunctionDecl(specialization);
        }
    }

    void DataCollector::getPureDeclaration(const clang::FunctionDecl * decl, Declarations & declarations)
    {
        if (const clang::CXXMethodDecl * cmd = clang::dyn_cast<clang::CXXMethodDecl>(decl))
        {
            for (auto && o : cmd->overridden_methods())
            {
                if (o->isPure())
                {
                    getPureDeclaration(o, declarations);
                    declarations.push_back(o);
                    return;
                }
            }
        }
    }

    clang::FunctionDecl * DataCollector::getBody(const clang::FunctionDecl * decl, Declarations & declarations)
    {
        clang::FunctionDecl * declWithBody = nullptr;
        for (auto && rd : decl->redecls())
        {
            if (!rd->isDeleted() && !rd->isDefaulted())
            {
                if (rd->doesThisDeclarationHaveABody())
                {
                    declWithBody = rd;
                }
                else
                {
                    declarations.push_back(rd);
                }
            }
        }
        getPureDeclaration(decl, declarations);
        return declWithBody;
    }

    clang::FunctionDecl * DataCollector::getBody(const clang::FunctionDecl * decl)
    {
        clang::FunctionDecl * declWithBody = nullptr;
        for (auto && rd : decl->redecls())
        {
            if (!rd->isDeleted() && !rd->isDefaulted())
            {
                if (rd->doesThisDeclarationHaveABody())
                {
                    declWithBody = rd;
                }
            }
        }

        return declWithBody;
    }

    void DataCollector::handleFunctionDecl(const clang::FunctionDecl * decl)
    {
        if (!decl->isDeleted())
        {
            auto i = defToDecl.find(decl);
            if (i == defToDecl.end())
            {
                Declarations declarations;
                if (clang::FunctionDecl * declWithBody = getBody(decl, declarations))
                {
                    if (clang::FunctionTemplateDecl * fd = declWithBody->getDescribedFunctionTemplate())
                    {
                        handleFunctionTemplateDecl(fd);
                    }
                    else if (getInfo(declWithBody, true))
                    {
                        defToDecl.emplace(declWithBody, declarations);
                        stack.push(declWithBody);
                        Super::TraverseFunctionDecl(declWithBody);
                        stack.pop();
                    }
                }
            }
        }
    }

    void DataCollector::handleDecl(clang::Decl * decl)
    {
        if (decl)
        {
            if (clang::FunctionDecl * fd = clang::dyn_cast<clang::FunctionDecl>(decl))
            {
                handleFunctionDecl(fd);
            }
            else if (clang::FunctionTemplateDecl * ftd = clang::dyn_cast<clang::FunctionTemplateDecl>(decl))
            {
                handleFunctionTemplateDecl(ftd);
            }
            else if (clang::FriendDecl * fd = clang::dyn_cast<clang::FriendDecl>(decl))
            {
                handleDecl(fd->getFriendDecl());
            }
        }
    }

    bool DataCollector::VisitCallExpr(clang::CallExpr * expr)
    {
        return handleCall(expr, expr->getCalleeDecl());
    }

    bool DataCollector::VisitCXXConstructExpr(clang::CXXConstructExpr * expr)
    {
        return handleCall(expr, expr->getConstructor());
    }

    bool DataCollector::handleCall(clang::Expr * expr, clang::Decl * d)
    {
        if (!cg.empty() && d && !stack.empty())
        {
            const clang::FunctionDecl * caller = stack.top();
            if (clang::FunctionDecl * callee = clang::dyn_cast<clang::FunctionDecl>(d))
            {
                if (Info info = getInfo(callee, true))
                {
                    if (const clang::FunctionDecl * calleeWithBody = getBody(callee))
                    {
                        // we call a function which has a body
                        handleFunctionDecl(calleeWithBody);
                        callgraph_resolved.emplace_back(caller, calleeWithBody, expr);
                    }
                    else if (!callee->isDeleted() && !callee->isDefaulted() && !callee->getBuiltinID())
                    {
                        // we call a function with only declarations (i.e. no definitions)
                        // so we need to postpone the definition resolution
                        callgraph_unresolved.emplace_back(caller, callee, expr);
                    }
                }
            }
        }

        return true;
    }

    bool DataCollector::VisitLambdaExpr(clang::LambdaExpr * expr)
    {
        return true;
    }

    bool DataCollector::VisitClassTemplateDecl(clang::ClassTemplateDecl * decl)
    {
        for (auto && specialization : decl->specializations())
        {
            for (auto && d : specialization->decls())
            {
                handleDecl(d);
            }
        }

        return true;
    }

    bool DataCollector::TraverseFunctionDecl(clang::FunctionDecl * decl)
    {
        if (!decl->isDeleted() && !isContainedInAClassTemplate(decl))
        {
            handleFunctionDecl(decl);
        }
        return true;
    }

    bool DataCollector::TraverseCXXMethodDecl(clang::CXXMethodDecl * decl)
    {
        return TraverseFunctionDecl(decl);
    }

    bool DataCollector::TraverseCXXConstructorDecl(clang::CXXConstructorDecl * decl)
    {
        return TraverseFunctionDecl(decl);
    }

    bool DataCollector::TraverseCXXConversionDecl(clang::CXXConversionDecl * decl)
    {
        return TraverseFunctionDecl(decl);
    }

    bool DataCollector::TraverseCXXDestructorDecl(clang::CXXDestructorDecl * decl)
    {
        return TraverseFunctionDecl(decl);
    }

    void DataCollector::pushVirtualInfo(DB & db, const clang::FunctionDecl * decl)
    {
        if (const clang::CXXMethodDecl * cmd = clang::dyn_cast<clang::CXXMethodDecl>(decl))
        {
            Info info = getInfo(decl, true);
            for (auto && o : cmd->overridden_methods())
            {
                if (!o->isDeleted() && !o->isDefaulted())
                {
                    if (o->doesThisDeclarationHaveABody())
                    {
                        if (Info oInfo = getInfo(o, true))
                        {
                            db.insertDefinition(oInfo);
                            db.insertVirtualResolved(info, oInfo);
                        }
                    }
                    else
                    {
                        if (Info oInfo = getInfo(o, true))
                        {
                            db.insertDeclaration(oInfo);
                            db.insertVirtualUnresolved(info, oInfo);
                        }
                    }
                }
            }
        }
    }
    
    void DataCollector::push()
    {
        const int fd = open(lock.c_str(), O_RDONLY);
        const int s = flock(fd, LOCK_EX);
        if (s == 0)
        {
            DB db;
            for (auto && i : defToDecl)
            {
                Info def = getInfo(i.first, true);
                db.insertDefinition(def);
                for (auto && j : i.second)
                {
                    Info decl = getInfo(j, true);
                    db.insertDeclaration(decl, def);
                }
                pushVirtualInfo(db, i.first);
            }

            if (!cg.empty())
            {
                for (auto && i : callgraph_resolved)
                {
                    Info def1 = getInfo(std::get<0>(i), true);
                    Info def2 = getInfo(std::get<1>(i), true);
                    const auto lc = getLineColumn(std::get<2>(i));
                    db.insertCallResolved(def1, def2, lc.first, lc.second, isVirtual(std::get<1>(i)));
                }
                
                for (auto && i : callgraph_unresolved)
                {
                    Info def = getInfo(std::get<0>(i), true);
                    Info dec = getInfo(std::get<1>(i), true);
                    db.insertDeclaration(dec);
                    const auto lc = getLineColumn(std::get<2>(i));
                    db.insertCallUnresolved(def, dec, lc.first, lc.second, isVirtual(std::get<1>(i)));
                }
            }
            
            for (auto && i : defToDecl)
            {
                pushVirtualInfo(db, i.first);
            }

            db.commit();

            flock(fd, LOCK_UN);
            close(fd);
        }
    }

    DataCollectorConsumer::DataCollectorConsumer(clang::CompilerInstance & __CI) : clang::ASTConsumer(), CI(__CI), visitor(__CI) { }

    DataCollectorConsumer::~DataCollectorConsumer() { }

    void DataCollectorConsumer::HandleTranslationUnit(clang::ASTContext & ctxt)
    {
        visitor.TraverseDecl(ctxt.getTranslationUnitDecl());
        visitor.push();
    }

    std::unique_ptr<clang::ASTConsumer> DataCollectorAction::CreateASTConsumer(clang::CompilerInstance & CI, llvm::StringRef)
    {
        CI.getFrontendOpts().SkipFunctionBodies = false;
        return llvm::make_unique<DataCollectorConsumer>(CI);
    }

    bool DataCollectorAction::ParseArgs(const clang::CompilerInstance & CI, const std::vector<std::string> & args)
    {
        return true;
    }

    // Automatically run the plugin after the main AST action
    clang::PluginASTAction::ActionType DataCollectorAction::getActionType()
    {
        return AddAfterMainAction;
    }
}

namespace
{
    static clang::FrontendPluginRegistry::Add<mocoda::DataCollectorAction> X("mocoda", "Collect data for Mozilla code");
}


// SELECT *, COUNT(*) c FROM definitions GROUP BY FUNNAME HAVING c > 1 and BEGIN=85;
// SELECT * from definitions WHERE FUNNAME="CycleCollectionNoteChild<nsPIDOMWindowInner>(nsCycleCollectionTraversalCallback &, nsPIDOMWindowInner *, const char *, uint32_t)";
// SELECT * from definitions WHERE FUNNAME="";
// select * from declarations    where FUNNAME="nsTArray_Impl<mozilla::StyleSheet *, nsTArrayInfallibleAllocator>::InsertElementsAt(unsigned long, unsigned long, const mozilla::fallible_t &)";

/*

"246798"	"247947"	"2788"	"0"

"dom/bindings/BindingUtils.h"	"mozilla::dom::DeferredFinalizerImpl<mozilla::dom::SVGAnimatedTransformList>::AppendDeferredFinalizePointer(void *, void *)"	"2781"	"2790"	"0"


"dom/bindings/BindingUtils.h"	"mozilla::dom::DeferredFinalizerImpl<mozilla::dom::SVGAnimatedTransformList>::AppendAndTake<mozilla::dom::SVGAnimatedTransformList>(SegmentedVector<RefPtr<mozilla::dom::SVGAnimatedTransformList> > &, mozilla::dom::SVGAnimatedTransformList *)"	"2769"	"2773"	"0"


"212104"	"gfx/layers/wr/WebRenderBridgeParent.cpp"	"mozilla::layers::WebRenderBridgeParent::RecvDPEnd(const gfx::IntSize &, InfallibleTArray<WebRenderParentCommand> &&, InfallibleTArray<OpDestroy> &&, const uint64_t &, const uint64_t &, const ByteBuffer &, const WrBuiltDisplayListDescriptor &, const ByteBuffer &, const WrAuxiliaryListsDescriptor &)"	"260"	"274"	"9931"
*/

