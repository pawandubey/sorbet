#include "gtest/gtest.h"
// has to go first as it violates are requirements

#include "ast/ast.h"
#include "ast/desugar/Desugar.h"
#include "common/common.h"
#include "core/BufferedErrorQueue.h"
#include "core/Errors.h"
#include "core/Unfreeze.h"
#include "dsl/dsl.h"
#include "namer/namer.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

namespace spd = spdlog;

using namespace std;

namespace sorbet::namer::test {

auto logger = spd::stderr_color_mt("namer_test");
auto errorQueue = make_shared<sorbet::core::BufferedErrorQueue>(*logger, *logger);

class NamerFixture : public ::testing::Test {
public:
    void SetUp() override {
        ctxPtr = make_unique<core::GlobalState>(errorQueue);
        ctxPtr->initEmpty();
    }
    core::MutableContext getCtx() {
        return core::MutableContext(*ctxPtr, core::Symbols::root());
    }

private:
    unique_ptr<core::GlobalState> ctxPtr;
};

static string_view testClass_str = "Test"sv;

unique_ptr<ast::Expression> getTree(core::GlobalState &gs, string str) {
    sorbet::core::UnfreezeNameTable nameTableAccess(gs); // enters original strings
    sorbet::core::UnfreezeFileTable ft(gs);              // enters original strings
    auto tree = parser::Parser::run(gs, "<test>", str);
    tree->loc.file().data(gs).strict = core::StrictLevel::Strict;
    sorbet::core::MutableContext ctx(gs, core::Symbols::root());
    auto ast = ast::desugar::node2Tree(ctx, move(tree));
    ast = dsl::DSL::run(ctx, move(ast));
    return ast;
}

unique_ptr<ast::Expression> hello_world(core::GlobalState &gs) {
    return getTree(gs, "def hello_world; end");
}

TEST_F(NamerFixture, HelloWorld) { // NOLINT
    auto ctx = getCtx();
    auto tree = hello_world(ctx);
    {
        sorbet::core::UnfreezeNameTable nameTableAccess(ctx);     // creates singletons and class names
        sorbet::core::UnfreezeSymbolTable symbolTableAccess(ctx); // enters symbols
        namer::Namer::run(ctx, move(tree));
    }

    const auto &objectScope = core::Symbols::Object().data(ctx);
    ASSERT_EQ(core::Symbols::root(), objectScope->owner);

    ASSERT_EQ(4, objectScope->members.size());
    auto methodSym = objectScope->members.at(ctx.state.enterNameUTF8("hello_world"));
    const auto &symbol = methodSym.data(ctx);
    ASSERT_EQ(core::Symbols::Object(), symbol->owner);
    ASSERT_EQ(0, symbol->arguments().size());
}

TEST_F(NamerFixture, Idempotent) { // NOLINT
    auto ctx = getCtx();
    auto baseSymbols = ctx.state.symbolsUsed();
    auto baseNames = ctx.state.namesUsed();

    auto tree = hello_world(ctx);
    unique_ptr<sorbet::ast::Expression> newtree;
    {
        sorbet::core::UnfreezeNameTable nameTableAccess(ctx);     // creates singletons and class names
        sorbet::core::UnfreezeSymbolTable symbolTableAccess(ctx); // enters symbols
        newtree = namer::Namer::run(ctx, move(tree));
    }
    ASSERT_EQ(baseSymbols + 1, ctx.state.symbolsUsed());
    ASSERT_EQ(baseNames + 1, ctx.state.namesUsed());

    // Run it again and get the same numbers
    namer::Namer::run(ctx, move(newtree));
    ASSERT_EQ(baseSymbols + 1, ctx.state.symbolsUsed());
    ASSERT_EQ(baseNames + 1, ctx.state.namesUsed());
}

TEST_F(NamerFixture, NameClass) { // NOLINT
    auto ctx = getCtx();
    auto tree = getTree(ctx, "class Test; class Foo; end; end");
    {
        sorbet::core::UnfreezeNameTable nameTableAccess(ctx);     // creates singletons and class names
        sorbet::core::UnfreezeSymbolTable symbolTableAccess(ctx); // enters symbols
        namer::Namer::run(ctx, move(tree));
    }
    const auto &rootScope =
        core::Symbols::root().data(ctx)->findMember(ctx, ctx.state.enterNameConstant(testClass_str)).data(ctx);

    ASSERT_EQ(3, rootScope->members.size());
    auto fooSym = rootScope->members.at(ctx.state.enterNameConstant("Foo"));
    const auto &fooInfo = fooSym.data(ctx);
    ASSERT_EQ(1, fooInfo->members.size());
}

TEST_F(NamerFixture, InsideClass) { // NOLINT
    auto ctx = getCtx();
    auto tree = getTree(ctx, "class Test; class Foo; def bar; end; end; end");
    {
        sorbet::core::UnfreezeNameTable nameTableAccess(ctx);     // creates singletons and class names
        sorbet::core::UnfreezeSymbolTable symbolTableAccess(ctx); // enters symbols
        namer::Namer::run(ctx, move(tree));
    }
    const auto &rootScope =
        core::Symbols::root().data(ctx)->findMember(ctx, ctx.state.enterNameConstant(testClass_str)).data(ctx);

    ASSERT_EQ(3, rootScope->members.size());
    auto fooSym = rootScope->members.at(ctx.state.enterNameConstant("Foo"));
    const auto &fooInfo = fooSym.data(ctx);
    ASSERT_EQ(2, fooInfo->members.size());

    auto barSym = fooInfo->members.at(ctx.state.enterNameUTF8("bar"));
    ASSERT_EQ(fooSym, barSym.data(ctx)->owner);
}

} // namespace sorbet::namer::test
