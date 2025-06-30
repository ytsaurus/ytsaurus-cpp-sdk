#include "program.h"

#include "bootstrap.h"
#include "config.h"

#include <yt/yt/library/program/program.h>
#include <yt/yt/library/program/program_config_mixin.h>
#include <yt/yt/library/program/helpers.h>

#include <util/system/thread.h>

namespace NYT::NLogTailer {

////////////////////////////////////////////////////////////////////////////////

class TLogTailerProgram
    : public virtual TProgram
    , public TProgramConfigMixin<TLogTailerBootstrapConfig>
{
public:
    TLogTailerProgram()
        : TProgramConfigMixin(Opts_, false)
    {
        Opts_.AddLongOption(
            "monitoring-port",
            "Server monitoring port")
            .DefaultValue(10242)
            .StoreResult(&MonitoringPort_);
        Opts_.SetFreeArgsMin(0);
        Opts_.SetFreeArgsMax(1);
        Opts_.SetFreeArgTitle(0, "writer-pid");
    }

protected:
    void DoRun() override
    {
        TThread::SetCurrentThreadName("LogTailer");

        ConfigureCrashHandler();
        RunMixinCallbacks();

        auto config = GetConfig();
        config->MonitoringPort = MonitoringPort_;

        const auto& parseResult = GetOptsParseResult();
        if (parseResult.GetFreeArgCount() == 1) {
            auto freeArgs = parseResult.GetFreeArgs();
            config->LogTailer->LogRotation->LogWriterPid = FromString<int>(freeArgs[0]);
        }

        ConfigureSingletons(config);

        TBootstrap bootstrap(std::move(config));
        bootstrap.Run();

        Sleep(TDuration::Max());
    }

private:
    int MonitoringPort_ = 0;
};

////////////////////////////////////////////////////////////////////////////////

void RunLogTailerProgram(int argc, const char** argv)
{
    TLogTailerProgram().Run(argc, argv);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NLogTailer
