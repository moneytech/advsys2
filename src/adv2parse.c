/* adv2parse.c - parser
 *
 * Copyright (c) 2018 by David Michael Betz.  All rights reserved.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "adv2compiler.h"
#include "adv2debug.h"
#include "adv2vm.h"
#include "adv2vmdebug.h"

/* local function prototypes */
static void ParseInclude(ParseContext *c);
static void ParseDef(ParseContext *c);
static void ParseConstantDef(ParseContext *c, char *name);
static void ParseFunctionDef(ParseContext *c, char *name);
static void ParseVar(ParseContext *c);
static void ParseObject(ParseContext *c, char *name);
static void ParseProperty(ParseContext *c);
static ParseTreeNode *ParseFunction(ParseContext *c, char *name);
static ParseTreeNode *ParseMethod(ParseContext *c, char *name);
static ParseTreeNode *ParseFunctionBody(ParseContext *c, ParseTreeNode *node, int offset);
static void ParseWords(ParseContext *c, int type);
static ParseTreeNode *ParseIf(ParseContext *c);
static ParseTreeNode *ParseWhile(ParseContext *c);
static ParseTreeNode *ParseDoWhile(ParseContext *c);
static ParseTreeNode *ParseFor(ParseContext *c);
static ParseTreeNode *ParseBreak(ParseContext *c);
static ParseTreeNode *ParseContinue(ParseContext *c);
static ParseTreeNode *ParseReturn(ParseContext *c);
static ParseTreeNode *ParseBlock(ParseContext *c);
static ParseTreeNode *ParseTry(ParseContext *c);
static ParseTreeNode *ParseThrow(ParseContext *c);
static ParseTreeNode *ParseExprStatement(ParseContext *c);
static ParseTreeNode *ParseEmpty(ParseContext *c);
static ParseTreeNode *ParseAsm(ParseContext *c);
static ParseTreeNode *ParsePrint(ParseContext *c, int newline);
static VMVALUE ParseIntegerLiteralExpr(ParseContext *c);
static VMVALUE ParseConstantLiteralExpr(ParseContext *c, FixupType fixupType, VMVALUE offset);
static ParseTreeNode *ParseExpr(ParseContext *c);
static ParseTreeNode *ParseAssignmentExpr(ParseContext *c);
static ParseTreeNode *ParseExpr0(ParseContext *c);
static ParseTreeNode *ParseExpr1(ParseContext *c);
static ParseTreeNode *ParseExpr2(ParseContext *c);
static ParseTreeNode *ParseExpr3(ParseContext *c);
static ParseTreeNode *ParseExpr4(ParseContext *c);
static ParseTreeNode *ParseExpr5(ParseContext *c);
static ParseTreeNode *ParseExpr6(ParseContext *c);
static ParseTreeNode *ParseExpr7(ParseContext *c);
static ParseTreeNode *ParseExpr8(ParseContext *c);
static ParseTreeNode *ParseExpr9(ParseContext *c);
static ParseTreeNode *ParseExpr10(ParseContext *c);
static ParseTreeNode *ParseExpr11(ParseContext *c);
static ParseTreeNode *ParsePrimary(ParseContext *c);
static ParseTreeNode *GetSymbolRef(ParseContext *c, char *name);
static ParseTreeNode *ParseSimplePrimary(ParseContext *c);
static ParseTreeNode *ParseArrayReference(ParseContext *c, ParseTreeNode *arrayNode, PvType type);
static ParseTreeNode *ParseCall(ParseContext *c, ParseTreeNode *functionNode);
static ParseTreeNode *ParseMethodCall(ParseContext *c, ParseTreeNode *object, ParseTreeNode *property);
static ParseTreeNode *ParseSuperMethodCall(ParseContext *c);
static ParseTreeNode *ParsePropertyRef(ParseContext *c, ParseTreeNode *object);
static ParseTreeNode *MakeUnaryOpNode(ParseContext *c, int op, ParseTreeNode *expr);
static ParseTreeNode *MakeBinaryOpNode(ParseContext *c, int op, ParseTreeNode *left, ParseTreeNode *right);
static ParseTreeNode *MakeAssignmentOpNode(ParseContext *c, int op, ParseTreeNode *left, ParseTreeNode *right);
static ParseTreeNode *MakeIntegerLitNode(ParseContext *c, VMVALUE value);
static ParseTreeNode *NewParseTreeNode(ParseContext *c, int type);
static void InitLocalSymbolTable(LocalSymbolTable *table);
static LocalSymbol *AddLocalSymbol(ParseContext *c, LocalSymbolTable *table, const char *name, int offset);
static LocalSymbol *MakeLocalSymbol(ParseContext *c, const char *name, int offset);
static LocalSymbol *FindLocalSymbol(LocalSymbolTable *table, const char *name);
static void AddNodeToList(ParseContext *c, NodeListEntry ***ppNextEntry, ParseTreeNode *node);
static int IsIntegerLit(ParseTreeNode *node, VMVALUE *pValue);
static void AddWord(ParseContext *c, int type, String *string);
static int FindWordType(char *namee);

/* ParseDeclarations - parse variable, object, and function declarations */
void ParseDeclarations(ParseContext *c)
{
    char name[MAXTOKEN];
    int tkn, type;
    
    /* parse declarations */
    while ((tkn = GetToken(c)) != T_EOF) {
        switch (tkn) {
        case T_INCLUDE:
            ParseInclude(c);
            break;
        case T_DEF:
            ParseDef(c);
            break;
        case T_VAR:
            ParseVar(c);
            break;
        case T_OBJECT:
            ParseObject(c, NULL);
            break;
        case T_IDENTIFIER:
            strcpy(name, c->token);
            if ((type = FindWordType(c->token)) != WT_NONE)
                ParseWords(c, type);
            else
                ParseObject(c, name);
            break;
        case T_PROPERTY:
            ParseProperty(c);
            break;
        case T_EOF:
            return;
        default:
            ParseError(c, "unknown declaration");
            break;
        }
    }
}

/* ParseInclude - parse the 'INCLUDE' statement */
static void ParseInclude(ParseContext *c)
{
    char name[MAXTOKEN];
    FRequire(c, T_STRING);
    strcpy(name, c->token);
    FRequire(c, ';');
    if (!PushFile(c, name))
        ParseError(c, "include file not found: %s", name);
}

/* ParseDef - parse the 'def' statement */
static void ParseDef(ParseContext *c)
{
    char name[MAXTOKEN];
    int tkn;

    /* get the name being defined */
    FRequire(c, T_IDENTIFIER);
    strcpy(name, c->token);

    /* check for a constant definition */
    if ((tkn = GetToken(c)) == '=')
        ParseConstantDef(c, name);

    /* otherwise, assume a function definition */
    else {
        SaveToken(c, tkn);
        ParseFunctionDef(c, name);
    }
}

/* ParseConstantDef - parse a 'def <name> =' statement */
static void ParseConstantDef(ParseContext *c, char *name)
{
    AddGlobal(c, name, SC_CONSTANT, ParseIntegerLiteralExpr(c));
    FRequire(c, ';');
}

/* ParseFunctionDef - parse a 'def <name> () {}' statement */
static void ParseFunctionDef(ParseContext *c, char *name)
{
    ParseTreeNode *node;
    uint8_t *code;
    int codeLength;
    
    /* enter the function name in the global symbol table */
    AddGlobal(c, name, SC_FUNCTION, (VMVALUE)(c->codeFree - c->codeBuf));
    
    node = ParseFunction(c, name);
    if (c->debugMode)
        PrintNode(c, node, 0);
    
    code = code_functiondef(c, node, &codeLength);
    if (c->debugMode)
        DecodeFunction(c->codeBuf, code, codeLength);
}

/* StoreInitializer - store a data initializer */
void StoreInitializer(ParseContext *c, VMVALUE value)
{
    if (c->dataFree + sizeof(VMVALUE) > c->dataTop)
        ParseError(c, "insufficient data space");
    *(VMVALUE *)c->dataFree = value;
    c->dataFree += sizeof(VMVALUE);
}

/* ParseAndStoreInitializer - parse and store a data initializer */
static void ParseAndStoreInitializer(ParseContext *c)
{
    VMVALUE offset = c->dataFree - c->dataBuf;
    if (c->dataFree + sizeof(VMVALUE) > c->dataTop)
        ParseError(c, "insufficient data space");
    *(VMVALUE *)c->dataFree = ParseConstantLiteralExpr(c, FT_DATA, offset);
    c->dataFree += sizeof(VMVALUE);
}

/* AddNestedArraySymbolRef - add a symbol reference */
int AddNestedArraySymbolRef(ParseContext *c, DataBlock *dataBlock, Symbol *symbol, VMVALUE offset)
{
    SymbolDataFixup *fixup;
    if (symbol->valueDefined)
        return symbol->v.value;
    fixup = (SymbolDataFixup *)LocalAlloc(c, sizeof(SymbolDataFixup));
    fixup->symbol = symbol;
    fixup->offset = offset;
    fixup->next = dataBlock->symbolFixups;
    dataBlock->symbolFixups = fixup;
    return 0;
}

/* AddNestedArrayStringRef - add a string reference */
void AddNestedArrayStringRef(ParseContext *c, DataBlock *dataBlock, String *string, VMVALUE offset)
{
    StringDataFixup *fixup;
    fixup = (StringDataFixup *)LocalAlloc(c, sizeof(StringDataFixup));
    fixup->string = string;
    fixup->offset = offset;
    fixup->next = dataBlock->stringFixups;
    dataBlock->stringFixups = fixup;
}

/* ParseNestedArrayConstantLiteralExpr - parse a constant literal expression (including objects and functions) */
static VMVALUE ParseNestedArrayConstantLiteralExpr(ParseContext *c, DataBlock *dataBlock, VMVALUE offset)
{
    ParseTreeNode *expr = ParseAssignmentExpr(c);
    VMVALUE value = NIL;
    switch (expr->nodeType) {
    case NodeTypeIntegerLit:
        value = expr->u.integerLit.value;
        break;
    case NodeTypeStringLit:
        AddNestedArrayStringRef(c, dataBlock, expr->u.stringLit.string, offset);
        if (c->wordType != WT_NONE)
            AddWord(c, c->wordType, expr->u.stringLit.string);
        value = 0;
        break;
    case NodeTypeGlobalSymbolRef:
        switch (expr->u.symbolRef.symbol->storageClass) {
        case SC_OBJECT:
        case SC_FUNCTION:
            if (expr->u.symbolRef.symbol->valueDefined)
                value = expr->u.symbolRef.symbol->v.value;
            else {
                AddNestedArraySymbolRef(c, dataBlock, expr->u.symbolRef.symbol, offset);
                value = 0;
            }
            break;
        default:
            ParseError(c, "expecting a constant expression, object, or function");
            break;
        }
        break;
    default:
        ParseError(c, "expecting a constant expression, object, or function");
        break;
    }
    return value;
}

/* ParseAndStoreNestedArrayInitializer - parse and store a data initializer */
static void ParseAndStoreNestedArrayInitializer(ParseContext *c, DataBlock *dataBlock, VMVALUE offset)
{
    if (c->dataFree + sizeof(VMVALUE) > c->dataTop)
        ParseError(c, "insufficient data space");
    *(VMVALUE *)c->dataFree = ParseNestedArrayConstantLiteralExpr(c, dataBlock, offset);
    c->dataFree += sizeof(VMVALUE);
}

/* ParseNestedArray - parse a nested array */
static void ParseNestedArray(ParseContext *c, DataBlock *parent, VMVALUE parentOffset)
{
    uint8_t *arrayBase = c->dataFree;
    DataBlock *dataBlock;
    VMVALUE size = 0;
    int tkn;
    
    dataBlock = (DataBlock *)LocalAlloc(c, sizeof(DataBlock));
    memset(dataBlock, 0, sizeof(DataBlock));
    dataBlock->parent = parent;
    dataBlock->parentOffset = parentOffset;
    
    do {
        if ((tkn = GetToken(c)) == '{') {
            VMVALUE offset = c->dataFree - arrayBase;
            StoreInitializer(c, 0);
            ParseNestedArray(c, dataBlock, offset);
        }
        else {
            SaveToken(c, tkn);
            ParseAndStoreNestedArrayInitializer(c, dataBlock, c->dataFree - arrayBase);
        }
        ++size;
    } while ((tkn = GetToken(c)) == ',');
    Require(c, tkn, '}');
    
    dataBlock->size = size;
    dataBlock->data = LocalAlloc(c, c->dataFree - arrayBase);
    memcpy(dataBlock->data, arrayBase, c->dataFree - arrayBase);
    
    *c->pNextDataBlock = dataBlock;
    c->pNextDataBlock = &dataBlock->next;
    
    c->dataFree = arrayBase;
}

/* PlaceNestedArrays - place nested arrays in data memory */
static void PlaceNestedArrays(ParseContext *c)
{
    SymbolDataFixup *symbolFixup;
    StringDataFixup *stringFixup;
    DataBlock *block;
    
    /* place each block in data memory */
    block = c->dataBlocks;
    while (block) {
        VMVALUE sizeInBytes = block->size * sizeof(VMVALUE);
    
        /* store the array size at array[-1] */
        StoreInitializer(c, block->size);
        
        /* copy the array data */
        block->offset = c->dataFree - c->dataBuf;
        if (c->dataFree + sizeInBytes > c->dataTop)
            ParseError(c, "insufficient data space - needed %d bytes", sizeInBytes);
        memcpy(c->dataFree, block->data, sizeInBytes);
        c->dataFree += sizeInBytes;
        
        /* store the pointer to the nested array in the parent array */
        if (block->parent)
            *(VMVALUE *)(c->dataBuf + block->parent->offset + block->parentOffset) = block->offset;
        else
            *(VMVALUE *)(c->dataBuf + block->parentOffset) = block->offset;
        
        /* copy the fixups to the symbol fixup lists */
        symbolFixup = block->symbolFixups;
        while (symbolFixup) {
            SymbolDataFixup *nextSymbol = symbolFixup->next;
            AddSymbolRef(c, symbolFixup->symbol, FT_DATA, block->offset + symbolFixup->offset);
            free(symbolFixup);
            symbolFixup = nextSymbol;
        }
        
        /* copy the fixups to the string fixup lists */
        stringFixup = block->stringFixups;
        while (stringFixup) {
            StringDataFixup *nextString = stringFixup->next;
            AddStringRef(c, stringFixup->string, FT_DATA, block->offset + stringFixup->offset);
            free(stringFixup);
            stringFixup = nextString;
        }
        
        /* move ahead to the next block */
        block = block->next;
    }
    
    /* free the blocks */
    block = c->dataBlocks;;
    while (block) {
        DataBlock *next = block->next;
        free(block->data);
        free(block);
        block = next;
    }
    
    /* reinitialize the block list */
    c->dataBlocks = NULL;
    c->pNextDataBlock = &c->dataBlocks;
}

/* ParseVar - parse the 'var' statement */
static void ParseVar(ParseContext *c)
{
    int tkn;
    do {
        FRequire(c, T_IDENTIFIER);
        if ((tkn = GetToken(c)) == '[') {
            VMVALUE *sizePtr;
            int declaredSize, remaining;
            VMVALUE value = 0;
            
            sizePtr = (VMVALUE *)c->dataFree;
            StoreInitializer(c, 0);
            AddGlobal(c, c->token, SC_OBJECT, (VMVALUE)(c->dataFree - c->dataBuf));
            
            if ((tkn = GetToken(c)) == ']') {
                declaredSize = -1;
                remaining = 0;
            }
            else {
                SaveToken(c, tkn);
                declaredSize = ParseIntegerLiteralExpr(c);
                remaining = declaredSize;
                if (declaredSize < 0)
                    ParseError(c, "expecting a positive array size");
                FRequire(c, ']');
            }
            
            if ((tkn = GetToken(c)) == '=') {
                if ((tkn = GetToken(c)) == '{') {
                    int initializerCount = 0;
                    do {
                        if (declaredSize != -1 && --remaining < 0)
                            ParseError(c, "too many initializers");
                        if ((tkn = GetToken(c)) == '{') {
                            VMVALUE offset = c->dataFree - c->dataBuf;
                            StoreInitializer(c, 0);
                            ParseNestedArray(c, NULL, offset);
                        }
                        else {
                            SaveToken(c, tkn);
                            ParseAndStoreInitializer(c);
                        }
                        ++initializerCount;
                    } while ((tkn = GetToken(c)) == ',');
                    if (declaredSize == -1)
                        declaredSize = initializerCount;
                    Require(c, tkn, '}');
                }
                else {
                    SaveToken(c, tkn);
                    value = ParseIntegerLiteralExpr(c);
                }
            }
            else {
                SaveToken(c, tkn);
            }
            
            while (--remaining >= 0)
                StoreInitializer(c, value);
                
            PlaceNestedArrays(c);
            
            *sizePtr = declaredSize;
        }
        else {
            AddGlobal(c, c->token, SC_VARIABLE, (VMVALUE)(c->dataFree - c->dataBuf));
            SaveToken(c, tkn);
            if ((tkn = GetToken(c)) == '=')
                ParseAndStoreInitializer(c);
            else {
                SaveToken(c, tkn);
                StoreInitializer(c, 0);
            }
        }
    } while ((tkn = GetToken(c)) == ',');
    Require(c, tkn, ';');
}

/* ParseObject - parse the 'object' statement */
static void ParseObject(ParseContext *c, char *className)
{
    VMVALUE class, object;
    char name[MAXTOKEN], pname[MAXTOKEN];
    ParseTreeNode *node;
    ObjectHdr *objectHdr;
    Property *property, *p;
    int tkn;
    
    /* get the name of the object being defined */
    FRequire(c, T_IDENTIFIER);
    strcpy(name, c->token);
    
    /* allocate space for an object header and initialize */
    if (c->dataFree + sizeof(ObjectHdr) > c->dataTop)
        ParseError(c, "insufficient data space");
    object = (VMVALUE)(c->dataFree - c->dataBuf);
    c->currentObjectSymbol = AddGlobal(c, name, SC_OBJECT, object);
    objectHdr = (ObjectHdr *)c->dataFree;
    c->dataFree += sizeof(ObjectHdr);
    objectHdr->nProperties = 0;
    property = (Property *)(objectHdr + 1);
    AddObject(c, object);
        
    /* copy non-shared properties from the class object */
    if (className) {
        ObjectHdr *classHdr;
        Property *srcProperty;
        VMVALUE nProperties;
        class = FindObject(c, className);
        objectHdr->class = class;
        classHdr = (ObjectHdr *)(c->dataBuf + class);
        srcProperty = (Property *)(classHdr + 1);
        for (nProperties = classHdr->nProperties; --nProperties >= 0; ++srcProperty) {
            if (!(srcProperty->tag & P_SHARED)) {
                if ((uint8_t *)property + sizeof(Property) > c->dataTop)
                    ParseError(c, "insufficient data space");
                *property++ = *srcProperty;
                ++objectHdr->nProperties;
            }
        }
    }
    else {
        objectHdr->class = NIL;
    }
    
    /* parse object properties */
    FRequire(c, '{');
    while ((tkn = GetToken(c)) != '}') {
        VMVALUE tag, flags = 0;
        int wordType;
        if (tkn == T_SHARED) {
            flags = P_SHARED;
            tkn = GetToken(c);
        }
        Require(c, tkn, T_IDENTIFIER);
        strcpy(pname, c->token);
        tag = AddProperty(c, pname);
        FRequire(c, ':');
        
        /* check to see if the property name is one of the vocabulary words */
        wordType = FindWordType(pname);
        
        /* find a property copied from the class */
        for (p = (Property *)(objectHdr + 1); p < property; ++p) {
            if ((p->tag & ~P_SHARED) == tag) {
                if (p->tag & P_SHARED)
                    ParseError(c, "can't set shared property in object definition");
                break;
            }
        }
        
        /* add a new property if one wasn't found that was copied from the class */
        if (p >= property) {
            if ((uint8_t *)property + sizeof(Property) > c->dataTop)
                ParseError(c, "insufficient data space");
            p = property;
            p->tag = tag | flags;
            ++objectHdr->nProperties;
            ++property;
        }
        
        /* handle methods */
        if ((tkn = GetToken(c)) == T_METHOD) {
            uint8_t *code;
            int codeLength;
            node = ParseMethod(c, pname);
            if (c->debugMode)
                PrintNode(c, node, 0);
            code = code_functiondef(c, node, &codeLength);
            if (c->debugMode)
                DecodeFunction(c->codeBuf, code, codeLength);
            p->value = (VMVALUE)(code - c->codeBuf);
        }
        
        /* handle values */
        else {
            VMVALUE offset = (uint8_t *)&p->value - c->dataBuf;
            c->wordType = wordType;
            if (tkn == '{') {
                ParseNestedArray(c, NULL, offset);
            }
            else {
                SaveToken(c, tkn);
                p->value = ParseConstantLiteralExpr(c, FT_DATA, offset);
            }
            c->wordType = WT_NONE;
        }

        FRequire(c, ';');
    }
    
    PlaceNestedArrays(c);
    
    /* move the free pointer past the new object */
    c->dataFree = (uint8_t *)property;
    
    /* not in an object definition anymore */
    c->currentObjectSymbol = NULL;
}

/* ParseProperty - parse the 'property' statement */
static void ParseProperty(ParseContext *c)
{
    int tkn;
    do {
        FRequire(c, T_IDENTIFIER);
        AddProperty(c, c->token);
    } while ((tkn = GetToken(c)) == ',');
    Require(c, tkn, ';');
}

/* ParseFunction - parse a function definition */
static ParseTreeNode *ParseFunction(ParseContext *c, char *name)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeFunctionDef);
    
    c->currentFunction = node;
    node->u.functionDef.name = (char *)LocalAlloc(c, strlen(name) + 1);
    strcpy(node->u.functionDef.name, name);
    InitLocalSymbolTable(&node->u.functionDef.arguments);
    InitLocalSymbolTable(&node->u.functionDef.locals);
    c->trySymbols = NULL;
    c->currentTryDepth = 0;
    c->block = NULL;
    
    return ParseFunctionBody(c, node, 0);
}

/* ParseMethod - parse a method definition */
static ParseTreeNode *ParseMethod(ParseContext *c, char *name)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeFunctionDef);
    
    c->currentFunction = node;
    node->u.functionDef.name = (char *)LocalAlloc(c, strlen(name) + 1);
    strcpy(node->u.functionDef.name, name);
    InitLocalSymbolTable(&node->u.functionDef.arguments);
    InitLocalSymbolTable(&node->u.functionDef.locals);
    c->trySymbols = NULL;
    c->currentTryDepth = 0;
    c->block = NULL;
    
    AddLocalSymbol(c, &node->u.functionDef.arguments, "self", 0);
    AddLocalSymbol(c, &node->u.functionDef.arguments, "(dummy)", 1);
    
    return ParseFunctionBody(c, node, 2);
}

/* ParseFunctionBody - parse a function argument list and body */
static ParseTreeNode *ParseFunctionBody(ParseContext *c, ParseTreeNode *node, int offset)
{
    int localOffset = 0;
    int tkn;

    /* parse the argument list */
    FRequire(c, '(');
    if ((tkn = GetToken(c)) != ')') {
        SaveToken(c, tkn);
        do {
            FRequire(c, T_IDENTIFIER);
            AddLocalSymbol(c, &node->u.functionDef.arguments, c->token, offset++);
        } while ((tkn = GetToken(c)) == ',');
    }
    Require(c, tkn, ')');
    FRequire(c, '{');
    
    while ((tkn = GetToken(c)) == T_VAR) {
        do {
            LocalSymbol *symbol;
            FRequire(c, T_IDENTIFIER);
            symbol = AddLocalSymbol(c, &node->u.functionDef.locals, c->token, localOffset++);
            if ((tkn = GetToken(c)) == '=') {
                symbol->initialValue = ParseAssignmentExpr(c);
            }
            else {
                SaveToken(c, tkn);
            }
        } while ((tkn = GetToken(c)) == ',');
        Require(c, tkn, ';');
    }
    SaveToken(c, tkn);

    /* parse the function body */
    node->u.functionDef.body = ParseBlock(c);
    
    /* not compiling a function anymore */
    c->currentFunction = NULL;
    
    return node;
}

/* ParseWords - parse a list of words of a specified type */
static void ParseWords(ParseContext *c, int type)
{
    int tkn;
    do {
        String *string;
        FRequire(c, T_STRING);
        string = AddString(c, c->token);
        AddWord(c, type, string);
    } while ((tkn = GetToken(c)) == ',');
    Require(c, tkn, ';');
}

/* ParseStatement - parse a statement */
ParseTreeNode *ParseStatement(ParseContext *c)
{
    ParseTreeNode *node;
    int tkn;
    
    /* dispatch on the statement keyword */
    switch (tkn = GetToken(c)) {
    case T_IF:
        node = ParseIf(c);
        break;
    case T_WHILE:
        node = ParseWhile(c);
        break;
    case T_DO:
        node = ParseDoWhile(c);
        break;
    case T_FOR:
        node = ParseFor(c);
        break;
    case T_BREAK:
        node = ParseBreak(c);
        break;
    case T_CONTINUE:
        node = ParseContinue(c);
        break;
    case T_RETURN:
        node = ParseReturn(c);
        break;
    case T_TRY:
        node = ParseTry(c);
        break;
    case T_THROW:
        node = ParseThrow(c);
        break;
    case T_ASM:
        node = ParseAsm(c);
        break;
    case T_PRINT:
    case T_PRINTLN:
        node = ParsePrint(c, tkn == T_PRINTLN);
        break;
    case '{':
        node = ParseBlock(c);
        break;
    case ';':
        node = ParseEmpty(c);
        break;
    default:
        SaveToken(c, tkn);
        node = ParseExprStatement(c);
        break;
    }
    
    return node;
}

/* ParseIf - parse an 'if' statement */
static ParseTreeNode *ParseIf(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeIf);
    int tkn;
    
    /* parse the test expression */
    FRequire(c, '(');
    node->u.ifStatement.test = ParseExpr(c);
    FRequire(c, ')');
    
    /* parse the 'then' statement */
    node->u.ifStatement.thenStatement = ParseStatement(c);
    
    /* check for an 'else' statement */
    if ((tkn = GetToken(c)) == T_ELSE)
        node->u.ifStatement.elseStatement = ParseStatement(c);
    else
        SaveToken(c, tkn);
        
    return node;
}

/* ParseWhile - parse a 'while' statement */
static ParseTreeNode *ParseWhile(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeWhile);
    FRequire(c, '(');
    node->u.whileStatement.test = ParseExpr(c);
    FRequire(c, ')');
    node->u.whileStatement.body = ParseStatement(c);
    return node;
}

/* ParseDoWhile - parse a 'do/while' statement */
static ParseTreeNode *ParseDoWhile(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeDoWhile);
    node->u.doWhileStatement.body = ParseStatement(c);
    FRequire(c, T_WHILE);
    FRequire(c, '(');
    node->u.doWhileStatement.test = ParseExpr(c);
    FRequire(c, ')');
    FRequire(c, ';');
    return node;
}

/* ParseFor - parse a 'for' statement */
static ParseTreeNode *ParseFor(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeFor);
    int tkn;
    
    /* parse the init part */
    FRequire(c, '(');
    if ((tkn = GetToken(c)) != ';') {
        SaveToken(c, tkn);
        node->u.forStatement.init = ParseExpr(c);
        FRequire(c, ';');
    }
    
    /* parse the test part */
    if ((tkn = GetToken(c)) != ';') {
        SaveToken(c, tkn);
        node->u.forStatement.test = ParseExpr(c);
        FRequire(c, ';');
    }
    
    /* parse the incr part of the 'for' stateme*/
    if ((tkn = GetToken(c)) != ')') {
        SaveToken(c, tkn);
        node->u.forStatement.incr = ParseExpr(c);
        FRequire(c, ')');
    }
    
    /* parse the body */
    node->u.forStatement.body = ParseStatement(c);
    
    return node;
}

/* ParseBreak - parse a 'break' statement */
static ParseTreeNode *ParseBreak(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeBreak);
    FRequire(c, ';');
    return node;
}

/* ParseContinue - parse a 'continue' statement */
static ParseTreeNode *ParseContinue(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeContinue);
    FRequire(c, ';');
    return node;
}

/* ParseReturn - parse a 'return' statement */
static ParseTreeNode *ParseReturn(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeReturn);
    int tkn;
    if ((tkn = GetToken(c)) != ';') {
        SaveToken(c, tkn);
        node->u.returnStatement.value = ParseExpr(c);
        FRequire(c, ';');
    }
    return node;
}

/* ParseBlock - parse a {} block */
static ParseTreeNode *ParseBlock(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeBlock);
    NodeListEntry **pNextStatement = &node->u.blockStatement.statements;
    int tkn;    
    while ((tkn = GetToken(c)) != '}') {
        SaveToken(c, tkn);
        AddNodeToList(c, &pNextStatement, ParseStatement(c));
    }
    return node;
}

/* ParseTry - parse the 'try/catch' statement */
static ParseTreeNode *ParseTry(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeTry);
    int tkn;
        
    FRequire(c, '{');
    node->u.tryStatement.statement = ParseBlock(c);
    
    if ((tkn = GetToken(c)) == T_CATCH) {
        LocalSymbol *sym;
        if (++c->currentTryDepth > c->currentFunction->u.functionDef.maximumTryDepth)
            c->currentFunction->u.functionDef.maximumTryDepth = c->currentTryDepth;
        FRequire(c, '(');
        FRequire(c, T_IDENTIFIER);
        sym = MakeLocalSymbol(c, c->token, c->currentFunction->u.functionDef.locals.count + c->currentTryDepth - 1);
        sym->next = c->trySymbols;
        c->trySymbols = sym;
        FRequire(c, ')');
        FRequire(c, '{');
        node->u.tryStatement.catchSymbol = sym;
        node->u.tryStatement.catchStatement = ParseBlock(c);
        sym = c->trySymbols;
        c->trySymbols = sym->next;
        --c->currentTryDepth;
    }
    else {
        SaveToken(c, tkn);
    }
    
    if (!node->u.tryStatement.catchStatement)
        ParseError(c, "try requires a catch clause");
            
    return node;
}

/* ParseThrow - parse the 'throw' statement */
static ParseTreeNode *ParseThrow(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeThrow);
    node->u.throwStatement.expr = ParseExpr(c);
    FRequire(c, ';');
    return node;
}

/* ParseExprStatement - parse an expression statement */
static ParseTreeNode *ParseExprStatement(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeExpr);
    node->u.exprStatement.expr = ParseExpr(c);
    FRequire(c, ';');
    return node;
}

/* ParseEmpty - parse an empty statement */
static ParseTreeNode *ParseEmpty(ParseContext *c)
{
    return NewParseTreeNode(c, NodeTypeEmpty);
}

/* ParseAsm - parse the 'ASM {}' statement */
static ParseTreeNode *ParseAsm(ParseContext *c)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeAsm);
    uint8_t *start = c->codeFree;
    int length, tkn;
    uint32_t value;
    OTDEF *def;
    char *p;
    
    FRequire(c, '{');
    
    /* parse each assembly instruction */
    while ((tkn = GetToken(c)) != '}') {
    
        /* get the opcode */
        Require(c, tkn, T_IDENTIFIER);
            
        /* assemble a single instruction */
        for (def = OpcodeTable; def->name != NULL; ++def) {
            if (strcasecmp(c->token, def->name) == 0) {
                putcbyte(c, def->code);
                switch (def->fmt) {
                case FMT_NONE:
                    break;
                case FMT_BYTE:
                case FMT_SBYTE:
                    putcbyte(c, ParseIntegerLiteralExpr(c));
                    break;
                case FMT_LONG:
                    putclong(c, ParseIntegerLiteralExpr(c));
                    break;
                case FMT_BR:
                    putcword(c, ParseIntegerLiteralExpr(c));
                    break;
                case FMT_NATIVE:
                    for (p = c->linePtr; *p != '\0' && isspace(*p); ++p)
                        ;
                    if (isdigit(*p))
                        putcword(c, ParseIntegerLiteralExpr(c));
                    else {
                        if (!PasmAssemble1(c->linePtr, &value))
                            ParseError(c, "native assembly failed");
                        putclong(c, (VMVALUE)value);
                        for (p = c->linePtr; *p != '\0' && *p != '\n'; ++p)
                            ;
                        c->linePtr = p;
                    }
                    break;
                default:
                    ParseError(c, "instruction not currently supported");
                    break;
                }
                break;
            }
        }
        if (!def->name)
            ParseError(c, "undefined opcode");
    }
    
    /* store the code */
    length = c->codeFree - start;
    node->u.asmStatement.code = LocalAlloc(c, length);
    node->u.asmStatement.length = length;
    memcpy(node->u.asmStatement.code, start, length);
    c->codeFree = start;
    
    return node;
}

/* ParsePrint - handle the 'PRINT' statement */
static ParseTreeNode *ParsePrint(ParseContext *c, int newline)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypePrint);
    PrintOp *op, **pNext = &node->u.printStatement.ops;
    ParseTreeNode *expr;
    int tkn;

    if ((tkn = GetToken(c)) != ';') {
        SaveToken(c, tkn);
        do {
            switch (tkn = GetToken(c)) {
            case '#':
                op = LocalAlloc(c, sizeof(PrintOp));
                op->trap = TRAP_PrintStr;
                op->expr = ParseAssignmentExpr(c);
                op->next = NULL;
                *pNext = op;
                pNext = &op->next;
                break;
            default:
                SaveToken(c, tkn);
                expr = ParseAssignmentExpr(c);
                switch (expr->nodeType) {
                case NodeTypeStringLit:
                    op = LocalAlloc(c, sizeof(PrintOp));
                    op->trap = TRAP_PrintStr;
                    op->expr = expr;
                    op->next = NULL;
                    *pNext = op;
                    pNext = &op->next;
                    break;
                default:
                    op = LocalAlloc(c, sizeof(PrintOp));
                    op->trap = TRAP_PrintInt;
                    op->expr = expr;
                    op->next = NULL;
                    *pNext = op;
                    pNext = &op->next;
                    break;
                }
                break;
            }
        } while ((tkn = GetToken(c)) == ',');
        Require(c, tkn, ';');
    }
    
    if (newline) {
        op = LocalAlloc(c, sizeof(PrintOp));
        op->trap = TRAP_PrintNL;
        op->expr = NULL;
        op->next = NULL;
        *pNext = op;
        pNext = &op->next;
    }
        
    return node;
}

/* ParseIntegerLiteralExpr - parse an integer literal expression */
static VMVALUE ParseIntegerLiteralExpr(ParseContext *c)
{
    ParseTreeNode *expr = ParseAssignmentExpr(c);
    VMVALUE value;
    if (!IsIntegerLit(expr, &value))
        ParseError(c, "expecting a constant expression");
    return value;
}

/* ParseConstantLiteralExpr - parse a constant literal expression (including objects and functions) */
static VMVALUE ParseConstantLiteralExpr(ParseContext *c, FixupType fixupType, VMVALUE offset)
{
    ParseTreeNode *expr = ParseAssignmentExpr(c);
    VMVALUE value = NIL;
    switch (expr->nodeType) {
    case NodeTypeIntegerLit:
        value = expr->u.integerLit.value;
        break;
    case NodeTypeStringLit:
        AddStringRef(c, expr->u.stringLit.string, fixupType, offset);
        if (c->wordType != WT_NONE)
            AddWord(c, c->wordType, expr->u.stringLit.string);
        value = 0;
        break;
    case NodeTypeGlobalSymbolRef:
        switch (expr->u.symbolRef.symbol->storageClass) {
        case SC_OBJECT:
        case SC_FUNCTION:
            value = AddSymbolRef(c, expr->u.symbolRef.symbol, fixupType, offset);
            break;
        default:
            ParseError(c, "expecting a constant expression, object, or function");
            break;
        }
        break;
    default:
        ParseError(c, "expecting a constant expression, object, or function");
        break;
    }
    return value;
}

/* ParseExpr - handle the ',' operator */
static ParseTreeNode *ParseExpr(ParseContext *c)
{
    ParseTreeNode *node;
    int tkn;
    node = ParseAssignmentExpr(c);
    while ((tkn = GetToken(c)) == ',') {
        ParseTreeNode *node2 = NewParseTreeNode(c, NodeTypeCommaOp);
        node2->u.commaOp.left = node;
        node2->u.commaOp.right = ParseExpr(c);
        node = node2;
    }
    SaveToken(c, tkn);
    return node;
}

/* ParseAssignmentExpr - handle assignment operators */
static ParseTreeNode *ParseAssignmentExpr(ParseContext *c)
{
    ParseTreeNode *node;
    int tkn;
    node = ParseExpr0(c);
    while ((tkn = GetToken(c)) == '='
    ||      tkn == T_ADDEQ || tkn == T_SUBEQ
    ||      tkn == T_MULEQ || tkn == T_DIVEQ || tkn == T_REMEQ
    ||      tkn == T_ANDEQ || tkn == T_OREQ  || tkn == T_XOREQ
    ||      tkn == T_SHLEQ || tkn == T_SHREQ) {
        ParseTreeNode *node2 = ParseExpr0(c);
        int op;
        switch (tkn) {
        case '=':
            op = OP_EQ; // indicator of simple assignment
            break;
        case T_ADDEQ:
            op = OP_ADD;
            break;
        case T_SUBEQ:
            op = OP_SUB;
            break;
        case T_MULEQ:
            op = OP_MUL;
            break;
        case T_DIVEQ:
            op = OP_DIV;
            break;
        case T_REMEQ:
            op = OP_REM;
            break;
        case T_ANDEQ:
            op = OP_BAND;
            break;
        case T_OREQ:
            op = OP_BOR;
            break;
        case T_XOREQ:
            op = OP_BXOR;
            break;
        case T_SHLEQ:
            op = OP_SHL;
            break;
        case T_SHREQ:
            op = OP_SHR;
            break;
        default:
            /* not reached */
            op = 0;
            break;
        }
        node = MakeAssignmentOpNode(c, op, node, node2);
    }
    SaveToken(c, tkn);
    return node;
}

/* ParseExpr0 - handle the '?:' operator */
static ParseTreeNode *ParseExpr0(ParseContext *c)
{
    ParseTreeNode *node;
    int tkn;
    node = ParseExpr1(c);
    while ((tkn = GetToken(c)) == '?') {
        ParseTreeNode *node2 = NewParseTreeNode(c, NodeTypeTernaryOp);
        node2->u.ternaryOp.test = node;
        node2->u.ternaryOp.thenExpr = ParseExpr1(c);
        FRequire(c, ':');
        node2->u.ternaryOp.elseExpr = ParseExpr1(c);
        node = node2;
    }
    SaveToken(c, tkn);
    return node;
}

/* ParseExpr1 - handle the '||' operator */
static ParseTreeNode *ParseExpr1(ParseContext *c)
{
    ParseTreeNode *node;
    int tkn;
    node = ParseExpr2(c);
    if ((tkn = GetToken(c)) == T_OR) {
        ParseTreeNode *node2 = NewParseTreeNode(c, NodeTypeDisjunction);
        NodeListEntry *entry, **pLast;
        node2->u.exprList.exprs = entry = (NodeListEntry *)LocalAlloc(c, sizeof(NodeListEntry));
        entry->node = node;
        entry->next = NULL;
        pLast = &entry->next;
        do {
            entry = (NodeListEntry *)LocalAlloc(c, sizeof(NodeListEntry));
            entry->node = ParseExpr2(c);
            entry->next = NULL;
            *pLast = entry;
            pLast = &entry->next;
        } while ((tkn = GetToken(c)) == T_OR);
        node = node2;
    }
    SaveToken(c, tkn);
    return node;
}

/* ParseExpr2 - handle the '&&' operator */
static ParseTreeNode *ParseExpr2(ParseContext *c)
{
    ParseTreeNode *node;
    int tkn;
    node = ParseExpr3(c);
    if ((tkn = GetToken(c)) == T_AND) {
        ParseTreeNode *node2 = NewParseTreeNode(c, NodeTypeConjunction);
        NodeListEntry *entry, **pLast;
        node2->u.exprList.exprs = entry = (NodeListEntry *)LocalAlloc(c, sizeof(NodeListEntry));
        entry->node = node;
        entry->next = NULL;
        pLast = &entry->next;
        do {
            entry = (NodeListEntry *)LocalAlloc(c, sizeof(NodeListEntry));
            entry->node = ParseExpr2(c);
            entry->next = NULL;
            *pLast = entry;
            pLast = &entry->next;
        } while ((tkn = GetToken(c)) == T_AND);
        node = node2;
    }
    SaveToken(c, tkn);
    return node;
}

/* ParseExpr3 - handle the '^' operator */
static ParseTreeNode *ParseExpr3(ParseContext *c)
{
    ParseTreeNode *expr, *expr2;
    int tkn;
    expr = ParseExpr4(c);
    while ((tkn = GetToken(c)) == '^') {
        VMVALUE value, value2;
        expr2 = ParseExpr4(c);
        if (IsIntegerLit(expr, &value) && IsIntegerLit(expr2, &value2))
            expr->u.integerLit.value = value ^ value2;
        else
            expr = MakeBinaryOpNode(c, OP_BXOR, expr, expr2);
    }
    SaveToken(c,tkn);
    return expr;
}

/* ParseExpr4 - handle the '|' operator */
static ParseTreeNode *ParseExpr4(ParseContext *c)
{
    ParseTreeNode *expr, *expr2;
    int tkn;
    expr = ParseExpr5(c);
    while ((tkn = GetToken(c)) == '|') {
        VMVALUE value, value2;
        expr2 = ParseExpr5(c);
        if (IsIntegerLit(expr, &value) && IsIntegerLit(expr2, &value2))
            expr->u.integerLit.value = value | value2;
        else
            expr = MakeBinaryOpNode(c, OP_BOR, expr, expr2);
    }
    SaveToken(c,tkn);
    return expr;
}

/* ParseExpr5 - handle the '&' operator */
static ParseTreeNode *ParseExpr5(ParseContext *c)
{
    ParseTreeNode *expr, *expr2;
    int tkn;
    expr = ParseExpr6(c);
    while ((tkn = GetToken(c)) == '&') {
        VMVALUE value, value2;
        expr2 = ParseExpr6(c);
        if (IsIntegerLit(expr, &value) && IsIntegerLit(expr2, &value2))
            expr->u.integerLit.value = value & value2;
        else
            expr = MakeBinaryOpNode(c, OP_BAND, expr, expr2);
    }
    SaveToken(c,tkn);
    return expr;
}

/* ParseExpr6 - handle the '==' and '!=' operators */
static ParseTreeNode *ParseExpr6(ParseContext *c)
{
    ParseTreeNode *expr, *expr2;
    int tkn;
    expr = ParseExpr7(c);
    while ((tkn = GetToken(c)) == T_EQ || tkn == T_NE) {
        int op;
        expr2 = ParseExpr7(c);
        switch (tkn) {
        case T_EQ:
            op = OP_EQ;
            break;
        case T_NE:
            op = OP_NE;
            break;
        default:
            /* not reached */
            op = 0;
            break;
        }
        expr = MakeBinaryOpNode(c, op, expr, expr2);
    }
    SaveToken(c,tkn);
    return expr;
}

/* ParseExpr7 - handle the '<', '<=', '>=' and '>' operators */
static ParseTreeNode *ParseExpr7(ParseContext *c)
{
    ParseTreeNode *expr, *expr2;
    int tkn;
    expr = ParseExpr8(c);
    while ((tkn = GetToken(c)) == '<' || tkn == T_LE || tkn == T_GE || tkn == '>') {
        int op;
        expr2 = ParseExpr8(c);
        switch (tkn) {
        case '<':
            op = OP_LT;
            break;
        case T_LE:
            op = OP_LE;
            break;
        case T_GE:
            op = OP_GE;
            break;
        case '>':
            op = OP_GT;
            break;
        default:
            /* not reached */
            op = 0;
            break;
        }
        expr = MakeBinaryOpNode(c, op, expr, expr2);
    }
    SaveToken(c,tkn);
    return expr;
}

/* ParseExpr8 - handle the '<<' and '>>' operators */
static ParseTreeNode *ParseExpr8(ParseContext *c)
{
    ParseTreeNode *expr, *expr2;
    int tkn;
    expr = ParseExpr9(c);
    while ((tkn = GetToken(c)) == T_SHL || tkn == T_SHR) {
        VMVALUE value, value2;
        expr2 = ParseExpr9(c);
        if (IsIntegerLit(expr, &value) && IsIntegerLit(expr2, &value2)) {
            switch (tkn) {
            case T_SHL:
                expr->u.integerLit.value = value << value2;
                break;
            case T_SHR:
                expr->u.integerLit.value = value >> value2;
                break;
            default:
                /* not reached */
                break;
            }
        }
        else {
            int op;
            switch (tkn) {
            case T_SHL:
                op = OP_SHL;
                break;
            case T_SHR:
                op = OP_SHR;
                break;
            default:
                /* not reached */
                op = 0;
                break;
            }
            expr = MakeBinaryOpNode(c, op, expr, expr2);
        }
    }
    SaveToken(c,tkn);
    return expr;
}

/* ParseExpr9 - handle the '+' and '-' operators */
static ParseTreeNode *ParseExpr9(ParseContext *c)
{
    ParseTreeNode *expr, *expr2;
    int tkn;
    expr = ParseExpr10(c);
    while ((tkn = GetToken(c)) == '+' || tkn == '-') {
        VMVALUE value, value2;
        expr2 = ParseExpr10(c);
        if (IsIntegerLit(expr, &value) && IsIntegerLit(expr2, &value2)) {
            switch (tkn) {
            case '+':
                expr->u.integerLit.value = value + value2;
                break;
            case '-':
                expr->u.integerLit.value = value - value2;
                break;
            default:
                /* not reached */
                break;
            }
        }
        else {
            int op;
            switch (tkn) {
            case '+':
                op = OP_ADD;
                break;
            case '-':
                op = OP_SUB;
                break;
            default:
                /* not reached */
                op = 0;
                break;
            }
            expr = MakeBinaryOpNode(c, op, expr, expr2);
        }
    }
    SaveToken(c, tkn);
    return expr;
}

/* ParseExpr10 - handle the '*', '/' and '%' operators */
static ParseTreeNode *ParseExpr10(ParseContext *c)
{
    ParseTreeNode *expr, *expr2;
    int tkn;
    expr = ParseExpr11(c);
    while ((tkn = GetToken(c)) == '*' || tkn == '/' || tkn == '%') {
        VMVALUE value, value2;
        expr2 = ParseExpr11(c);
        if (IsIntegerLit(expr, &value) && IsIntegerLit(expr2, &value2)) {
            switch (tkn) {
            case '*':
                expr->u.integerLit.value = value * value2;
                break;
            case '/':
                if (expr2->u.integerLit.value == 0)
                    ParseError(c, "division by zero in constant expression");
                expr->u.integerLit.value = value / value2;
                break;
            case '%':
                if (value2 == 0)
                    ParseError(c, "division by zero in constant expression");
                expr->u.integerLit.value = value % value2;
                break;
            default:
                /* not reached */
                break;
            }
        }
        else {
            int op;
            switch (tkn) {
            case '*':
                op = OP_MUL;
                break;
            case '/':
                op = OP_DIV;
                break;
            case '%':
                op = OP_REM;
                break;
            default:
                /* not reached */
                op = 0;
                break;
            }
            expr = MakeBinaryOpNode(c, op, expr, expr2);
        }
    }
    SaveToken(c, tkn);
    return expr;
}

/* ParseExpr11 - handle unary operators */
static ParseTreeNode *ParseExpr11(ParseContext *c)
{
    ParseTreeNode *node;
    VMVALUE value;
    int tkn;
    switch (tkn = GetToken(c)) {
    case '+':
        node = ParsePrimary(c);
        break;
    case '-':
        node = ParsePrimary(c);
        if (IsIntegerLit(node, &value))
            node->u.integerLit.value = -value;
        else
            node = MakeUnaryOpNode(c, OP_NEG, node);
        break;
    case '!':
        node = ParsePrimary(c);
        if (IsIntegerLit(node, &value))
            node->u.integerLit.value = !value;
        else
            node = MakeUnaryOpNode(c, OP_NOT, node);
        break;
    case '~':
        node = ParsePrimary(c);
        if (IsIntegerLit(node, &value))
            node->u.integerLit.value = ~value;
        else
            node = MakeUnaryOpNode(c, OP_BNOT, node);
        break;
    case T_INC:
        node = NewParseTreeNode(c, NodeTypePreincrementOp);
        node->u.incrementOp.increment = 1;
        node->u.incrementOp.expr = ParsePrimary(c);
        break;
    case T_DEC:
        node = NewParseTreeNode(c, NodeTypePreincrementOp);
        node->u.incrementOp.increment = -1;
        node->u.incrementOp.expr = ParsePrimary(c);
        break;
    default:
        SaveToken(c,tkn);
        node = ParsePrimary(c);
        break;
    }
    return node;
}

/* ParsePrimary - parse function calls and array references */
static ParseTreeNode *ParsePrimary(ParseContext *c)
{
    ParseTreeNode *node, *node2;
    int tkn;
    node = ParseSimplePrimary(c);
    while ((tkn = GetToken(c)) == '[' || tkn == '(' || tkn == '.' || tkn == T_INC || tkn == T_DEC) {
        switch (tkn) {
        case '[':
            node = ParseArrayReference(c, node, PVT_LONG);
            break;
        case '(':
            node = ParseCall(c, node);
            break;
        case '.':
            node = ParsePropertyRef(c, node);
            break;
        case T_INC:
            node2 = NewParseTreeNode(c, NodeTypePostincrementOp);
            node2->u.incrementOp.increment = 1;
            node2->u.incrementOp.expr = node;
            node = node2;
            break;
        case T_DEC:
            node2 = NewParseTreeNode(c, NodeTypePostincrementOp);
            node2->u.incrementOp.increment = -1;
            node2->u.incrementOp.expr = node;
            node = node2;
            break;
        }
    }
    SaveToken(c, tkn);
    return node;
}

/* ParseArrayReference - parse an array reference */
static ParseTreeNode *ParseArrayReference(ParseContext *c, ParseTreeNode *arrayNode, PvType type)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeArrayRef);
    node->u.arrayRef.array = arrayNode;
    node->u.arrayRef.index = ParseExpr(c);
    node->u.arrayRef.type = type;
    FRequire(c, ']');
    return node;
}

/* ParseCall - parse a function call */
static ParseTreeNode *ParseCall(ParseContext *c, ParseTreeNode *functionNode)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeFunctionCall);
    NodeListEntry **pLast;
    int tkn;

    /* intialize the function call node */
    node->u.functionCall.fcn = functionNode;
    pLast = &node->u.functionCall.args;

    /* parse the argument list */
    if ((tkn = GetToken(c)) != ')') {
        SaveToken(c, tkn);
        do {
            NodeListEntry *actual;
            actual = (NodeListEntry *)LocalAlloc(c, sizeof(NodeListEntry));
            actual->node = ParseAssignmentExpr(c);
            actual->next = NULL;
            *pLast = actual;
            pLast = &actual->next;
            ++node->u.functionCall.argc;
        } while ((tkn = GetToken(c)) == ',');
        Require(c, tkn, ')');
    }

    /* return the function call node */
    return node;
}

/* ParseSelector - parse a property selector */
static ParseTreeNode *ParseSelector(ParseContext *c)
{
    ParseTreeNode *node;
    int tkn;
    if ((tkn = GetToken(c)) == T_IDENTIFIER)
        node = MakeIntegerLitNode(c, AddProperty(c, c->token));
    else if (tkn == '(') {
        SaveToken(c, tkn);
        node = ParseExpr(c);
    }
    else {
        ParseError(c, "expecting a property name or parenthesized expression");
        node = NULL; // never reached
    }
    return node;
}

/* ParseMethodCall - parse a method call */
static ParseTreeNode *ParseMethodCall(ParseContext *c, ParseTreeNode *object, ParseTreeNode *selector)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeMethodCall);
    NodeListEntry **pLast;
    int tkn;

    /* get the value of 'super' if needed */
    if (object == NULL) {
        if (!c->currentObjectSymbol)
            ParseError(c, "super outside of a method definition");
        node->u.methodCall.class = NewParseTreeNode(c, NodeTypeGlobalSymbolRef);
        node->u.methodCall.class->u.symbolRef.symbol = c->currentObjectSymbol;
        node->u.methodCall.object = NewParseTreeNode(c, NodeTypeArgumentRef);
        node->u.methodCall.object->u.localSymbolRef.symbol = FindLocalSymbol(&c->currentFunction->u.functionDef.arguments, "self");
    }
    else {
        node->u.methodCall.class = NULL;
        node->u.methodCall.object = object;
    }
    node->u.methodCall.selector = selector;

    /* parse the argument list */
    pLast = &node->u.methodCall.args;
    if ((tkn = GetToken(c)) != ')') {
        SaveToken(c, tkn);
        do {
            NodeListEntry *actual;
            actual = (NodeListEntry *)LocalAlloc(c, sizeof(NodeListEntry));
            actual->node = ParseAssignmentExpr(c);
            actual->next = NULL;
            *pLast = actual;
            pLast = &actual->next;
            ++node->u.methodCall.argc;
        } while ((tkn = GetToken(c)) == ',');
        Require(c, tkn, ')');
    }

    /* return the method call node */
    return node;
}

/* ParseSuperMethodCall - parse a 'super' method call */
static ParseTreeNode *ParseSuperMethodCall(ParseContext *c)
{
    ParseTreeNode *selector;
    FRequire(c, '.');
    selector = ParseSelector(c);
    FRequire(c, '(');
    return ParseMethodCall(c, NULL, selector);
}

/* ParsePropertyRef - parse a property reference */
static ParseTreeNode *ParsePropertyRef(ParseContext *c, ParseTreeNode *object)
{
    ParseTreeNode *node;
    int tkn;
    
    if ((tkn = GetToken(c)) == T_CLASS) {
        node = NewParseTreeNode(c, NodeTypeClassRef);
        node->u.classRef.object = object;
    }
    else if (tkn == T_BYTE) {
        FRequire(c, '[');
        node = ParseArrayReference(c, object, PVT_BYTE);
    }
    else {
        ParseTreeNode *selector;
        if (tkn == T_IDENTIFIER) {
            selector = MakeIntegerLitNode(c, AddProperty(c, c->token));
        }
        else if (tkn == '(') {
            selector = ParseExpr(c);
            FRequire(c, ')');
        }
        else {
            ParseError(c, "expecting 'class', a property name, parenthesized expression, or 'byte'");
            node = selector = NULL; // never reached
        }
        if ((tkn = GetToken(c)) == '(') {
            node = ParseMethodCall(c, object, selector);
        }
        else {
            SaveToken(c, tkn);
            node = NewParseTreeNode(c, NodeTypePropertyRef);
            node->u.propertyRef.object = object;
            node->u.propertyRef.selector = selector;
        }
    }
    
    return node;
}

/* ParseSimplePrimary - parse a primary expression */
static ParseTreeNode *ParseSimplePrimary(ParseContext *c)
{
    ParseTreeNode *node;
    switch (GetToken(c)) {
    case '(':
        node = ParseExpr(c);
        FRequire(c,')');
        break;
    case T_SUPER:
        node = ParseSuperMethodCall(c);
        break;
    case T_NUMBER:
        node = MakeIntegerLitNode(c, c->value);
        break;
    case T_STRING:
        node = NewParseTreeNode(c, NodeTypeStringLit);
        node->u.stringLit.string = AddString(c, c->token);
        break;
    case T_IDENTIFIER:
        node = GetSymbolRef(c, c->token);
        break;
    default:
        ParseError(c, "Expecting a primary expression");
        node = NULL; /* not reached */
        break;
    }
    return node;
}

/* GetSymbolRef - setup a symbol reference */
static ParseTreeNode *GetSymbolRef(ParseContext *c, char *name)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeGlobalSymbolRef);
    LocalSymbol *localSymbol = NULL;
    Symbol *symbol;

    /* handle references to try/catch symbols */
    if (c->currentFunction) {
        for (localSymbol = c->trySymbols; localSymbol != NULL; localSymbol = localSymbol->next) {
            if (strcmp(name, localSymbol->name) == 0) {
                node->nodeType = NodeTypeLocalSymbolRef;
                node->u.localSymbolRef.symbol = localSymbol;
                return node;
            }
        }
    }
    
    /* handle local variables within a function */
    if (c->currentFunction && (localSymbol = FindLocalSymbol(&c->currentFunction->u.functionDef.locals, name)) != NULL) {
        node->nodeType = NodeTypeLocalSymbolRef;
        node->u.localSymbolRef.symbol = localSymbol;
    }

    /* handle function arguments */
    else if (c->currentFunction && (localSymbol = FindLocalSymbol(&c->currentFunction->u.functionDef.arguments, name)) != NULL) {
        node->nodeType = NodeTypeArgumentRef;
        node->u.localSymbolRef.symbol = localSymbol;
    }

    /* handle global symbols */
    else if ((symbol = FindSymbol(c, c->token)) != NULL) {
        if (symbol->storageClass == SC_CONSTANT) {
            node->nodeType = NodeTypeIntegerLit;
            node->u.integerLit.value = symbol->v.value;
        }
        else {
            node->u.symbolRef.symbol = symbol;
        }
    }

    /* handle undefined symbols */
    else {
        node->u.symbolRef.symbol = AddUndefinedSymbol(c, name, SC_OBJECT);
    }

    /* return the symbol reference node */
    return node;
}

/* MakeUnaryOpNode - allocate a unary operation parse tree node */
static ParseTreeNode *MakeUnaryOpNode(ParseContext *c, int op, ParseTreeNode *expr)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeUnaryOp);
    node->u.unaryOp.op = op;
    node->u.unaryOp.expr = expr;
    return node;
}

/* MakeBinaryOpNode - allocate a binary operation parse tree node */
static ParseTreeNode *MakeBinaryOpNode(ParseContext *c, int op, ParseTreeNode *left, ParseTreeNode *right)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeBinaryOp);
    node->u.binaryOp.op = op;
    node->u.binaryOp.left = left;
    node->u.binaryOp.right = right;
    return node;
}

/* MakeAssignmentOpNode - allocate an assignment operation parse tree node */
static ParseTreeNode *MakeAssignmentOpNode(ParseContext *c, int op, ParseTreeNode *left, ParseTreeNode *right)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeAssignmentOp);
    node->u.binaryOp.op = op;
    node->u.binaryOp.left = left;
    node->u.binaryOp.right = right;
    return node;
}

/* MakeIntegerLitNode - allocate an integer literal parse tree node */
static ParseTreeNode *MakeIntegerLitNode(ParseContext *c, VMVALUE value)
{
    ParseTreeNode *node = NewParseTreeNode(c, NodeTypeIntegerLit);
    node->u.integerLit.value = value;
    return node;
}

/* NewParseTreeNode - allocate a new parse tree node */
static ParseTreeNode *NewParseTreeNode(ParseContext *c, int type)
{
    ParseTreeNode *node = (ParseTreeNode *)LocalAlloc(c, sizeof(ParseTreeNode));
    memset(node, 0, sizeof(ParseTreeNode));
    node->nodeType = type;
    return node;
}

/* AddNodeToList - add a node to a parse tree node list */
static void AddNodeToList(ParseContext *c, NodeListEntry ***ppNextEntry, ParseTreeNode *node)
{
    NodeListEntry *entry = (NodeListEntry *)LocalAlloc(c, sizeof(NodeListEntry));
    entry->node = node;
    entry->next = NULL;
    **ppNextEntry = entry;
    *ppNextEntry = &entry->next;
}

/* InitLocalSymbolTable - initialize a local symbol table */
static void InitLocalSymbolTable(LocalSymbolTable *table)
{
    table->head = NULL;
    table->pTail = &table->head;
    table->count = 0;
}

/* AddLocalSymbol - add a symbol to a local symbol table */
static LocalSymbol *AddLocalSymbol(ParseContext *c, LocalSymbolTable *table, const char *name, int offset)
{
    LocalSymbol *sym = MakeLocalSymbol(c, name, offset);
    *table->pTail = sym;
    table->pTail = &sym->next;
    ++table->count;
    return sym;
}

/* MakeLocalSymbol - allocate and initialize a local symbol structure */
static LocalSymbol *MakeLocalSymbol(ParseContext *c, const char *name, int offset)
{
    size_t size = sizeof(LocalSymbol) + strlen(name);
    LocalSymbol *sym = (LocalSymbol *)LocalAlloc(c, size);
    memset(sym, 0, sizeof(LocalSymbol));
    strcpy(sym->name, name);
    sym->offset = offset;
    return sym;
}

/* FindLocalSymbol - find a symbol in a local symbol table */
static LocalSymbol *FindLocalSymbol(LocalSymbolTable *table, const char *name)
{
    LocalSymbol *sym = table->head;
    while (sym) {
        if (strcmp(name, sym->name) == 0)
            return sym;
        sym = sym->next;
    }
    return NULL;
}

/* PrintLocalSymbols - print a symbol table */
void PrintLocalSymbols(LocalSymbolTable *table, char *tag, int indent)
{
    LocalSymbol *sym;
    if ((sym = table->head) != NULL) {
	    printf("%*s%s\n", indent, "", tag);
        for (; sym != NULL; sym = sym->next)
            printf("%*s%s\t%d\n", indent + 2, "", sym->name, sym->offset);
    }
}

/* IsIntegerLit - check to see if a node is an integer literal */
static int IsIntegerLit(ParseTreeNode *node, VMVALUE *pValue)
{
    int result = VMTRUE;
    switch (node->nodeType) {
    case NodeTypeIntegerLit:
        *pValue = node->u.integerLit.value;
        break;
    case NodeTypeStringLit:
        *pValue = node->u.stringLit.string->offset;
        break;
    default:
        result = VMFALSE;
        break;
    }
    return result;
}

/* word type name table */
static WordType wordTypes[] = {
{   "noun",         WT_NOUN         },
{   "verb",         WT_VERB         },
{   "adjective",    WT_ADJECTIVE    },
{   "preposition",  WT_PREPOSITION  },
{   "conjunction",  WT_CONJUNCTION  },
{   "article",      WT_ARTICLE      },
{   NULL,           0               }
};

/* AddWord - add a vocabulary word */
static void AddWord(ParseContext *c, int type, String *string)
{
    Word *word = c->words;
    while (word != NULL) {
        if (strcmp(string->data, word->string->data) == 0) {
            if (type != word->type)
                ParseError(c, "'%s' already has type %s", string->data, wordTypes[word->type - 1].name);
            return; // word is already in the list of words
        }
        word = word->next;
    }
    word = (Word *)LocalAlloc(c, sizeof(Word));
    word->type = type;
    word->string = string;
    word->next = NULL;
    *c->pNextWord = word;
    c->pNextWord = &word->next;
    ++c->wordCount;
}

/* FindWordType - find a word type by name */
static int FindWordType(char *name)
{
    int i;
    for (i = 0; wordTypes[i].name != NULL; ++i) {
        if (strcmp(name, wordTypes[i].name) == 0)
            return wordTypes[i].type;
    }
    return WT_NONE;
}

