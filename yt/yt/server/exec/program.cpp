#include "program.h"

#include "user_job_synchronizer.h"

#include <yt/yt/server/lib/user_job/config.h>

#include <yt/yt/library/program/program.h>
#include <yt/yt/library/program/program_config_mixin.h>

#include <yt/yt/library/process/pipe.h>

#include <yt/yt/core/logging/formatter.h>
#include <yt/yt/core/logging/log_manager.h>

#include <yt/yt/core/misc/proc.h>
#include <yt/yt/core/misc/fs.h>

#include <library/cpp/yt/system/handle_eintr.h>

#include <util/system/thread.h>

#include <sys/ioctl.h>

#ifdef _unix_
    #include <sys/resource.h>
    #include <unistd.h>
#endif

namespace NYT::NExec {

////////////////////////////////////////////////////////////////////////////////

class TExecProgram
    : public virtual TProgram
    , public TProgramConfigMixin<NUserJob::TUserJobExecutorConfig>
{
public:
    TExecProgram()
        : TProgramConfigMixin(Opts_, false)
    { }

private:
    TFile ExecutorStderr_;
    TString JobId_;

    void DoRun() final
    {
        RunMixinCallbacks();

        ConfigureUids();
        ConfigureCrashHandler();

        TThread::SetCurrentThreadName("ExecMain");

        // Don't start any other singleton or parse config in executor mode.
        // Explicitly shut down log manager to ensure it doesn't spoil dup-ed descriptors.
        NLogging::TLogManager::Get()->Shutdown();

        auto config = GetConfig();

        JobId_ = config->JobId;

        if (config->Uid > 0) {
            SetUid(config->Uid);
        }

        TError executorError;

        try {
            auto enableCoreDump = config->EnableCoreDump;
            struct rlimit rlimit = {
                enableCoreDump ? RLIM_INFINITY : 0,
                enableCoreDump ? RLIM_INFINITY : 0
            };

            auto rv = setrlimit(RLIMIT_CORE, &rlimit);
            if (rv) {
                THROW_ERROR_EXCEPTION("Failed to configure core dump limits")
                    << TError::FromSystem();
            }

            for (const auto& pipe : config->Pipes) {
                auto streamFd = pipe->FD;
                const auto& path = pipe->Path;

                try {
                    // Behaviour of named pipe:
                    // reader blocks on open if no writer and O_NONBLOCK is not set,
                    // writer blocks on open if no reader and O_NONBLOCK is not set.
                    auto flags = (pipe->Write ? O_WRONLY : O_RDONLY);
                    auto fd = HandleEintr(::open, path.c_str(), flags);
                    if (fd == -1) {
                        THROW_ERROR_EXCEPTION("Failed to open named pipe")
                            << TErrorAttribute("path", path)
                            << TError::FromSystem();
                    }

                    if (streamFd != fd) {
                        SafeDup2(fd, streamFd);
                        SafeClose(fd, false);
                    }
                } catch (const std::exception& ex) {
                    THROW_ERROR_EXCEPTION("Failed to prepare named pipe")
                        << TErrorAttribute("path", path)
                        << TErrorAttribute("fd", streamFd)
                        << ex;
                }
            }
        } catch (const std::exception& ex) {
            executorError = ex;
        }

        try {
            OpenExecutorStderr();
        } catch (const std::exception& ex) {
            Exit(ToUnderlying(EProgramExitCode::ExecutorStderrOpenError));
        }

        if (!executorError.IsOK()) {
            LogToStderr(Format("Failed to prepare pipes, unexpected executor error\n%v\n", executorError));
            Exit(ToUnderlying(EProgramExitCode::ExecutorError));
        }

        // NB: Intentionally open executor_stderr after processing pipes to avoid fd clashes.
        try {
            if (ExecutorStderr_.GetHandle() == STDOUT_FILENO) {
                auto newFile = ExecutorStderr_.Duplicate();
                ExecutorStderr_.Close();
                ExecutorStderr_ = newFile;
            }
        } catch (const std::exception& ex) {
            Exit(ToUnderlying(EProgramExitCode::ExecutorStderrDuplicateError));
        }

        if (config->Pty) {
            CloseAllDescriptors({*config->Pty});
            if (HandleEintr(setsid) == -1) {
                THROW_ERROR_EXCEPTION("Failed to create a new session") << TError::FromSystem();
            }
            if (HandleEintr(::ioctl, *config->Pty, TIOCSCTTY, 1) == -1) {
                THROW_ERROR_EXCEPTION("Failed to set controlling pseudoterminal") << TError::FromSystem();
            }
            SafeDup2(*config->Pty, 0);
            SafeDup2(*config->Pty, 1);
            SafeDup2(*config->Pty, 2);
            if (*config->Pty > 2) {
                SafeClose(*config->Pty);
            }
        }

        std::vector<const char*> env;
        for (const auto& environment : config->Environment) {
            env.push_back(environment.c_str());
        }
        env.push_back("SHELL=/bin/bash");
        env.push_back(nullptr);

        std::vector<const char*> args;
        args.push_back("/bin/bash");

        TString command;
        if (!config->Command.empty()) {
            // :; is added avoid fork/exec (one-shot) optimization.
            command = ":; " + config->Command;
            args.push_back("-c");
            args.push_back(command.c_str());
        }
        args.push_back(nullptr);

        // We are ready to execute user code, send signal to JobProxy.
        // Config is absent for job shell.
        if (const auto& userJobSynchronizerConnectionConfig = config->UserJobSynchronizerConnectionConfig) {
            try {
                auto jobProxyControl = CreateUserJobSynchronizerClient(userJobSynchronizerConnectionConfig);
                jobProxyControl->NotifyExecutorPrepared();
            } catch (const std::exception& ex) {
                LogToStderr(Format("Unable to notify job proxy\n%v", ex.what()));
                Exit(ToUnderlying(EProgramExitCode::JobProxyNotificationError));
            }
        }

        TryExecve(
            "/bin/bash",
            args.data(),
            env.data());

        LogToStderr(Format("execve failed: %v", TError::FromSystem()));
        Exit(ToUnderlying(EProgramExitCode::ExecveError));
    }

    void OnError(const TString& message) noexcept
    {
        LogToStderr(message);
    }

    void LogToStderr(const TString& message)
    {
        auto logRecord = Format("%v (JobId: %v)", message, JobId_);

        if (!ExecutorStderr_.IsOpen()) {
            Cerr << logRecord << Endl;
            return;
        }

        ExecutorStderr_.Write(logRecord.data(), logRecord.size());
        ExecutorStderr_.Flush();
    }

    void OpenExecutorStderr()
    {
        NFS::MakeDirRecursive(NFS::GetDirectoryName(GetConfig()->StderrPath));

        ExecutorStderr_ = TFile(
            GetConfig()->StderrPath,
            EOpenModeFlag::WrOnly | EOpenModeFlag::ForAppend | EOpenModeFlag::OpenAlways);
    }
};

////////////////////////////////////////////////////////////////////////////////

void RunExecProgram(int argc, const char** argv)
{
    TExecProgram().Run(argc, argv);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NExec
