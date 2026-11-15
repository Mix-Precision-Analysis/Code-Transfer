#include <clang/AST/AST.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/Lexer.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>

#include <string>
#include <vector>

using namespace clang;
using namespace llvm;
using namespace std;

static Rewriter rewriter;
static vector<pair<string, int>> callCounts;
static vector<string> addressTakenFunctions;
static string OUTPUT_PATH = "";
static string OUTPUT_FILE = "";

static string basenamePath(const string &path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == string::npos)
        return path;
    return path.substr(pos + 1);
}

static string dirnamePath(const string &path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == string::npos)
        return "";
    return path.substr(0, pos + 1);
}

static string functionKey(const FunctionDecl *fd)
{
    if (!fd)
        return "";
    const FunctionDecl *canon = fd->getCanonicalDecl();
    if (!canon)
        canon = fd;
    string name = canon->getNameAsString();
    if (!name.empty())
        return name;
    return fd->getNameAsString();
}

static int getCallCountForKey(const string &key)
{
    for (const auto &entry : callCounts)
    {
        if (entry.first == key)
            return entry.second;
    }
    return 0;
}

static void incrementCallCountForKey(const string &key)
{
    for (auto &entry : callCounts)
    {
        if (entry.first == key)
        {
            entry.second += 1;
            return;
        }
    }
    callCounts.push_back(make_pair(key, 1));
}

static bool containsAddressTakenFunction(const string &key)
{
    for (const auto &entry : addressTakenFunctions)
    {
        if (entry == key)
            return true;
    }
    return false;
}

static void insertAddressTakenFunction(const string &key)
{
    if (containsAddressTakenFunction(key))
        return;
    addressTakenFunctions.push_back(key);
}

class FunctionUsageVisitor : public RecursiveASTVisitor<FunctionUsageVisitor> {
private:
    ASTContext *astContext;

public:
    explicit FunctionUsageVisitor(ASTContext *Context) : astContext(Context) {}

    bool VisitCallExpr(CallExpr *call)
    {
        if (!call || !astContext)
            return true;
        FunctionDecl *callee = call->getDirectCallee();
        if (!callee)
            return true;
        string key = functionKey(callee);
        if (key.empty())
            return true;
        incrementCallCountForKey(key);
        return true;
    }

    bool VisitImplicitCastExpr(ImplicitCastExpr *castExpr)
    {
        if (!castExpr || !astContext)
            return true;
        if (castExpr->getCastKind() != CK_FunctionToPointerDecay)
            return true;
        Expr *sub = castExpr->getSubExpr()->IgnoreParenImpCasts();
        auto *declRef = dyn_cast<DeclRefExpr>(sub);
        if (!declRef)
            return true;
        auto *fd = dyn_cast<FunctionDecl>(declRef->getDecl());
        if (!fd)
            return true;
        string key = functionKey(fd);
        if (!key.empty())
            insertAddressTakenFunction(key);
        return true;
    }

    bool VisitUnaryOperator(UnaryOperator *unOp)
    {
        if (!unOp || !astContext)
            return true;
        if (unOp->getOpcode() != UO_AddrOf)
            return true;
        Expr *sub = unOp->getSubExpr()->IgnoreParenImpCasts();
        auto *declRef = dyn_cast<DeclRefExpr>(sub);
        if (!declRef)
            return true;
        auto *fd = dyn_cast<FunctionDecl>(declRef->getDecl());
        if (!fd)
            return true;
        string key = functionKey(fd);
        if (!key.empty())
            insertAddressTakenFunction(key);
        return true;
    }
};

class CleanupUnusedConsumer : public ASTConsumer {
public:
    explicit CleanupUnusedConsumer(CompilerInstance *CI)
    {
        ASTContext &Context = CI->getASTContext();
        rewriter.setSourceMgr(Context.getSourceManager(), Context.getLangOpts());
    }

    void HandleTranslationUnit(ASTContext &Context) override
    {
        callCounts.clear();
        addressTakenFunctions.clear();

        FunctionUsageVisitor usageVisitor(&Context);
        usageVisitor.TraverseDecl(Context.getTranslationUnitDecl());

        SourceManager &SM = rewriter.getSourceMgr();

        for (Decl *decl : Context.getTranslationUnitDecl()->decls())
        {
            FunctionDecl *fd = dyn_cast<FunctionDecl>(decl);
            if (!fd)
                continue;
            if (!fd->hasBody())
                continue;
            if (fd->isMain())
                continue;
            SourceLocation beginLoc = SM.getSpellingLoc(fd->getBeginLoc());
            if (!beginLoc.isValid() || beginLoc.isMacroID() || !SM.isWrittenInMainFile(beginLoc))
                continue;
            string key = functionKey(fd);
            if (key.empty())
                continue;
            if (containsAddressTakenFunction(key))
                continue;
            int totalCalls = getCallCountForKey(key);
            if (totalCalls > 0)
                continue;
            SourceLocation endLoc = SM.getSpellingLoc(fd->getEndLoc());
            if (!endLoc.isValid())
                continue;
            SourceLocation endToken = Lexer::getLocForEndOfToken(endLoc, 0, SM, Context.getLangOpts());
            if (!endToken.isValid())
                continue;
            CharSourceRange range = CharSourceRange::getCharRange(beginLoc, endToken);
            rewriter.RemoveText(range);
        }

        for (Decl *decl : Context.getTranslationUnitDecl()->decls())
        {
            FunctionDecl *fd = dyn_cast<FunctionDecl>(decl);
            if (!fd)
                continue;
            if (fd->hasBody())
                continue;
            SourceLocation beginLoc = SM.getSpellingLoc(fd->getBeginLoc());
            if (!beginLoc.isValid() || beginLoc.isMacroID() || !SM.isWrittenInMainFile(beginLoc))
                continue;
            string key = functionKey(fd);
            if (key.empty())
                continue;
            if (containsAddressTakenFunction(key))
                continue;
            int totalCalls = getCallCountForKey(key);
            if (totalCalls > 0)
                continue;
            SourceLocation endLoc = SM.getSpellingLoc(fd->getEndLoc());
            if (!endLoc.isValid())
                continue;
            SourceLocation endToken = Lexer::getLocForEndOfToken(endLoc, 0, SM, Context.getLangOpts());
            if (!endToken.isValid())
                continue;
            CharSourceRange range = CharSourceRange::getCharRange(beginLoc, endToken);
            rewriter.RemoveText(range);
        }

        FileID id = SM.getMainFileID();
        string inputPath = SM.getFilename(SM.getLocForStartOfFile(id)).str();
        string outDir = OUTPUT_PATH.empty() ? dirnamePath(inputPath) : OUTPUT_PATH;
        if (!outDir.empty() && outDir.back() != '/')
            outDir += "/";
        string outFile = OUTPUT_FILE.empty() ? (outDir + "clean_" + basenamePath(inputPath)) : OUTPUT_FILE;

        std::error_code OutErrorInfo;
        std::error_code ok;
        llvm::raw_fd_ostream outStream(llvm::StringRef(outFile), OutErrorInfo, llvm::sys::fs::OF_None);
        if (OutErrorInfo != ok)
        {
            llvm::errs() << "Could not create file - " << outFile << "\n";
            return;
        }

        const RewriteBuffer *RewriteBuf = rewriter.getRewriteBufferFor(id);
        if (RewriteBuf)
        {
            outStream << std::string(RewriteBuf->begin(), RewriteBuf->end());
            llvm::errs() << "Output file created - " << outFile << "\n";
            return;
        }

        if (auto MainBuf = SM.getBufferOrNone(id))
        {
            outStream << MainBuf->getBuffer();
            llvm::errs() << "Output file created - " << outFile << " (no rewrite changes)\n";
            return;
        }

        llvm::errs() << "No file created!\n";
    }
};

class CleanupUnusedAction : public PluginASTAction {
protected:
    unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override
    {
        return make_unique<CleanupUnusedConsumer>(&CI);
    }

    bool ParseArgs(const CompilerInstance &CI, const vector<string> &args) override
    {
        llvm::errs() << "Plugin arg size = " << args.size() << "\n";
        for (unsigned i = 0, e = args.size(); i != e; ++i)
        {
            if (args[i] == "-output-path")
            {
                if (i + 1 < e)
                    OUTPUT_PATH = args[i + 1];
                else
                    llvm::errs() << "Missing output path! Could not generate output file.\n";
                llvm::errs() << "Output path = " << OUTPUT_PATH << "\n";
            }
            if (args[i] == "-output-file")
            {
                if (i + 1 < e)
                    OUTPUT_FILE = args[i + 1];
                else
                    llvm::errs() << "Missing output file! Could not generate output file.\n";
                llvm::errs() << "Output file = " << OUTPUT_FILE << "\n";
            }
        }
        return true;
    }
};

static clang::FrontendPluginRegistry::Add<CleanupUnusedAction>
    Y("cleanup-unused", "remove unused function declarations/definitions in main file");
