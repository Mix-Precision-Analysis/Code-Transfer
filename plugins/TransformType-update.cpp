// trans-type Clang plugin
//

#include "TransformType.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <cstring>
#include <regex>
#include <clang/Lex/Lexer.h>
#include <clang/AST/ParentMapContext.h>
#include <llvm/ADT/SmallString.h>
#include "../utilities/json.hpp"

using namespace clang;
using namespace std;
using namespace llvm;

nlohmann::json jf;
string funcName;
string current_ParamName = "";
string current_ParamType = "";
string current_InternalParamType = "";
string current_ParamShimName = "";
string OUTPUT_PATH = "";
string INPUT_CONFIG = "config_temp.json";
bool ENABLE_CHANGE = false;
bool ENABLE_FIELD_CHANGE = false;
bool ENABLE_X86_FP16_ABI_COMPAT = false;
bool STRICT_CONFIG = false;
bool CALLER_DRIVEN_CLONE = false;
enum TransformPhase {
    PHASE_ALL,
    PHASE_TYPES_ONLY,
    PHASE_CLONE_ONLY
};
TransformPhase TRANSFORM_PHASE = PHASE_ALL;
int libcall_flag = 0;
static unordered_set<string> replacedTypeLocs;
static unordered_set<string> externalAbiProtectedVarKeys;
static vector<string> rewrittenCallNameLocs;
static vector<string> insertedOverloadKeys;
static vector<string> insertedFunctionNames;
static vector<string> insertedFunctionDecls;
static vector<string> insertedFunctionPrototypeTexts;
struct PendingFunctionPrototype
{
    unsigned sourceOffset;
    string calleeName;
    string text;
};

// Keep each generated clone prototype at the source offset where its own
// original declaration became visible.  A single shared insertion offset is
// unsafe: a later unrelated callee can move the whole declaration block past
// an earlier rewritten call site.
static vector<PendingFunctionPrototype> pendingFunctionPrototypes;
static vector<string> pendingExternalAbiPrototypes;
static vector<string> pendingFunctionDefinitions;
static vector<pair<string, string>> clonedFunctionNameBySignature;
static vector<pair<string, string>> externalAbiWrapperNameBySignature;
static vector<pair<string, string>> preservedFunctionBodies;
static vector<pair<string, string>> preservedFunctionTexts;
static vector<string> rewrittenParamRefLocs;
static vector<string> rewrittenGlobalDeclStmtKeys;
static vector<string> rewrittenFloatingLiteralLocs;
static bool prototypeInsertLocReady = false;
static unsigned basePrototypeInsertOffset = 0;
static bool externalAbiInsertLocReady = false;
static unsigned externalAbiInsertOffset = 0;
// Original-AST usage accounting for safe caller-driven clone cleanup.
// A static source function is removable only when every DeclRefExpr is a
// direct call and every one of those calls was successfully redirected.
static unordered_map<const FunctionDecl *, unsigned> originalFunctionRefCounts;
static unordered_map<const FunctionDecl *, unsigned> originalFunctionCallCounts;
static unordered_map<const FunctionDecl *, unsigned> rewrittenOriginalCallCounts;
// Functions excluded from the precision search can still be storage
// boundaries.  Their bodies stay fixed, while pointer/array parameter types
// are derived from the transformed caller storage.
static unordered_set<string> fixedBoundaryFunctions;

struct VarRule {
    string location;   // globalVar | localVar | parmVar
    string name;
    string func;       // empty for globals
    string file;
    string type;
    int line = -1;     // optional
};

struct CallRule {
    string callerFunc;
    string file;
    string targetCallee; // desired callee name
    int line = -1;
    int column = -1;
};

struct FieldRule {
    string record;
    int index = -1;
    string type;
};

vector<VarRule> varRules;
vector<CallRule> callRules;
vector<FieldRule> fieldRules;
vector<VarRule> returnRules;

string replaceX86Fp80WithLongDouble(string typeName)
{
    const string needle = "x86_fp80";
    const string replacement = "long double";
    size_t pos = 0;
    while ((pos = typeName.find(needle, pos)) != string::npos)
    {
        typeName.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return typeName;
}

string trimSpaces(const string &s);
string stripHeaderPrefixFromCallName(const string &name);
bool parseBoolFlagValue(const string &value, bool &out);
string preserveConstPrefix(const string &oldType, const string &newType);
string stripLeadingConst(string typeName);
string normalizeTypeForSignature(string typeName);

static const FunctionDecl *canonicalFunctionDecl(const FunctionDecl *func)
{
    if (!func)
        return nullptr;
    const FunctionDecl *canonical = func->getCanonicalDecl();
    return canonical ? canonical : func;
}

class OriginalFunctionUsageVisitor
    : public RecursiveASTVisitor<OriginalFunctionUsageVisitor> {
public:
    bool VisitDeclRefExpr(DeclRefExpr *ref)
    {
        if (!ref)
            return true;
        auto *func = dyn_cast<FunctionDecl>(ref->getDecl());
        const FunctionDecl *canonical = canonicalFunctionDecl(func);
        if (canonical)
            ++originalFunctionRefCounts[canonical];
        return true;
    }

    bool VisitCallExpr(CallExpr *call)
    {
        if (!call)
            return true;
        const FunctionDecl *canonical =
            canonicalFunctionDecl(call->getDirectCallee());
        if (canonical)
            ++originalFunctionCallCounts[canonical];
        return true;
    }
};

string normalizeTypeForCompare(string typeName)
{
    return trimSpaces(replaceX86Fp80WithLongDouble(typeName));
}

string normalizeNewType(const string &oldType, string newType)
{
    // Avoid duplicate qualifiers when replacing only the base type.
    auto startsWithConst = [](const string &s) {
        return s.rfind("const ", 0) == 0;
    };
    if (startsWithConst(oldType) && startsWithConst(newType))
    {
        newType = newType.substr(strlen("const "));
    }
    return replaceX86Fp80WithLongDouble(newType);
}

string getSourceTextRaw(CharSourceRange range, ASTContext *astContext);
string getSourceTextPreferRewritten(CharSourceRange range, ASTContext *astContext);
string getFunctionSpecifiers(FunctionDecl *callee);
string getFileName(ASTContext *Context, VarDecl *val);
string getFileName(ASTContext *Context, FunctionDecl *val);
int getLineNumber(ASTContext *Context, SourceLocation loc);
string getNewType(string valueName, string typeName, string funcName, string fileName, string location, int line);
string getNewReturnType(string funcName, string fileName, string typeName, int line);
string locKey(SourceLocation loc, ASTContext *astContext);
string getExprTypeAfterTransform(Expr *expr, ASTContext *astContext, const string &callerFunc);
string getDirectPrecisionSourceType(Expr *expr, ASTContext *astContext, const string &callerFunc);
string getConfiguredParamTypeForCallee(FunctionDecl *callee, unsigned index, ASTContext *astContext);
string pickClonedFunctionName(FunctionDecl *callee, const vector<string> &desiredParamTypes, ASTContext *astContext);
bool hasMatchingFunctionByName(FunctionDecl *callee, const string &name, const vector<string> &desiredParamTypes, ASTContext *astContext);
string buildClonedFunctionWithName(FunctionDecl *callee, const string &newName, const vector<string> &desiredParamTypes, ASTContext *astContext);
void insertFunctionPrototype(FunctionDecl *callee, const string &name, const vector<string> &desiredParamTypes, ASTContext *astContext);
bool isFloatingScalarTypeString(const string &typeName);
bool isFloatingPointerTypeString(const string &typeName);
bool isAggregateStorageType(const string &typeName);
string getFloatingStorageBaseType(string typeName);
bool isAddressOfNonArrayScalar(Expr *expr);
VarDecl *getAddressedNonArrayScalarVar(Expr *expr);
string buildExternalAbiWrapperName(FunctionDecl *callee);
string buildExternalAbiWrapperFunction(FunctionDecl *callee,
                                       const string &wrapperName,
                                       const vector<string> &actualParamTypes,
                                       const vector<string> &declaredParamTypes,
                                       const vector<bool> &scalarPointerBridges);
string buildExternalAbiWrapperPrototype(FunctionDecl *callee,
                                        const string &wrapperName,
                                        const vector<string> &actualParamTypes,
                                        const vector<string> &declaredParamTypes);
string pickLibmCallNameForActualTypes(const string &calleeName, const vector<string> &actualParamTypes);
static bool hasRelevantRulesForFile(const string &fileName);
static bool shouldRewriteLoc(ASTContext *astContext, SourceLocation loc);
string buildParamDeclText(const string &typeName, const string &paramName);
static string storageClassPrefix(VarDecl *var);

bool isExactFp16AbiIncompatibleType(const string &typeName)
{
    string t = normalizeTypeForSignature(stripLeadingConst(typeName));
    return t == "__fp16";
}

string getAbiCompatibleInterfaceType(const string &typeName)
{
    if (!ENABLE_X86_FP16_ABI_COMPAT)
        return typeName;
    if (!isExactFp16AbiIncompatibleType(typeName))
        return typeName;
    return preserveConstPrefix(typeName, "float");
}

bool needsReturnAbiShim(const string &desiredType, const string &interfaceType)
{
    return ENABLE_X86_FP16_ABI_COMPAT &&
           isExactFp16AbiIncompatibleType(desiredType) &&
           normalizeTypeForSignature(desiredType) != normalizeTypeForSignature(interfaceType);
}

bool needsParamAbiShim(const string &desiredType, const string &interfaceType)
{
    return needsReturnAbiShim(desiredType, interfaceType);
}

void collectReturnStmts(Stmt *stmt, vector<ReturnStmt *> &returns)
{
    if (!stmt)
        return;
    if (auto *ret = dyn_cast<ReturnStmt>(stmt))
        returns.push_back(ret);
    for (Stmt *child : stmt->children())
        collectReturnStmts(child, returns);
}

int returnedParameterIndex(FunctionDecl *callee)
{
    if (!callee || !callee->hasBody())
        return -1;
    vector<ReturnStmt *> returns;
    collectReturnStmts(callee->getBody(), returns);
    if (returns.empty())
        return -1;
    int commonIndex = -1;
    for (ReturnStmt *ret : returns)
    {
        Expr *value = ret ? ret->getRetValue() : nullptr;
        value = value ? value->IgnoreParenImpCasts() : nullptr;
        auto *ref = dyn_cast_or_null<DeclRefExpr>(value);
        auto *param = ref ? dyn_cast<ParmVarDecl>(ref->getDecl()) : nullptr;
        if (!param)
            return -1;
        int index = static_cast<int>(param->getFunctionScopeIndex());
        if (commonIndex < 0)
            commonIndex = index;
        else if (commonIndex != index)
            return -1;
    }
    return commonIndex;
}

string inferReturnTypeFromDesiredParameter(FunctionDecl *callee,
                                           const vector<string> &desiredParamTypes)
{
    if (!callee || !isAggregateStorageType(callee->getReturnType().getAsString()))
        return "";
    int index = returnedParameterIndex(callee);
    if (index < 0 || static_cast<size_t>(index) >= desiredParamTypes.size())
        return "";
    string desired = desiredParamTypes[static_cast<size_t>(index)];
    if (!isAggregateStorageType(desired))
        return "";
    return desired;
}

void collectLocalVarDecls(Stmt *stmt, vector<VarDecl *> &vars)
{
    if (!stmt)
        return;
    if (auto *declStmt = dyn_cast<DeclStmt>(stmt))
        for (Decl *decl : declStmt->decls())
            if (auto *var = dyn_cast<VarDecl>(decl))
                vars.push_back(var);
    for (Stmt *child : stmt->children())
        collectLocalVarDecls(child, vars);
}

ParmVarDecl *directParameterInitializer(VarDecl *var)
{
    if (!var || !var->hasInit())
        return nullptr;
    Expr *init = var->getInit()->IgnoreParenImpCasts();
    auto *ref = dyn_cast_or_null<DeclRefExpr>(init);
    return ref ? dyn_cast<ParmVarDecl>(ref->getDecl()) : nullptr;
}

ParmVarDecl *pointerExpressionParameter(Expr *expr)
{
    if (!expr)
        return nullptr;
    expr = expr->IgnoreParenImpCasts();
    if (auto *ref = dyn_cast<DeclRefExpr>(expr))
        return dyn_cast<ParmVarDecl>(ref->getDecl());
    if (auto *unary = dyn_cast<UnaryOperator>(expr))
        return pointerExpressionParameter(unary->getSubExpr());
    if (auto *subscript = dyn_cast<ArraySubscriptExpr>(expr))
        return pointerExpressionParameter(subscript->getBase());
    return nullptr;
}

void collectAssignedParameterAliases(Stmt *stmt, VarDecl *var,
                                     vector<ParmVarDecl *> &params)
{
    if (!stmt || !var)
        return;
    if (auto *assign = dyn_cast<BinaryOperator>(stmt))
    {
        if (assign->getOpcode() == BO_Assign)
        {
            Expr *lhs = assign->getLHS()->IgnoreParenImpCasts();
            auto *ref = dyn_cast<DeclRefExpr>(lhs);
            if (ref && ref->getDecl() == var)
            {
                ParmVarDecl *param =
                    pointerExpressionParameter(assign->getRHS());
                if (param)
                    params.push_back(param);
            }
        }
    }
    for (Stmt *child : stmt->children())
        collectAssignedParameterAliases(child, var, params);
}

ParmVarDecl *parameterAliasSource(VarDecl *var, Stmt *body)
{
    if (ParmVarDecl *param = directParameterInitializer(var))
        return param;
    vector<ParmVarDecl *> params;
    collectAssignedParameterAliases(body, var, params);
    if (params.empty())
        return nullptr;
    ParmVarDecl *common = params.front();
    for (ParmVarDecl *param : params)
        if (param != common)
            return nullptr;
    return common;
}

bool rewriteAliasVarBaseType(VarDecl *var, const string &targetType,
                             ASTContext *astContext)
{
    if (!var || !astContext || !isAggregateStorageType(var->getType().getAsString()) ||
        !isAggregateStorageType(targetType))
        return false;
    string oldBase = getFloatingStorageBaseType(var->getType().getAsString());
    string newBase = getFloatingStorageBaseType(targetType);
    if (oldBase.empty() || newBase.empty() ||
        normalizeTypeForCompare(oldBase) == normalizeTypeForCompare(newBase))
        return false;
    SourceLocation begin = var->getTypeSpecStartLoc();
    SourceLocation end = var->getTypeSpecEndLoc();
    if (!begin.isValid() || !end.isValid())
        return false;
    CharSourceRange range = CharSourceRange::getTokenRange(begin, end);
    // TypeSourceRange for a pointer declaration includes its `*` tokens;
    // replacing it with only the floating base would turn `double *temp`
    // into the scalar declaration `double temp`.
    rewriter.ReplaceText(range,
                         preserveConstPrefix(var->getType().getAsString(),
                                             targetType));
    return true;
}

void restoreParameterAliasLocals(FunctionDecl *func, ASTContext *astContext)
{
    if (!func || !func->hasBody())
        return;
    vector<VarDecl *> vars;
    collectLocalVarDecls(func->getBody(), vars);
    for (VarDecl *var : vars)
    {
        ParmVarDecl *param = parameterAliasSource(var, func->getBody());
        if (param)
            rewriteAliasVarBaseType(var, param->getType().getAsString(), astContext);
    }
}

string rewriteParameterAliasLocalsForClone(string bodyText,
                                           FunctionDecl *callee,
                                           const vector<string> &desiredParamTypes,
                                           ASTContext *astContext)
{
    if (!callee || !callee->hasBody() || !astContext)
        return bodyText;
    vector<VarDecl *> vars;
    collectLocalVarDecls(callee->getBody(), vars);
    for (VarDecl *var : vars)
    {
        ParmVarDecl *param = parameterAliasSource(var, callee->getBody());
        if (!param)
            continue;
        unsigned index = param->getFunctionScopeIndex();
        if (index >= desiredParamTypes.size())
            continue;
        // clone-only first restores the original body alias to the AST
        // parameter type; derive the text replacement from that parameter,
        // not from the independently configured VarDecl type.
        string oldBase = getFloatingStorageBaseType(param->getType().getAsString());
        string newBase = getFloatingStorageBaseType(desiredParamTypes[index]);
        if (oldBase.empty() || newBase.empty() ||
            normalizeTypeForCompare(oldBase) == normalizeTypeForCompare(newBase))
            continue;
        CharSourceRange declRange = CharSourceRange::getTokenRange(var->getSourceRange());
        string declText = getSourceTextPreferRewritten(declRange, astContext);
        if (declText.empty())
            continue;
        string replacement = declText;
        regex basePattern("\\b" + oldBase + "\\b");
        replacement = regex_replace(replacement, basePattern, newBase,
                                    regex_constants::format_first_only);
        size_t pos = bodyText.find(declText);
        if (pos != string::npos)
            bodyText.replace(pos, declText.size(), replacement);
    }
    return bodyText;
}

string rewriteReturnStatementsForAbiShim(const string &bodyText,
                                         FunctionDecl *callee,
                                         ASTContext *astContext,
                                         const string &desiredType,
                                         const string &interfaceType)
{
    if (!needsReturnAbiShim(desiredType, interfaceType))
        return bodyText;
    if (!callee || !callee->hasBody() || !astContext)
        return bodyText;

    string rewritten = bodyText;
    vector<ReturnStmt *> returns;
    collectReturnStmts(callee->getBody(), returns);
    for (ReturnStmt *ret : returns)
    {
        Expr *retExpr = ret->getRetValue();
        if (!retExpr)
            continue;
        CharSourceRange retRange = CharSourceRange::getTokenRange(ret->getSourceRange());
        CharSourceRange exprRange = CharSourceRange::getTokenRange(retExpr->getSourceRange());
        string retText = trimSpaces(getSourceTextPreferRewritten(retRange, astContext));
        string exprText = trimSpaces(getSourceTextPreferRewritten(exprRange, astContext));
        if (retText.empty() || exprText.empty())
            continue;
        string replacement = "return (" + interfaceType + ")((" + desiredType + ")(" + exprText + "))";
        size_t pos = rewritten.find(retText);
        if (pos == string::npos)
            continue;
        rewritten.replace(pos, retText.size(), replacement);
    }
    return rewritten;
}

string stripArraySuffix(const string &typeName)
{
    size_t bracket = typeName.find('[');
    if (bracket != string::npos)
        return typeName.substr(0, bracket);
    return typeName;
}

string preserveConstPrefix(const string &oldType, const string &newType)
{
    string result = newType;
    if (oldType.rfind("const ", 0) == 0 && newType.rfind("const ", 0) != 0)
        result = "const " + newType;
    while (result.rfind("const const ", 0) == 0)
        result = result.substr(strlen("const "));
    return result;
}

string stripLeadingConst(string typeName)
{
    while (typeName.rfind("const ", 0) == 0)
        typeName = typeName.substr(strlen("const "));
    return typeName;
}

string stripNonConstTypeQualifiers(string typeName)
{
    static const vector<string> qualifiers = {
        "__restrict__",
        "__restrict",
        "restrict",
        "volatile"
    };
    for (const string &qualifier : qualifiers)
    {
        size_t pos = 0;
        while ((pos = typeName.find(qualifier, pos)) != string::npos)
        {
            bool leftOk = (pos == 0) ||
                          !(isalnum(static_cast<unsigned char>(typeName[pos - 1])) || typeName[pos - 1] == '_');
            size_t after = pos + qualifier.size();
            bool rightOk = (after >= typeName.size()) ||
                           !(isalnum(static_cast<unsigned char>(typeName[after])) || typeName[after] == '_');
            if (leftOk && rightOk)
            {
                typeName.replace(pos, qualifier.size(), " ");
                pos += 1;
                continue;
            }
            pos = after;
        }
    }
    return typeName;
}

string stripPointerSuffix(string typeName)
{
    while (!typeName.empty() && (typeName.back() == ' ' || typeName.back() == '*'))
    {
        if (typeName.back() == '*')
        {
            typeName.pop_back();
        }
        else
        {
            typeName.pop_back();
        }
    }
    return typeName;
}

string trimSpaces(const string &s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

string stripHeaderPrefixFromCallName(const string &name)
{
    // Preserve namespace-qualified names like std::sqrt.
    if (name.find("::") != string::npos)
        return name;
    size_t pos = name.rfind(':');
    if (pos == string::npos)
        return name;
    if (pos + 1 >= name.size())
        return name;
    return name.substr(pos + 1);
}

bool parseBoolFlagValue(const string &value, bool &out)
{
    string v = value;
    for (char &c : v)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v == "1" || v == "true" || v == "on" || v == "yes")
    {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "off" || v == "no")
    {
        out = false;
        return true;
    }
    return false;
}

bool isTupleType(const string &typeName)
{
    string t = trimSpaces(typeName);
    return !t.empty() && t.front() == '{' && t.back() == '}';
}

bool isIntegerPlaceholderType(const string &typeName)
{
    string base = normalizeTypeForSignature(stripPointerSuffix(stripArraySuffix(typeName)));
    return base == "integer";
}

string normalizeTypeForSignature(string typeName)
{
    typeName = stripLeadingConst(stripNonConstTypeQualifiers(normalizeTypeForCompare(typeName)));
    string collapsed;
    bool prevSpace = false;
    for (char c : typeName)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (!prevSpace)
                collapsed.push_back(' ');
            prevSpace = true;
            continue;
        }
        prevSpace = false;
        if ((c == '*' || c == '&') && !collapsed.empty() && collapsed.back() == ' ')
            collapsed.pop_back();
        collapsed.push_back(c);
    }
    return trimSpaces(collapsed);
}

bool isAggregateStorageType(const string &typeName)
{
    string t = normalizeTypeForSignature(typeName);
    return t.find('*') != string::npos || t.find('[') != string::npos;
}

string getFloatingStorageBaseType(string typeName)
{
    typeName = stripArraySuffix(stripNonConstTypeQualifiers(normalizeTypeForCompare(typeName)));
    string base;
    for (char c : typeName)
    {
        // Pointer-to-array parameters are commonly spelled as "T (*)[N]".
        // Drop declarator structure and keep the floating element spelling.
        if (c == '*' || c == '&' || c == '(' || c == ')')
            continue;
        base.push_back(c);
    }
    base = trimSpaces(stripLeadingConst(base));
    while (base.size() > strlen(" const") &&
           base.compare(base.size() - strlen(" const"), strlen(" const"), " const") == 0)
    {
        base = trimSpaces(base.substr(0, base.size() - strlen(" const")));
    }
    if (base == "float" || base == "double" || base == "long double" ||
        base == "_Float16" || base == "__fp16" || base == "__half")
        return base;
    return "";
}

bool isPrecisionRuleType(const string &typeName)
{
    return !getFloatingStorageBaseType(typeName).empty();
}

// Plan A 辅助: 把类型串里的浮点"元素基类型"替换成 newBase, 保留所有 * / [] / (*) 结构。
// 用于让聚合(数组/指针到数组)形参跟随调用方降精度时保持正确形状,
// 例如 rebase("long double (*)[64]", "double") -> "double (*)[64]"; 修复多维数组形参
// 被衰变成单指针(double*)、与调用方 double[N][N] 步长不一致导致越界崩溃的 bug。
string rebaseFloatingStorageType(const string &typeName, const string &newBase)
{
    string oldBase = getFloatingStorageBaseType(typeName);
    if (oldBase.empty() || newBase.empty() || oldBase == newBase)
        return typeName;
    string t = typeName;
    size_t pos = t.find(oldBase);
    while (pos != string::npos)
    {
        bool leftOk = (pos == 0) ||
                      !(isalnum(static_cast<unsigned char>(t[pos - 1])) || t[pos - 1] == '_');
        size_t after = pos + oldBase.size();
        bool rightOk = (after >= t.size()) ||
                       !(isalnum(static_cast<unsigned char>(t[after])) || t[after] == '_');
        if (leftOk && rightOk)
        {
            t.replace(pos, oldBase.size(), newBase);
            return t;
        }
        pos = t.find(oldBase, after);
    }
    return typeName;
}

bool shouldPreferCallerStorageTypeAtCallEdge(const string &actualType, const string &desiredType)
{
    if (normalizeTypeForSignature(actualType) == normalizeTypeForSignature(desiredType))
        return false;
    string actualBase = getFloatingStorageBaseType(actualType);
    string desiredBase = getFloatingStorageBaseType(desiredType);
    if (actualBase.empty() || desiredBase.empty())
        return false;
    if (!isAggregateStorageType(actualType) && !isAggregateStorageType(desiredType))
        return actualBase != desiredBase;
    if (!isAggregateStorageType(actualType) || !isAggregateStorageType(desiredType))
        return false;
    return actualBase != desiredBase;
}

bool isCallerStorageArgument(Expr *expr)
{
    if (!expr)
        return false;
    Expr *base = expr->IgnoreParenImpCasts();
    if (auto *unOp = dyn_cast<UnaryOperator>(base))
    {
        if (unOp->getOpcode() == UO_AddrOf)
        {
            Expr *sub = unOp->getSubExpr()->IgnoreParenImpCasts();
            if (isa<ArraySubscriptExpr>(sub) || isa<MemberExpr>(sub))
                return true;
            if (auto *declRef = dyn_cast<DeclRefExpr>(sub))
                return isa<VarDecl>(declRef->getDecl());
            return false;
        }
        if (unOp->getOpcode() == UO_Deref)
            return true;
    }
    if (isa<ArraySubscriptExpr>(base) || isa<MemberExpr>(base))
        return true;
    if (auto *declRef = dyn_cast<DeclRefExpr>(base))
        return isa<VarDecl>(declRef->getDecl());
    return false;
}

int floatingTypeRank(const string &typeName)
{
    string base = getFloatingStorageBaseType(typeName);
    if (base == "long double")
        return 4;
    if (base == "double")
        return 3;
    if (base == "float")
        return 2;
    if (base == "_Float16" || base == "__fp16" || base == "__half")
        return 1;
    return 0;
}

string floatingTypeForRank(int rank)
{
    if (rank >= 4)
        return "long double";
    if (rank == 3)
        return "double";
    if (rank == 2)
        return "float";
    if (rank == 1)
        return "_Float16";
    return "";
}

string usualArithmeticTypeAfterTransform(const string &lhsType,
                                         const string &rhsType,
                                         const string &fallbackType)
{
    if (isAggregateStorageType(lhsType) || isAggregateStorageType(rhsType))
        return fallbackType;
    int lhsRank = floatingTypeRank(lhsType);
    int rhsRank = floatingTypeRank(rhsType);
    int rank = std::max(lhsRank, rhsRank);
    if (rank == 0)
        return fallbackType;
    return floatingTypeForRank(rank);
}

string applyTypeReplacement(const string &exprType, const string &oldType, const string &newType)
{
    string baseOld = stripLeadingConst(stripPointerSuffix(stripArraySuffix(oldType)));
    string baseNew = stripLeadingConst(stripPointerSuffix(stripArraySuffix(newType)));
    if (baseOld.empty() || baseNew.empty())
        return exprType;
    string result = exprType;
    size_t pos = result.find(baseOld);
    if (pos == string::npos)
        return exprType;
    result.replace(pos, baseOld.size(), baseNew);
    return result;
}

string addPointerSuffix(string typeName)
{
    typeName = trimSpaces(typeName);
    if (typeName.empty())
        return "";
    return trimSpaces(typeName + " *");
}

string peelOneArraySuffix(string typeName)
{
    typeName = trimSpaces(typeName);
    size_t open = typeName.find('[');
    if (open == string::npos)
        return typeName;
    size_t close = typeName.find(']', open);
    if (close == string::npos)
        return trimSpaces(typeName.substr(0, open));
    return trimSpaces(typeName.substr(0, open) + typeName.substr(close + 1));
}

string peelOnePointerSuffix(string typeName)
{
    typeName = trimSpaces(typeName);
    if (typeName.empty() || typeName.back() != '*')
        return typeName;
    typeName.pop_back();
    return trimSpaces(typeName);
}

string peelOneIndirection(string typeName)
{
    typeName = trimSpaces(typeName);
    if (typeName.find('[') != string::npos)
        return peelOneArraySuffix(typeName);
    if (!typeName.empty() && typeName.back() == '*')
        return peelOnePointerSuffix(typeName);
    return typeName;
}

int countArraySubscriptLayers(Expr *expr, Expr **rootOut = nullptr)
{
    int count = 0;
    Expr *cur = expr ? expr->IgnoreParenImpCasts() : nullptr;
    while (auto *subscript = dyn_cast_or_null<ArraySubscriptExpr>(cur))
    {
        ++count;
        cur = subscript->getBase()->IgnoreParenImpCasts();
    }
    if (rootOut)
        *rootOut = cur;
    return count;
}

string typeAfterSubscriptLayers(string typeName, int layers)
{
    string result = trimSpaces(typeName);
    for (int i = 0; i < layers; ++i)
        result = peelOneIndirection(result);
    return result;
}

bool containsIdentifier(const string &text, const string &name)
{
    if (name.empty())
        return false;
    size_t pos = 0;
    while ((pos = text.find(name, pos)) != string::npos)
    {
        bool leftOk = (pos == 0) ||
                      !(isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
        size_t after = pos + name.size();
        bool rightOk = (after >= text.size()) ||
                       !(isalnum(static_cast<unsigned char>(text[after])) || text[after] == '_');
        if (leftOk && rightOk)
            return true;
        pos = after;
    }
    return false;
}

string ensureParamNameInReplacement(const string &replacement, const string &paramName, const string &rangeText)
{
    if (paramName.empty())
        return replacement;
    if (!containsIdentifier(rangeText, paramName))
        return replacement;
    if (containsIdentifier(replacement, paramName))
        return replacement;
    if (!replacement.empty() && replacement.back() == ' ')
        return replacement + paramName;
    return replacement + " " + paramName;
}

string buildShimVariableName(const string &paramName, const string &internalType)
{
    if (paramName.empty())
        return "param_cast";
    if (isExactFp16AbiIncompatibleType(internalType))
        return paramName + "_fp16";
    return paramName + "_cast";
}

static int countPointerDepth(string typeName)
{
    int depth = 0;
    for (char c : typeName)
    {
        if (c == '*')
            ++depth;
    }
    return depth;
}

static string buildPointerTypeString(const string &baseType, int pointerDepth)
{
    string result = trimSpaces(baseType);
    for (int i = 0; i < pointerDepth; ++i)
        result += "*";
    return result;
}

static string escapeRegex(const string &text)
{
    static const regex special(R"([-[\]{}()*+?.,\^$|#\s])");
    return regex_replace(text, special, R"(\$&)");
}

static string rewriteAllocationInitializerText(const string &initText,
                                               const string &oldType,
                                               const string &newType,
                                               const string &lhsText = "")
{
    string rewritten = initText;
    if (getFloatingStorageBaseType(oldType).empty() || getFloatingStorageBaseType(newType).empty())
        return rewritten;
    string oldBase = trimSpaces(stripLeadingConst(stripPointerSuffix(stripArraySuffix(oldType))));
    string newBase = trimSpaces(stripLeadingConst(stripPointerSuffix(stripArraySuffix(newType))));
    int oldPtrDepth = countPointerDepth(oldType);
    int newPtrDepth = countPointerDepth(newType);
    if (oldBase.empty() || newBase.empty())
        return rewritten;
    if (oldBase == newBase && oldPtrDepth == newPtrDepth)
        return rewritten;

    if (newPtrDepth > 0)
    {
        string newPtrType = buildPointerTypeString(newBase, newPtrDepth);
        string oldPtrPattern = escapeRegex(oldBase);
        for (int i = 0; i < std::max(oldPtrDepth, 1); ++i)
            oldPtrPattern += "\\s*\\*";
        regex castPattern("\\(\\s*" + oldPtrPattern + "\\s*\\)");
        rewritten = regex_replace(rewritten, castPattern, "(" + newPtrType + ")");
    }

    string sizeofReplacement = lhsText.empty() ? ("sizeof(" + newBase + ")") : ("sizeof(*" + lhsText + ")");
    string oldSizeofTypePattern = escapeRegex(oldBase);
    for (int i = 0; i < std::max(oldPtrDepth, 1); ++i)
        oldSizeofTypePattern += "(?:\\s*\\*)?";
    regex oldSizeofPattern("sizeof\\s*\\(\\s*" + oldSizeofTypePattern + "\\s*\\)");
    rewritten = regex_replace(rewritten, oldSizeofPattern, sizeofReplacement);

    return rewritten;
}

static int countArraySubscriptDepth(Expr *expr)
{
    int depth = 0;
    Expr *cur = expr ? expr->IgnoreParenImpCasts() : nullptr;
    while (auto *sub = dyn_cast_or_null<ArraySubscriptExpr>(cur))
    {
        ++depth;
        cur = sub->getBase() ? sub->getBase()->IgnoreParenImpCasts() : nullptr;
    }
    return depth;
}

static VarDecl *getBaseVarDeclFromStorageExpr(Expr *expr)
{
    Expr *cur = expr ? expr->IgnoreParenImpCasts() : nullptr;
    while (auto *sub = dyn_cast_or_null<ArraySubscriptExpr>(cur))
        cur = sub->getBase() ? sub->getBase()->IgnoreParenImpCasts() : nullptr;
    if (auto *declRef = dyn_cast_or_null<DeclRefExpr>(cur))
        return dyn_cast<VarDecl>(declRef->getDecl());
    return nullptr;
}

static string dropPointerLevels(string typeName, int levels)
{
    string t = trimSpaces(typeName);
    for (int i = 0; i < levels; ++i)
    {
        size_t pos = t.rfind('*');
        if (pos == string::npos)
            break;
        t.erase(pos, 1);
        t = trimSpaces(t);
    }
    return t;
}

static void rewriteAllocatorInitializerForVar(VarDecl *val,
                                              const string &oldType,
                                              const string &newType,
                                              ASTContext *astContext)
{
    if (!val || !val->hasInit())
        return;
    const Expr *init = val->getInit();
    if (!init)
        return;
    CharSourceRange initRange = CharSourceRange::getTokenRange(init->getSourceRange());
    string initText = getSourceTextPreferRewritten(initRange, astContext);
    if (initText.find("calloc") == string::npos &&
        initText.find("malloc") == string::npos &&
        initText.find("realloc") == string::npos)
    {
        return;
    }

    string rewritten = rewriteAllocationInitializerText(initText, oldType, newType, val->getNameAsString());
    if (rewritten != initText)
        rewriter.ReplaceText(initRange, rewritten);
}

static bool rewriteAllocatorAssignmentForVar(BinaryOperator *op,
                                             VarDecl *val,
                                             ASTContext *astContext,
                                             const string &callerFunc,
                                             const string &lhsText = "",
                                             int derefLevels = 0,
                                             const string &lhsOldType = "")
{
    if (!op || !val || !astContext || !op->isAssignmentOp())
        return false;
    Expr *rhs = op->getRHS();
    if (!rhs)
        return false;

    CharSourceRange rhsRange = CharSourceRange::getTokenRange(rhs->getSourceRange());
    string rhsText = getSourceTextPreferRewritten(rhsRange, astContext);
    if (rhsText.find("calloc") == string::npos &&
        rhsText.find("malloc") == string::npos &&
        rhsText.find("realloc") == string::npos)
    {
        return false;
    }

    string fileName = getFileName(astContext, val);
    if (fileName.empty())
        return false;
    string location = val->hasGlobalStorage() ? "globalVar" : "localVar";
    if (isa<ParmVarDecl>(val))
        location = "parmVar";
    int line = getLineNumber(astContext, val->getLocation());
    string oldType = lhsOldType.empty() ? val->getType().getAsString() : lhsOldType;
    string baseOldType = val->getType().getAsString();
    string newType = getNewType(val->getNameAsString(), oldType, callerFunc, fileName, location, line);
    if (newType.empty() && oldType != baseOldType)
        newType = getNewType(val->getNameAsString(), baseOldType, callerFunc, fileName, location, line);
    newType = normalizeNewType(oldType, newType);
    if (newType.empty())
        return false;
    if (derefLevels > 0)
        newType = dropPointerLevels(newType, derefLevels);

    string rewritten = rewriteAllocationInitializerText(rhsText, oldType, newType, lhsText);
    if (rewritten == rhsText)
        return false;
    rewriter.ReplaceText(rhsRange, rewritten);
    return true;
}

static bool rewriteAllocatorDeclStmtForVar(VarDecl *val,
                                           const string &oldType,
                                           const string &newType,
                                           ASTContext *astContext)
{
    if (!val || !val->hasInit())
        return false;
    const Expr *init = val->getInit();
    if (!init)
        return false;

    CharSourceRange initRange = CharSourceRange::getTokenRange(init->getSourceRange());
    string initText = getSourceTextPreferRewritten(initRange, astContext);
    if (initText.find("calloc") == string::npos &&
        initText.find("malloc") == string::npos &&
        initText.find("realloc") == string::npos)
    {
        return false;
    }

    auto parents = astContext->getParents(*val);
    for (const auto &parent : parents)
    {
        const DeclStmt *declSt = parent.get<DeclStmt>();
        if (!declSt || !declSt->isSingleDecl())
            continue;

        string rewrittenInit = rewriteAllocationInitializerText(initText, oldType, newType, val->getNameAsString());
        if (rewrittenInit == initText)
            return false;

        string storage = "";
        switch (val->getStorageClass())
        {
        case SC_Static:
            storage = "static ";
            break;
        case SC_Extern:
            storage = "extern ";
            break;
        case SC_Register:
            storage = "register ";
            break;
        case SC_Auto:
            storage = "auto ";
            break;
        default:
            break;
        }

        string declText = storage + buildParamDeclText(newType, val->getNameAsString()) + " = " + rewrittenInit + ";";
        CharSourceRange declRange = CharSourceRange::getTokenRange(declSt->getSourceRange());
        rewriter.ReplaceText(declRange, declText);
        return true;
    }
    return false;
}

static bool typeNeedsWholeDeclaratorRewrite(const string &typeName)
{
    string t = trimSpaces(typeName);
    return t.find('[') != string::npos && t.find('*') != string::npos;
}

static bool rewriteSingleDeclStmtForVar(VarDecl *val,
                                        const string &newType,
                                        ASTContext *astContext)
{
    if (!val || !astContext || val->getNameAsString().empty())
        return false;
    if (!typeNeedsWholeDeclaratorRewrite(newType))
        return false;

    auto parents = astContext->getParents(*val);
    for (const auto &parent : parents)
    {
        const DeclStmt *declSt = parent.get<DeclStmt>();
        if (!declSt || !declSt->isSingleDecl())
            continue;

        string storage = storageClassPrefix(val);
        string declText = storage + buildParamDeclText(newType, val->getNameAsString());
        if (val->hasInit())
        {
            const Expr *init = val->getInit();
            if (!init)
                return false;
            CharSourceRange initRange = CharSourceRange::getTokenRange(init->getSourceRange());
            string initText = getSourceTextPreferRewritten(initRange, astContext);
            if (initText.empty())
                return false;
            declText += " = " + initText;
        }
        declText += ";";
        CharSourceRange declRange = CharSourceRange::getTokenRange(declSt->getSourceRange());
        rewriter.ReplaceText(declRange, declText);
        return true;
    }
    return false;
}

string stripTrailingParamName(string typeName, const string &paramName)
{
    if (paramName.empty())
        return typeName;
    string t = trimSpaces(typeName);
    size_t namePos = t.rfind(paramName);
    if (namePos == string::npos || namePos + paramName.size() != t.size())
        return typeName;
    if (namePos == 0)
        return typeName;
    char prev = t[namePos - 1];
    if (std::isspace(static_cast<unsigned char>(prev)) || prev == '*')
    {
        t = trimSpaces(t.substr(0, namePos));
        return t;
    }
    return typeName;
}

bool containsInsertedFunctionName(const string &key)
{
    for (const string &nameKey : insertedFunctionNames)
    {
        if (nameKey == key)
            return true;
    }
    return false;
}

void addInsertedFunctionName(const string &key)
{
    if (!containsInsertedFunctionName(key))
        insertedFunctionNames.push_back(key);
}

bool containsInsertedOverloadKey(const string &key)
{
    for (const string &overloadKey : insertedOverloadKeys)
    {
        if (overloadKey == key)
            return true;
    }
    return false;
}

void addInsertedOverloadKey(const string &key)
{
    if (!containsInsertedOverloadKey(key))
        insertedOverloadKeys.push_back(key);
}

string findClonedFunctionName(const string &signatureKey)
{
    for (const auto &entry : clonedFunctionNameBySignature)
    {
        if (entry.first == signatureKey)
            return entry.second;
    }
    return "";
}

string findExternalAbiWrapperName(const string &signatureKey)
{
    for (const auto &entry : externalAbiWrapperNameBySignature)
    {
        if (entry.first == signatureKey)
            return entry.second;
    }
    return "";
}

void rememberClonedFunctionName(const string &signatureKey, const string &functionName)
{
    for (auto &entry : clonedFunctionNameBySignature)
    {
        if (entry.first == signatureKey)
        {
            entry.second = functionName;
            return;
        }
    }
    clonedFunctionNameBySignature.push_back({signatureKey, functionName});
}

void forgetClonedFunctionName(const string &signatureKey)
{
    for (auto it = clonedFunctionNameBySignature.begin(); it != clonedFunctionNameBySignature.end(); ++it)
    {
        if (it->first == signatureKey)
        {
            clonedFunctionNameBySignature.erase(it);
            return;
        }
    }
}

void rememberExternalAbiWrapperName(const string &signatureKey, const string &functionName)
{
    for (auto &entry : externalAbiWrapperNameBySignature)
    {
        if (entry.first == signatureKey)
        {
            entry.second = functionName;
            return;
        }
    }
    externalAbiWrapperNameBySignature.push_back({signatureKey, functionName});
}

string findPreservedText(const vector<pair<string, string>> &entries, const string &key)
{
    for (const auto &entry : entries)
    {
        if (entry.first == key)
            return entry.second;
    }
    return "";
}

void rememberPreservedText(vector<pair<string, string>> &entries, const string &key, const string &value)
{
    for (auto &entry : entries)
    {
        if (entry.first == key)
        {
            entry.second = value;
            return;
        }
    }
    entries.push_back({key, value});
}

string buildOverloadKey(FunctionDecl *callee, const vector<string> &paramTypes, ASTContext *astContext)
{
    SourceManager &SM = astContext->getSourceManager();
    string file = SM.getFilename(callee->getLocation()).str();
    string key = file + ":" + callee->getNameAsString() + "(";
    for (size_t i = 0; i < paramTypes.size(); ++i)
    {
        if (i > 0)
            key += ",";
        string paramType = paramTypes[i];
        key += normalizeTypeForSignature(paramType);
    }
    key += ")";
    return key;
}

string buildCloneSignatureKey(FunctionDecl *callee, const vector<string> &paramTypes, ASTContext *astContext)
{
    return buildOverloadKey(callee, paramTypes, astContext);
}

string buildWrapperKeyByName(FunctionDecl *callee, const string &wrapperName, const vector<string> &paramTypes, ASTContext *astContext)
{
    SourceManager &SM = astContext->getSourceManager();
    string file = SM.getFilename(callee->getLocation()).str();
    string key = file + ":" + wrapperName + "(";
    for (size_t i = 0; i < paramTypes.size(); ++i)
    {
        if (i > 0)
            key += ",";
        string paramType = paramTypes[i];
        key += normalizeTypeForSignature(paramType);
    }
    key += ")";
    return key;
}

string buildFunctionNameKey(FunctionDecl *callee, const string &name, ASTContext *astContext)
{
    SourceManager &SM = astContext->getSourceManager();
    string file = SM.getFilename(callee->getLocation()).str();
    return file + ":" + name;
}

string buildExternalAbiWrapperKey(FunctionDecl *callee, const vector<string> &actualParamTypes, ASTContext *astContext)
{
    return buildOverloadKey(callee, actualParamTypes, astContext) + ":external-abi";
}

string buildParamDeclText(const string &typeName, const string &paramName)
{
    string type = trimSpaces(typeName);
    size_t bracket = type.find('[');
    if (bracket != string::npos)
    {
        string base = trimSpaces(type.substr(0, bracket));
        string arrays;
        string tail = type.substr(bracket);
        size_t pos = 0;
        while (pos < tail.size())
        {
            while (pos < tail.size() && isspace(static_cast<unsigned char>(tail[pos])))
                ++pos;
            if (pos >= tail.size() || tail[pos] != '[')
                break;
            size_t close = tail.find(']', pos);
            if (close == string::npos)
                break;
            arrays += tail.substr(pos, close - pos + 1);
            pos = close + 1;
        }
        string afterArrays = trimSpaces(tail.substr(pos));

        size_t anonymousPtrToArray = base.find("(*)");
        if (anonymousPtrToArray != string::npos)
        {
            string namedBase = base;
            namedBase.replace(anonymousPtrToArray, strlen("(*)"), "(*" + paramName + ")");
            return namedBase + arrays;
        }

        int pointerDepth = countPointerDepth(afterArrays);
        if (pointerDepth > 0)
        {
            string stars(pointerDepth, '*');
            return base + " (" + stars + paramName + ")" + arrays;
        }
        return base + " " + paramName + arrays;
    }
    return type + " " + paramName;
}

string buildFunctionPrototype(FunctionDecl *callee, const string &name, const vector<string> &desiredParamTypes, ASTContext *astContext)
{
    if (!callee)
        return "";
    string spec = getFunctionSpecifiers(callee);
    string returnType = callee->getReturnType().getAsString();
    bool configuredReturnType = false;
    if (astContext)
    {
        string fileName = getFileName(astContext, callee);
        if (!fileName.empty())
        {
            int line = getLineNumber(astContext, callee->getLocation());
            string oldReturn = callee->getReturnType().getAsString();
            string newReturn = getNewReturnType(callee->getNameAsString(), fileName, oldReturn, line);
            if (!newReturn.empty())
            {
                returnType = getAbiCompatibleInterfaceType(normalizeNewType(oldReturn, newReturn));
                configuredReturnType = true;
            }
        }
    }
    if (!configuredReturnType)
    {
        string inferredReturn = inferReturnTypeFromDesiredParameter(callee, desiredParamTypes);
        if (!inferredReturn.empty())
            returnType = getAbiCompatibleInterfaceType(inferredReturn);
    }
    vector<string> paramDecls;
    for (size_t i = 0; i < desiredParamTypes.size(); ++i)
    {
        string paramName = "p" + std::to_string(i);
        string desiredType = desiredParamTypes[i];
        if (desiredType.empty())
            desiredType = callee->getParamDecl(i)->getType().getAsString();
        desiredType = getAbiCompatibleInterfaceType(desiredType);
        paramDecls.push_back(buildParamDeclText(desiredType, paramName));
    }
    string proto = spec + returnType + " " + name + "(";
    for (size_t i = 0; i < paramDecls.size(); ++i)
    {
        if (i > 0)
            proto += ", ";
        proto += paramDecls[i];
    }
    proto += ");\n";
    return proto;
}

void initPrototypeInsertLoc(ASTContext *astContext)
{
    if (prototypeInsertLocReady)
        return;
    SourceManager &SM = astContext->getSourceManager();
    unsigned bestOff = 0;
    bool found = false;
    bool invalid = false;
    StringRef buffer = SM.getBufferData(SM.getMainFileID(), &invalid);
    TranslationUnitDecl *tu = astContext->getTranslationUnitDecl();
    for (Decl *decl : tu->decls())
    {
        auto *fd = dyn_cast<FunctionDecl>(decl);
        if (!fd || !fd->hasBody())
            continue;
        SourceLocation loc = SM.getSpellingLoc(fd->getBeginLoc());
        if (!loc.isValid() || !SM.isWrittenInMainFile(loc))
            continue;
        unsigned off = SM.getFileOffset(loc);
        unsigned lineStartOff = off;
        if (!invalid)
        {
            while (lineStartOff > 0 &&
                   lineStartOff <= buffer.size() &&
                   buffer[lineStartOff - 1] != '\n' &&
                   buffer[lineStartOff - 1] != '\r')
            {
                --lineStartOff;
            }
        }
        if (!found || lineStartOff < bestOff)
        {
            bestOff = lineStartOff;
            found = true;
        }
    }
    if (!found)
    {
        bestOff = 0;
    }
    basePrototypeInsertOffset = bestOff;
    prototypeInsertLocReady = true;
}

// External ABI wrappers are called directly from rewritten function bodies.
// Keep their declaration block independent from internal clone prototypes:
// a clone may depend on a typedef declared much later in the file, and using
// one shared maximum insertion offset would place an ABI declaration after an
// earlier rewritten call site.
void raiseExternalAbiInsertLocForDecl(FunctionDecl *callee, ASTContext *astContext)
{
    if (!callee || !astContext)
        return;
    initPrototypeInsertLoc(astContext);
    if (!externalAbiInsertLocReady)
    {
        externalAbiInsertOffset = basePrototypeInsertOffset;
        externalAbiInsertLocReady = true;
    }

    SourceManager &SM = astContext->getSourceManager();
    bool invalid = false;
    StringRef buffer = SM.getBufferData(SM.getMainFileID(), &invalid);
    bool found = false;
    unsigned earliestOff = 0;
    for (FunctionDecl *decl : callee->redecls())
    {
        SourceLocation loc = SM.getSpellingLoc(decl->getBeginLoc());
        if (!loc.isValid() || !SM.isWrittenInMainFile(loc))
            continue;
        unsigned off = SM.getFileOffset(loc);
        while (!invalid && off > 0 && off <= buffer.size() &&
               buffer[off - 1] != '\n' && buffer[off - 1] != '\r')
        {
            --off;
        }
        if (!found || off < earliestOff)
        {
            found = true;
            earliestOff = off;
        }
    }
    if (found && earliestOff > externalAbiInsertOffset)
        externalAbiInsertOffset = earliestOff;
}

void insertFunctionPrototype(FunctionDecl *callee, const string &name, const vector<string> &desiredParamTypes, ASTContext *astContext)
{
    if (!callee || !astContext)
        return;
    initPrototypeInsertLoc(astContext);
    SourceManager &SM = astContext->getSourceManager();
    bool invalid = false;
    StringRef buffer = SM.getBufferData(SM.getMainFileID(), &invalid);
    unsigned insertOffset = basePrototypeInsertOffset;
    bool found = false;
    unsigned earliestOff = 0;
    for (FunctionDecl *decl : callee->redecls())
    {
        SourceLocation loc = SM.getSpellingLoc(decl->getBeginLoc());
        if (!loc.isValid() || !SM.isWrittenInMainFile(loc))
            continue;
        unsigned off = SM.getFileOffset(loc);
        while (!invalid && off > 0 && off <= buffer.size() &&
               buffer[off - 1] != '\n' && buffer[off - 1] != '\r')
        {
            --off;
        }
        if (!found || off < earliestOff)
        {
            found = true;
            earliestOff = off;
        }
    }
    if (found)
        insertOffset = std::max(insertOffset, earliestOff);
    string key = buildFunctionNameKey(callee, name, astContext) + ":decl";
    if (std::find(insertedFunctionDecls.begin(), insertedFunctionDecls.end(), key) != insertedFunctionDecls.end())
        return;
    string proto = buildFunctionPrototype(callee, name, desiredParamTypes, astContext);
    if (proto.empty())
        return;
    if (std::find(insertedFunctionPrototypeTexts.begin(), insertedFunctionPrototypeTexts.end(), proto) != insertedFunctionPrototypeTexts.end())
        return;
    insertedFunctionDecls.push_back(key);
    insertedFunctionPrototypeTexts.push_back(proto);
    pendingFunctionPrototypes.push_back(
        {insertOffset, callee->getNameAsString(), proto});
}

bool hasMatchingOverload(FunctionDecl *callee, const vector<string> &desiredParamTypes, ASTContext *astContext)
{
    DeclContext *dc = callee->getDeclContext();
    if (!dc)
        return false;
    auto lookup = dc->lookup(callee->getDeclName());
    if (lookup.empty())
        return false;
    for (NamedDecl *decl : lookup)
    {
        FunctionDecl *fd = dyn_cast<FunctionDecl>(decl);
        if (!fd)
            continue;
        if (fd->getNumParams() != desiredParamTypes.size())
            continue;
        bool allMatch = true;
        for (size_t i = 0; i < desiredParamTypes.size(); ++i)
        {
            string paramType = fd->getParamDecl(i)->getType().getAsString();
            string desiredType = desiredParamTypes[i];
            if (normalizeTypeForSignature(paramType) != normalizeTypeForSignature(desiredType))
            {
                allMatch = false;
                break;
            }
        }
        if (allMatch)
            return true;
    }
    return false;
}

bool hasMatchingFunctionByName(FunctionDecl *callee, const string &name, const vector<string> &desiredParamTypes, ASTContext *astContext)
{
    DeclContext *dc = callee->getDeclContext();
    if (!dc)
        return false;
    IdentifierInfo &ident = astContext->Idents.get(name);
    DeclarationName declName(&ident);
    auto lookup = dc->lookup(declName);
    if (lookup.empty())
        return false;
    for (NamedDecl *decl : lookup)
    {
        FunctionDecl *fd = dyn_cast<FunctionDecl>(decl);
        if (!fd)
            continue;
        if (fd->getNumParams() != desiredParamTypes.size())
            continue;
        bool allMatch = true;
        for (size_t i = 0; i < desiredParamTypes.size(); ++i)
        {
            string paramType = fd->getParamDecl(i)->getType().getAsString();
            string desiredType = desiredParamTypes[i];
            if (normalizeTypeForSignature(paramType) != normalizeTypeForSignature(desiredType))
            {
                allMatch = false;
                break;
            }
        }
        if (allMatch)
            return true;
    }
    return false;
}

bool hasAnyFunctionByName(FunctionDecl *callee, const string &name, ASTContext *astContext)
{
    DeclContext *dc = callee->getDeclContext();
    if (!dc)
        return false;
    IdentifierInfo &ident = astContext->Idents.get(name);
    DeclarationName declName(&ident);
    auto lookup = dc->lookup(declName);
    if (lookup.empty())
        return false;
    for (NamedDecl *decl : lookup)
    {
        if (isa<FunctionDecl>(decl))
            return true;
    }
    return false;
}

string buildWrapperFunction(FunctionDecl *callee, const vector<string> &desiredParamTypes)
{
    if (!callee || callee->getNumParams() != desiredParamTypes.size())
        return "";
    string storage = "";
    if (callee->getStorageClass() == SC_Static)
        storage = "static ";
    string returnType = callee->getReturnType().getAsString();
    string funcName = callee->getNameAsString();
    vector<string> paramDecls;
    vector<string> callArgs;
    for (size_t i = 0; i < desiredParamTypes.size(); ++i)
    {
        string paramName = "p" + std::to_string(i);
        string desiredType = desiredParamTypes[i];
        if (desiredType.empty())
            desiredType = callee->getParamDecl(i)->getType().getAsString();
        desiredType = getAbiCompatibleInterfaceType(desiredType);
        paramDecls.push_back(buildParamDeclText(desiredType, paramName));
        string paramType = callee->getParamDecl(i)->getType().getAsString();
        if (normalizeTypeForSignature(paramType) == normalizeTypeForSignature(desiredType))
            callArgs.push_back(paramName);
        else
            callArgs.push_back("(" + paramType + ")" + paramName);
    }

    string wrapper = storage + returnType + " " + funcName + "(";
    for (size_t i = 0; i < paramDecls.size(); ++i)
    {
        if (i > 0)
            wrapper += ", ";
        wrapper += paramDecls[i];
    }
    wrapper += ") {\n  ";
    string callExpr = funcName + "(";
    for (size_t i = 0; i < callArgs.size(); ++i)
    {
        if (i > 0)
            callExpr += ", ";
        callExpr += callArgs[i];
    }
    callExpr += ")";
    if (callee->getReturnType()->isVoidType())
        wrapper += callExpr + ";\n";
    else
        wrapper += "return " + callExpr + ";\n";
    wrapper += "}\n\n";
    return wrapper;
}

string getFunctionSpecifiers(FunctionDecl *callee)
{
    if (!callee)
        return "";
    StorageClass storage = callee->getStorageClass();
    bool inlineSpecified = callee->isInlineSpecified();
    for (FunctionDecl *redeclaration : callee->redecls())
    {
        if (!redeclaration)
            continue;
        StorageClass redeclStorage = redeclaration->getStorageClass();
        if (redeclStorage == SC_Static)
            storage = SC_Static;
        else if (storage != SC_Static && redeclStorage != SC_None)
            storage = redeclStorage;
        inlineSpecified = inlineSpecified || redeclaration->isInlineSpecified();
    }
    if (FunctionDecl *definition = callee->getDefinition())
    {
        StorageClass defStorage = definition->getStorageClass();
        if (defStorage == SC_Static)
            storage = SC_Static;
        else if (storage != SC_Static && defStorage != SC_None)
            storage = defStorage;
        inlineSpecified = inlineSpecified || definition->isInlineSpecified();
    }

    string spec;
    switch (storage)
    {
    case SC_Static:
        spec += "static ";
        break;
    case SC_Extern:
        spec += "extern ";
        break;
    case SC_Register:
        spec += "register ";
        break;
    case SC_Auto:
        spec += "auto ";
        break;
    default:
        break;
    }
    if (inlineSpecified)
        spec += "inline ";
    return spec;
}

void collectCallExprs(Stmt *stmt, vector<CallExpr *> &calls)
{
    if (!stmt)
        return;
    if (auto *call = dyn_cast<CallExpr>(stmt))
        calls.push_back(call);
    for (Stmt *child : stmt->children())
        collectCallExprs(child, calls);
}

string replaceFunctionCallTokens(string text, const string &oldName, const string &newName)
{
    if (oldName.empty() || newName.empty() || oldName == newName)
        return text;
    size_t pos = 0;
    while ((pos = text.find(oldName, pos)) != string::npos)
    {
        bool leftOk = (pos == 0) || !(isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
        size_t after = pos + oldName.size();
        bool rightOk = (after >= text.size()) || !(isalnum(static_cast<unsigned char>(text[after])) || text[after] == '_');
        size_t look = after;
        while (look < text.size() && isspace(static_cast<unsigned char>(text[look])))
            ++look;
        if (leftOk && rightOk && look < text.size() && text[look] == '(')
        {
            text.replace(pos, oldName.size(), newName);
            pos += newName.size();
        }
        else
        {
            pos += oldName.size();
        }
    }
    return text;
}

string repairMalformedAbiCallNameReplacement(string text)
{
    static const regex abiNameBetweenClosedAndOpenParen(
        R"((\))\s+([A-Za-z_][A-Za-z_0-9]*_abi_[0-9]+)\s*(\())");
    return regex_replace(text, abiNameBetweenClosedAndOpenParen, "$1 + $3");
}

string getCloneMappedDeclRefType(DeclRefExpr *declRef,
                                 const vector<pair<string, string>> &paramTypeMap)
{
    if (!declRef)
        return "";
    auto *param = dyn_cast<ParmVarDecl>(declRef->getDecl());
    if (!param)
        return "";
    string name = param->getNameAsString();
    for (const auto &entry : paramTypeMap)
    {
        if (entry.first == name)
            return entry.second;
    }
    return "";
}

string getMappedArraySubscriptType(ArraySubscriptExpr *subscript,
                                   ASTContext *astContext,
                                   const string &callerFunc,
                                   const vector<pair<string, string>> &paramTypeMap)
{
    if (!subscript)
        return "";
    Expr *root = nullptr;
    int layers = countArraySubscriptLayers(subscript, &root);
    if (auto *declRef = dyn_cast_or_null<DeclRefExpr>(root))
    {
        string mapped = getCloneMappedDeclRefType(declRef, paramTypeMap);
        if (!mapped.empty())
            return typeAfterSubscriptLayers(mapped, layers);
    }
    return getExprTypeAfterTransform(subscript, astContext, callerFunc);
}

string refineArrayMemberArgumentType(Expr *arg,
                                     const string &currentType,
                                     ASTContext *astContext,
                                     const string &callerFunc,
                                     const vector<pair<string, string>> &paramTypeMap = {})
{
    if (!arg)
        return currentType;
    Expr *base = arg->IgnoreParenImpCasts();
    if (auto *unOp = dyn_cast<UnaryOperator>(base))
    {
        if (unOp->getOpcode() == UO_AddrOf)
        {
            Expr *sub = unOp->getSubExpr()->IgnoreParenImpCasts();
            if (auto *subscript = dyn_cast<ArraySubscriptExpr>(sub))
            {
                string subscriptType = getMappedArraySubscriptType(subscript, astContext, callerFunc, paramTypeMap);
                if (!subscriptType.empty())
                    return addPointerSuffix(subscriptType);
            }
        }
    }
    if (auto *subscript = dyn_cast<ArraySubscriptExpr>(base))
    {
        string subscriptType = getMappedArraySubscriptType(subscript, astContext, callerFunc, paramTypeMap);
        if (!subscriptType.empty())
            return subscriptType;
    }
    return currentType;
}

string getExprTypeAfterCloneParamMapping(Expr *expr,
                                         ASTContext *astContext,
                                         const string &callerFunc,
                                         const vector<pair<string, string>> &paramTypeMap)
{
    if (!expr)
        return "";
    Expr *base = expr->IgnoreParenImpCasts();
    if (auto *declRef = dyn_cast<DeclRefExpr>(base))
    {
        string mapped = getCloneMappedDeclRefType(declRef, paramTypeMap);
        if (!mapped.empty())
            return mapped;
    }
    if (auto *unOp = dyn_cast<UnaryOperator>(base))
    {
        if (unOp->getOpcode() == UO_AddrOf)
        {
            Expr *sub = unOp->getSubExpr()->IgnoreParenImpCasts();
            if (auto *subscript = dyn_cast<ArraySubscriptExpr>(sub))
            {
                Expr *root = nullptr;
                int layers = countArraySubscriptLayers(subscript, &root);
                if (auto *declRef = dyn_cast_or_null<DeclRefExpr>(root))
                {
                    string mapped = getCloneMappedDeclRefType(declRef, paramTypeMap);
                    if (!mapped.empty())
                    {
                        string subscriptType = typeAfterSubscriptLayers(mapped, layers);
                        if (!subscriptType.empty())
                            return addPointerSuffix(subscriptType);
                    }
                }
            }
            if (auto *declRef = dyn_cast<DeclRefExpr>(sub))
            {
                string mapped = getCloneMappedDeclRefType(declRef, paramTypeMap);
                if (!mapped.empty())
                    return addPointerSuffix(mapped);
            }
        }
    }
    if (auto *subscript = dyn_cast<ArraySubscriptExpr>(base))
    {
        Expr *root = nullptr;
        int layers = countArraySubscriptLayers(subscript, &root);
        if (auto *declRef = dyn_cast_or_null<DeclRefExpr>(root))
        {
            string mapped = getCloneMappedDeclRefType(declRef, paramTypeMap);
            if (!mapped.empty())
            {
                string subscriptType = typeAfterSubscriptLayers(mapped, layers);
                if (!subscriptType.empty())
                    return subscriptType;
            }
        }
    }
    return getExprTypeAfterTransform(expr, astContext, callerFunc);
}

string rewriteNestedCallsForClone(FunctionDecl *callee,
                                  const vector<string> &desiredParamTypes,
                                  string bodyText,
                                  ASTContext *astContext,
                                  string &extraFunctions)
{
    if (!callee || !callee->hasBody() || !astContext || callee->getNumParams() != desiredParamTypes.size())
        return bodyText;

    vector<pair<string, string>> paramTypeMap;
    for (size_t i = 0; i < desiredParamTypes.size(); ++i)
    {
        string name = callee->getParamDecl(i)->getNameAsString();
        if (!name.empty())
            paramTypeMap.push_back({name, desiredParamTypes[i].empty() ? callee->getParamDecl(i)->getType().getAsString() : desiredParamTypes[i]});
    }

    SourceManager &SM = astContext->getSourceManager();
    vector<CallExpr *> calls;
    collectCallExprs(callee->getBody(), calls);
    for (CallExpr *call : calls)
    {
        FunctionDecl *nested = call->getDirectCallee();
        if (!nested || nested->isVariadic())
            continue;
        if (FunctionDecl *definition = nested->getDefinition())
            nested = definition;
        if (call->getNumArgs() != nested->getNumParams())
            continue;
        SourceLocation nestedLoc = nested->getLocation();
        bool isInternalMainFileFunction = nested->hasBody() &&
                                          nestedLoc.isValid() &&
                                          !nestedLoc.isMacroID() &&
                                          SM.isWrittenInMainFile(SM.getSpellingLoc(nestedLoc));

        if (!isInternalMainFileFunction)
        {
            vector<string> actualParamTypes;
            vector<string> declaredParamTypes;
            vector<bool> scalarPointerBridges;
            bool needsWrapper = false;
            bool unsupportedAggregateMismatch = false;

            for (unsigned i = 0; i < call->getNumArgs(); ++i)
            {
                Expr *arg = call->getArg(i);
                string actualType = getExprTypeAfterCloneParamMapping(arg, astContext, callee->getNameAsString(), paramTypeMap);
                actualType = refineArrayMemberArgumentType(arg, actualType, astContext, callee->getNameAsString(), paramTypeMap);
                string declaredType = nested->getParamDecl(i)->getType().getAsString();
                if (isFloatingPointerTypeString(declaredType) && !isFloatingPointerTypeString(actualType))
                {
                    if (VarDecl *addressedVar = getAddressedNonArrayScalarVar(arg))
                        actualType = addressedVar->getType().getAsString() + " *";
                }

                actualParamTypes.push_back(actualType);
                declaredParamTypes.push_back(declaredType);
                scalarPointerBridges.push_back(false);

                if (normalizeTypeForSignature(actualType) == normalizeTypeForSignature(declaredType))
                    continue;

                if (isFloatingPointerTypeString(actualType) &&
                    isFloatingPointerTypeString(declaredType) &&
                    isAddressOfNonArrayScalar(arg))
                {
                    scalarPointerBridges.back() = true;
                    needsWrapper = true;
                    continue;
                }

                if (isFloatingScalarTypeString(actualType) && isFloatingScalarTypeString(declaredType))
                {
                    needsWrapper = true;
                    continue;
                }

                if (isAggregateStorageType(actualType) || isAggregateStorageType(declaredType))
                {
                    unsupportedAggregateMismatch = true;
                    continue;
                }
            }

            string libmCallName = pickLibmCallNameForActualTypes(nested->getNameAsString(), actualParamTypes);
            if (!libmCallName.empty() && libmCallName != nested->getNameAsString())
            {
                bodyText = replaceFunctionCallTokens(bodyText, nested->getNameAsString(), libmCallName);
                string spelledCallName = trimSpaces(getSourceTextPreferRewritten(
                    CharSourceRange::getTokenRange(call->getCallee()->getSourceRange()),
                    astContext));
                spelledCallName = stripHeaderPrefixFromCallName(spelledCallName);
                if (!spelledCallName.empty() && spelledCallName != nested->getNameAsString())
                    bodyText = replaceFunctionCallTokens(bodyText, spelledCallName, libmCallName);
                continue;
            }

            if (!needsWrapper || unsupportedAggregateMismatch)
                continue;

            string wrapperKey = buildExternalAbiWrapperKey(nested, actualParamTypes, astContext);
            string wrapperName = findExternalAbiWrapperName(wrapperKey);
            if (wrapperName.empty())
            {
                wrapperName = buildExternalAbiWrapperName(nested);
                string wrapper = buildExternalAbiWrapperFunction(nested,
                                                                 wrapperName,
                                                                 actualParamTypes,
                                                                 declaredParamTypes,
                                                                 scalarPointerBridges);
                if (wrapper.empty())
                    continue;
                // The generated ABI wrapper calls the fixed-precision
                // callee.  Queue an original-signature declaration before
                // the wrapper definition: the source declaration may occur
                // later in the file (for example an extern RNG prototype).
                string originalPrototype = buildFunctionPrototype(
                    nested, nested->getNameAsString(), declaredParamTypes, astContext);
                if (!originalPrototype.empty() &&
                    std::find(insertedFunctionPrototypeTexts.begin(), insertedFunctionPrototypeTexts.end(), originalPrototype) == insertedFunctionPrototypeTexts.end())
                {
                    raiseExternalAbiInsertLocForDecl(nested, astContext);
                    insertedFunctionPrototypeTexts.push_back(originalPrototype);
                    pendingExternalAbiPrototypes.push_back(originalPrototype);
                }
                string prototype = buildExternalAbiWrapperPrototype(nested,
                                                                    wrapperName,
                                                                    actualParamTypes,
                                                                    declaredParamTypes);
                if (!prototype.empty() &&
                    std::find(insertedFunctionPrototypeTexts.begin(), insertedFunctionPrototypeTexts.end(), prototype) == insertedFunctionPrototypeTexts.end())
                {
                    raiseExternalAbiInsertLocForDecl(nested, astContext);
                    insertedFunctionPrototypeTexts.push_back(prototype);
                    pendingExternalAbiPrototypes.push_back(prototype);
                }
                pendingExternalAbiPrototypes.push_back(wrapper + "\n");
                rememberExternalAbiWrapperName(wrapperKey, wrapperName);
            }
            bodyText = replaceFunctionCallTokens(bodyText, nested->getNameAsString(), wrapperName);
            string spelledCallName = trimSpaces(getSourceTextPreferRewritten(
                CharSourceRange::getTokenRange(call->getCallee()->getSourceRange()),
                astContext));
            spelledCallName = stripHeaderPrefixFromCallName(spelledCallName);
            if (!spelledCallName.empty() && spelledCallName != nested->getNameAsString())
                bodyText = replaceFunctionCallTokens(bodyText, spelledCallName, wrapperName);
            continue;
        }

        vector<string> nestedDesiredTypes;
        bool mismatch = false;
        bool forceClone = false;
        for (unsigned i = 0; i < call->getNumArgs(); ++i)
        {
            Expr *arg = call->getArg(i);
            string actualType = getExprTypeAfterCloneParamMapping(arg, astContext, callee->getNameAsString(), paramTypeMap);
            actualType = refineArrayMemberArgumentType(arg, actualType, astContext, callee->getNameAsString(), paramTypeMap);
            string desiredType = getConfiguredParamTypeForCallee(nested, i, astContext);
            string paramType = nested->getParamDecl(i)->getType().getAsString();
            bool hadConfiguredDesiredType = !desiredType.empty();
            if (desiredType.empty())
                desiredType = paramType;
            if (isCallerStorageArgument(arg) &&
                shouldPreferCallerStorageTypeAtCallEdge(actualType, desiredType))
            {
                if (hadConfiguredDesiredType)
                    forceClone = true;
                // Plan A: 聚合形参保留原有指针/数组结构, 只换浮点基类型(避免多维数组衰变成单指针)
                if (isAggregateStorageType(paramType))
                    desiredType = rebaseFloatingStorageType(paramType, getFloatingStorageBaseType(actualType));
                else
                    desiredType = actualType;
            }
            string interfaceDesiredType = getAbiCompatibleInterfaceType(desiredType);
            if (needsParamAbiShim(desiredType, interfaceDesiredType))
                forceClone = true;
            nestedDesiredTypes.push_back(interfaceDesiredType);
            if (normalizeTypeForSignature(paramType) != normalizeTypeForSignature(interfaceDesiredType))
                mismatch = true;
        }
        if (!mismatch && !forceClone)
            continue;

        string cloneKey = buildCloneSignatureKey(nested, nestedDesiredTypes, astContext);
        string nestedCloneName = findClonedFunctionName(cloneKey);
        if (nestedCloneName.empty())
        {
            nestedCloneName = pickClonedFunctionName(nested, nestedDesiredTypes, astContext);
            if (nestedCloneName.empty() || nestedCloneName == nested->getNameAsString())
                continue;
            if (hasMatchingFunctionByName(nested, nestedCloneName, nestedDesiredTypes, astContext))
            {
                rememberClonedFunctionName(cloneKey, nestedCloneName);
                bodyText = replaceFunctionCallTokens(bodyText, nested->getNameAsString(), nestedCloneName);
                continue;
            }
            string wrapperKey = buildWrapperKeyByName(nested, nestedCloneName, nestedDesiredTypes, astContext);
            rememberClonedFunctionName(cloneKey, nestedCloneName);
            addInsertedFunctionName(buildFunctionNameKey(nested, nestedCloneName, astContext));
            if (!containsInsertedOverloadKey(wrapperKey))
            {
                string nestedClone = buildClonedFunctionWithName(nested, nestedCloneName, nestedDesiredTypes, astContext);
                if (nestedClone.empty())
                {
                    forgetClonedFunctionName(cloneKey);
                    continue;
                }
                insertFunctionPrototype(nested, nestedCloneName, nestedDesiredTypes, astContext);
                addInsertedOverloadKey(wrapperKey);
                addInsertedFunctionName(buildFunctionNameKey(nested, nestedCloneName, astContext));
                extraFunctions += "\n\n" + nestedClone;
            }
            rememberClonedFunctionName(cloneKey, nestedCloneName);
        }
        bodyText = replaceFunctionCallTokens(bodyText, nested->getNameAsString(), nestedCloneName);
    }
    return bodyText;
}

string buildClonedFunctionWithName(FunctionDecl *callee, const string &newName, const vector<string> &desiredParamTypes, ASTContext *astContext)
{
    if (!callee || !callee->hasBody() || callee->getNumParams() != desiredParamTypes.size())
        return "";
    string oldName = callee->getNameAsString();
    string desiredReturnType = callee->getReturnType().getAsString();
    string internalDesiredReturnType = desiredReturnType;
    bool configuredReturnType = false;
    if (astContext)
    {
        string fileName = getFileName(astContext, callee);
        if (!fileName.empty())
        {
            int line = getLineNumber(astContext, callee->getLocation());
            string oldReturn = callee->getReturnType().getAsString();
            string newReturn = getNewReturnType(callee->getNameAsString(), fileName, oldReturn, line);
            if (!newReturn.empty())
            {
                internalDesiredReturnType = normalizeNewType(oldReturn, newReturn);
                desiredReturnType = internalDesiredReturnType;
                configuredReturnType = true;
            }
        }
    }
    if (!configuredReturnType)
    {
        string inferredReturn = inferReturnTypeFromDesiredParameter(callee, desiredParamTypes);
        if (!inferredReturn.empty())
        {
            internalDesiredReturnType = inferredReturn;
            desiredReturnType = inferredReturn;
        }
    }
    desiredReturnType = getAbiCompatibleInterfaceType(desiredReturnType);
    vector<string> paramDecls;
    for (size_t i = 0; i < desiredParamTypes.size(); ++i)
    {
        string paramName = callee->getParamDecl(i)->getNameAsString();
        if (paramName.empty())
            paramName = "p" + std::to_string(i);
        string desiredType = desiredParamTypes[i];
        if (desiredType.empty())
            desiredType = callee->getParamDecl(i)->getType().getAsString();
        desiredType = getAbiCompatibleInterfaceType(desiredType);
        paramDecls.push_back(buildParamDeclText(desiredType, paramName));
    }

    string header;
    string functionKey = buildFunctionNameKey(callee, oldName, astContext);
    auto buildHeaderFromSignature = [&](const string &signatureText) -> string {
        auto findFunctionNameToken = [&](const string &text, const string &name) -> size_t {
            size_t pos = 0;
            while ((pos = text.find(name, pos)) != string::npos)
            {
                bool leftOk = (pos == 0) ||
                              !(isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
                size_t after = pos + name.size();
                bool rightOk = (after >= text.size()) ||
                               !(isalnum(static_cast<unsigned char>(text[after])) || text[after] == '_');
                if (leftOk && rightOk)
                {
                    size_t look = after;
                    while (look < text.size() && isspace(static_cast<unsigned char>(text[look])))
                        ++look;
                    if (look < text.size() && text[look] == '(')
                        return pos;
                }
                pos = after;
            }
            return string::npos;
        };

        size_t namePos = findFunctionNameToken(signatureText, oldName);
        if (namePos == string::npos)
            return "";
        size_t lparen = signatureText.find('(', namePos + oldName.size());
        size_t rparen = signatureText.rfind(')');
        if (lparen == string::npos || rparen == string::npos || rparen <= lparen)
            return "";
        string prefix = signatureText.substr(0, namePos);
        string suffix = signatureText.substr(rparen + 1);
        string built = prefix + newName + "(";
        for (size_t i = 0; i < paramDecls.size(); ++i)
        {
            if (i > 0)
                built += ", ";
            built += paramDecls[i];
        }
        built += ")" + suffix;
        return built;
    };

    string cachedFunctionText = findPreservedText(preservedFunctionTexts, functionKey);

    if (!cachedFunctionText.empty())
    {
        size_t bracePos = cachedFunctionText.find('{');
        if (bracePos != string::npos)
            header = buildHeaderFromSignature(cachedFunctionText.substr(0, bracePos));
    }

    SourceManager &SM = astContext->getSourceManager();
    SourceLocation beginLoc = SM.getSpellingLoc(callee->getBeginLoc());
    SourceLocation bodyBegin = SM.getSpellingLoc(callee->getBody()->getBeginLoc());
    if (header.empty() && beginLoc.isValid() && bodyBegin.isValid() && SM.isWrittenInMainFile(beginLoc))
    {
        CharSourceRange signatureRange = CharSourceRange::getCharRange(beginLoc, bodyBegin);
        string signatureText = getSourceTextPreferRewritten(signatureRange, astContext);
        if (!signatureText.empty())
            header = buildHeaderFromSignature(signatureText);
    }
    if (header.empty())
    {
        string spec = getFunctionSpecifiers(callee);
        header = spec + desiredReturnType + " " + newName + "(";
        for (size_t i = 0; i < paramDecls.size(); ++i)
        {
            if (i > 0)
                header += ", ";
            header += paramDecls[i];
        }
        header += ") ";
    }
    else
    {
        auto findFunctionNameToken = [&](const string &text, const string &name) -> size_t {
            size_t pos = 0;
            while ((pos = text.find(name, pos)) != string::npos)
            {
                bool leftOk = (pos == 0) ||
                              !(isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
                size_t after = pos + name.size();
                bool rightOk = (after >= text.size()) ||
                               !(isalnum(static_cast<unsigned char>(text[after])) || text[after] == '_');
                if (leftOk && rightOk)
                    return pos;
                pos = after;
            }
            return string::npos;
        };
        size_t namePos = findFunctionNameToken(header, newName);
        if (namePos != string::npos)
        {
            string spec = getFunctionSpecifiers(callee);
            header = spec + desiredReturnType + " " + header.substr(namePos);
        }
    }

    CharSourceRange bodyRange = CharSourceRange::getTokenRange(callee->getBody()->getSourceRange());
    string bodyKey = functionKey + ":body";
    string bodyText = findPreservedText(preservedFunctionBodies, bodyKey);
    if (bodyText.empty() && !cachedFunctionText.empty())
    {
        size_t bracePos = cachedFunctionText.find('{');
        if (bracePos != string::npos)
            bodyText = cachedFunctionText.substr(bracePos);
    }
    if (bodyText.empty())
        bodyText = getSourceTextPreferRewritten(bodyRange, astContext);
    if (bodyText.empty())
        return "";
    bodyText = rewriteParameterAliasLocalsForClone(bodyText, callee,
                                                   desiredParamTypes, astContext);
    if (header.find('{') != string::npos)
    {
        size_t i = 0;
        while (i < bodyText.size() && (bodyText[i] == ' ' || bodyText[i] == '\t' || bodyText[i] == '\n' || bodyText[i] == '\r'))
            ++i;
        if (i < bodyText.size() && bodyText[i] == '{')
            bodyText.erase(0, i + 1);
    }

    string replacedBody = rewriteReturnStatementsForAbiShim(bodyText,
                                                            callee,
                                                            astContext,
                                                            internalDesiredReturnType,
                                                            desiredReturnType);
    string extraFunctions;
    replacedBody = rewriteNestedCallsForClone(callee,
                                              desiredParamTypes,
                                              replacedBody,
                                              astContext,
                                              extraFunctions);
    replacedBody = replaceFunctionCallTokens(replacedBody, oldName, newName);
    replacedBody = repairMalformedAbiCallNameReplacement(replacedBody);

    return header + replacedBody + "\n\n" + extraFunctions;
}

string pickClonedFunctionName(FunctionDecl *callee, const vector<string> &desiredParamTypes, ASTContext *astContext)
{
    string base = callee->getNameAsString();
    for (int i = 1; i <= 9999; ++i)
    {
        string candidate = base + "_" + std::to_string(i);
        if (candidate == base)
            continue;
        string nameKey = buildFunctionNameKey(callee, candidate, astContext);
        if (containsInsertedFunctionName(nameKey))
            continue;
        if (hasAnyFunctionByName(callee, candidate, astContext))
        {
            if (hasMatchingFunctionByName(callee, candidate, desiredParamTypes, astContext))
                return candidate;
            continue;
        }
        string key = buildWrapperKeyByName(callee, candidate, desiredParamTypes, astContext);
        if (containsInsertedOverloadKey(key))
            continue;
        return candidate;
    }
    return "";
}

string ensureTrailingSpaceIfNeeded(const string &replacement, CharSourceRange range, ASTContext *astContext)
{
    string result = replacement;
    SourceManager &SM = astContext->getSourceManager();
    bool invalid = false;
    const char *endBuf = SM.getCharacterData(range.getEnd(), &invalid);
    if (!invalid)
    {
        char endChar = *endBuf;
        if (endChar == ' ' || endChar == '\t' || endChar == '\n' || endChar == '\r')
        {
            if (result.empty() || result.back() != ' ')
                result.push_back(' ');
            return result;
        }
    }
    if (!result.empty() && result.back() != ' ')
    {
        SourceLocation after = Lexer::getLocForEndOfToken(range.getEnd(), 0, SM, astContext->getLangOpts());
        if (after.isValid())
        {
            const char *buf = SM.getCharacterData(after, &invalid);
            if (!invalid)
            {
                char c = *buf;
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                {
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
                        result.push_back(' ');
                }
            }
        }
    }
    return result;
}

void ensureSpaceBeforeIdentifier(SourceLocation nameLoc, ASTContext *astContext)
{
    if (!nameLoc.isValid())
        return;
    SourceManager &SM = astContext->getSourceManager();
    SourceLocation spelling = SM.getSpellingLoc(nameLoc);
    if (!spelling.isValid())
        return;
    bool invalid = false;
    FileID fid = SM.getFileID(spelling);
    if (fid.isInvalid())
        return;
    StringRef buffer = SM.getBufferData(fid, &invalid);
    if (invalid || buffer.empty())
        return;
    const char *buf = SM.getCharacterData(spelling, &invalid);
    if (invalid || buf == nullptr)
        return;
    if (buf <= buffer.data())
        return;
    char prev = *(buf - 1);
    if (prev == ' ' || prev == '\t' || prev == '\n' || prev == '\r')
        return;
    if (prev == '*' || prev == '&' || prev == '(' || prev == '[' || prev == ',')
        return;
    if ((prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z') || (prev >= '0' && prev <= '9') || prev == '_')
        rewriter.InsertText(nameLoc, " ", true, true);
}

int jsonToInt(const nlohmann::json &j)
{
    if (j.is_number_integer())
    {
        return j.get<int>();
    }
    if (j.is_string())
    {
        return std::atoi(j.get<std::string>().c_str());
    }
    return -1;
}

size_t countSubString(const string &str, const string &sub)
{
    size_t ret = 0;
    size_t loc = str.find(sub);
    while (loc != string::npos)
    {
        ++ret;
        loc = str.find(sub, loc + 1);
    }
    return ret;
}

string locKey(SourceLocation loc, ASTContext *astContext)
{
    if (!loc.isValid())
        return "";
    SourceManager &SM = astContext->getSourceManager();
    SourceLocation spelling = SM.getSpellingLoc(loc);
    if (!spelling.isValid())
        return "";
    FileID fid = SM.getFileID(spelling);
    if (fid.isInvalid())
        return "";
    string file = SM.getFilename(spelling).str();
    unsigned offset = SM.getFileOffset(spelling);
    return file + ":" + std::to_string(offset);
}

string getSourceTextRaw(CharSourceRange range, ASTContext *astContext)
{
    SourceManager &SM = astContext->getSourceManager();
    return Lexer::getSourceText(range, SM, astContext->getLangOpts()).str();
}

string getSourceTextPreferRewritten(CharSourceRange range, ASTContext *astContext)
{
    string rewritten = rewriter.getRewrittenText(range.getAsRange());
    if (!rewritten.empty())
        return rewritten;
    return getSourceTextRaw(range, astContext);
}

string ltrim(const string &s)
{
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
    return s.substr(i);
}

static vector<string> splitTopLevelCommasForDecl(const string &text)
{
    vector<string> parts;
    string cur;
    int paren = 0, bracket = 0, brace = 0;
    bool inString = false, inChar = false, escape = false;
    for (char c : text)
    {
        if (escape)
        {
            cur.push_back(c);
            escape = false;
            continue;
        }
        if (c == '\\')
        {
            cur.push_back(c);
            escape = true;
            continue;
        }
        if (!inChar && c == '"')
            inString = !inString;
        else if (!inString && c == '\'')
            inChar = !inChar;
        if (!inString && !inChar)
        {
            if (c == '(') ++paren;
            else if (c == ')' && paren > 0) --paren;
            else if (c == '[') ++bracket;
            else if (c == ']' && bracket > 0) --bracket;
            else if (c == '{') ++brace;
            else if (c == '}' && brace > 0) --brace;
            else if (c == ',' && paren == 0 && bracket == 0 && brace == 0)
            {
                parts.push_back(cur);
                cur.clear();
                continue;
            }
        }
        cur.push_back(c);
    }
    parts.push_back(cur);
    return parts;
}

static bool extractDeclSegmentName(const string &segment, string &name)
{
    size_t pos = 0;
    while (pos < segment.size())
    {
        while (pos < segment.size() &&
               !(isalpha(static_cast<unsigned char>(segment[pos])) || segment[pos] == '_'))
            ++pos;
        if (pos >= segment.size())
            return false;
        size_t start = pos++;
        while (pos < segment.size() &&
               (isalnum(static_cast<unsigned char>(segment[pos])) || segment[pos] == '_'))
            ++pos;
        name = segment.substr(start, pos - start);
        return true;
    }
    return false;
}

static string storageClassPrefix(VarDecl *var)
{
    if (!var)
        return "";
    switch (var->getStorageClass())
    {
    case SC_Static:
        return "static ";
    case SC_Extern:
        return "extern ";
    case SC_Register:
        return "register ";
    case SC_Auto:
        return "auto ";
    default:
        return "";
    }
}

static bool rewriteGlobalMultiDeclForVar(VarDecl *val, ASTContext *astContext)
{
    if (!val || !astContext || !val->hasGlobalStorage())
        return false;
    SourceLocation loc = val->getLocation();
    if (!shouldRewriteLoc(astContext, loc))
        return false;

    SourceManager &SM = astContext->getSourceManager();
    SourceLocation spelling = SM.getSpellingLoc(loc);
    if (!spelling.isValid() || spelling.isMacroID())
        return false;
    FileID fid = SM.getFileID(spelling);
    if (fid.isInvalid())
        return false;
    bool invalid = false;
    StringRef buffer = SM.getBufferData(fid, &invalid);
    if (invalid || buffer.empty())
        return false;

    unsigned varOff = SM.getFileOffset(spelling);
    if (varOff >= buffer.size())
        return false;
    unsigned lineStartOff = varOff;
    while (lineStartOff > 0 && buffer[lineStartOff - 1] != '\n' && buffer[lineStartOff - 1] != '\r')
        --lineStartOff;
    unsigned semiOff = varOff;
    while (semiOff < buffer.size() && buffer[semiOff] != ';' && buffer[semiOff] != '\n' && buffer[semiOff] != '\r')
        ++semiOff;
    if (semiOff >= buffer.size() || buffer[semiOff] != ';')
        return false;

    string stmtKey = SM.getFilename(spelling).str() + ":" + std::to_string(lineStartOff) + ":" + std::to_string(semiOff);
    if (std::find(rewrittenGlobalDeclStmtKeys.begin(), rewrittenGlobalDeclStmtKeys.end(), stmtKey) != rewrittenGlobalDeclStmtKeys.end())
        return true;

    string stmtText = buffer.substr(lineStartOff, semiOff - lineStartOff).str();
    vector<string> segments = splitTopLevelCommasForDecl(stmtText);
    if (segments.size() <= 1)
        return false;

    size_t prefixLen = 0;
    if (varOff >= lineStartOff)
        prefixLen = static_cast<size_t>(varOff - lineStartOff);
    if (prefixLen > segments[0].size())
        prefixLen = segments[0].size();
    segments[0] = segments[0].substr(prefixLen);

    string fileName = getFileName(astContext, val);
    if (fileName.empty())
        return false;
    int line = getLineNumber(astContext, val->getLocation());
    string originalType = val->getType().getAsString();
    string originalBase = stripArraySuffix(originalType);
    originalBase = preserveConstPrefix(originalType, originalBase);

    vector<string> finalTypes;
    vector<string> declTexts;
    vector<string> varNames;
    bool needsRewrite = false;
    for (const string &segment : segments)
    {
        string declText = ltrim(segment);
        string varName;
        if (!extractDeclSegmentName(declText, varName))
            return false;

        string newTypeName = getNewType(varName, originalType, funcName, fileName, "globalVar", line);
        newTypeName = normalizeNewType(originalType, newTypeName);
        string finalBase = newTypeName.empty() ? originalBase : newTypeName;
        if (finalBase.find('[') != string::npos && finalBase.find('*') == string::npos)
            finalBase = stripArraySuffix(finalBase);
        finalBase = preserveConstPrefix(originalType, finalBase);

        string finalType = storageClassPrefix(val) + finalBase;
        finalTypes.push_back(finalType);
        declTexts.push_back(declText);
        varNames.push_back(varName);

        string oldType = storageClassPrefix(val) + originalBase;
        if (normalizeTypeForCompare(finalType) != normalizeTypeForCompare(oldType))
            needsRewrite = true;
    }

    bool hasDifferentTypes = false;
    bool needsWholeDeclaratorRewrite = false;
    for (const string &type : finalTypes)
    {
        if (typeNeedsWholeDeclaratorRewrite(type))
        {
            needsWholeDeclaratorRewrite = true;
            break;
        }
    }
    for (size_t i = 1; i < finalTypes.size(); ++i)
    {
        if (normalizeTypeForCompare(finalTypes[i]) != normalizeTypeForCompare(finalTypes[0]))
        {
            hasDifferentTypes = true;
            break;
        }
    }
    if (!needsRewrite && !hasDifferentTypes)
        return false;

    string indent;
    for (char c : stmtText)
    {
        if (c == ' ' || c == '\t')
            indent.push_back(c);
        else
            break;
    }

    string newText;
    if (!hasDifferentTypes && !needsWholeDeclaratorRewrite)
    {
        newText = indent + finalTypes[0] + " ";
        for (size_t i = 0; i < declTexts.size(); ++i)
        {
            if (i > 0)
                newText += ", ";
            newText += declTexts[i];
        }
        newText += ";";
    }
    else
    {
        for (size_t i = 0; i < declTexts.size(); ++i)
        {
            if (i > 0)
                newText += " ";
            string initText;
            size_t eq = declTexts[i].find('=');
            if (eq != string::npos)
                initText = " " + trimSpaces(declTexts[i].substr(eq));
            if (typeNeedsWholeDeclaratorRewrite(finalTypes[i]))
                newText += indent + buildParamDeclText(finalTypes[i], varNames[i]) + initText + ";";
            else
                newText += indent + finalTypes[i] + " " + declTexts[i] + ";";
        }
    }

    SourceLocation start = SM.getLocForStartOfFile(fid).getLocWithOffset(lineStartOff);
    SourceLocation end = SM.getLocForStartOfFile(fid).getLocWithOffset(semiOff + 1);
    if (!start.isValid() || !end.isValid())
        return false;
    rewriter.ReplaceText(CharSourceRange::getCharRange(start, end), newText);
    rewrittenGlobalDeclStmtKeys.push_back(stmtKey);
    return true;
}

static clang::TypeLoc getBaseTypeLoc(clang::TypeLoc TL)
{
    clang::TypeLoc cur = TL;
    while (true)
    {
        if (auto ATL = cur.getAs<clang::ArrayTypeLoc>())
        {
            cur = ATL.getElementLoc();
            continue;
        }
        if (auto PTL = cur.getAs<clang::PointerTypeLoc>())
        {
            cur = PTL.getPointeeLoc();
            continue;
        }
        if (auto ATL = cur.getAs<clang::AttributedTypeLoc>())
        {
            cur = ATL.getModifiedLoc();
            continue;
        }
        break;
    }
    return cur;
}

string getFileName(ASTContext *Context, VarDecl *val)
{
    SourceManager &SrcMgr = Context->getSourceManager();
    auto Entry = SrcMgr.getFileEntryRefForID(SrcMgr.getFileID(val->getLocation()));
    if (!Entry)
    {
        return "";
    }
    string FileName = basename(Entry->getName().str());
    return FileName;
}

string getFileName(ASTContext *Context, DeclRefExpr *val)
{
    SourceManager &SrcMgr = Context->getSourceManager();
    auto Entry = SrcMgr.getFileEntryRefForID(SrcMgr.getFileID(val->getLocation()));
    if (!Entry)
    {
        return "";
    }
    string FileName = basename(Entry->getName().str());
    return FileName;
}

string getFileName(ASTContext *Context, FieldDecl *val)
{
    SourceManager &SrcMgr = Context->getSourceManager();
    auto Entry = SrcMgr.getFileEntryRefForID(SrcMgr.getFileID(val->getLocation()));
    if (!Entry)
    {
        return "";
    }
    string FileName = basename(Entry->getName().str());
    return FileName;
}

string getFileName(ASTContext *Context, FunctionDecl *val)
{
    SourceManager &SrcMgr = Context->getSourceManager();
    auto Entry = SrcMgr.getFileEntryRefForID(SrcMgr.getFileID(val->getLocation()));
    if (!Entry)
    {
        return "";
    }
    string FileName = basename(Entry->getName().str());
    return FileName;
}

int getLineNumber(ASTContext *Context, SourceLocation loc)
{
    return Context->getSourceManager().getSpellingLineNumber(loc);
}

static bool findVarRuleType(string valueName,
                            string funcName,
                            string fileName,
                            string location,
                            int line,
                            string &matchedType)
{
    for (const auto &rule : varRules) {
        if (rule.location != location) continue;
        if (basename(rule.file) != fileName) continue;
        if (!rule.func.empty() && rule.func != funcName) continue;
        if (rule.name != valueName) continue;
        if (rule.line > 0 && line > 0 && rule.line != line) continue;
        matchedType = rule.type;
        return true;
    }
    matchedType.clear();
    return false;
}

string makeExternalAbiProtectedVarKey(const string &valueName,
                                      const string &fileName,
                                      const string &location,
                                      int line)
{
    return basename(fileName) + ":" + location + ":" + valueName + ":" + std::to_string(line);
}

string getNewType(string valueName, string typeName, string funcName, string fileName, string location, int line)
{
    string protectedKey = makeExternalAbiProtectedVarKey(valueName, fileName, location, line);
    if (externalAbiProtectedVarKeys.find(protectedKey) != externalAbiProtectedVarKeys.end())
        return "";
    string matchedType;
    if (!findVarRuleType(valueName, funcName, fileName, location, line, matchedType))
        return "";
    if (isIntegerPlaceholderType(matchedType))
        return "";
    if (!isPrecisionRuleType(matchedType))
        return "";
    string normTypeName = normalizeTypeForCompare(typeName);
    if (normalizeTypeForCompare(matchedType) == normTypeName)
        return "";
    return matchedType;
}

string getConfiguredTypeRaw(string valueName, string funcName, string fileName, string location, int line)
{
    string matchedType;
    if (!findVarRuleType(valueName, funcName, fileName, location, line, matchedType))
        return "";
    return matchedType;
}

string getNewReturnType(string funcName, string fileName, string typeName, int line)
{
    string normTypeName = normalizeTypeForCompare(typeName);
    for (const auto &rule : returnRules) {
        if (!rule.func.empty() && rule.func != funcName) continue;
        if (basename(rule.file) != fileName) continue;
        if (rule.line > 0 && line > 0 && rule.line != line) continue;
        if (isTupleType(rule.type)) return "";
        if (isIntegerPlaceholderType(rule.type)) return "";
        if (!isPrecisionRuleType(rule.type)) return "";
        if (normalizeTypeForCompare(rule.type) == normTypeName) return "";
        return rule.type;
    }
    return "";
}

string getStrictHeaderReturnType(string funcName, string typeName)
{
    if (!STRICT_CONFIG)
        return "";
    string normTypeName = normalizeTypeForCompare(typeName);
    for (const auto &rule : returnRules)
    {
        if (!rule.func.empty() && rule.func != funcName)
            continue;
        if (isIntegerPlaceholderType(rule.type))
            continue;
        if (!isPrecisionRuleType(rule.type))
            continue;
        if (normalizeTypeForCompare(rule.type) == normTypeName)
            continue;
        return rule.type;
    }
    return "";
}

static bool sameHeaderParamRuleCategory(const string &oldType, const string &ruleType)
{
    bool oldAggregate = isAggregateStorageType(oldType);
    bool ruleAggregate = isAggregateStorageType(ruleType);
    if (oldAggregate || ruleAggregate)
        return oldAggregate == ruleAggregate;
    return isFloatingScalarTypeString(oldType) && isFloatingScalarTypeString(ruleType);
}

string getStrictHeaderParamType(FunctionDecl *func, unsigned paramIndex, string valueName, string typeName)
{
    if (!STRICT_CONFIG || !func)
        return "";
    string funcName = func->getNameAsString();
    string normTypeName = normalizeTypeForCompare(typeName);
    for (const auto &rule : varRules)
    {
        if (rule.location != "parmVar")
            continue;
        if (!rule.func.empty() && rule.func != funcName)
            continue;
        if (rule.name != valueName)
            continue;
        if (isIntegerPlaceholderType(rule.type))
            continue;
        if (!isPrecisionRuleType(rule.type))
            continue;
        if (normalizeTypeForCompare(rule.type) == normTypeName)
            continue;
        return rule.type;
    }
    if (!valueName.empty())
        return "";

    unsigned categoryOrdinal = 0;
    for (unsigned i = 0; i < paramIndex; ++i)
    {
        string priorType = func->getParamDecl(i)->getOriginalType().getAsString();
        if (sameHeaderParamRuleCategory(priorType, typeName))
            ++categoryOrdinal;
    }

    unsigned matchingRuleOrdinal = 0;
    for (const auto &rule : varRules)
    {
        if (rule.location != "parmVar")
            continue;
        if (!rule.func.empty() && rule.func != funcName)
            continue;
        if (isIntegerPlaceholderType(rule.type))
            continue;
        if (!isPrecisionRuleType(rule.type))
            continue;
        if (!sameHeaderParamRuleCategory(typeName, rule.type))
            continue;
        if (matchingRuleOrdinal++ != categoryOrdinal)
            continue;
        if (normalizeTypeForCompare(rule.type) == normTypeName)
            return "";
        return rule.type;
    }
    return "";
}

string getNewCallTarget(string callerFunc, string fileName, string currentCallee, int line, int column)
{
    for (const auto &rule : callRules) {
        if (rule.callerFunc != callerFunc) continue;
        if (basename(rule.file) != fileName) continue;
        if (rule.line > 0 && line > 0 && rule.line != line) continue;
        if (rule.column > 0 && column > 0 && rule.column != column) continue;
        if (rule.targetCallee == currentCallee) return "";
        return rule.targetCallee;
    }
    return "";
}

string replaceDoubleWithFloat(const string &typeName)
{
    string res = typeName;
    size_t pos = res.find("double");
    if (pos != string::npos)
    {
        res.replace(pos, strlen("double"), "float");
    }
    return res;
}

string getNewFieldType(string fieldName, string recordName, int fieldIndex, string typeName, string fileName)
{
    string normTypeName = normalizeTypeForCompare(typeName);
    for (const auto &rule : fieldRules) {
        if (!recordName.empty() && !rule.record.empty() && rule.record != recordName) continue;
        if (rule.index >= 0 && fieldIndex != rule.index) continue;
        // Handle pointer placeholder: "*" means convert floating-pointer base to float
        if (rule.type == "*")
        {
            string candidate = replaceDoubleWithFloat(typeName);
            if (candidate == typeName)
                return "";
            return candidate;
        }
        // Skip integer placeholders
        if (isIntegerPlaceholderType(rule.type)) continue;
        if (normalizeTypeForCompare(rule.type) == normTypeName) return "";
        return rule.type;
    }
    return "";
}

string getTransformedVarExprType(VarDecl *var,
                                 const string &exprType,
                                 ASTContext *astContext,
                                 const string &callerFunc)
{
    if (!var)
        return exprType;
    string oldType = var->getType().getAsString();
    string fileName = getFileName(astContext, var);
    if (fileName.empty())
        return exprType;
    int line = getLineNumber(astContext, var->getLocation());
    string location = var->hasGlobalStorage() ? "globalVar" : "localVar";
    if (isa<ParmVarDecl>(var))
        location = "parmVar";
    string newType = getNewType(var->getNameAsString(), oldType, callerFunc, fileName, location, line);
    if (newType.empty())
        return exprType;
    newType = normalizeNewType(oldType, newType);
    return applyTypeReplacement(exprType, oldType, newType);
}

string getTransformedFieldExprType(FieldDecl *field,
                                   const string &exprType,
                                   ASTContext *astContext)
{
    if (!field)
        return exprType;
    string oldType = field->getType().getAsString();
    string recordName = "";
    if (const RecordDecl *parent = field->getParent())
        recordName = parent->getNameAsString();
    string fileName = getFileName(astContext, field);
    if (fileName.empty())
        return exprType;
    int fieldIndex = static_cast<int>(field->getFieldIndex());
    string newType = getNewFieldType(field->getNameAsString(), recordName, fieldIndex, oldType, fileName);
    if (newType.empty())
        return exprType;
    newType = normalizeNewType(oldType, newType);
    return applyTypeReplacement(exprType, oldType, newType);
}

string getKnownLibCallReturnType(const string &calleeName)
{
    string name = stripHeaderPrefixFromCallName(calleeName);
    static const vector<string> floatLibCalls = {
        "sqrtf", "expf", "powf", "sinf", "cosf", "tanf",
        "asinf", "acosf", "atanf", "atan2f", "logf", "log10f",
        "fabsf", "cbrtf", "hypotf", "floorf", "ceilf",
        "truncf", "roundf"
    };
    static const vector<string> longDoubleLibCalls = {
        "sqrtl", "expl", "powl", "sinl", "cosl", "tanl",
        "asinl", "acosl", "atanl", "atan2l", "logl", "log10l",
        "fabsl", "cbrtl", "hypotl", "floorl", "ceill",
        "truncl", "roundl"
    };
    static const vector<string> doubleLibCalls = {
        "sqrt", "exp", "pow", "sin", "cos", "tan",
        "asin", "acos", "atan", "atan2", "log", "log10",
        "fabs", "cbrt", "hypot", "floor", "ceil",
        "trunc", "round"
    };
    for (const string &known : floatLibCalls)
    {
        if (name == known)
            return "float";
    }
    for (const string &known : longDoubleLibCalls)
    {
        if (name == known)
            return "long double";
    }
    for (const string &known : doubleLibCalls)
    {
        if (name == known)
            return "double";
    }
    return "";
}

string getLibmFamilyBaseName(const string &calleeName)
{
    string name = stripHeaderPrefixFromCallName(calleeName);
    static const vector<string> libmFamilies = {
        "sqrt", "exp", "pow", "sin", "cos", "tan",
        "asin", "acos", "atan", "atan2", "log", "log10",
        "fabs", "cbrt", "hypot", "floor", "ceil",
        "trunc", "round"
    };
    for (const string &base : libmFamilies)
    {
        if (name == base || name == base + "f" || name == base + "l")
            return base;
    }
    return "";
}

string pickLibmCallNameForActualTypes(const string &calleeName, const vector<string> &actualParamTypes)
{
    string base = getLibmFamilyBaseName(calleeName);
    if (base.empty() || actualParamTypes.empty())
        return "";

    int rank = 0;
    for (const string &typeName : actualParamTypes)
    {
        int argRank = floatingTypeRank(typeName);
        if (argRank == 0)
            continue;
        if (isAggregateStorageType(typeName))
            return "";
        rank = std::max(rank, argRank);
    }
    if (rank == 0)
        return "";
    if (rank >= 4)
        return base + "l";
    if (rank <= 2)
        return base + "f";
    return base;
}

static Expr *stripLocalPrecisionNoise(Expr *expr)
{
    Expr *base = expr;
    while (base)
    {
        if (auto *paren = dyn_cast<ParenExpr>(base))
        {
            base = paren->getSubExpr();
            continue;
        }
        if (auto *cast = dyn_cast<ImplicitCastExpr>(base))
        {
            base = cast->getSubExpr();
            continue;
        }
        if (auto *unOp = dyn_cast<UnaryOperator>(base))
        {
            if (unOp->getOpcode() == UO_Plus || unOp->getOpcode() == UO_Minus)
            {
                base = unOp->getSubExpr();
                continue;
            }
        }
        break;
    }
    return base;
}

string getDirectPrecisionSourceType(Expr *expr, ASTContext *astContext, const string &callerFunc)
{
    Expr *base = stripLocalPrecisionNoise(expr);
    if (!base)
        return "";

    string typeName;
    if (auto *cast = dyn_cast<ExplicitCastExpr>(base))
    {
        typeName = cast->getType().getAsString();
    }
    else if (auto *declRef = dyn_cast<DeclRefExpr>(base))
    {
        if (auto *var = dyn_cast<VarDecl>(declRef->getDecl()))
            typeName = getTransformedVarExprType(var, base->getType().getAsString(), astContext, callerFunc);
    }
    else if (auto *subscript = dyn_cast<ArraySubscriptExpr>(base))
    {
        Expr *root = nullptr;
        int layers = countArraySubscriptLayers(subscript, &root);
        if (auto *declRef = dyn_cast_or_null<DeclRefExpr>(root))
        {
            if (auto *var = dyn_cast<VarDecl>(declRef->getDecl()))
            {
                string aggregateType = getTransformedVarExprType(var, root->getType().getAsString(), astContext, callerFunc);
                typeName = typeAfterSubscriptLayers(aggregateType, layers);
            }
        }
    }
    else if (auto *member = dyn_cast<MemberExpr>(base))
    {
        if (auto *field = dyn_cast<FieldDecl>(member->getMemberDecl()))
            typeName = getTransformedFieldExprType(field, base->getType().getAsString(), astContext);
    }
    else if (auto *call = dyn_cast<CallExpr>(base))
    {
        if (FunctionDecl *callee = call->getDirectCallee())
        {
            SourceLocation calleeLoc = callee->getLocation();
            if (calleeLoc.isValid() && !calleeLoc.isMacroID() &&
                astContext->getSourceManager().isWrittenInMainFile(astContext->getSourceManager().getSpellingLoc(calleeLoc)))
            {
                string fileName = getFileName(astContext, callee);
                if (!fileName.empty())
                {
                    string oldReturn = callee->getReturnType().getAsString();
                    string newReturn = getNewReturnType(callee->getNameAsString(), fileName, oldReturn,
                                                        getLineNumber(astContext, callee->getLocation()));
                    if (!newReturn.empty())
                        typeName = normalizeNewType(oldReturn, newReturn);
                }
            }
            if (typeName.empty())
                typeName = getKnownLibCallReturnType(callee->getNameAsString());
            if (typeName.empty())
                typeName = callee->getReturnType().getAsString();
        }
    }

    if (floatingTypeRank(typeName) == 0)
        return "";
    if (isAggregateStorageType(typeName))
        return "";
    return typeName;
}

static FloatingLiteral *getBareFloatingLiteral(Expr *expr)
{
    Expr *base = stripLocalPrecisionNoise(expr);
    return dyn_cast_or_null<FloatingLiteral>(base);
}

static bool rewriteFloatingLiteralForPrecision(FloatingLiteral *literal,
                                               const string &targetType,
                                               ASTContext *astContext)
{
    if (!literal || !astContext || floatingTypeRank(targetType) == 0)
        return false;
    string targetBase = normalizeTypeForSignature(stripLeadingConst(targetType));
    string suffix;
    if (targetBase == "float" || targetBase == "_Float16" ||
        targetBase == "__fp16" || targetBase == "__half")
    {
        suffix = "f";
    }
    else if (targetBase == "long double")
    {
        suffix = "L";
    }
    else
    {
        return false;
    }

    SourceLocation loc = literal->getBeginLoc();
    if (!shouldRewriteLoc(astContext, loc))
        return false;
    string key = locKey(loc, astContext);
    if (!key.empty() &&
        std::find(rewrittenFloatingLiteralLocs.begin(), rewrittenFloatingLiteralLocs.end(), key) != rewrittenFloatingLiteralLocs.end())
        return false;

    CharSourceRange range = CharSourceRange::getTokenRange(literal->getSourceRange());
    string text = trimSpaces(getSourceTextPreferRewritten(range, astContext));
    if (text.empty())
        return false;

    string baseText = text;
    while (!baseText.empty() &&
           (baseText.back() == 'f' || baseText.back() == 'F' ||
            baseText.back() == 'l' || baseText.back() == 'L'))
    {
        baseText.pop_back();
    }
    if (baseText.empty())
        return false;

    string replacement = baseText + suffix;
    if (replacement == text)
        return false;
    rewriter.ReplaceText(range, replacement);
    if (!key.empty())
        rewrittenFloatingLiteralLocs.push_back(key);
    return true;
}

static void rewriteLocalFloatingLiteralPrecision(BinaryOperator *op,
                                                 ASTContext *astContext,
                                                 const string &callerFunc)
{
    if (!op || !astContext)
        return;
    if (!(op->isAdditiveOp() || op->isMultiplicativeOp()))
        return;
    SourceLocation opLoc = op->getExprLoc();
    if (!shouldRewriteLoc(astContext, opLoc))
        return;

    FloatingLiteral *lhsLiteral = getBareFloatingLiteral(op->getLHS());
    FloatingLiteral *rhsLiteral = getBareFloatingLiteral(op->getRHS());
    if (lhsLiteral && !rhsLiteral)
    {
        string rhsSourceType = getDirectPrecisionSourceType(op->getRHS(), astContext, callerFunc);
        rewriteFloatingLiteralForPrecision(lhsLiteral, rhsSourceType, astContext);
        return;
    }
    if (rhsLiteral && !lhsLiteral)
    {
        string lhsSourceType = getDirectPrecisionSourceType(op->getLHS(), astContext, callerFunc);
        rewriteFloatingLiteralForPrecision(rhsLiteral, lhsSourceType, astContext);
    }
}

string getExprTypeAfterTransform(Expr *expr, ASTContext *astContext, const string &callerFunc)
{
    if (!expr)
        return "";
    Expr *base = expr;
    while (true)
    {
        if (auto *paren = dyn_cast<ParenExpr>(base))
        {
            base = paren->getSubExpr();
            continue;
        }
        if (auto *cast = dyn_cast<ImplicitCastExpr>(base))
        {
            base = cast->getSubExpr();
            continue;
        }
        break;
    }

    if (auto *cast = dyn_cast<ExplicitCastExpr>(base))
        return cast->getType().getAsString();

    if (auto *unOp = dyn_cast<UnaryOperator>(base))
    {
        if (unOp->getOpcode() == UO_AddrOf)
        {
            Expr *subExpr = unOp->getSubExpr()->IgnoreParenImpCasts();
            if (auto *subscript = dyn_cast<ArraySubscriptExpr>(subExpr))
            {
                Expr *root = nullptr;
                int layers = countArraySubscriptLayers(subscript, &root);
                if (auto *declRef = dyn_cast_or_null<DeclRefExpr>(root))
                {
                    if (auto *var = dyn_cast<VarDecl>(declRef->getDecl()))
                    {
                        string aggregateType = getTransformedVarExprType(var, root->getType().getAsString(), astContext, callerFunc);
                        string subscriptType = typeAfterSubscriptLayers(aggregateType, layers);
                        if (!subscriptType.empty())
                            return addPointerSuffix(subscriptType);
                    }
                }
            }
            if (auto *declRef = dyn_cast<DeclRefExpr>(subExpr))
            {
                if (auto *var = dyn_cast<VarDecl>(declRef->getDecl()))
                    return getTransformedVarExprType(var, base->getType().getAsString(), astContext, callerFunc);
            }
        }
        if (unOp->getOpcode() == UO_Plus || unOp->getOpcode() == UO_Minus)
            return getExprTypeAfterTransform(unOp->getSubExpr(), astContext, callerFunc);
        return base->getType().getAsString();
    }
    if (auto *declRef = dyn_cast<DeclRefExpr>(base))
    {
        if (auto *var = dyn_cast<VarDecl>(declRef->getDecl()))
            return getTransformedVarExprType(var, base->getType().getAsString(), astContext, callerFunc);
    }
    if (auto *subscript = dyn_cast<ArraySubscriptExpr>(base))
    {
        Expr *root = nullptr;
        int layers = countArraySubscriptLayers(subscript, &root);
        if (auto *declRef = dyn_cast_or_null<DeclRefExpr>(root))
        {
            if (auto *var = dyn_cast<VarDecl>(declRef->getDecl()))
            {
                string aggregateType = getTransformedVarExprType(var, root->getType().getAsString(), astContext, callerFunc);
                string subscriptType = typeAfterSubscriptLayers(aggregateType, layers);
                if (!subscriptType.empty())
                    return subscriptType;
            }
        }
    }
    if (auto *member = dyn_cast<MemberExpr>(base))
    {
        if (auto *field = dyn_cast<FieldDecl>(member->getMemberDecl()))
            return getTransformedFieldExprType(field, base->getType().getAsString(), astContext);
    }
    if (auto *binOp = dyn_cast<BinaryOperator>(base))
    {
        if (binOp->isAssignmentOp() || binOp->isCompoundAssignmentOp())
            return getExprTypeAfterTransform(binOp->getLHS(), astContext, callerFunc);
        if (binOp->getOpcode() == BO_Comma)
            return getExprTypeAfterTransform(binOp->getRHS(), astContext, callerFunc);
        if (binOp->isAdditiveOp() || binOp->isMultiplicativeOp())
        {
            string lhsType = getExprTypeAfterTransform(binOp->getLHS(), astContext, callerFunc);
            string rhsType = getExprTypeAfterTransform(binOp->getRHS(), astContext, callerFunc);
            return usualArithmeticTypeAfterTransform(lhsType, rhsType, base->getType().getAsString());
        }
        return base->getType().getAsString();
    }
    if (auto *cond = dyn_cast<ConditionalOperator>(base))
    {
        string trueType = getExprTypeAfterTransform(cond->getTrueExpr(), astContext, callerFunc);
        string falseType = getExprTypeAfterTransform(cond->getFalseExpr(), astContext, callerFunc);
        return usualArithmeticTypeAfterTransform(trueType, falseType, base->getType().getAsString());
    }
    if (auto *call = dyn_cast<CallExpr>(base))
    {
        if (FunctionDecl *callee = call->getDirectCallee())
        {
            SourceLocation calleeLoc = callee->getLocation();
            if (calleeLoc.isValid() && !calleeLoc.isMacroID() &&
                astContext->getSourceManager().isWrittenInMainFile(astContext->getSourceManager().getSpellingLoc(calleeLoc)))
            {
                string fileName = getFileName(astContext, callee);
                if (!fileName.empty())
                {
                    string oldReturn = callee->getReturnType().getAsString();
                    string newReturn = getNewReturnType(callee->getNameAsString(), fileName, oldReturn,
                                                        getLineNumber(astContext, callee->getLocation()));
                    if (!newReturn.empty())
                        return normalizeNewType(oldReturn, newReturn);
                }
            }
            string libType = getKnownLibCallReturnType(callee->getNameAsString());
            if (!libType.empty())
                return libType;
        }
    }
    return base->getType().getAsString();
}

string getConfiguredParamTypeForCallee(FunctionDecl *callee, unsigned index, ASTContext *astContext)
{
    if (!callee || index >= callee->getNumParams() || !astContext)
        return "";
    // A fixed boundary parameter is not an independent precision choice.
    // VisitCallExpr derives its interface from the actual caller storage.
    if (fixedBoundaryFunctions.count(callee->getNameAsString()) != 0)
        return "";
    ParmVarDecl *param = callee->getParamDecl(index);
    if (!param)
        return "";
    string fileName = getFileName(astContext, param);
    if (fileName.empty())
        return "";
    string oldType = param->getOriginalType().getAsString();
    string newType = getConfiguredTypeRaw(param->getNameAsString(),
                                          callee->getNameAsString(),
                                          fileName,
                                          "parmVar",
                                          getLineNumber(astContext, param->getLocation()));
    if (newType.empty())
        return "";
    if (isIntegerPlaceholderType(newType))
        return "";
    if (!isPrecisionRuleType(newType))
        return "";
    newType = stripTrailingParamName(normalizeNewType(oldType, newType), param->getNameAsString());
    if (normalizeTypeForSignature(newType) == normalizeTypeForSignature(oldType))
        return "";
    return newType;
}

string basename(std::string path)
{
    return std::string(std::find_if(path.rbegin(), path.rend(), MatchPathSeparator()).base(), path.end());
}

string stripExtension(const string &name)
{
    size_t pos = name.rfind('.');
    if (pos == string::npos)
        return name;
    return name.substr(0, pos);
}

FloatingPointTypeInfo DissectFloatingPointType(const clang::Type *typeObj, bool builtIn)
{
    if (typeObj == NULL)
    {
        FloatingPointTypeInfo info;
        info.isFloatingPoint = false;
        info.isVector = 0;
        info.typeObj = NULL;
        info.isArray = false;
        info.isPointer = false;
        return info;
    }
    FloatingPointTypeInfo info;
    info.isArray = false;
    info.isPointer = false;
    if (const clang::ArrayType *arr = dyn_cast<clang::ArrayType>(typeObj))
    {
        //        PRINT_DEBUG_MESSAGE("\t\tis array type");
        info = DissectFloatingPointType(arr->getElementType().getTypePtrOrNull(), false);
        info.isArray = true;
        return info;
    }
    else if (const clang::PointerType *ptr = dyn_cast<clang::PointerType>(typeObj))
    {
        //        PRINT_DEBUG_MESSAGE("\t\tis pointer type");
        info = DissectFloatingPointType(ptr->getPointeeType().getTypePtrOrNull(), false);
        info.isPointer = true;
        return info;
    }
    else if (const clang::PointerType *ptr = dyn_cast<clang::PointerType>(typeObj->getCanonicalTypeInternal()))
    {
        //        PRINT_DEBUG_MESSAGE("\t\tis pointer type");
        info = DissectFloatingPointType(ptr->getPointeeType().getTypePtrOrNull(), false);
        info.isPointer = false;
        return info;
    }

    //    PRINT_DEBUG_MESSAGE("\t\tinnermost type " << typeObj->getCanonicalTypeInternal().getAsString());
    if (const clang::BuiltinType *bltin = dyn_cast<clang::BuiltinType>(typeObj))
    {
        //        PRINT_DEBUG_MESSAGE("\t\tis builtin type, floating point: " << bltin->isFloatingPoint());
        info.isFloatingPoint = bltin->isFloatingPoint();
        info.isVector = 0;
        info.typeObj = typeObj;
        return info;
    }
    else if (typeObj->isStructureType())
    {
        //        PRINT_DEBUG_MESSAGE("\t\tis struct type");
        // TODO: with floating point built-in vectors and __half
        info.isFloatingPoint = false;
        info.isVector = 0;
        std::string typeStr = typeObj->getCanonicalTypeInternal().getAsString();
        if (typeStr == "struct __half")
        {
            info.isFloatingPoint = true;
            info.isVector = 0;
        }
        else
        {
            size_t pos = typeStr.find("struct float");
            if (pos != std::string::npos)
            {
                info.isFloatingPoint = true;
                info.isVector = typeStr[pos + strlen("struct float")] - '1';
            }
            pos = typeStr.find("struct double");
            if (pos != std::string::npos)
            {
                info.isFloatingPoint = true;
                info.isVector = typeStr[pos + strlen("struct double")] - '1';
            }
        }
        info.typeObj = typeObj;
        return info;
    }
    else
    {
        //        PRINT_DEBUG_MESSAGE("\t\tis another type");
        info.isFloatingPoint = false;
        info.isVector = 0;
        info.typeObj = typeObj;
        info.isArray = false;
        info.isPointer = false;
        return info;
    }
}

void loadRules()
{
    varRules.clear();
    callRules.clear();
    fieldRules.clear();
    returnRules.clear();
    fixedBoundaryFunctions.clear();

    if (!jf.is_object())
    {
        return;
    }

    // PrintConfig records excluded function interfaces separately from the
    // search space.  Keep both the source name and IR symbol where available.
    if (jf.contains("boundary_interfaces") &&
        jf["boundary_interfaces"].is_object())
    {
        for (auto it = jf["boundary_interfaces"].begin();
             it != jf["boundary_interfaces"].end(); ++it)
        {
            fixedBoundaryFunctions.insert(it.key());
            const auto &boundary = it.value();
            if (boundary.is_object())
            {
                if (boundary.contains("name") && boundary["name"].is_string())
                    fixedBoundaryFunctions.insert(boundary["name"].get<string>());
                if (boundary.contains("symbol") && boundary["symbol"].is_string())
                    fixedBoundaryFunctions.insert(boundary["symbol"].get<string>());
            }
        }
    }

    // global
    if (jf.contains("global") && jf["global"].is_array())
    {
        for (const auto &g : jf["global"])
        {
            if (!g.contains("name") || !g.contains("file") || !g.contains("type"))
                continue;
            VarRule rule;
            rule.location = "globalVar";
            rule.name = g["name"];
            rule.file = g["file"];
            rule.type = g["type"];
            if (g.contains("line"))
                rule.line = jsonToInt(g["line"]);
            if (rule.name == "ret.")
                returnRules.push_back(rule);
            else
                varRules.push_back(rule);
        }
    }

    // local / parameters
    if (jf.contains("local") && jf["local"].is_object())
    {
        for (auto it = jf["local"].begin(); it != jf["local"].end(); ++it)
        {
            string func = it.key();
            const auto &arr = it.value();
            if (!arr.is_array())
                continue;
            for (const auto &v : arr)
            {
                if (!v.contains("name") || !v.contains("file") || !v.contains("type"))
                    continue;
                VarRule rule;
                string name = v["name"];
                bool isParam = v.contains("isParam") && v["isParam"].get<bool>();
                rule.location = isParam ? "parmVar" : "localVar";
                rule.name = name;
                rule.func = func;
                rule.file = v["file"];
                rule.type = v["type"];
                if (v.contains("line"))
                    rule.line = jsonToInt(v["line"]);
                if (name == "ret.")
                    returnRules.push_back(rule);
                else
                    varRules.push_back(rule);
            }
        }
    }

    // calls
    if (jf.contains("calls") && jf["calls"].is_object())
    {
        for (auto it = jf["calls"].begin(); it != jf["calls"].end(); ++it)
        {
            string caller = it.key();
            const auto &arr = it.value();
            if (!arr.is_array())
                continue;
            for (const auto &c : arr)
            {
                if (!c.contains("file") || !c.contains("line") || !c.contains("type"))
                    continue;
                CallRule rule;
                rule.callerFunc = caller;
                rule.file = c["file"];
                rule.targetCallee = c["type"];
                rule.line = jsonToInt(c["line"]);
                if (c.contains("column"))
                    rule.column = jsonToInt(c["column"]);
                callRules.push_back(rule);
            }
        }
    }

    // structs / record fields
    if (jf.contains("structs") && jf["structs"].is_object())
    {
        for (auto it = jf["structs"].begin(); it != jf["structs"].end(); ++it)
        {
            string recordName = it.key();
            const auto &arr = it.value();
            if (!arr.is_array())
                continue;
            for (const auto &m : arr)
            {
                if (!m.contains("member_index") || !m.contains("type"))
                    continue;
                FieldRule rule;
                rule.record = recordName;
                rule.index = m["member_index"].get<int>();
                rule.type = m["type"];
                fieldRules.push_back(rule);
            }
        }
    }
}

static bool hasRelevantRulesForFile(const string &fileName)
{
    string base = basename(fileName);
    for (const auto &rule : varRules)
    {
        if (basename(rule.file) == base)
            return true;
    }
    for (const auto &rule : returnRules)
    {
        if (basename(rule.file) == base)
            return true;
    }
    for (const auto &rule : callRules)
    {
        if (basename(rule.file) == base)
            return true;
    }
    if (!fieldRules.empty())
        return true;
    return false;
}

static bool isStrictRewriteCandidateLoc(ASTContext *astContext, SourceLocation loc)
{
    if (!STRICT_CONFIG || !astContext || !loc.isValid() || loc.isMacroID())
        return false;
    SourceManager &SM = astContext->getSourceManager();
    SourceLocation spelling = SM.getSpellingLoc(loc);
    if (!spelling.isValid() || spelling.isMacroID())
        return false;
    if (SM.isWrittenInMainFile(spelling))
        return true;
    if (SM.isInSystemHeader(spelling))
        return false;
    auto entry = SM.getFileEntryRefForID(SM.getFileID(spelling));
    if (!entry)
        return false;
    string base = basename(entry->getName().str());
    return hasRelevantRulesForFile(base) || base.size() > 2;
}

static bool shouldRewriteLoc(ASTContext *astContext, SourceLocation loc)
{
    if (!astContext || !loc.isValid() || loc.isMacroID())
        return false;
    SourceManager &SM = astContext->getSourceManager();
    SourceLocation spelling = SM.getSpellingLoc(loc);
    if (!spelling.isValid() || spelling.isMacroID())
        return false;
    return SM.isWrittenInMainFile(spelling) || isStrictRewriteCandidateLoc(astContext, spelling);
}

void transformType(string location, VarDecl *val, ASTContext *astContext)
{
    SourceLocation loc = val->getLocation();
    if (!shouldRewriteLoc(astContext, loc))
        return;

    const clang::Type *typeObj = val->getType().getTypePtrOrNull();
    if (typeObj)
    {
        FloatingPointTypeInfo info = DissectFloatingPointType(typeObj, true);

        bool FPStrMatch = false;
        size_t pos1 = val->getType().getAsString().find("double");
        size_t pos2 = val->getType().getAsString().find("float");
        if (pos1 != std::string::npos || pos2 != std::string::npos)
        {
            FPStrMatch = true;
        }

        if (info.isFloatingPoint || FPStrMatch)
        {
            string valueName = val->getNameAsString();
            string typeName = "";
            int line = getLineNumber(astContext, val->getLocation());
            if (location == "parmVar")
                typeName = dyn_cast<clang::ParmVarDecl>(val)->getOriginalType().getAsString();
            else
            {
                typeName = val->getType().getAsString();
                string fileName = getFileName(astContext, val);
                if(fileName.empty()){
                    return ;
                }
                string newTypeName = getNewType(valueName, typeName, funcName, fileName, location, line);
                newTypeName = normalizeNewType(typeName, newTypeName);
                if (newTypeName != "")
                {
                    if (rewriteAllocatorDeclStmtForVar(val, typeName, newTypeName, astContext))
                        return;
                    if (rewriteSingleDeclStmtForVar(val, newTypeName, astContext))
                        return;
                    rewriteAllocatorInitializerForVar(val, typeName, newTypeName, astContext);
                    TypeSourceInfo *TSI = val->getTypeSourceInfo();
                    if (!TSI)
                        return;
                    auto TL = TSI->getTypeLoc();
                    // NOTE: Disabled replacedTypeLocs de-dup here due sporadic
                    // crashes on some LLVM builds. Range text comparison below
                    // still prevents redundant rewrites in most cases.
                    CharSourceRange range;
                    if (val->getType()->isArrayType())
                    {
                        TypeLoc baseLoc = getBaseTypeLoc(TL);
                        if (baseLoc.getBeginLoc().isValid() && baseLoc.getEndLoc().isValid())
                        {
                            string baseNewType = stripArraySuffix(newTypeName);
                            if (typeName.rfind("const ", 0) == 0)
                                baseNewType = stripLeadingConst(baseNewType);
                            range = CharSourceRange::getTokenRange(baseLoc.getBeginLoc(), baseLoc.getEndLoc());
                            string rangeText = getSourceTextPreferRewritten(range, astContext);
                            if (normalizeTypeForCompare(rangeText) == normalizeTypeForCompare(baseNewType))
                                return;
                            baseNewType = ensureTrailingSpaceIfNeeded(baseNewType, range, astContext);
                            rewriter.ReplaceText(range, baseNewType);
                            return;
                        }
                        SourceLocation tsBegin = val->getTypeSpecStartLoc();
                        SourceLocation tsEnd = val->getTypeSpecEndLoc();
                        if (tsBegin.isValid() && tsEnd.isValid())
                        {
                            string baseNewType = stripArraySuffix(newTypeName);
                            if (typeName.rfind("const ", 0) == 0)
                                baseNewType = stripLeadingConst(baseNewType);
                            range = CharSourceRange::getTokenRange(tsBegin, tsEnd);
                            string rangeText = getSourceTextPreferRewritten(range, astContext);
                            if (normalizeTypeForCompare(rangeText) == normalizeTypeForCompare(baseNewType))
                                return;
                            baseNewType = ensureTrailingSpaceIfNeeded(baseNewType, range, astContext);
                            rewriter.ReplaceText(range, baseNewType);
                            return;
                        }
                    }
                    SourceLocation tsBegin = val->getTypeSpecStartLoc();
                    SourceLocation tsEnd = val->getTypeSpecEndLoc();
                    if (tsBegin.isValid() && tsEnd.isValid())
                    {
                        range = CharSourceRange::getTokenRange(tsBegin, tsEnd);
                    }
                    else
                    {
                        range = CharSourceRange::getTokenRange(TL.getBeginLoc(), TL.getEndLoc());
                    }
                    string rangeText = getSourceTextPreferRewritten(range, astContext);
                    if (normalizeTypeForCompare(rangeText) == normalizeTypeForCompare(newTypeName))
                        return;
                    string replacement = ensureTrailingSpaceIfNeeded(newTypeName, range, astContext);
                    rewriter.ReplaceText(range, replacement);
                    ensureSpaceBeforeIdentifier(val->getLocation(), astContext);
                }
            }
        }
    }
}

void transformSwitch(clang::DeclRefExpr *declSt, clang::FunctionDecl *libcall, ASTContext *astContext)
{
    string valueName = libcall->getNameAsString();
    string switchName = libcall->getNameAsString();
    auto isBuiltinLikeRewriteTarget = [&](const string &name) {
        static const vector<string> allowed = {
            "sqrt", "sqrtf", "sqrtl",
            "exp", "expf", "expl",
            "pow", "powf", "powl",
            "sin", "sinf", "sinl",
            "cos", "cosf", "cosl",
            "tan", "tanf", "tanl",
            "asin", "asinf", "asinl",
            "acos", "acosf", "acosl",
            "atan", "atanf", "atanl",
            "atan2", "atan2f", "atan2l",
            "log", "logf", "logl",
            "log10", "log10f", "log10l",
            "fabs", "fabsf", "fabsl",
            "cbrt", "cbrtf", "cbrtl",
            "hypot", "hypotf", "hypotl",
            "floor", "floorf", "floorl",
            "ceil", "ceilf", "ceill",
            "trunc", "truncf", "truncl",
            "round", "roundf", "roundl"
        };
        for (const string &allowedName : allowed)
        {
            if (allowedName == name)
                return true;
        }
        return false;
    };
    string fileName = getFileName(astContext, declSt);
    if(fileName.empty()){
                    return ;
                }
    auto loc = declSt->getLocation();
    string rewrittenCallKey = locKey(loc, astContext);
    if (!rewrittenCallKey.empty() &&
        std::find(rewrittenCallNameLocs.begin(), rewrittenCallNameLocs.end(), rewrittenCallKey) != rewrittenCallNameLocs.end())
        return;
    CharSourceRange nameRange = CharSourceRange::getTokenRange(declSt->getSourceRange());
    string currentNameText = trimSpaces(getSourceTextPreferRewritten(nameRange, astContext));
    if (currentNameText != valueName)
        return;
    int line = getLineNumber(astContext, loc);
    int column = astContext->getSourceManager().getSpellingColumnNumber(loc);
    string newCallName = getNewCallTarget(funcName, fileName, switchName, line, column);
    newCallName = stripHeaderPrefixFromCallName(trimSpaces(newCallName));
    if (!newCallName.empty() && !getLibmFamilyBaseName(newCallName).empty())
    {
        auto parents = astContext->getParents(*declSt);
        for (const auto &parent : parents)
        {
            if (auto *call = parent.get<CallExpr>())
            {
                if (call->getCallee()->IgnoreParenImpCasts() != declSt)
                    continue;
                vector<string> actualParamTypes;
                for (unsigned i = 0; i < call->getNumArgs(); ++i)
                {
                    Expr *arg = const_cast<Expr *>(call->getArg(i));
                    string actualType = getExprTypeAfterTransform(arg, astContext, funcName);
                    actualType = refineArrayMemberArgumentType(arg, actualType, astContext, funcName);
                    actualParamTypes.push_back(actualType);
                }
                string typeDrivenCallName = pickLibmCallNameForActualTypes(newCallName, actualParamTypes);
                if (!typeDrivenCallName.empty())
                    newCallName = typeDrivenCallName;
                break;
            }
        }
    }
    if (newCallName != "")
    {
        string castName = trimSpaces(newCallName);
        if (castName == "float" || castName == "double" || castName == "long double")
        {
            auto parents = astContext->getParents(*declSt);
            for (const auto &parent : parents)
            {
                if (const auto *call = parent.get<CallExpr>())
                {
                    if (call->getCallee()->IgnoreParenImpCasts() != declSt)
                        continue;
                    if (call->getNumArgs() != 1)
                        return;
                    CharSourceRange callRange = CharSourceRange::getTokenRange(call->getSourceRange());
                    CharSourceRange argRange = CharSourceRange::getTokenRange(call->getArg(0)->getSourceRange());
                    string argText = getSourceTextPreferRewritten(argRange, astContext);
                    string replacement = "(" + castName + ")(" + argText + ")";
                    rewriter.ReplaceText(callRange, replacement);
                    return;
                }
            }
            return;
        }
        if (!isBuiltinLikeRewriteTarget(switchName) && !isBuiltinLikeRewriteTarget(castName))
            return;
        SourceManager &SM = astContext->getSourceManager();
        const LangOptions &LangOpts = astContext->getLangOpts();
        unsigned tokLen = Lexer::MeasureTokenLength(loc, SM, LangOpts);
        if (tokLen > 0)
        {
            SourceLocation tokEnd = loc.getLocWithOffset(tokLen);
            rewriter.ReplaceText(CharSourceRange::getCharRange(loc, tokEnd), newCallName);
        }
        else
        {
            rewriter.ReplaceText(loc, valueName.length(), newCallName);
        }
        if (!rewrittenCallKey.empty() &&
            std::find(rewrittenCallNameLocs.begin(), rewrittenCallNameLocs.end(), rewrittenCallKey) == rewrittenCallNameLocs.end())
            rewrittenCallNameLocs.push_back(rewrittenCallKey);
    }
}

bool GlobVisitor::VisitVarDecl(VarDecl *val)
{
    if (val->hasGlobalStorage())
    {
        if (rewriteGlobalMultiDeclForVar(val, astContext))
            return true;
        transformType("globalVar", val, astContext);
    }
    return true;
}

bool FieldVisitor::VisitFieldDecl(FieldDecl *val)
{
    SourceLocation loc = val->getLocation();
    if (!shouldRewriteLoc(astContext, loc))
        return true;

    const clang::Type *typeObj = val->getType().getTypePtrOrNull();
    if (typeObj)
    {
        FloatingPointTypeInfo info = DissectFloatingPointType(typeObj, true);

        bool FPStrMatch = false;
        size_t pos1 = val->getType().getAsString().find("double");
        size_t pos2 = val->getType().getAsString().find("float");
        if (pos1 != std::string::npos || pos2 != std::string::npos)
        {
            FPStrMatch = true;
        }

        if (info.isFloatingPoint || FPStrMatch)
        {
            string fieldName = val->getNameAsString();
            string recordName = "";
            if (const RecordDecl *parent = val->getParent())
            {
                recordName = parent->getNameAsString();
            }
            string typeName = val->getType().getAsString();
            string fileName = getFileName(astContext, val);
            if(fileName.empty()){
                return true;
            }
            int fieldIndex = static_cast<int>(val->getFieldIndex());
            string newTypeName = getNewFieldType(fieldName, recordName, fieldIndex, typeName, fileName);
            if (newTypeName != "")
            {
                newTypeName = normalizeNewType(typeName, newTypeName);
                TypeSourceInfo *TSI = val->getTypeSourceInfo();
                if (!TSI)
                    return true;
                auto TL = TSI->getTypeLoc();
                // NOTE: FieldDecl de-dup tracking via replacedTypeLocs was causing
                // sporadic crashes on some TU/LLVM builds. For field rewrites we
                // skip this guard and rely on text-equality checks below.
                CharSourceRange range;
                if (val->getType()->isArrayType())
                {
                    TypeLoc baseLoc = getBaseTypeLoc(TL);
                    if (baseLoc.getBeginLoc().isValid() && baseLoc.getEndLoc().isValid())
                    {
                        string baseNewType = stripArraySuffix(newTypeName);
                        if (typeName.rfind("const ", 0) == 0)
                            baseNewType = stripLeadingConst(baseNewType);
                        range = CharSourceRange::getTokenRange(baseLoc.getBeginLoc(), baseLoc.getEndLoc());
                        string rangeText = getSourceTextPreferRewritten(range, astContext);
                        if (normalizeTypeForCompare(rangeText) == normalizeTypeForCompare(baseNewType))
                            return true;
                        baseNewType = ensureTrailingSpaceIfNeeded(baseNewType, range, astContext);
                        rewriter.ReplaceText(range, baseNewType);
                        return true;
                    }
                    SourceLocation tsBegin = val->getTypeSpecStartLoc();
                    SourceLocation tsEnd = val->getTypeSpecEndLoc();
                    if (tsBegin.isValid() && tsEnd.isValid())
                    {
                        string baseNewType = stripArraySuffix(newTypeName);
                        if (typeName.rfind("const ", 0) == 0)
                            baseNewType = stripLeadingConst(baseNewType);
                        range = CharSourceRange::getTokenRange(tsBegin, tsEnd);
                        string rangeText = getSourceTextPreferRewritten(range, astContext);
                        if (normalizeTypeForCompare(rangeText) == normalizeTypeForCompare(baseNewType))
                            return true;
                        baseNewType = ensureTrailingSpaceIfNeeded(baseNewType, range, astContext);
                        rewriter.ReplaceText(range, baseNewType);
                        return true;
                    }
                }
                SourceLocation tsBegin = val->getTypeSpecStartLoc();
                SourceLocation tsEnd = val->getTypeSpecEndLoc();
                if (tsBegin.isValid() && tsEnd.isValid())
                {
                    range = CharSourceRange::getTokenRange(tsBegin, tsEnd);
                }
                else
                {
                    range = CharSourceRange::getTokenRange(TL.getBeginLoc(), TL.getEndLoc());
                }
                string rangeText = getSourceTextPreferRewritten(range, astContext);
                if (normalizeTypeForCompare(rangeText) == normalizeTypeForCompare(newTypeName))
                    return true;
                string replacement = ensureTrailingSpaceIfNeeded(newTypeName, range, astContext);
                rewriter.ReplaceText(range, replacement);
                ensureSpaceBeforeIdentifier(val->getLocation(), astContext);
            }
        }
    }
    return true;
}

bool VarsVisitor::VisitVarDecl(VarDecl *val)
{
    auto parents = astContext->getParents(*val);
    for (const auto &parent : parents)
    {
        if (const auto *declSt = parent.get<DeclStmt>())
        {
            if (!declSt->isSingleDecl())
            {
                return true;
            }
        }
    }
    transformType("localVar", val, astContext);
    return true;
}

bool VarsVisitor::VisitDeclStmt(DeclStmt *st)
{
    if (st == nullptr || st->isSingleDecl())
        return true;

    if (!shouldRewriteLoc(astContext, st->getBeginLoc()))
        return true;
    SourceManager &SM = astContext->getSourceManager();

    vector<VarDecl *> vars;
    for (Decl *decl : st->decls())
    {
        if (VarDecl *var = dyn_cast<VarDecl>(decl))
        {
            if (!var->getNameAsString().empty())
                vars.push_back(var);
        }
    }
    if (vars.size() <= 1)
        return true;

    vector<string> finalTypes;
    vector<string> declTexts;
    for (VarDecl *var : vars)
    {
        string typeName = var->getType().getAsString();
        string fileName = getFileName(astContext, var);
        if (fileName.empty())
            return true;
        int line = getLineNumber(astContext, var->getLocation());
        string newTypeName = getNewType(var->getNameAsString(), typeName, funcName, fileName, "localVar", line);
        newTypeName = normalizeNewType(typeName, newTypeName);
        if (newTypeName.empty())
            newTypeName = typeName;

        string baseType = newTypeName;
        if (baseType.find('[') != string::npos)
            baseType = stripArraySuffix(baseType);
        baseType = preserveConstPrefix(typeName, baseType);

        string storage = "";
        switch (var->getStorageClass())
        {
        case SC_Static:
            storage = "static ";
            break;
        case SC_Extern:
            storage = "extern ";
            break;
        case SC_Register:
            storage = "register ";
            break;
        case SC_Auto:
            storage = "auto ";
            break;
        default:
            break;
        }

        string finalType = storage + baseType;
        finalTypes.push_back(finalType);

    }

    bool hasDifferentType = false;
    if (!finalTypes.empty())
    {
        const string &firstType = finalTypes.front();
        for (size_t i = 1; i < finalTypes.size(); ++i)
        {
            if (finalTypes[i] != firstType)
            {
                hasDifferentType = true;
                break;
            }
        }
    }

    bool needsRewrite = hasDifferentType;
    if (!needsRewrite)
    {
        for (size_t i = 0; i < vars.size(); ++i)
        {
            VarDecl *var = vars[i];
            string typeName = var->getType().getAsString();
            string originalBase = typeName;
            if (originalBase.find('[') != string::npos)
                originalBase = stripArraySuffix(originalBase);
            originalBase = preserveConstPrefix(typeName, originalBase);

            string storage = "";
            switch (var->getStorageClass())
            {
            case SC_Static:
                storage = "static ";
                break;
            case SC_Extern:
                storage = "extern ";
                break;
            case SC_Register:
                storage = "register ";
                break;
            case SC_Auto:
                storage = "auto ";
                break;
            default:
                break;
            }

            string originalType = storage + originalBase;
            if (normalizeTypeForCompare(finalTypes[i]) != normalizeTypeForCompare(originalType))
            {
                needsRewrite = true;
                break;
            }
        }
    }

    if (!needsRewrite)
        return true;

    CharSourceRange stmtRange = CharSourceRange::getTokenRange(st->getSourceRange());
    string stmtText = getSourceTextRaw(stmtRange, astContext);
    size_t semicolon = stmtText.rfind(';');
    if (semicolon != string::npos)
        stmtText = stmtText.substr(0, semicolon);

    vector<string> segments;
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    bool inString = false;
    bool inChar = false;
    bool escape = false;
    bool inLineComment = false;
    bool inBlockComment = false;
    size_t segStart = 0;
    for (size_t i = 0; i < stmtText.size(); ++i)
    {
        char c = stmtText[i];
        if (inLineComment)
        {
            if (c == '\n')
                inLineComment = false;
            continue;
        }
        if (inBlockComment)
        {
            if (c == '*' && i + 1 < stmtText.size() && stmtText[i + 1] == '/')
            {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (escape)
        {
            escape = false;
            continue;
        }
        if (c == '\\')
        {
            escape = true;
            continue;
        }
        if (!inChar && c == '"' )
        {
            inString = !inString;
            continue;
        }
        if (!inString && c == '\'')
        {
            inChar = !inChar;
            continue;
        }
        if (inString || inChar)
            continue;
        if (c == '/' && i + 1 < stmtText.size() && stmtText[i + 1] == '/')
        {
            inLineComment = true;
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < stmtText.size() && stmtText[i + 1] == '*')
        {
            inBlockComment = true;
            ++i;
            continue;
        }
        if (c == '(') paren++;
        else if (c == ')') paren = (paren > 0) ? paren - 1 : 0;
        else if (c == '[') bracket++;
        else if (c == ']') bracket = (bracket > 0) ? bracket - 1 : 0;
        else if (c == '{') brace++;
        else if (c == '}') brace = (brace > 0) ? brace - 1 : 0;
        else if (c == ',' && paren == 0 && bracket == 0 && brace == 0)
        {
            segments.push_back(stmtText.substr(segStart, i - segStart));
            segStart = i + 1;
        }
    }
    segments.push_back(stmtText.substr(segStart));

    if (segments.size() != vars.size())
        return true;

    if (!segments.empty())
    {
        size_t prefixLen = 0;
        string seg0 = segments[0];
        if (!vars.empty())
        {
            SourceLocation stmtBegin = SM.getSpellingLoc(st->getBeginLoc());
            SourceLocation firstVarLoc = SM.getSpellingLoc(vars[0]->getLocation());
            if (stmtBegin.isValid() && firstVarLoc.isValid() &&
                SM.getFileID(stmtBegin) == SM.getFileID(firstVarLoc))
            {
                unsigned beginOff = SM.getFileOffset(stmtBegin);
                unsigned varOff = SM.getFileOffset(firstVarLoc);
                if (varOff >= beginOff)
                    prefixLen = static_cast<size_t>(varOff - beginOff);
            }

            if (prefixLen == 0)
            {
                // Fallback for unusual rewritten text where offset-based slicing is not usable.
                // For multi-declarator statements like
                //   double d[4], dd[4], e[4];
                // keep the first declarator name (`d`) and only strip the type prefix.
                const string firstName = vars[0]->getNameAsString();
                if (!firstName.empty())
                {
                    size_t pos = 0;
                    while ((pos = seg0.find(firstName, pos)) != string::npos)
                    {
                        bool leftOk = (pos == 0) ||
                                      !(isalnum(static_cast<unsigned char>(seg0[pos - 1])) || seg0[pos - 1] == '_');
                        size_t after = pos + firstName.size();
                        bool rightOk = (after >= seg0.size()) ||
                                       !(isalnum(static_cast<unsigned char>(seg0[after])) || seg0[after] == '_');
                        if (leftOk && rightOk)
                        {
                            prefixLen = pos;
                            break;
                        }
                        pos = after;
                    }
                }
            }

            if (prefixLen == 0)
            {
                SourceLocation typeEnd = SM.getSpellingLoc(vars[0]->getTypeSpecEndLoc());
                if (stmtBegin.isValid() && typeEnd.isValid())
                {
                    SourceLocation typeEndTok = Lexer::getLocForEndOfToken(typeEnd, 0, SM, astContext->getLangOpts());
                    unsigned beginOff = SM.getFileOffset(stmtBegin);
                    unsigned endOff = SM.getFileOffset(typeEndTok);
                    if (endOff >= beginOff)
                        prefixLen = static_cast<size_t>(endOff - beginOff);
                }
            }
        }

        if (prefixLen > seg0.size())
            prefixLen = seg0.size();
        segments[0] = segments[0].substr(prefixLen);
    }

    for (size_t i = 0; i < segments.size(); ++i)
    {
        string seg = ltrim(segments[i]);
        if (seg.empty())
            return true;
        const string &varName = vars[i]->getNameAsString();
        if (!varName.empty())
        {
            size_t pos = 0;
            while ((pos = seg.find(varName, pos)) != string::npos)
            {
                bool leftOk = (pos == 0) ||
                              !(isalnum(static_cast<unsigned char>(seg[pos - 1])) || seg[pos - 1] == '_');
                size_t after = pos + varName.size();
                bool rightOk = (after >= seg.size()) ||
                               !(isalnum(static_cast<unsigned char>(seg[after])) || seg[after] == '_');
                if (leftOk && rightOk)
                {
                    seg = seg.substr(pos);
                    break;
                }
                pos = after;
            }
        }
        declTexts.push_back(seg);
    }

    string indent = "";
    for (char c : stmtText)
    {
        if (c == ' ' || c == '\t')
            indent.push_back(c);
        else
            break;
    }
    string newText;
    for (size_t i = 0; i < vars.size(); ++i)
    {
        if (i == 0)
            newText += indent + finalTypes[i] + " " + declTexts[i] + ";";
        else
            newText += " " + finalTypes[i] + " " + declTexts[i] + ";";
    }

    rewriter.ReplaceText(stmtRange, newText);
    // NOTE: Disabled replacedTypeLocs insert here due instability.
    return true;
}

bool FuncStmtVisitor::VisitStmt(Stmt *st)
{
    // Excluded boundary functions still need call-edge rewriting, but their
    // ordinary references, literals and assignments must remain untouched.
    if (callOnly)
        return true;
    if (clang::DeclRefExpr *declSt = dyn_cast<clang::DeclRefExpr>(st))
    {
        if (libcall_flag == 0)
        {
            if (clang::VarDecl *var = dyn_cast<clang::VarDecl>(declSt->getDecl()))
            {
                if (!current_ParamShimName.empty() &&
                    var->getNameAsString() == current_ParamName)
                {
                    CharSourceRange refRange = CharSourceRange::getTokenRange(declSt->getSourceRange());
                    string refText = getSourceTextPreferRewritten(refRange, astContext);
                    if (trimSpaces(refText) == current_ParamName)
                    {
                        rewriter.ReplaceText(refRange, current_ParamShimName);
                        string key = locKey(declSt->getLocation(), astContext);
                        if (!key.empty() &&
                            std::find(rewrittenParamRefLocs.begin(), rewrittenParamRefLocs.end(), key) == rewrittenParamRefLocs.end())
                            rewrittenParamRefLocs.push_back(key);
                    }
                }
            }
        }
        else
        {
            if (clang::FunctionDecl *libcall = dyn_cast<clang::FunctionDecl>(declSt->getDecl()))
            {
                transformSwitch(declSt, libcall, astContext);
            }
        }
    }
    return true;
}

bool rewriteCallCalleeName(CallExpr *call, FunctionDecl *callee, const string &newName, ASTContext *astContext = nullptr)
{
    if (!call || !callee || newName.empty())
        return false;
    if (auto *calleeRef = dyn_cast<DeclRefExpr>(call->getCallee()->IgnoreParenImpCasts()))
    {
        SourceLocation nameLoc = calleeRef->getLocation();
        if (nameLoc.isValid())
        {
            if (astContext)
            {
                SourceManager &SM = astContext->getSourceManager();
                const LangOptions &LangOpts = astContext->getLangOpts();
                unsigned tokLen = Lexer::MeasureTokenLength(nameLoc, SM, LangOpts);
                if (tokLen > 0)
                {
                    SourceLocation tokEnd = nameLoc.getLocWithOffset(tokLen);
                    rewriter.ReplaceText(CharSourceRange::getCharRange(nameLoc, tokEnd), newName);
                }
                else
                {
                    rewriter.ReplaceText(nameLoc, callee->getNameAsString().size(), newName);
                }
                string key = locKey(nameLoc, astContext);
                bool firstRewriteAtLocation =
                    !key.empty() &&
                    std::find(rewrittenCallNameLocs.begin(), rewrittenCallNameLocs.end(), key) == rewrittenCallNameLocs.end();
                if (firstRewriteAtLocation)
                {
                    rewrittenCallNameLocs.push_back(key);
                    if (newName != callee->getNameAsString())
                    {
                        const FunctionDecl *canonical =
                            canonicalFunctionDecl(callee);
                        if (canonical)
                            ++rewrittenOriginalCallCounts[canonical];
                    }
                }
            }
            else
            {
                rewriter.ReplaceText(nameLoc, callee->getNameAsString().size(), newName);
            }
            return true;
        }
    }
    return false;
}

bool rewriteLibmCallNameForActualArgs(CallExpr *call, FunctionDecl *callee, ASTContext *astContext, const string &callerFunc)
{
    if (!call || !callee || !astContext)
        return false;
    if (auto *calleeRef = dyn_cast<DeclRefExpr>(call->getCallee()->IgnoreParenImpCasts()))
    {
        string key = locKey(calleeRef->getLocation(), astContext);
        if (!key.empty() &&
            std::find(rewrittenCallNameLocs.begin(), rewrittenCallNameLocs.end(), key) != rewrittenCallNameLocs.end())
            return false;
    }
    if (getLibmFamilyBaseName(callee->getNameAsString()).empty())
        return false;
    vector<string> actualParamTypes;
    for (unsigned i = 0; i < call->getNumArgs(); ++i)
    {
        Expr *arg = call->getArg(i);
        string actualType = getExprTypeAfterTransform(arg, astContext, callerFunc);
        actualType = refineArrayMemberArgumentType(arg, actualType, astContext, callerFunc);
        actualParamTypes.push_back(actualType);
    }
    string libmCallName = pickLibmCallNameForActualTypes(callee->getNameAsString(), actualParamTypes);
    if (libmCallName.empty() || libmCallName == callee->getNameAsString())
        return false;
    return rewriteCallCalleeName(call, callee, libmCallName, astContext);
}

bool containsRewrittenParamRef(Stmt *st, ASTContext *astContext)
{
    if (!st || !astContext)
        return false;
    if (auto *declRef = dyn_cast<DeclRefExpr>(st))
    {
        string key = locKey(declRef->getLocation(), astContext);
        if (!key.empty() &&
            std::find(rewrittenParamRefLocs.begin(), rewrittenParamRefLocs.end(), key) != rewrittenParamRefLocs.end())
            return true;
    }
    for (Stmt *child : st->children())
    {
        if (containsRewrittenParamRef(child, astContext))
            return true;
    }
    return false;
}

bool containsRewrittenCallName(Stmt *st, ASTContext *astContext)
{
    if (!st || !astContext)
        return false;
    if (auto *declRef = dyn_cast<DeclRefExpr>(st))
    {
        string key = locKey(declRef->getLocation(), astContext);
        if (!key.empty() &&
            std::find(rewrittenCallNameLocs.begin(), rewrittenCallNameLocs.end(), key) != rewrittenCallNameLocs.end())
            return true;
    }
    for (Stmt *child : st->children())
    {
        if (containsRewrittenCallName(child, astContext))
            return true;
    }
    return false;
}

bool containsCallExprNode(Stmt *st)
{
    if (!st)
        return false;
    if (isa<CallExpr>(st))
        return true;
    for (Stmt *child : st->children())
    {
        if (containsCallExprNode(child))
            return true;
    }
    return false;
}

bool isFloatingScalarTypeString(const string &typeName)
{
    string t = normalizeTypeForSignature(stripLeadingConst(typeName));
    return t == "float" || t == "double" || t == "long double" ||
           t == "_Float16" || t == "__fp16" || t == "__half";
}

static bool isPrintfLikeFunctionName(const string &calleeName)
{
    string name = stripHeaderPrefixFromCallName(calleeName);
    return name == "printf";
}

static bool isFloatingPrintfConversion(char conv)
{
    switch (conv)
    {
    case 'a':
    case 'A':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
        return true;
    default:
        return false;
    }
}

static string rewriteFloatingPrintfFormatText(const string &formatText,
                                              CallExpr *call,
                                              ASTContext *astContext,
                                              const string &callerFunc)
{
    if (!call || !astContext || formatText.empty() || call->getNumArgs() < 2)
        return formatText;

    string rewritten = formatText;
    unsigned valueArgIndex = 1;
    size_t i = 0;
    while (i < rewritten.size())
    {
        if (rewritten[i] == '\\')
        {
            i += 2;
            continue;
        }
        if (rewritten[i] != '%')
        {
            ++i;
            continue;
        }
        if (i + 1 < rewritten.size() && rewritten[i + 1] == '%')
        {
            i += 2;
            continue;
        }

        size_t specStart = i;
        ++i;
        while (i < rewritten.size() && strchr("-+ #0'", rewritten[i]) != nullptr)
            ++i;
        if (i < rewritten.size() && rewritten[i] == '*')
        {
            ++valueArgIndex;
            ++i;
        }
        else
        {
            while (i < rewritten.size() && isdigit(static_cast<unsigned char>(rewritten[i])))
                ++i;
        }
        if (i < rewritten.size() && rewritten[i] == '.')
        {
            ++i;
            if (i < rewritten.size() && rewritten[i] == '*')
            {
                ++valueArgIndex;
                ++i;
            }
            else
            {
                while (i < rewritten.size() && isdigit(static_cast<unsigned char>(rewritten[i])))
                    ++i;
            }
        }

        size_t lengthStart = i;
        string length;
        if (i + 1 < rewritten.size())
        {
            string two = rewritten.substr(i, 2);
            if (two == "hh" || two == "ll")
            {
                length = two;
                i += 2;
            }
        }
        if (length.empty() && i < rewritten.size() && strchr("hljztL", rewritten[i]) != nullptr)
        {
            length.push_back(rewritten[i]);
            ++i;
        }
        size_t lengthEnd = i;
        if (i >= rewritten.size())
            break;

        char conv = rewritten[i];
        if (isFloatingPrintfConversion(conv) && valueArgIndex < call->getNumArgs())
        {
            string argType = getExprTypeAfterTransform(call->getArg(valueArgIndex), astContext, callerFunc);
            string normArgType = normalizeTypeForSignature(argType);
            bool wantsLongDouble = normArgType == "long double";
            bool wantsDoubleLike = normArgType == "double" || normArgType == "float" ||
                                   normArgType == "_Float16" || normArgType == "__fp16" ||
                                   normArgType == "__half";
            if (wantsLongDouble && length != "L")
            {
                rewritten.insert(lengthEnd, "L");
                ++i;
            }
            else if (wantsDoubleLike && length == "L")
            {
                rewritten.erase(lengthStart, lengthEnd - lengthStart);
                i -= (lengthEnd - lengthStart);
            }
        }

        ++valueArgIndex;
        if (i <= specStart)
            ++i;
        else
            ++i;
    }

    return rewritten;
}

static void rewritePrintfFormatForTransformedArgs(CallExpr *call,
                                                  FunctionDecl *callee,
                                                  ASTContext *astContext,
                                                  const string &callerFunc)
{
    if (!call || !callee || !astContext || call->getNumArgs() == 0)
        return;
    if (!isPrintfLikeFunctionName(callee->getNameAsString()))
        return;
    Expr *fmtExpr = call->getArg(0)->IgnoreParenImpCasts();
    auto *literal = dyn_cast<clang::StringLiteral>(fmtExpr);
    if (!literal)
        return;
    CharSourceRange fmtRange = CharSourceRange::getTokenRange(literal->getSourceRange());
    string fmtText = getSourceTextPreferRewritten(fmtRange, astContext);
    string rewritten = rewriteFloatingPrintfFormatText(fmtText, call, astContext, callerFunc);
    if (rewritten != fmtText)
        rewriter.ReplaceText(fmtRange, rewritten);
}

bool isFloatingPointerTypeString(const string &typeName)
{
    string t = normalizeTypeForSignature(typeName);
    if (t.find('*') == string::npos)
        return false;
    return !getFloatingStorageBaseType(t).empty();
}

bool isAddressOfNonArrayScalar(Expr *expr)
{
    if (!expr)
        return false;
    Expr *base = expr->IgnoreParenImpCasts();
    auto *unOp = dyn_cast<UnaryOperator>(base);
    if (!unOp || unOp->getOpcode() != UO_AddrOf)
        return false;
    Expr *subExpr = unOp->getSubExpr()->IgnoreParenImpCasts();
    auto *declRef = dyn_cast<DeclRefExpr>(subExpr);
    if (!declRef)
        return false;
    auto *var = dyn_cast<VarDecl>(declRef->getDecl());
    if (!var)
        return false;
    QualType type = var->getType();
    return !type->isArrayType() && !type->isPointerType();
}

VarDecl *getAddressedNonArrayScalarVar(Expr *expr)
{
    if (!expr)
        return nullptr;
    Expr *base = expr->IgnoreParenImpCasts();
    auto *unOp = dyn_cast<UnaryOperator>(base);
    if (!unOp || unOp->getOpcode() != UO_AddrOf)
        return nullptr;
    Expr *subExpr = unOp->getSubExpr()->IgnoreParenImpCasts();
    auto *declRef = dyn_cast<DeclRefExpr>(subExpr);
    if (!declRef)
        return nullptr;
    auto *var = dyn_cast<VarDecl>(declRef->getDecl());
    if (!var)
        return nullptr;
    QualType type = var->getType();
    if (type->isArrayType() || type->isPointerType())
        return nullptr;
    return var;
}

VarDecl *findReferencedVarDeclForAbiBoundary(Expr *expr)
{
    if (!expr)
        return nullptr;
    Expr *base = expr->IgnoreParenImpCasts();
    if (auto *declRef = dyn_cast<DeclRefExpr>(base))
        return dyn_cast<VarDecl>(declRef->getDecl());
    if (auto *unOp = dyn_cast<UnaryOperator>(base))
    {
        if (unOp->getOpcode() == UO_AddrOf)
            return findReferencedVarDeclForAbiBoundary(unOp->getSubExpr());
    }
    if (auto *subscript = dyn_cast<ArraySubscriptExpr>(base))
        return findReferencedVarDeclForAbiBoundary(subscript->getBase());
    return nullptr;
}

void protectExternalAggregateAbiArg(Expr *arg,
                                    const string &declaredParamType,
                                    ASTContext *astContext,
                                    const string &callerFunc)
{
    VarDecl *var = findReferencedVarDeclForAbiBoundary(arg);
    if (!var || !astContext)
        return;
    string fileName = getFileName(astContext, var);
    if (fileName.empty())
        return;
    string location = var->hasGlobalStorage() ? "globalVar" : "localVar";
    if (isa<ParmVarDecl>(var))
        location = "parmVar";
    int line = getLineNumber(astContext, var->getLocation());
    string oldType = var->getType().getAsString();
    string configuredType = getConfiguredTypeRaw(var->getNameAsString(),
                                                 callerFunc,
                                                 fileName,
                                                 location,
                                                 line);
    if (configuredType.empty() || isIntegerPlaceholderType(configuredType))
        return;
    string newType = normalizeNewType(oldType, configuredType);
    if (normalizeTypeForSignature(newType) == normalizeTypeForSignature(oldType))
        return;
    if (!isAggregateStorageType(oldType) && !isAggregateStorageType(newType))
        return;
    string declaredBase = getFloatingStorageBaseType(declaredParamType);
    string newBase = getFloatingStorageBaseType(newType);
    if (declaredBase.empty() || newBase.empty())
        return;
    if (normalizeTypeForSignature(declaredBase) == normalizeTypeForSignature(newBase))
        return;
    externalAbiProtectedVarKeys.insert(makeExternalAbiProtectedVarKey(var->getNameAsString(), fileName, location, line));
}

void collectExternalAbiProtectedVarsFromStmt(Stmt *stmt, ASTContext *astContext, const string &callerFunc)
{
    if (!stmt || !astContext)
        return;
    if (auto *call = dyn_cast<CallExpr>(stmt))
    {
        FunctionDecl *callee = call->getDirectCallee();
        if (callee && !callee->isVariadic() && call->getNumArgs() == callee->getNumParams())
        {
            SourceManager &SM = astContext->getSourceManager();
            FunctionDecl *definition = callee->getDefinition();
            FunctionDecl *target = definition ? definition : callee;
            SourceLocation calleeLoc = target->getLocation();
            bool calleeInMainFile = definition &&
                                    calleeLoc.isValid() &&
                                    !calleeLoc.isMacroID() &&
                                    SM.isWrittenInMainFile(SM.getSpellingLoc(calleeLoc));
            if (!calleeInMainFile)
            {
                for (unsigned i = 0; i < call->getNumArgs(); ++i)
                {
                    string declaredType = callee->getParamDecl(i)->getType().getAsString();
                    if (isAggregateStorageType(declaredType) && isFloatingPointerTypeString(declaredType))
                        protectExternalAggregateAbiArg(call->getArg(i), declaredType, astContext, callerFunc);
                }
            }
        }
    }
    for (Stmt *child : stmt->children())
        collectExternalAbiProtectedVarsFromStmt(child, astContext, callerFunc);
}

void collectExternalAbiProtectedVars(ASTContext &Context)
{
    TranslationUnitDecl *tu = Context.getTranslationUnitDecl();
    for (Decl *decl : tu->decls())
    {
        auto *func = dyn_cast<FunctionDecl>(decl);
        if (!func || !func->doesThisDeclarationHaveABody())
            continue;
        SourceManager &SM = Context.getSourceManager();
        SourceLocation loc = func->getLocation();
        if (!loc.isValid() || loc.isMacroID() || !SM.isWrittenInMainFile(SM.getSpellingLoc(loc)))
            continue;
        collectExternalAbiProtectedVarsFromStmt(func->getBody(), &Context, func->getNameAsString());
    }
}

string buildExternalAbiWrapperName(FunctionDecl *callee)
{
    string base = callee ? callee->getNameAsString() : "external";
    return base + "_abi_" + std::to_string(externalAbiWrapperNameBySignature.size() + 1);
}

string buildExternalAbiWrapperFunction(FunctionDecl *callee,
                                       const string &wrapperName,
                                       const vector<string> &actualParamTypes,
                                       const vector<string> &declaredParamTypes,
                                       const vector<bool> &scalarPointerBridges)
{
    if (!callee || wrapperName.empty() || actualParamTypes.size() != declaredParamTypes.size() ||
        actualParamTypes.size() != scalarPointerBridges.size())
        return "";

    string returnType = callee->getReturnType().getAsString();
    returnType = getAbiCompatibleInterfaceType(returnType);
    vector<string> paramDecls;
    for (size_t i = 0; i < actualParamTypes.size(); ++i)
    {
        string typeName = actualParamTypes[i].empty() ? declaredParamTypes[i] : actualParamTypes[i];
        typeName = getAbiCompatibleInterfaceType(typeName);
        paramDecls.push_back(buildParamDeclText(typeName, "p" + std::to_string(i)));
    }

    string wrapper = "static " + returnType + " " + wrapperName + "(";
    for (size_t i = 0; i < paramDecls.size(); ++i)
    {
        if (i > 0)
            wrapper += ", ";
        wrapper += paramDecls[i];
    }
    wrapper += ")\n{\n";

    vector<string> callArgs;
    vector<string> postWrites;
    for (size_t i = 0; i < actualParamTypes.size(); ++i)
    {
        string paramName = "p" + std::to_string(i);
        string actualType = normalizeTypeForSignature(actualParamTypes[i]);
        string declaredType = normalizeTypeForSignature(declaredParamTypes[i]);
        if (scalarPointerBridges[i])
        {
            string declaredBase = stripLeadingConst(getFloatingStorageBaseType(declaredType));
            string actualBase = stripLeadingConst(getFloatingStorageBaseType(actualType));
            if (declaredBase.empty() || actualBase.empty())
                return "";
            string tmpName = paramName + "_tmp";
            wrapper += "    " + declaredBase + " " + tmpName + " = (" + declaredBase + ")(*" + paramName + ");\n";
            callArgs.push_back("&" + tmpName);
            postWrites.push_back("    *" + paramName + " = (" + actualBase + ")" + tmpName + ";\n");
            continue;
        }
        if (normalizeTypeForSignature(actualType) != normalizeTypeForSignature(declaredType) &&
            isFloatingScalarTypeString(actualType) && isFloatingScalarTypeString(declaredType))
        {
            callArgs.push_back("(" + declaredType + ")" + paramName);
            continue;
        }
        callArgs.push_back(paramName);
    }

    string callText = callee->getNameAsString() + "(";
    for (size_t i = 0; i < callArgs.size(); ++i)
    {
        if (i > 0)
            callText += ", ";
        callText += callArgs[i];
    }
    callText += ")";

    if (callee->getReturnType()->isVoidType())
    {
        wrapper += "    " + callText + ";\n";
        for (const string &post : postWrites)
            wrapper += post;
        wrapper += "}\n";
    }
    else
    {
        wrapper += "    " + returnType + " ret = " + callText + ";\n";
        for (const string &post : postWrites)
            wrapper += post;
        wrapper += "    return ret;\n";
        wrapper += "}\n";
    }
    return wrapper;
}

string buildExternalAbiWrapperPrototype(FunctionDecl *callee,
                                        const string &wrapperName,
                                        const vector<string> &actualParamTypes,
                                        const vector<string> &declaredParamTypes)
{
    if (!callee || wrapperName.empty() || actualParamTypes.size() != declaredParamTypes.size())
        return "";

    string returnType = getAbiCompatibleInterfaceType(callee->getReturnType().getAsString());
    vector<string> paramDecls;
    for (size_t i = 0; i < actualParamTypes.size(); ++i)
    {
        string typeName = actualParamTypes[i].empty() ? declaredParamTypes[i] : actualParamTypes[i];
        typeName = getAbiCompatibleInterfaceType(typeName);
        paramDecls.push_back(buildParamDeclText(typeName, "p" + std::to_string(i)));
    }

    string proto = "static " + returnType + " " + wrapperName + "(";
    for (size_t i = 0; i < paramDecls.size(); ++i)
    {
        if (i > 0)
            proto += ", ";
        proto += paramDecls[i];
    }
    proto += ");\n";
    return proto;
}

bool handleExternalAbiBoundaryCall(CallExpr *call, FunctionDecl *callee, ASTContext *astContext, const string &callerFunc)
{
    if (!call || !callee || !astContext)
        return false;
    if (callee->isVariadic() || call->getNumArgs() != callee->getNumParams())
        return false;

    vector<string> actualParamTypes;
    vector<string> declaredParamTypes;
    vector<bool> scalarPointerBridges;
    bool needsWrapper = false;
    bool unsupportedAggregateMismatch = false;

    for (unsigned i = 0; i < call->getNumArgs(); ++i)
    {
        Expr *arg = call->getArg(i);
        string actualType = getExprTypeAfterTransform(arg, astContext, callerFunc);
        actualType = refineArrayMemberArgumentType(arg, actualType, astContext, callerFunc);
        string declaredType = callee->getParamDecl(i)->getType().getAsString();
        if (isFloatingPointerTypeString(declaredType))
        {
            if (!isFloatingPointerTypeString(actualType))
            {
                if (VarDecl *addressedVar = getAddressedNonArrayScalarVar(arg))
                    actualType = addressedVar->getType().getAsString() + " *";
            }
        }
        actualParamTypes.push_back(actualType);
        declaredParamTypes.push_back(declaredType);
        scalarPointerBridges.push_back(false);

        if (normalizeTypeForSignature(actualType) == normalizeTypeForSignature(declaredType))
            continue;

        if (isFloatingPointerTypeString(actualType) &&
            isFloatingPointerTypeString(declaredType) &&
            isAddressOfNonArrayScalar(arg))
        {
            scalarPointerBridges.back() = true;
            needsWrapper = true;
            continue;
        }

        if (isFloatingScalarTypeString(actualType) && isFloatingScalarTypeString(declaredType))
        {
            needsWrapper = true;
            continue;
        }

        if (isAggregateStorageType(actualType) || isAggregateStorageType(declaredType))
        {
            unsupportedAggregateMismatch = true;
            continue;
        }
    }

    string libmCallName = pickLibmCallNameForActualTypes(callee->getNameAsString(), actualParamTypes);
    if (!libmCallName.empty() && libmCallName != callee->getNameAsString())
    {
        rewriteCallCalleeName(call, callee, libmCallName, astContext);
        return true;
    }

    if (!needsWrapper || unsupportedAggregateMismatch)
        return false;

    string wrapperKey = buildExternalAbiWrapperKey(callee, actualParamTypes, astContext);
    string wrapperName = findExternalAbiWrapperName(wrapperKey);
    if (wrapperName.empty())
    {
        wrapperName = buildExternalAbiWrapperName(callee);
        string wrapper = buildExternalAbiWrapperFunction(callee,
                                                         wrapperName,
                                                         actualParamTypes,
                                                         declaredParamTypes,
                                                         scalarPointerBridges);
        if (wrapper.empty())
            return false;
        // Keep the fixed callee visible to the generated wrapper even
        // when its source prototype is textually below the insertion point.
        string originalPrototype = buildFunctionPrototype(
            callee, callee->getNameAsString(), declaredParamTypes, astContext);
        if (!originalPrototype.empty() &&
            std::find(insertedFunctionPrototypeTexts.begin(), insertedFunctionPrototypeTexts.end(), originalPrototype) == insertedFunctionPrototypeTexts.end())
        {
            raiseExternalAbiInsertLocForDecl(callee, astContext);
            insertedFunctionPrototypeTexts.push_back(originalPrototype);
            pendingExternalAbiPrototypes.push_back(originalPrototype);
        }
        string prototype = buildExternalAbiWrapperPrototype(callee,
                                                           wrapperName,
                                                           actualParamTypes,
                                                           declaredParamTypes);
        if (!prototype.empty() &&
            std::find(insertedFunctionPrototypeTexts.begin(), insertedFunctionPrototypeTexts.end(), prototype) == insertedFunctionPrototypeTexts.end())
        {
            raiseExternalAbiInsertLocForDecl(callee, astContext);
            insertedFunctionPrototypeTexts.push_back(prototype);
            pendingExternalAbiPrototypes.push_back(prototype);
        }
        pendingExternalAbiPrototypes.push_back(wrapper + "\n");
        rememberExternalAbiWrapperName(wrapperKey, wrapperName);
    }

    rewriteCallCalleeName(call, callee, wrapperName, astContext);
    return true;
}

bool FuncStmtVisitor::VisitCallExpr(CallExpr *call)
{
    if (libcall_flag == 0)
        return true;
    if (!call)
        return true;
    SourceManager &SM = astContext->getSourceManager();
    SourceLocation callLoc = call->getExprLoc();
    if (!shouldRewriteLoc(astContext, callLoc))
        return true;
    FunctionDecl *callee = call->getDirectCallee();
    if (!callee)
        return true;
    rewritePrintfFormatForTransformedArgs(call, callee, astContext, funcName);
    rewriteLibmCallNameForActualArgs(call, callee, astContext, funcName);
    if (TRANSFORM_PHASE == PHASE_TYPES_ONLY)
        return true;
    if (STRICT_CONFIG && !CALLER_DRIVEN_CLONE)
        return true;
    if (callee->isVariadic())
        return true;
    FunctionDecl *definition = callee->getDefinition();
    FunctionDecl *callDecl = callee;
    if (definition)
        callee = definition;
    SourceLocation calleeLoc = callee->getLocation();
    bool calleeInMainFile = calleeLoc.isValid() &&
                            !calleeLoc.isMacroID() &&
                            SM.isWrittenInMainFile(SM.getSpellingLoc(calleeLoc));
    if (!definition || !callee->hasBody() || !calleeInMainFile)
    {
        handleExternalAbiBoundaryCall(call, callDecl, astContext, funcName);
        return true;
    }
    if (call->getNumArgs() != callee->getNumParams())
        return true;

    const bool fixedBoundary =
        fixedBoundaryFunctions.count(callee->getNameAsString()) != 0;
    vector<string> desiredParamTypes;
    desiredParamTypes.reserve(call->getNumArgs());
    bool mismatch = false;
    bool returnTypeChanged = false;
    bool forceClone = false;
    string calleeFileName = getFileName(astContext, callee);
    if (!calleeFileName.empty())
    {
        int calleeLine = getLineNumber(astContext, callee->getLocation());
        string oldReturn = callee->getReturnType().getAsString();
        string newReturn = getNewReturnType(callee->getNameAsString(), calleeFileName, oldReturn, calleeLine);
        if (!newReturn.empty() &&
            normalizeTypeForSignature(normalizeNewType(oldReturn, newReturn)) != normalizeTypeForSignature(oldReturn))
        {
            returnTypeChanged = true;
        }
    }
    for (unsigned i = 0; i < call->getNumArgs(); ++i)
    {
        Expr *arg = call->getArg(i);
        string actualType = getExprTypeAfterTransform(arg, astContext, funcName);
        actualType = refineArrayMemberArgumentType(arg, actualType, astContext, funcName);
        string desiredType = getConfiguredParamTypeForCallee(callee, i, astContext);
        string paramType = callee->getParamDecl(i)->getType().getAsString();
        bool hadConfiguredDesiredType = !desiredType.empty();
        // Only preserve a callee-side parameter rule when it actually changes
        // the original signature. Unchanged JSON entries should not block
        // caller-driven cloning.
        if (desiredType.empty())
        {
            desiredType = paramType;
        }
        // If caller and callee disagree on the element type of a floating-point
        // pointer/array, preserving the callee-side type would force an unsafe
        // reinterpret-cast at the call edge. In that case prefer the caller's
        // actual storage type and force a cloned function with a compatible
        // signature. For a fixed boundary this is the only permitted source of
        // an interface change: its parameter is derived from core storage and
        // is never an independent precision decision.
        // A scalar passed by address is storage just as much as an array or
        // pointer expression.  Its pointee type must be derived from the
        // caller-side declaration; otherwise an independently configured
        // callee parameter can produce (for example) float * while the
        // actual object is still double.
        if ((isCallerStorageArgument(arg) || isAddressOfNonArrayScalar(arg)) &&
            shouldPreferCallerStorageTypeAtCallEdge(actualType, desiredType))
        {
            if (hadConfiguredDesiredType || fixedBoundary)
                forceClone = true;
            // Plan A: 聚合形参保留原有指针/数组结构, 只换浮点基类型(避免多维数组衰变成单指针)
            if (isAggregateStorageType(paramType))
                desiredType = rebaseFloatingStorageType(paramType, getFloatingStorageBaseType(actualType));
            else
                desiredType = actualType;
        }
        string interfaceDesiredType = getAbiCompatibleInterfaceType(desiredType);
        if (needsParamAbiShim(desiredType, interfaceDesiredType))
            forceClone = true;
        desiredParamTypes.push_back(interfaceDesiredType);
        if (normalizeTypeForSignature(paramType) != normalizeTypeForSignature(interfaceDesiredType))
            mismatch = true;
    }
    if (!mismatch && !returnTypeChanged && !forceClone)
        return true;

    bool isCMainFile = false;
    if (auto mainFile = SM.getFileEntryRefForID(SM.getMainFileID()))
    {
        string mainName = mainFile->getName().str();
        if (mainName.size() >= 2 && mainName.rfind(".c") == mainName.size() - 2)
            isCMainFile = true;
    }

    if (astContext->getLangOpts().CPlusPlus && !isCMainFile)
    {
        if (isa<CXXMethodDecl>(callee))
            return true;
        if (hasMatchingOverload(callee, desiredParamTypes, astContext))
            return true;
        string key = buildOverloadKey(callee, desiredParamTypes, astContext);
        if (containsInsertedOverloadKey(key))
            return true;
        string wrapper = buildWrapperFunction(callee, desiredParamTypes);
        if (wrapper.empty())
            return true;
        insertFunctionPrototype(callee, callee->getNameAsString(), desiredParamTypes, astContext);
        SourceLocation insertLoc = SM.getSpellingLoc(callee->getBeginLoc());
        if (!insertLoc.isValid())
            return true;
        rewriter.InsertText(insertLoc, wrapper, true, true);
        addInsertedOverloadKey(key);
        return true;
    }

    string cloneSignatureKey = buildCloneSignatureKey(callee, desiredParamTypes, astContext);
    string cachedClone = findClonedFunctionName(cloneSignatureKey);
    if (!cachedClone.empty())
    {
        rewriteCallCalleeName(call, callee, cachedClone, astContext);
        return true;
    }

    string wrapperName = pickClonedFunctionName(callee, desiredParamTypes, astContext);
    if (wrapperName.empty() || callee->getNameAsString() == wrapperName)
        return true;
    if (hasMatchingFunctionByName(callee, wrapperName, desiredParamTypes, astContext))
    {
        rememberClonedFunctionName(cloneSignatureKey, wrapperName);
        rewriteCallCalleeName(call, callee, wrapperName, astContext);
        return true;
    }
    string wrapperKey = buildWrapperKeyByName(callee, wrapperName, desiredParamTypes, astContext);
    if (containsInsertedOverloadKey(wrapperKey))
    {
        rememberClonedFunctionName(cloneSignatureKey, wrapperName);
        rewriteCallCalleeName(call, callee, wrapperName, astContext);
        return true;
    }
    rememberClonedFunctionName(cloneSignatureKey, wrapperName);
    addInsertedFunctionName(buildFunctionNameKey(callee, wrapperName, astContext));
    string wrapper = buildClonedFunctionWithName(callee, wrapperName, desiredParamTypes, astContext);
    if (wrapper.empty())
    {
        forgetClonedFunctionName(cloneSignatureKey);
        return true;
    }
    insertFunctionPrototype(callee, wrapperName, desiredParamTypes, astContext);
    // Defer generated C clone definitions until final output assembly.  A
    // clone used to be inserted at the original definition's closing token;
    // removing an unreferenced static original could then consume the clone
    // in the same Rewriter range.
    pendingFunctionDefinitions.push_back(wrapper);
    addInsertedOverloadKey(wrapperKey);
    addInsertedFunctionName(buildFunctionNameKey(callee, wrapperName, astContext));
    rememberClonedFunctionName(cloneSignatureKey, wrapperName);

    rewriteCallCalleeName(call, callee, wrapperName, astContext);

    return true;
}

bool FuncStmtVisitor::VisitBinaryOperator(BinaryOperator *op)
{
    if (callOnly)
        return true;
    if (libcall_flag == 0)
        return true;
    if (!op)
        return true;
    rewriteLocalFloatingLiteralPrecision(op, astContext, funcName);
    if (!op->isAssignmentOp())
        return true;
    SourceLocation opLoc = op->getExprLoc();
    if (!shouldRewriteLoc(astContext, opLoc))
        return true;

    Expr *lhs = op->getLHS();
    if (!lhs)
        return true;
    auto *var = getBaseVarDeclFromStorageExpr(lhs);
    if (!var)
        return true;

    CharSourceRange lhsRange = CharSourceRange::getTokenRange(lhs->getSourceRange());
    string lhsText = getSourceTextPreferRewritten(lhsRange, astContext);
    int derefLevels = countArraySubscriptDepth(lhs);
    string lhsOldType = lhs->getType().getAsString();
    rewriteAllocatorAssignmentForVar(op, var, astContext, funcName, lhsText, derefLevels, lhsOldType);
    return true;
}

bool FuncVisitor::VisitFunctionDecl(FunctionDecl *func)
{
    SourceLocation loc = func->getLocation();
    if (!shouldRewriteLoc(astContext, loc))
        return true;

    funcName = func->getNameInfo().getName().getAsString();
    const bool fixedBoundary =
        fixedBoundaryFunctions.count(funcName) != 0;
    bool hasBody = func->doesThisDeclarationHaveABody();
    bool preserveOriginalSignature = hasBody && !func->isMain() && !STRICT_CONFIG && TRANSFORM_PHASE == PHASE_ALL;
    bool signatureNeedsClone = false;
    bool paramTypesChanged = false;
    string originalFunctionText;
    CharSourceRange functionRange;
    if (preserveOriginalSignature)
    {
        functionRange = CharSourceRange::getTokenRange(func->getSourceRange());
        originalFunctionText = getSourceTextRaw(functionRange, astContext);
        string fileName = getFileName(astContext, func);
        if (!fileName.empty())
        {
            int line = hasBody ? getLineNumber(astContext, func->getLocation()) : -1;
            string returnType = func->getReturnType().getAsString();
            string newReturnType = getNewReturnType(funcName, fileName, returnType, line);
            newReturnType = normalizeNewType(returnType, newReturnType);
            if (!newReturnType.empty() &&
                normalizeTypeForCompare(newReturnType) != normalizeTypeForCompare(returnType))
            {
                signatureNeedsClone = true;
            }
            for (ParmVarDecl *param : func->parameters())
            {
                string valueName = param->getNameAsString();
                string typeName = param->getOriginalType().getAsString();
                string paramFileName = getFileName(astContext, param);
                if (paramFileName.empty())
                    continue;
                int paramLine = getLineNumber(astContext, param->getLocation());
                string newTypeName = getNewType(valueName, typeName, funcName, paramFileName, "parmVar", paramLine);
                newTypeName = stripTrailingParamName(normalizeNewType(typeName, newTypeName), valueName);
                if (!newTypeName.empty() &&
                    normalizeTypeForCompare(newTypeName) != normalizeTypeForCompare(typeName))
                {
                    signatureNeedsClone = true;
                    break;
                }
            }
        }
    }

    if (!hasBody && !STRICT_CONFIG)
        return true;

    if (!fixedBoundary && !preserveOriginalSignature &&
        TRANSFORM_PHASE != PHASE_CLONE_ONLY)
    {
        string fileName = getFileName(astContext, func);
        if (!fileName.empty())
        {
            int line = hasBody ? getLineNumber(astContext, func->getLocation()) : -1;
            string returnType = func->getReturnType().getAsString();
            string newReturnType = getNewReturnType(funcName, fileName, returnType, line);
            if (newReturnType.empty() && STRICT_CONFIG && !hasBody)
                newReturnType = getStrictHeaderReturnType(funcName, returnType);
            newReturnType = normalizeNewType(returnType, newReturnType);
            if (!newReturnType.empty())
            {
                SourceRange retRange = func->getReturnTypeSourceRange();
                if (retRange.isValid())
                {
                    CharSourceRange retCharRange = CharSourceRange::getTokenRange(retRange);
                    string replacement = ensureTrailingSpaceIfNeeded(getAbiCompatibleInterfaceType(newReturnType), retCharRange, astContext);
                    rewriter.ReplaceText(retCharRange, replacement);
                }
            }
        }
    }

    if (!fixedBoundary && !preserveOriginalSignature &&
        TRANSFORM_PHASE != PHASE_CLONE_ONLY)
    {
        for (ParmVarDecl *param : func->parameters())
        {
            string valueName = param->getNameAsString();
            string typeName = param->getOriginalType().getAsString();
            string fileName = getFileName(astContext, param);
            if (fileName.empty())
                continue;
            int line = hasBody ? getLineNumber(astContext, param->getLocation()) : -1;
            string newTypeName = getNewType(valueName, typeName, funcName, fileName, "parmVar", line);
            if (newTypeName.empty() && STRICT_CONFIG && !hasBody)
                newTypeName = getStrictHeaderParamType(func, param->getFunctionScopeIndex(), valueName, typeName);
            newTypeName = stripTrailingParamName(normalizeNewType(typeName, newTypeName), valueName);
            if (newTypeName.empty())
                continue;
            string interfaceTypeName = getAbiCompatibleInterfaceType(newTypeName);
            TypeSourceInfo *TSI = param->getTypeSourceInfo();
            if (!TSI)
                continue;
            auto TL = TSI->getTypeLoc();
            CharSourceRange range;
            if (param->getType()->isArrayType())
            {
                TypeLoc baseLoc = getBaseTypeLoc(TL);
                if (baseLoc.getBeginLoc().isValid() && baseLoc.getEndLoc().isValid())
                {
                    string baseNewType = stripArraySuffix(newTypeName);
                    if (typeName.rfind("const ", 0) == 0)
                        baseNewType = stripLeadingConst(baseNewType);
                    range = CharSourceRange::getTokenRange(baseLoc.getBeginLoc(), baseLoc.getEndLoc());
                    string rangeText = getSourceTextPreferRewritten(range, astContext);
                    string interfaceBaseType = stripArraySuffix(interfaceTypeName);
                    if (typeName.rfind("const ", 0) == 0)
                        interfaceBaseType = stripLeadingConst(interfaceBaseType);
                    if (normalizeTypeForCompare(rangeText) == normalizeTypeForCompare(interfaceBaseType))
                        continue;
                    interfaceBaseType = ensureTrailingSpaceIfNeeded(interfaceBaseType, range, astContext);
                    rewriter.ReplaceText(range, interfaceBaseType);
                    continue;
                }
                SourceLocation tsBegin = param->getTypeSpecStartLoc();
                SourceLocation tsEnd = param->getTypeSpecEndLoc();
                if (tsBegin.isValid() && tsEnd.isValid())
                {
                    string baseNewType = stripArraySuffix(interfaceTypeName);
                    if (typeName.rfind("const ", 0) == 0)
                        baseNewType = stripLeadingConst(baseNewType);
                    range = CharSourceRange::getTokenRange(tsBegin, tsEnd);
                    string rangeText = getSourceTextPreferRewritten(range, astContext);
                    if (normalizeTypeForCompare(rangeText) == normalizeTypeForCompare(baseNewType))
                        continue;
                    baseNewType = ensureTrailingSpaceIfNeeded(baseNewType, range, astContext);
                    rewriter.ReplaceText(range, baseNewType);
                    continue;
                }
            }
            SourceLocation tsBegin = param->getTypeSpecStartLoc();
            SourceLocation tsEnd = param->getTypeSpecEndLoc();
            if (tsBegin.isValid() && tsEnd.isValid())
            {
                range = CharSourceRange::getTokenRange(tsBegin, tsEnd);
            }
            else
            {
                range = CharSourceRange::getTokenRange(TL.getBeginLoc(), TL.getEndLoc());
            }
            string rangeText = getSourceTextPreferRewritten(range, astContext);
            if (normalizeTypeForCompare(rangeText) == normalizeTypeForCompare(interfaceTypeName))
                continue;
            string replacement = ensureParamNameInReplacement(interfaceTypeName, valueName, rangeText);
            replacement = ensureTrailingSpaceIfNeeded(replacement, range, astContext);
            rewriter.ReplaceText(range, replacement);
            ensureSpaceBeforeIdentifier(param->getLocation(), astContext);
            paramTypesChanged = true;
        }
    }

    if (!hasBody)
        return true;

    // The types-only phase may tune a local pointer that merely aliases a
    // parameter (for example a ping-pong swap temporary) independently from
    // that parameter.  Keep the original function internally type-correct in
    // clone-only output; each generated clone re-derives the alias type from
    // its own desired parameter signature.
    if (TRANSFORM_PHASE == PHASE_CLONE_ONLY)
        restoreParameterAliasLocals(func, astContext);

    if (!fixedBoundary && !STRICT_CONFIG &&
        TRANSFORM_PHASE != PHASE_CLONE_ONLY)
    {
        libcall_flag = 0;
        for (ParmVarDecl *param : func->parameters())
        {
            const clang::Type *typeObj = param->getType().getTypePtrOrNull();
            if (typeObj)
            {
                FloatingPointTypeInfo info = DissectFloatingPointType(typeObj, true);
                if (info.isFloatingPoint)
                {
                    string valueName = param->getNameAsString();
                    string typeName = dyn_cast<clang::ParmVarDecl>(param)->getOriginalType().getAsString();
                    string fileName = getFileName(astContext, param);
                    if(fileName.empty()){
                        return true;
                    }
                    int line = getLineNumber(astContext, param->getLocation());
                    string newTypeName = getNewType(valueName, typeName, funcName, fileName, "parmVar", line);
                    //                if (1) {
                    if (newTypeName != "")
                    {
                        string internalParamType = stripTrailingParamName(normalizeNewType(typeName, newTypeName), valueName);
                        string interfaceParamType = getAbiCompatibleInterfaceType(internalParamType);
                        bool paramAbiShimEnabled = needsParamAbiShim(internalParamType, interfaceParamType);
                        if (!paramAbiShimEnabled && (paramTypesChanged || preserveOriginalSignature))
                            continue;
                        current_ParamName = valueName;
                        current_ParamType = typeName;
                        current_InternalParamType = internalParamType;
                        current_ParamShimName = buildShimVariableName(valueName, internalParamType);

                        SourceLocation ST = func->getBody()->getSourceRange().getBegin().getLocWithOffset(1);
                        string tobeinsert = "";
                        if (current_ParamType.back() == ']' || current_ParamType.back() == '*')
                        {
                            int count1 = countSubString(current_ParamType, "]");
                            int count2 = countSubString(current_ParamType, "*");
                            int ptrLevel = (count2 > 0) ? count2 : count1;
                            string baseNewType = stripPointerSuffix(stripArraySuffix(current_InternalParamType));
                            baseNewType = stripLeadingConst(baseNewType);
                            string stars(ptrLevel, '*');
                            tobeinsert = "\n  " + baseNewType + " " + stars + " " + current_ParamShimName +
                                         " = (" + baseNewType + " " + stars + ")" + current_ParamName + ";\n";
                        }
                        else
                        {
                            tobeinsert = "\n  " + current_InternalParamType + " " + current_ParamShimName +
                                         " = " + current_ParamName + ";\n";
                        }
                        rewriter.InsertText(ST, tobeinsert, true, true);

                        st_visitor->TraverseStmt(func->getBody());
                    }
                }
            }
        }
    }
    libcall_flag = 1;
    // Clone-rewrite must also see call sites in main(); otherwise a mixed
    // object allocated in main can still call the original function
    // signature and leave argument/parameter types inconsistent.
    // An excluded boundary body is deliberately opaque to precision
    // rewriting. Its compatible clone is created from call-site storage
    // types, but its local declarations and arithmetic remain as written.
    // Excluding a function removes its locals and arithmetic from the
    // precision search; it does not freeze calls from that function into the
    // transformed core.  Always rewrite call edges so their signatures follow
    // the actual transformed storage.  In a fixed boundary, suppress every
    // other statement rewrite through call-only mode.
    st_visitor->setCallOnly(fixedBoundary);
    st_visitor->TraverseStmt(func->getBody());
    st_visitor->setCallOnly(false);
    if (!fixedBoundary && TRANSFORM_PHASE != PHASE_CLONE_ONLY)
        vars_visitor->TraverseStmt(func->getBody());
    current_ParamName.clear();
    current_ParamType.clear();
    current_InternalParamType.clear();
    current_ParamShimName.clear();

    if (preserveOriginalSignature && signatureNeedsClone && !originalFunctionText.empty())
    {
        CharSourceRange bodyRange = CharSourceRange::getTokenRange(func->getBody()->getSourceRange());
        string functionKey = buildFunctionNameKey(func, funcName, astContext);
        string bodyKey = buildFunctionNameKey(func, funcName, astContext) + ":body";
        string rewrittenFunctionText = rewriter.getRewrittenText(functionRange.getAsRange());
        string rewrittenBody = getSourceTextPreferRewritten(bodyRange, astContext);
        if (!rewrittenFunctionText.empty())
            rememberPreservedText(preservedFunctionTexts, functionKey, rewrittenFunctionText);
        if (!rewrittenBody.empty())
            rememberPreservedText(preservedFunctionBodies, bodyKey, rewrittenBody);
        rewriter.ReplaceText(functionRange, originalFunctionText);
    }

    return true;
}

static void cleanupUnreferencedStaticCloneSources(ASTContext &Context)
{
    if (!CALLER_DRIVEN_CLONE || TRANSFORM_PHASE == PHASE_TYPES_ONLY)
        return;

    SourceManager &SM = Context.getSourceManager();
    unordered_set<const FunctionDecl *> cloneSources;
    for (Decl *decl : Context.getTranslationUnitDecl()->decls())
    {
        auto *func = dyn_cast<FunctionDecl>(decl);
        if (!func || !func->doesThisDeclarationHaveABody() ||
            func->isMain() || func->getStorageClass() != SC_Static)
            continue;
        SourceLocation loc = SM.getSpellingLoc(func->getLocation());
        if (!loc.isValid() || loc.isMacroID() ||
            !SM.isWrittenInMainFile(loc))
            continue;

        const FunctionDecl *canonical = canonicalFunctionDecl(func);
        unsigned refs = originalFunctionRefCounts[canonical];
        unsigned calls = originalFunctionCallCounts[canonical];
        unsigned rewritten = rewrittenOriginalCallCounts[canonical];

        // refs == calls proves that the function address did not escape and
        // there are no non-call references. rewritten == calls proves that no
        // direct source call still targets the original static definition.
        if (calls > 0 && refs == calls && rewritten == calls)
            cloneSources.insert(canonical);
    }

    unordered_set<unsigned> annotatedLocations;
    for (const FunctionDecl *canonical : cloneSources)
    {
        for (const FunctionDecl *redecl : canonical->redecls())
        {
            SourceLocation begin = SM.getSpellingLoc(redecl->getBeginLoc());
            if (!begin.isValid() || begin.isMacroID() ||
                !SM.isWrittenInMainFile(begin))
                continue;
            unsigned raw = begin.getRawEncoding();
            if (!annotatedLocations.insert(raw).second)
                continue;
            // Do not delete the source definition.  A rewritten call count
            // does not prove that every retained original function has
            // stopped calling it: cloned definitions are assembled from
            // rewritten text while original definitions may intentionally
            // remain as dependency roots.  Removing a transitively referenced
            // source function therefore creates implicit/undeclared calls.
            // Marking the original definition unused keeps that dependency
            // graph valid and still makes strict -Wunused-function builds
            // clean.
            if (!redecl->doesThisDeclarationHaveABody())
                continue;
            SourceLocation insertLoc = SM.getSpellingLoc(redecl->getBeginLoc());
            if (!insertLoc.isValid())
                continue;
            rewriter.InsertText(insertLoc, "__attribute__((unused)) ",
                                false, true);
            PRINT_DEBUG_MESSAGE(
                "Marked cloned static original unused: "
                << redecl->getNameAsString());
        }
    }
}

void TransTypeConsumer::HandleTranslationUnit(ASTContext &Context)
{
    replacedTypeLocs.clear();
    insertedOverloadKeys.clear();
    externalAbiProtectedVarKeys.clear();
    rewrittenCallNameLocs.clear();
    insertedFunctionNames.clear();
    insertedFunctionDecls.clear();
    insertedFunctionPrototypeTexts.clear();
    pendingFunctionPrototypes.clear();
    pendingExternalAbiPrototypes.clear();
    pendingFunctionDefinitions.clear();
    clonedFunctionNameBySignature.clear();
    externalAbiWrapperNameBySignature.clear();
    preservedFunctionBodies.clear();
    preservedFunctionTexts.clear();
    rewrittenParamRefLocs.clear();
    rewrittenGlobalDeclStmtKeys.clear();
    rewrittenFloatingLiteralLocs.clear();
    originalFunctionRefCounts.clear();
    originalFunctionCallCounts.clear();
    rewrittenOriginalCallCounts.clear();
    prototypeInsertLocReady = false;
    basePrototypeInsertOffset = 0;
    externalAbiInsertLocReady = false;
    externalAbiInsertOffset = 0;
    FileID id = rewriter.getSourceMgr().getMainFileID();
    string basefilename = basename(rewriter.getSourceMgr().getFilename(rewriter.getSourceMgr().getLocForStartOfFile(id)).str());
    if(basefilename.empty()){
        return ;
    }
    // Normalize output path to end with '/'
    if (!OUTPUT_PATH.empty() && OUTPUT_PATH.back() != '/')
    {
        OUTPUT_PATH += "/";
    }
    string configBase = stripExtension(basename(INPUT_CONFIG));
    string srcExt = "";
    size_t dot = basefilename.rfind('.');
    if (dot != string::npos)
    {
        srcExt = basefilename.substr(dot);
    }
    else
    {
        srcExt = ".c";
    }
    string filename = OUTPUT_PATH + configBase + srcExt;
    //    PRINT_DEBUG_MESSAGE(filename);
    std::ifstream ifs(INPUT_CONFIG);
    jf = nlohmann::json::parse(ifs, nullptr, false);
    if (jf.is_discarded())
    {
        PRINT_DEBUG_MESSAGE("Failed to parse config json.");
        return;
    }
    loadRules();
    if (!STRICT_CONFIG)
        collectExternalAbiProtectedVars(Context);

    // Capture usage from the untouched AST before any call-site rewrite.
    OriginalFunctionUsageVisitor usageVisitor;
    usageVisitor.TraverseDecl(Context.getTranslationUnitDecl());

    // Struct/record field rewriting is opt-in because some historical inputs crashed.
    if (TRANSFORM_PHASE != PHASE_CLONE_ONLY && ENABLE_FIELD_CHANGE && !fieldRules.empty())
    {
        field_visitor->TraverseDecl(Context.getTranslationUnitDecl());
    }
    if (TRANSFORM_PHASE != PHASE_CLONE_ONLY)
        glob_visitor->TraverseDecl(Context.getTranslationUnitDecl());
    func_visitor->TraverseDecl(Context.getTranslationUnitDecl());
    cleanupUnreferencedStaticCloneSources(Context);

    // Create an output file to write the updated code
    std::error_code OutErrorInfo;
    std::error_code ok;
    const RewriteBuffer *RewriteBuf = rewriter.getRewriteBufferFor(id);
    const SourceManager &SM = rewriter.getSourceMgr();
    llvm::StringRef original = SM.getBufferData(id);
    bool hasChanges = false;
    bool hasRelevantRules = hasRelevantRulesForFile(basefilename);
    auto applyPendingFunctions = [&](std::string text) -> std::string {
        if (pendingFunctionPrototypes.empty() &&
            pendingExternalAbiPrototypes.empty() &&
            pendingFunctionDefinitions.empty())
            return text;
        if (!pendingFunctionPrototypes.empty())
        {
            auto findTopLevelCalleeItem = [](const string &source,
                                             const string &calleeName) -> size_t {
                size_t itemStart = 0;
                unsigned braceDepth = 0;
                bool inString = false;
                bool inChar = false;
                bool inLineComment = false;
                bool inBlockComment = false;
                bool escape = false;
                bool atLineStart = true;
                for (size_t i = 0; i < source.size(); ++i)
                {
                    char c = source[i];
                    char next = i + 1 < source.size() ? source[i + 1] : '\0';
                    if (inLineComment)
                    {
                        if (c == '\n')
                        {
                            inLineComment = false;
                            atLineStart = true;
                        }
                        continue;
                    }
                    if (inBlockComment)
                    {
                        if (c == '*' && next == '/')
                        {
                            inBlockComment = false;
                            ++i;
                        }
                        continue;
                    }
                    if (escape)
                    {
                        escape = false;
                        continue;
                    }
                    if ((inString || inChar) && c == '\\')
                    {
                        escape = true;
                        continue;
                    }
                    if (!inChar && c == '"')
                    {
                        inString = !inString;
                        continue;
                    }
                    if (!inString && c == '\'')
                    {
                        inChar = !inChar;
                        continue;
                    }
                    if (inString || inChar)
                        continue;
                    if (c == '/' && next == '/')
                    {
                        inLineComment = true;
                        ++i;
                        continue;
                    }
                    if (c == '/' && next == '*')
                    {
                        inBlockComment = true;
                        ++i;
                        continue;
                    }
                    if (atLineStart && (c == ' ' || c == '\t' || c == '\r'))
                        continue;
                    if (atLineStart && c == '#')
                    {
                        size_t newline = source.find('\n', i);
                        if (newline == string::npos)
                            return string::npos;
                        i = newline;
                        itemStart = i + 1;
                        atLineStart = true;
                        continue;
                    }
                    atLineStart = c == '\n';
                    if (c == '{')
                    {
                        ++braceDepth;
                        continue;
                    }
                    if (c == '}')
                    {
                        if (braceDepth > 0)
                            --braceDepth;
                        if (braceDepth == 0)
                            itemStart = i + 1;
                        continue;
                    }
                    if (braceDepth == 0 && c == ';')
                    {
                        itemStart = i + 1;
                        continue;
                    }
                    if (braceDepth != 0 ||
                        i + calleeName.size() > source.size() ||
                        source.compare(i, calleeName.size(), calleeName) != 0)
                        continue;
                    bool leftBoundary = i == 0 ||
                        !(isalnum(static_cast<unsigned char>(source[i - 1])) ||
                          source[i - 1] == '_');
                    size_t after = i + calleeName.size();
                    bool rightBoundary = after >= source.size() ||
                        !(isalnum(static_cast<unsigned char>(source[after])) ||
                          source[after] == '_');
                    if (!leftBoundary || !rightBoundary)
                        continue;
                    while (after < source.size() &&
                           isspace(static_cast<unsigned char>(source[after])))
                        ++after;
                    if (after < source.size() && source[after] == '(')
                        return itemStart;
                }
                return string::npos;
            };
            vector<pair<size_t, string>> positionedPrototypes;
            positionedPrototypes.reserve(pendingFunctionPrototypes.size());
            for (const auto &entry : pendingFunctionPrototypes)
            {
                size_t offset = findTopLevelCalleeItem(text, entry.calleeName);
                if (offset == string::npos)
                {
                    offset = static_cast<size_t>(entry.sourceOffset);
                    if (entry.sourceOffset > 0)
                    {
                        SourceLocation start = SM.getLocForStartOfFile(id);
                        SourceLocation end = start.getLocWithOffset(entry.sourceOffset);
                        if (start.isValid() && end.isValid())
                        {
                            int rewrittenPrefixSize = rewriter.getRangeSize(
                                CharSourceRange::getCharRange(start, end));
                            if (rewrittenPrefixSize >= 0)
                                offset = static_cast<size_t>(rewrittenPrefixSize);
                        }
                    }
                }
                positionedPrototypes.emplace_back(
                    std::min(offset, text.size()), "\n" + entry.text + "\n");
            }
            // Insert from the end of the file toward the beginning so earlier
            // insertions cannot invalidate the remaining rewritten offsets.
            stable_sort(positionedPrototypes.begin(), positionedPrototypes.end(),
                        [](const auto &lhs, const auto &rhs) {
                            return lhs.first > rhs.first;
                        });
            for (const auto &entry : positionedPrototypes)
                text.insert(entry.first, entry.second);
        }
        std::string externalAbiPrototypes;
        for (const string &proto : pendingExternalAbiPrototypes)
            externalAbiPrototypes += proto + "\n";
        if (!externalAbiPrototypes.empty())
        {
            externalAbiPrototypes += "\n";
            size_t offset = static_cast<size_t>(externalAbiInsertOffset);
            if (externalAbiInsertOffset > 0)
            {
                SourceLocation start = SM.getLocForStartOfFile(id);
                SourceLocation end = start.getLocWithOffset(externalAbiInsertOffset);
                if (start.isValid() && end.isValid())
                {
                    int rewrittenPrefixSize = rewriter.getRangeSize(
                        CharSourceRange::getCharRange(start, end));
                    if (rewrittenPrefixSize >= 0)
                        offset = static_cast<size_t>(rewrittenPrefixSize);
                }
            }
            offset = std::min(offset, text.size());
            text.insert(offset, externalAbiPrototypes);
        }
        for (const string &definition : pendingFunctionDefinitions)
            text += "\n\n" + definition + "\n";
        return text;
    };
    if (RewriteBuf)
    {
        std::string rewritten(RewriteBuf->begin(), RewriteBuf->end());
        rewritten = applyPendingFunctions(rewritten);
        hasChanges = (rewritten != original);
    }
    if (!ENABLE_CHANGE && !hasChanges && !hasRelevantRules)
    {
        PRINT_DEBUG_MESSAGE("No changes detected; skipping output.");
        return;
    }
    if (RewriteBuf)
    {
        llvm::raw_fd_ostream outFile(llvm::StringRef(filename),
                                     OutErrorInfo, llvm::sys::fs::OF_None);
        if (OutErrorInfo == ok)
        {
            std::string rewritten(RewriteBuf->begin(), RewriteBuf->end());
            outFile << applyPendingFunctions(rewritten);
            PRINT_DEBUG_MESSAGE("Output file created - " << filename);
        }
        else
        {
            PRINT_DEBUG_MESSAGE("Could not create file - " << filename);
        }
    }
    else
    {
        llvm::raw_fd_ostream outFile(llvm::StringRef(filename),
                                     OutErrorInfo, llvm::sys::fs::OF_None);
        if (OutErrorInfo == ok)
        {
            outFile << applyPendingFunctions(original.str());
            PRINT_DEBUG_MESSAGE("Output file created (no rewrite buffer) - " << filename);
        }
        else
        {
            PRINT_DEBUG_MESSAGE("Could not create file - " << filename);
        }
    }

    if (STRICT_CONFIG)
    {
        for (auto it = rewriter.buffer_begin(); it != rewriter.buffer_end(); ++it)
        {
            FileID fid = it->first;
            if (fid == id)
                continue;
            auto entry = SM.getFileEntryRefForID(fid);
            if (!entry)
                continue;
            string base = basename(entry->getName().str());
            if (base.empty() || !hasRelevantRulesForFile(base))
            {
                bool isHeader = base.size() >= 2 &&
                                (base.rfind(".h") == base.size() - 2 ||
                                 base.rfind(".hpp") == base.size() - 4 ||
                                 base.rfind(".hh") == base.size() - 3);
                if (!isHeader)
                    continue;
            }
            string outName = OUTPUT_PATH + base;
            std::error_code headerError;
            llvm::raw_fd_ostream headerOut(llvm::StringRef(outName),
                                           headerError, llvm::sys::fs::OF_None);
            if (headerError == ok)
            {
                const RewriteBuffer &buf = it->second;
                headerOut << std::string(buf.begin(), buf.end());
                PRINT_DEBUG_MESSAGE("Strict config output file created - " << outName);
            }
            else
            {
                PRINT_DEBUG_MESSAGE("Could not create strict config output file - " << outName);
            }
        }
    }
}

unique_ptr<ASTConsumer> TransTypeAction::CreateASTConsumer(CompilerInstance &CI, StringRef file)
{
    return make_unique<TransTypeConsumer>(&CI);
}

bool TransTypeAction::ParseArgs(const clang::CompilerInstance &CI,
                                const std::vector<std::string> &args)
{
    // To be written...
    llvm::errs() << "Plugin arg size = " << args.size() << "\n";
    for (unsigned i = 0, e = args.size(); i != e; ++i)
    {
        // Usage: --enable-change=0|1|true|false (default: disabled; when enabled, output even if no changes)
        if (args[i].rfind("--enable-change=", 0) == 0)
        {
            string value = args[i].substr(strlen("--enable-change="));
            bool parsed = false;
            if (parseBoolFlagValue(value, parsed))
                ENABLE_CHANGE = parsed;
            else
                PRINT_DEBUG_MESSAGE("Invalid --enable-change value: " << value);
            llvm::errs() << "Enable change = " << (ENABLE_CHANGE ? "true" : "false") << "\n";
            continue;
        }
        // Usage: --enable-field-change=0|1|true|false (default: disabled)
        if (args[i].rfind("--enable-field-change=", 0) == 0)
        {
            string value = args[i].substr(strlen("--enable-field-change="));
            bool parsed = false;
            if (parseBoolFlagValue(value, parsed))
                ENABLE_FIELD_CHANGE = parsed;
            else
                PRINT_DEBUG_MESSAGE("Invalid --enable-field-change value: " << value);
            llvm::errs() << "Enable field change = " << (ENABLE_FIELD_CHANGE ? "true" : "false") << "\n";
            continue;
        }
        if (args[i].rfind("--x86-fp16-abi-compat=", 0) == 0)
        {
            string value = args[i].substr(strlen("--x86-fp16-abi-compat="));
            bool parsed = false;
            if (parseBoolFlagValue(value, parsed))
                ENABLE_X86_FP16_ABI_COMPAT = parsed;
            else
                PRINT_DEBUG_MESSAGE("Invalid --x86-fp16-abi-compat value: " << value);
            llvm::errs() << "Enable x86 fp16 ABI compat = " << (ENABLE_X86_FP16_ABI_COMPAT ? "true" : "false") << "\n";
            continue;
        }
        if (args[i].rfind("--strict-config=", 0) == 0)
        {
            string value = args[i].substr(strlen("--strict-config="));
            bool parsed = false;
            if (parseBoolFlagValue(value, parsed))
                STRICT_CONFIG = parsed;
            else
                PRINT_DEBUG_MESSAGE("Invalid --strict-config value: " << value);
            llvm::errs() << "Strict config = " << (STRICT_CONFIG ? "true" : "false") << "\n";
            continue;
        }
        if (args[i].rfind("--caller-driven-clone=", 0) == 0)
        {
            string value = args[i].substr(strlen("--caller-driven-clone="));
            bool parsed = false;
            if (parseBoolFlagValue(value, parsed))
                CALLER_DRIVEN_CLONE = parsed;
            else
                PRINT_DEBUG_MESSAGE("Invalid --caller-driven-clone value: " << value);
            llvm::errs() << "Caller-driven clone = " << (CALLER_DRIVEN_CLONE ? "true" : "false") << "\n";
            continue;
        }
        if (args[i].rfind("--transform-phase=", 0) == 0)
        {
            string value = args[i].substr(strlen("--transform-phase="));
            if (value == "all")
                TRANSFORM_PHASE = PHASE_ALL;
            else if (value == "types-only")
                TRANSFORM_PHASE = PHASE_TYPES_ONLY;
            else if (value == "clone-only")
                TRANSFORM_PHASE = PHASE_CLONE_ONLY;
            else
                PRINT_DEBUG_MESSAGE("Invalid --transform-phase value: " << value);
            llvm::errs() << "Transform phase = " << value << "\n";
            continue;
        }
        if (args[i] == "--enable-change" || args[i] == "-enable-change")
        {
            if (i + 1 < e)
            {
                bool parsed = false;
                if (parseBoolFlagValue(args[i + 1], parsed))
                    ENABLE_CHANGE = parsed;
                else
                    PRINT_DEBUG_MESSAGE("Invalid --enable-change value: " << args[i + 1]);
                llvm::errs() << "Enable change = " << (ENABLE_CHANGE ? "true" : "false") << "\n";
            }
            else
            {
                PRINT_DEBUG_MESSAGE("Missing value for --enable-change");
            }
            continue;
        }
        if (args[i] == "--enable-field-change" || args[i] == "-enable-field-change")
        {
            if (i + 1 < e)
            {
                bool parsed = false;
                if (parseBoolFlagValue(args[i + 1], parsed))
                    ENABLE_FIELD_CHANGE = parsed;
                else
                    PRINT_DEBUG_MESSAGE("Invalid --enable-field-change value: " << args[i + 1]);
                llvm::errs() << "Enable field change = " << (ENABLE_FIELD_CHANGE ? "true" : "false") << "\n";
            }
            else
            {
                PRINT_DEBUG_MESSAGE("Missing value for --enable-field-change");
            }
            continue;
        }
        if (args[i] == "--x86-fp16-abi-compat" || args[i] == "-x86-fp16-abi-compat")
        {
            if (i + 1 < e)
            {
                bool parsed = false;
                if (parseBoolFlagValue(args[i + 1], parsed))
                    ENABLE_X86_FP16_ABI_COMPAT = parsed;
                else
                    PRINT_DEBUG_MESSAGE("Invalid --x86-fp16-abi-compat value: " << args[i + 1]);
                llvm::errs() << "Enable x86 fp16 ABI compat = " << (ENABLE_X86_FP16_ABI_COMPAT ? "true" : "false") << "\n";
            }
            else
            {
                PRINT_DEBUG_MESSAGE("Missing value for --x86-fp16-abi-compat");
            }
            continue;
        }
        if (args[i] == "--strict-config" || args[i] == "-strict-config")
        {
            if (i + 1 < e)
            {
                bool parsed = false;
                if (parseBoolFlagValue(args[i + 1], parsed))
                    STRICT_CONFIG = parsed;
                else
                    PRINT_DEBUG_MESSAGE("Invalid --strict-config value: " << args[i + 1]);
                llvm::errs() << "Strict config = " << (STRICT_CONFIG ? "true" : "false") << "\n";
            }
            else
            {
                PRINT_DEBUG_MESSAGE("Missing value for --strict-config");
            }
            continue;
        }
        if (args[i] == "--caller-driven-clone" || args[i] == "-caller-driven-clone")
        {
            if (i + 1 < e)
            {
                bool parsed = false;
                if (parseBoolFlagValue(args[i + 1], parsed))
                    CALLER_DRIVEN_CLONE = parsed;
                else
                    PRINT_DEBUG_MESSAGE("Invalid --caller-driven-clone value: " << args[i + 1]);
                llvm::errs() << "Caller-driven clone = " << (CALLER_DRIVEN_CLONE ? "true" : "false") << "\n";
            }
            else
            {
                PRINT_DEBUG_MESSAGE("Missing value for --caller-driven-clone");
            }
            continue;
        }
        if (args[i] == "--transform-phase" || args[i] == "-transform-phase")
        {
            if (i + 1 < e)
            {
                string value = args[i + 1];
                if (value == "all")
                    TRANSFORM_PHASE = PHASE_ALL;
                else if (value == "types-only")
                    TRANSFORM_PHASE = PHASE_TYPES_ONLY;
                else if (value == "clone-only")
                    TRANSFORM_PHASE = PHASE_CLONE_ONLY;
                else
                    PRINT_DEBUG_MESSAGE("Invalid --transform-phase value: " << value);
                llvm::errs() << "Transform phase = " << value << "\n";
            }
            else
            {
                PRINT_DEBUG_MESSAGE("Missing value for --transform-phase");
            }
            continue;
        }
        if (args[i] == "-output-path")
        {
            if (i + 1 < e)
                OUTPUT_PATH = args[i + 1]; // ./temp-NPB3.3-SER-C/CG/
            else
                PRINT_DEBUG_MESSAGE("Missing output path! Could not generate output file.");
            llvm::errs() << "Output path = " << OUTPUT_PATH << "\n";
        }
        if (args[i] == "-input-config")
        {
            if (i + 1 < e)
                INPUT_CONFIG = args[i + 1]; // config_temp.json
            else
                PRINT_DEBUG_MESSAGE("Missing input config! Could not generate output file.");
            llvm::errs() << "Input config  = " << INPUT_CONFIG << "\n";
        }
    }
    return true;
}

static clang::FrontendPluginRegistry::Add<TransTypeAction>
    X("trans-type", "transform all floating point variables' type from double to float");
