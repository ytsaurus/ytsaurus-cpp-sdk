#include "tools.h"
#include "registry.h"

#include <yt/yt/core/logging/config.h>
#include <yt/yt/core/logging/log.h>
#include <yt/yt/core/logging/log_manager.h>

#include <yt/yt/library/process/subprocess.h>

#include <yt/yt/core/ytree/fluent.h>

#include <util/system/thread.h>

namespace NYT::NTools {

////////////////////////////////////////////////////////////////////////////////

using namespace NYson;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

static TYsonString DoExecuteTool(const TString& toolName, const TYsonString& serializedArgument, bool shutdownLogging);

TYsonString SpawnTool(const TString& toolName, const TYsonString& serializedArgument)
{
    auto process = TSubprocess(TString(ToolsProgramName));
    process.AddArguments({
        "--tool-name",
        toolName,
        "--tool-spec",
        serializedArgument.AsStringBuf()
    });

    auto result = process.Execute();
    if (!result.Status.IsOK()) {
        THROW_ERROR_EXCEPTION("Failed to run %v", toolName)
            << result.Status
            << TErrorAttribute("command_line", process.GetCommandLine())
            << TErrorAttribute("error", TString(result.Error.Begin(), result.Error.End()));
    }

    auto serializedResultOrError = TString(result.Output.Begin(), result.Output.End());

    // Treat empty string as OK
    if (serializedResultOrError.empty()) {
        return ConvertToYsonString(TError(), NYson::EYsonFormat::Text);
    }

    return TYsonString(serializedResultOrError);
}

TYsonString DoRunTool(const TString& toolName, const TYsonString& serializedArgument)
{
    return SpawnTool(toolName, serializedArgument);
}

TYsonString DoRunToolInProcess(const TString& toolName, const TYsonString& serializedArgument)
{
    auto serializedResultOrError = DoExecuteTool(toolName, serializedArgument, false);

    // Treat empty string as OK
    if (serializedResultOrError.AsStringBuf().empty()) {
        return ConvertToYsonString(TError(), NYson::EYsonFormat::Text);
    }

    return serializedResultOrError;
}

static TYsonString DoExecuteTool(const TString& toolName, const TYsonString& serializedArgument, bool shutdownLogging)
{
    try {
        if (shutdownLogging) {
            NLogging::TLogManager::Get()->Shutdown();
        }

        const auto* registry = GetToolRegistry();
        YT_VERIFY(registry != nullptr);

        auto it = registry->find(toolName);
        if (it == registry->end()) {
            THROW_ERROR_EXCEPTION("Failed to execute %v: no such tool", toolName);
        }

        const auto& toolDescription = it->second;

        TThread::SetCurrentThreadName(toolDescription.Name.c_str());

        auto result = toolDescription.Tool(serializedArgument);
        return result;
    } catch (const TErrorException& ex) {
        return ConvertToYsonString(ex.Error(), NYson::EYsonFormat::Text);
    }
}

TYsonString ExecuteTool(const TString& toolName, const TYsonString& serializedArgument)
{
    return DoExecuteTool(toolName, serializedArgument, true);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTools
