#ifndef TSRE_EDITOR_TOOLS_TEST_SUITE_H
#define TSRE_EDITOR_TOOLS_TEST_SUITE_H

namespace TsreTests {
// The Route Editor's tools (routeEditor/tools) through a fake ToolContext:
// the registry, view mode gating, and what tools do with the context.
int runEditorToolsSuite(bool verbose);
}

#endif
